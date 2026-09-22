// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "BindGuardStatics.h"

#include "BindGuardLog.h"
#include "BindGuardSettings.h"
#include "BindGuardSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EnhancedInputLibrary.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Misc/StringBuilder.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace BindGuardRules
{
	/** A bit per device family, so "which devices is this action reachable on" fits in one integer. */
	static uint8 DeviceBit(const EBindDevice Device)
	{
		return static_cast<uint8>(1u << static_cast<uint8>(Device));
	}

	/** Everything one action's mappings add up to. Built once, read by every device rule. */
	struct FActionCoverage
	{
		uint8 DeviceMask = 0;

		/** A representative binding per device, so the sentence can name a real key rather than a count. */
		TMap<EBindDevice, FBindGuardMapping> Examples;

		/** Every context this action appears in, in first-seen order. */
		TArray<FName> Contexts;
	};

	static void BuildCoverage(const TArray<FBindGuardMapping>& Mappings, TMap<FName, FActionCoverage>& Out)
	{
		for (const FBindGuardMapping& Mapping : Mappings)
		{
			if (Mapping.ActionName.IsNone())
			{
				continue;
			}

			FActionCoverage& Coverage = Out.FindOrAdd(Mapping.ActionName);

			const EBindDevice Device = UBindGuardStatics::ClassifyKey(Mapping.Key);
			Coverage.DeviceMask |= DeviceBit(Device);
			if (!Coverage.Examples.Contains(Device))
			{
				Coverage.Examples.Add(Device, Mapping);
			}

			if (!Mapping.ContextName.IsNone())
			{
				Coverage.Contexts.AddUnique(Mapping.ContextName);
			}
		}
	}

	/**
	 * Applies the exemption list to a finding that has already been made.
	 *
	 * Made, not skipped. The finding still exists, still names the action and still gets printed - it is
	 * forced down to Info and counted as excluded. An exemption list that could make its own effect
	 * invisible would be a way to turn a report green from a settings page, and that is precisely the
	 * thing a gate exists to prevent.
	 */
	static void ApplyExemptions(FBindGuardFinding& Finding, const FBindGuardRequirements& Requirements)
	{
		if (!Requirements.bUseExemptions)
		{
			return;
		}

		const bool bActionExempt = !Finding.ActionName.IsNone() && Requirements.ExemptActions.Contains(Finding.ActionName);
		const bool bContextExempt = !Finding.ContextName.IsNone() && Requirements.ExemptContexts.Contains(Finding.ContextName);

		// A context exemption only silences findings that are about the context itself. It would be wrong
		// for "this loading-screen context is never added" to also silence "two actions collide inside it".
		const bool bContextFindingExempt = bContextExempt
			&& (Finding.Kind == EBindFindingKind::ContextNeverAdded || Finding.Kind == EBindFindingKind::Unreachable);

		if (bActionExempt || bContextFindingExempt)
		{
			Finding.bExcluded = true;
			Finding.Severity = EBindSeverity::Info;
		}
	}

	/** Fills in the sentence and applies the exemption list. Every finding leaves through here. */
	static FBindGuardFinding Finish(FBindGuardFinding Finding, const FBindGuardRequirements& Requirements)
	{
		ApplyExemptions(Finding, Requirements);
		Finding.Detail = UBindGuardStatics::Explain(Finding);
		return Finding;
	}

	/** The grouping key a conflict is decided on: one context, one profile, one key, one chord layer. */
	static FString ConflictKeyWithinContext(const FBindGuardMapping& Mapping)
	{
		return FString::Printf(TEXT("%s|%s|%s|%s"),
			*Mapping.ContextName.ToString(), *Mapping.ProfileId, *Mapping.Key.ToString(), *Mapping.ChordSignature);
	}

	/** The same, minus the context - which is what makes it a hint rather than an error. */
	static FString ConflictKeyAcrossContexts(const FBindGuardMapping& Mapping)
	{
		return FString::Printf(TEXT("%s|%s|%s"),
			*Mapping.ProfileId, *Mapping.Key.ToString(), *Mapping.ChordSignature);
	}

	static int32 SeverityRank(const EBindSeverity Severity)
	{
		switch (Severity)
		{
		case EBindSeverity::Error:	return 0;
		case EBindSeverity::Warning:	return 1;
		default:						return 2;
		}
	}
}

//~ The rules ------------------------------------------------------------------------------------------

EBindDevice UBindGuardStatics::ClassifyKey(const FKey& Key)
{
	// An invalid key is not a binding. Treating it as one would let a mapping with an empty key satisfy a
	// device requirement, which is exactly the shape of the bug this plugin is meant to find.
	if (!Key.IsValid())
	{
		return EBindDevice::Other;
	}

	// Asked in a fixed order so the answer is reproducible. Touch first because a touch key is never a
	// gamepad key and never a mouse button, gamepad second because the sticks and triggers are analog and
	// would otherwise have to be special-cased, and mouse third.
	if (Key.IsTouch())
	{
		return EBindDevice::Touch;
	}

	if (Key.IsGamepadKey())
	{
		return EBindDevice::Gamepad;
	}

	if (Key.IsMouseButton())
	{
		return EBindDevice::KeyboardMouse;
	}

	// Gestures - pinch, flick, rotate - are touch hardware but are not touch points, and no keyboard has
	// one. Calling them keyboard keys because they are none of the above would quietly turn a phone-only
	// binding into proof of desktop coverage.
	if (Key.IsGesture())
	{
		return EBindDevice::Other;
	}

	// What is left is a keyboard key. Note that this is the one branch decided by elimination, which is
	// why the three above are asked first and why an invalid key never reaches here.
	return EBindDevice::KeyboardMouse;
}

TArray<FBindGuardFinding> UBindGuardStatics::FindUnbound(
	const TArray<FBindGuardMapping>& Mappings,
	const TArray<FName>& Actions,
	const FBindGuardRequirements& Requirements)
{
	using namespace BindGuardRules;

	TSet<FName> Bound;
	Bound.Reserve(Mappings.Num());
	for (const FBindGuardMapping& Mapping : Mappings)
	{
		if (!Mapping.ActionName.IsNone() && Mapping.Key.IsValid())
		{
			Bound.Add(Mapping.ActionName);
		}
	}

	TArray<FBindGuardFinding> Findings;

	TSet<FName> Seen;
	for (const FName Action : Actions)
	{
		if (Action.IsNone() || Bound.Contains(Action))
		{
			continue;
		}

		// The list of actions can legitimately contain the same name twice - two assets with the same
		// name in two folders - and reporting it twice would be noise, not accuracy.
		bool bAlreadySeen = false;
		Seen.Add(Action, &bAlreadySeen);
		if (bAlreadySeen)
		{
			continue;
		}

		FBindGuardFinding Finding;
		Finding.Kind = EBindFindingKind::Unbound;
		Finding.Severity = Requirements.UnboundSeverity;
		Finding.ActionName = Action;
		Findings.Add(Finish(MoveTemp(Finding), Requirements));
	}

	return Findings;
}

TArray<FBindGuardFinding> UBindGuardStatics::FindDeviceGaps(
	const TArray<FBindGuardMapping>& Mappings,
	const FBindGuardRequirements& Requirements)
{
	using namespace BindGuardRules;

	TMap<FName, FActionCoverage> Coverage;
	BuildCoverage(Mappings, Coverage);

	// Which devices this project insists on. An empty set means the device check is switched off, and the
	// report says so rather than reporting nothing and letting you assume it passed.
	TArray<EBindDevice> Required;
	if (Requirements.bRequireKeyboardMouse)	{ Required.Add(EBindDevice::KeyboardMouse); }
	if (Requirements.bRequireGamepad)		{ Required.Add(EBindDevice::Gamepad); }
	if (Requirements.bRequireTouch)			{ Required.Add(EBindDevice::Touch); }

	// Sorted so two runs over the same project produce the same report in the same order. A findings list
	// that reshuffles itself makes a diff between two builds unreadable.
	TArray<FName> ActionNames;
	Coverage.GetKeys(ActionNames);
	ActionNames.Sort(FNameLexicalLess());

	TArray<FBindGuardFinding> Findings;

	for (const FName ActionName : ActionNames)
	{
		const FActionCoverage& Entry = Coverage[ActionName];

		const bool bHasKeyboardMouse = (Entry.DeviceMask & DeviceBit(EBindDevice::KeyboardMouse)) != 0;
		const bool bHasGamepad = (Entry.DeviceMask & DeviceBit(EBindDevice::Gamepad)) != 0;

		for (const EBindDevice Device : Required)
		{
			if ((Entry.DeviceMask & DeviceBit(Device)) != 0)
			{
				continue;
			}

			FBindGuardFinding Finding;
			Finding.Severity = Requirements.MissingDeviceSeverity;
			Finding.ActionName = ActionName;
			Finding.MissingDevice = Device;

			// The kind is the shape a person recognises. "Keyboard only" is only allowed to be said when
			// the action really is on keyboard and mouse and really is missing the gamepad; anything else
			// is the general DeviceGap, because a finding that has to stretch its own name is a finding
			// that gets argued with instead of fixed.
			if (Device == EBindDevice::Gamepad && bHasKeyboardMouse)
			{
				Finding.Kind = EBindFindingKind::KeyboardOnly;
			}
			else if (Device == EBindDevice::KeyboardMouse && bHasGamepad)
			{
				Finding.Kind = EBindFindingKind::GamepadOnly;
			}
			else
			{
				Finding.Kind = EBindFindingKind::DeviceGap;
			}

			// Name a key and a context the action really does live in, so the sentence can say where to go
			// and add the missing one.
			for (const TPair<EBindDevice, FBindGuardMapping>& Example : Entry.Examples)
			{
				Finding.Key = Example.Value.Key;
				Finding.ContextName = Example.Value.ContextName;
				if (Example.Key == EBindDevice::KeyboardMouse)
				{
					break;
				}
			}

			Findings.Add(Finish(MoveTemp(Finding), Requirements));
		}
	}

	return Findings;
}

TArray<FBindGuardFinding> UBindGuardStatics::FindConflicts(
	const TArray<FBindGuardMapping>& Mappings,
	const FBindGuardRequirements& Requirements)
{
	using namespace BindGuardRules;

	TArray<FBindGuardFinding> Findings;

	// The same action bound to the same key twice in the same context is a duplicate, not a conflict, and
	// the engine handles it. Only distinct actions are collected per group.
	TMap<FString, TArray<FBindGuardMapping>> WithinContext;
	TMap<FString, TArray<FBindGuardMapping>> AcrossContexts;

	for (const FBindGuardMapping& Mapping : Mappings)
	{
		if (Mapping.ActionName.IsNone() || !Mapping.Key.IsValid())
		{
			continue;
		}

		{
			TArray<FBindGuardMapping>& Group = WithinContext.FindOrAdd(ConflictKeyWithinContext(Mapping));
			if (!Group.ContainsByPredicate([&Mapping](const FBindGuardMapping& Other) { return Other.ActionName == Mapping.ActionName; }))
			{
				Group.Add(Mapping);
			}
		}

		{
			TArray<FBindGuardMapping>& Group = AcrossContexts.FindOrAdd(ConflictKeyAcrossContexts(Mapping));
			if (!Group.ContainsByPredicate([&Mapping](const FBindGuardMapping& Other)
				{ return Other.ActionName == Mapping.ActionName && Other.ContextName == Mapping.ContextName; }))
			{
				Group.Add(Mapping);
			}
		}
	}

	// Deterministic order, again so a report can be diffed between two builds.
	TArray<FString> GroupKeys;
	WithinContext.GetKeys(GroupKeys);
	GroupKeys.Sort();

	TSet<FString> ReportedPairs;

	for (const FString& GroupKey : GroupKeys)
	{
		TArray<FBindGuardMapping>& Group = WithinContext[GroupKey];
		if (Group.Num() < 2)
		{
			continue;
		}

		Group.Sort([](const FBindGuardMapping& A, const FBindGuardMapping& B)
		{
			return A.ActionName.LexicalLess(B.ActionName);
		});

		// n actions on one key produce n-1 findings, each naming the pair, rather than every one of the
		// n*(n-1)/2 pairs. Four actions on the space bar is one problem with four names in it, not six
		// problems.
		for (int32 Index = 1; Index < Group.Num(); ++Index)
		{
			FBindGuardFinding Finding;
			Finding.Kind = EBindFindingKind::Conflict;
			Finding.Severity = Requirements.ConflictSeverity;
			Finding.ActionName = Group[0].ActionName;
			Finding.OtherActionName = Group[Index].ActionName;
			Finding.ContextName = Group[0].ContextName;
			Finding.OtherContextName = Group[Index].ContextName;
			Finding.Key = Group[0].Key;
			Finding.bCrossContext = false;

			ReportedPairs.Add(FString::Printf(TEXT("%s|%s|%s"),
				*Finding.Key.ToString(), *Finding.ActionName.ToString(), *Finding.OtherActionName.ToString()));

			Findings.Add(Finish(MoveTemp(Finding), Requirements));
		}
	}

	AcrossContexts.GetKeys(GroupKeys);
	GroupKeys.Sort();

	for (const FString& GroupKey : GroupKeys)
	{
		TArray<FBindGuardMapping>& Group = AcrossContexts[GroupKey];
		if (Group.Num() < 2)
		{
			continue;
		}

		Group.Sort([](const FBindGuardMapping& A, const FBindGuardMapping& B)
		{
			return A.ActionName == B.ActionName
				? A.ContextName.LexicalLess(B.ContextName)
				: A.ActionName.LexicalLess(B.ActionName);
		});

		for (int32 Index = 1; Index < Group.Num(); ++Index)
		{
			// Same context is the error case above; it has already been reported and must not be
			// reported a second time as a hint.
			if (Group[0].ContextName == Group[Index].ContextName)
			{
				continue;
			}

			// One action legitimately living in two contexts on the same key is not two actions fighting.
			if (Group[0].ActionName == Group[Index].ActionName)
			{
				continue;
			}

			const FString PairKey = FString::Printf(TEXT("%s|%s|%s"),
				*Group[0].Key.ToString(), *Group[0].ActionName.ToString(), *Group[Index].ActionName.ToString());
			if (ReportedPairs.Contains(PairKey))
			{
				continue;
			}
			ReportedPairs.Add(PairKey);

			FBindGuardFinding Finding;
			Finding.Kind = EBindFindingKind::Conflict;
			Finding.Severity = Requirements.CrossContextConflictSeverity;
			Finding.ActionName = Group[0].ActionName;
			Finding.OtherActionName = Group[Index].ActionName;
			Finding.ContextName = Group[0].ContextName;
			Finding.OtherContextName = Group[Index].ContextName;
			Finding.Key = Group[0].Key;
			Finding.bCrossContext = true;

			Findings.Add(Finish(MoveTemp(Finding), Requirements));
		}
	}

	return Findings;
}

TArray<FBindGuardFinding> UBindGuardStatics::FindContextGaps(
	const FBindGuardScanInput& Input,
	const FBindGuardRequirements& Requirements)
{
	using namespace BindGuardRules;

	TArray<FBindGuardFinding> Findings;

	// The line this whole plugin's honesty rests on. With no session behind the scan there is nothing to
	// say about which contexts get added, so nothing is said - rather than an empty list that a reader
	// would take for a clean bill of health.
	if (!Input.bObservationAvailable)
	{
		return Findings;
	}

	const TSet<FName> Observed(Input.ObservedContexts);

	TArray<FName> Contexts = Input.Contexts;
	Contexts.Sort(FNameLexicalLess());

	TSet<FName> SeenContexts;
	for (const FName Context : Contexts)
	{
		if (Context.IsNone() || Observed.Contains(Context))
		{
			continue;
		}

		bool bAlreadySeen = false;
		SeenContexts.Add(Context, &bAlreadySeen);
		if (bAlreadySeen)
		{
			continue;
		}

		FBindGuardFinding Finding;
		Finding.Kind = EBindFindingKind::ContextNeverAdded;
		Finding.Severity = Requirements.ContextNeverAddedSeverity;
		Finding.ContextName = Context;
		Findings.Add(Finish(MoveTemp(Finding), Requirements));
	}

	// An action is unreachable when every context it lives in went unadded. An action that lives in no
	// context at all is Unbound, which is a different and worse finding, and reporting both would be
	// telling somebody the same thing twice in two vocabularies.
	TMap<FName, FActionCoverage> Coverage;
	BuildCoverage(Input.Mappings, Coverage);

	TArray<FName> ActionNames;
	Coverage.GetKeys(ActionNames);
	ActionNames.Sort(FNameLexicalLess());

	for (const FName ActionName : ActionNames)
	{
		const FActionCoverage& Entry = Coverage[ActionName];
		if (Entry.Contexts.Num() == 0)
		{
			continue;
		}

		const bool bReachable = Entry.Contexts.ContainsByPredicate([&Observed](const FName Context)
		{
			return Observed.Contains(Context);
		});

		if (bReachable)
		{
			continue;
		}

		FBindGuardFinding Finding;
		Finding.Kind = EBindFindingKind::Unreachable;
		Finding.Severity = Requirements.UnreachableSeverity;
		Finding.ActionName = ActionName;
		Finding.ContextName = Entry.Contexts[0];
		Findings.Add(Finish(MoveTemp(Finding), Requirements));
	}

	return Findings;
}

EBindVerdict UBindGuardStatics::Judge(const TArray<FBindGuardFinding>& Findings)
{
	bool bAnyWarning = false;

	for (const FBindGuardFinding& Finding : Findings)
	{
		// An excluded finding has already been forced down to Info, so it cannot reach either branch. The
		// check is written out anyway because it is the rule a reader will want to see stated: an
		// exemption never changes the verdict.
		if (Finding.bExcluded)
		{
			continue;
		}

		if (Finding.Severity == EBindSeverity::Error)
		{
			return EBindVerdict::Fail;
		}

		bAnyWarning |= (Finding.Severity == EBindSeverity::Warning);
	}

	return bAnyWarning ? EBindVerdict::Warn : EBindVerdict::Ok;
}

FString UBindGuardStatics::Explain(const FBindGuardFinding& Finding)
{
	const FString Action = Finding.ActionName.IsNone() ? TEXT("(unnamed action)") : Finding.ActionName.ToString();
	const FString Context = Finding.ContextName.IsNone() ? TEXT("(unnamed context)") : Finding.ContextName.ToString();
	const FString Other = Finding.OtherActionName.IsNone() ? TEXT("(unnamed action)") : Finding.OtherActionName.ToString();
	const FString OtherContext = Finding.OtherContextName.IsNone() ? Context : Finding.OtherContextName.ToString();
	const FString KeyName = Finding.Key.IsValid() ? Finding.Key.ToString() : TEXT("no key");

	FString Sentence;

	switch (Finding.Kind)
	{
	case EBindFindingKind::Unbound:
		Sentence = FString::Printf(
			TEXT("%s is in no mapping context at all, so nothing can ever trigger it. Add it to a context, or delete the asset."),
			*Action);
		break;

	case EBindFindingKind::KeyboardOnly:
		Sentence = FString::Printf(
			TEXT("%s is bound on keyboard and mouse (%s in %s) and on no gamepad key. Map it to a gamepad key in %s - this is the check a console submission fails first."),
			*Action, *KeyName, *Context, *Context);
		break;

	case EBindFindingKind::GamepadOnly:
		Sentence = FString::Printf(
			TEXT("%s is bound on the gamepad (%s in %s) and on no keyboard or mouse key. Map it to a key in %s so a player without a controller can reach it."),
			*Action, *KeyName, *Context, *Context);
		break;

	case EBindFindingKind::DeviceGap:
		Sentence = FString::Printf(
			TEXT("%s has no %s binding. It is bound in %s; add a %s binding there, or name the action in the exemption list if it is meant to be missing."),
			*Action, *DeviceName(Finding.MissingDevice), *Context, *DeviceName(Finding.MissingDevice));
		break;

	case EBindFindingKind::Conflict:
		if (Finding.bCrossContext)
		{
			Sentence = FString::Printf(
				TEXT("%s in %s and %s in %s both use %s. Two contexts are allowed to share a key - that is what contexts are for - so this is only worth a look if both are ever active at once."),
				*Action, *Context, *Other, *OtherContext, *KeyName);
		}
		else
		{
			Sentence = FString::Printf(
				TEXT("%s and %s are both bound to %s in %s with the same chord layer, so both will fire. Move one of them, or give one a Chorded Action trigger."),
				*Action, *Other, *KeyName, *Context);
		}
		break;

	case EBindFindingKind::ContextNeverAdded:
		Sentence = FString::Printf(
			TEXT("%s was never handed to AddMappingContext in this session. Either nothing adds it, or nothing in this session reached the code that does - play through the part of the game that should use it and scan again."),
			*Context);
		break;

	case EBindFindingKind::Unreachable:
		Sentence = FString::Printf(
			TEXT("%s is bound only in contexts that were never added in this session (%s), so nothing could have triggered it. Check that something adds %s, or play through the part of the game that does."),
			*Action, *Context, *Context);
		break;

	default:
		Sentence = FString::Printf(TEXT("%s: unclassified finding."), *Action);
		break;
	}

	if (Finding.bExcluded)
	{
		Sentence += TEXT(" Excluded by settings - counted, shown, and not allowed to change the verdict.");
	}

	return Sentence;
}

FBindGuardReport UBindGuardStatics::Analyze(const FBindGuardScanInput& Input, const FBindGuardRequirements& Requirements)
{
	using namespace BindGuardRules;

	const double Start = FPlatformTime::Seconds();

	FBindGuardReport Report;
	Report.bHasRun = true;
	Report.ActionCount = Input.Actions.Num();
	Report.ContextCount = Input.Contexts.Num();
	Report.MappingCount = Input.Mappings.Num();
	Report.bObservationAvailable = Input.bObservationAvailable;
	Report.ObservedContextCount = Input.ObservedContexts.Num();
	Report.SourceName = Input.SourceName;

	Report.Findings.Append(FindUnbound(Input.Mappings, Input.Actions, Requirements));
	Report.Findings.Append(FindDeviceGaps(Input.Mappings, Requirements));
	Report.Findings.Append(FindConflicts(Input.Mappings, Requirements));
	Report.Findings.Append(FindContextGaps(Input, Requirements));

	// Errors first, because a report you have to scroll is a report whose worst line you never read.
	// Everything below severity is sorted by name so the order is stable between two runs.
	Report.Findings.Sort([](const FBindGuardFinding& A, const FBindGuardFinding& B)
	{
		const int32 SeverityA = SeverityRank(A.Severity);
		const int32 SeverityB = SeverityRank(B.Severity);
		if (SeverityA != SeverityB)
		{
			return SeverityA < SeverityB;
		}

		if (A.bExcluded != B.bExcluded)
		{
			return !A.bExcluded;
		}

		if (A.Kind != B.Kind)
		{
			return static_cast<uint8>(A.Kind) < static_cast<uint8>(B.Kind);
		}

		if (A.ActionName != B.ActionName)
		{
			return A.ActionName.LexicalLess(B.ActionName);
		}

		return A.ContextName.LexicalLess(B.ContextName);
	});

	for (const FBindGuardFinding& Finding : Report.Findings)
	{
		if (Finding.bExcluded)
		{
			++Report.ExcludedCount;
		}

		switch (Finding.Severity)
		{
		case EBindSeverity::Error:		++Report.ErrorCount; break;
		case EBindSeverity::Warning:	++Report.WarningCount; break;
		default:						++Report.InfoCount; break;
		}
	}

	Report.Verdict = Judge(Report.Findings);

	// What was actually looked for, in words, so a green report can never be read as a report that never
	// ran. Every line here is a check that really executed on this scan.
	Report.ChecksRun.Add(TEXT("actions that appear in no mapping context (static)"));

	if (Requirements.bRequireKeyboardMouse)
	{
		Report.ChecksRun.Add(TEXT("keyboard/mouse coverage for every bound action (static)"));
	}
	if (Requirements.bRequireGamepad)
	{
		Report.ChecksRun.Add(TEXT("gamepad coverage for every bound action (static)"));
	}
	if (Requirements.bRequireTouch)
	{
		Report.ChecksRun.Add(TEXT("touch coverage for every bound action (static)"));
	}
	if (!Requirements.bRequireKeyboardMouse && !Requirements.bRequireGamepad && !Requirements.bRequireTouch)
	{
		Report.ChecksRun.Add(TEXT("device coverage: NOT CHECKED, no device is required in Project Settings"));
	}

	Report.ChecksRun.Add(TEXT("two actions on one key inside one context (static)"));
	Report.ChecksRun.Add(TEXT("keys shared between two contexts (static, reported as information)"));

	if (Input.bObservationAvailable)
	{
		Report.ChecksRun.Add(FString::Printf(
			TEXT("contexts actually added at runtime (observed: %d added in this session)"), Input.ObservedContexts.Num()));
		Report.ChecksRun.Add(TEXT("actions left unreachable by an unadded context (observed, this session only)"));
	}
	else
	{
		Report.ChecksRun.Add(TEXT("contexts actually added at runtime: NOT CHECKED, no session was observed"));
		Report.ChecksRun.Add(TEXT("actions left unreachable: NOT CHECKED, needs an observed session"));
	}

	if (Requirements.bUseExemptions && (Requirements.ExemptActions.Num() > 0 || Requirements.ExemptContexts.Num() > 0))
	{
		Report.ChecksRun.Add(FString::Printf(TEXT("exemption list applied: %d action(s), %d context(s)"),
			Requirements.ExemptActions.Num(), Requirements.ExemptContexts.Num()));
	}

	Report.ScanMilliseconds = static_cast<float>((FPlatformTime::Seconds() - Start) * 1000.0);
	return Report;
}

//~ Formatting -----------------------------------------------------------------------------------------

FString UBindGuardStatics::FormatHeadline(const FBindGuardReport& Report)
{
	TStringBuilder<256> Line;

	Line.Appendf(TEXT("actions %d | contexts %d | errors %d  warnings %d  info %d | scan %.1f ms"),
		Report.ActionCount, Report.ContextCount,
		Report.ErrorCount, Report.WarningCount, Report.InfoCount,
		Report.ScanMilliseconds);

	if (Report.ExcludedCount > 0)
	{
		Line.Appendf(TEXT(" | %d excluded by settings"), Report.ExcludedCount);
	}

	return FString(Line.ToView());
}

FString UBindGuardStatics::FormatFinding(const FBindGuardFinding& Finding)
{
	TStringBuilder<256> Line;

	Line.Appendf(TEXT("[%s] %-16s "), *SeverityName(Finding.Severity), *KindName(Finding.Kind));

	if (!Finding.ActionName.IsNone())
	{
		Line.Appendf(TEXT("%s"), *Finding.ActionName.ToString());
	}

	if (!Finding.OtherActionName.IsNone())
	{
		Line.Appendf(TEXT(" vs %s"), *Finding.OtherActionName.ToString());
	}

	if (!Finding.ContextName.IsNone())
	{
		Line.Appendf(TEXT("   in %s"), *Finding.ContextName.ToString());
	}

	if (Finding.bCrossContext && !Finding.OtherContextName.IsNone())
	{
		Line.Appendf(TEXT(" / %s"), *Finding.OtherContextName.ToString());
	}

	if (Finding.Key.IsValid())
	{
		Line.Appendf(TEXT("   %s"), *Finding.Key.ToString());
	}

	if (Finding.bExcluded)
	{
		Line.Append(TEXT("   (excluded by settings)"));
	}

	return FString(Line.ToView());
}

FString UBindGuardStatics::DeviceName(const EBindDevice Device)
{
	switch (Device)
	{
	case EBindDevice::KeyboardMouse:	return TEXT("keyboard/mouse");
	case EBindDevice::Gamepad:			return TEXT("gamepad");
	case EBindDevice::Touch:			return TEXT("touch");
	default:							return TEXT("other");
	}
}

FString UBindGuardStatics::KindName(const EBindFindingKind Kind)
{
	switch (Kind)
	{
	case EBindFindingKind::Unbound:				return TEXT("unbound");
	case EBindFindingKind::KeyboardOnly:		return TEXT("keyboard only");
	case EBindFindingKind::GamepadOnly:			return TEXT("gamepad only");
	case EBindFindingKind::DeviceGap:			return TEXT("device gap");
	case EBindFindingKind::Conflict:			return TEXT("conflict");
	case EBindFindingKind::ContextNeverAdded:	return TEXT("never added");
	case EBindFindingKind::Unreachable:			return TEXT("unreachable");
	default:									return TEXT("?");
	}
}

FString UBindGuardStatics::SeverityName(const EBindSeverity Severity)
{
	switch (Severity)
	{
	case EBindSeverity::Error:		return TEXT("error");
	case EBindSeverity::Warning:	return TEXT("warning");
	default:						return TEXT("info");
	}
}

FString UBindGuardStatics::VerdictName(const EBindVerdict Verdict)
{
	switch (Verdict)
	{
	case EBindVerdict::Fail:	return TEXT("fail");
	case EBindVerdict::Warn:	return TEXT("warn");
	default:					return TEXT("ok");
	}
}

int32 UBindGuardStatics::VerdictExitCode(const EBindVerdict Verdict)
{
	switch (Verdict)
	{
	case EBindVerdict::Fail:	return 2;
	case EBindVerdict::Warn:	return 1;
	default:					return 0;
	}
}

FString UBindGuardStatics::ReportToJson(const FBindGuardReport& Report)
{
	// Hand-shaped rather than reflected out of the struct. The field names below are what a build script
	// greps for, which makes them a published interface - and a published interface must not change
	// because somebody renamed a C++ member.
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

	Root->SetStringField(TEXT("tool"), TEXT("BindGuard"));
	Root->SetStringField(TEXT("version"), TEXT("1.0.0"));
	Root->SetStringField(TEXT("verdict"), VerdictName(Report.Verdict));
	Root->SetNumberField(TEXT("exitCode"), VerdictExitCode(Report.Verdict));
	Root->SetStringField(TEXT("source"), Report.SourceName);

	Root->SetNumberField(TEXT("actions"), Report.ActionCount);
	Root->SetNumberField(TEXT("contexts"), Report.ContextCount);
	Root->SetNumberField(TEXT("mappings"), Report.MappingCount);
	Root->SetNumberField(TEXT("errors"), Report.ErrorCount);
	Root->SetNumberField(TEXT("warnings"), Report.WarningCount);
	Root->SetNumberField(TEXT("info"), Report.InfoCount);
	Root->SetNumberField(TEXT("excludedBySettings"), Report.ExcludedCount);

	Root->SetNumberField(TEXT("scanMilliseconds"), Report.ScanMilliseconds);
	Root->SetNumberField(TEXT("gatherMilliseconds"), Report.GatherMilliseconds);

	// The observed half is nested and flagged rather than flattened in beside the static counts, so a
	// script cannot read "0 contexts never added" without also reading whether anything was watching.
	const TSharedRef<FJsonObject> Observation = MakeShared<FJsonObject>();
	Observation->SetBoolField(TEXT("available"), Report.bObservationAvailable);
	Observation->SetNumberField(TEXT("contextsAdded"), Report.ObservedContextCount);
	Root->SetObjectField(TEXT("observation"), Observation);

	TArray<TSharedPtr<FJsonValue>> Checks;
	for (const FString& Check : Report.ChecksRun)
	{
		Checks.Add(MakeShared<FJsonValueString>(Check));
	}
	Root->SetArrayField(TEXT("checks"), Checks);

	TArray<TSharedPtr<FJsonValue>> Findings;
	for (const FBindGuardFinding& Finding : Report.Findings)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("kind"), KindName(Finding.Kind));
		Entry->SetStringField(TEXT("severity"), SeverityName(Finding.Severity));
		Entry->SetStringField(TEXT("action"), Finding.ActionName.ToString());
		Entry->SetStringField(TEXT("context"), Finding.ContextName.ToString());
		Entry->SetStringField(TEXT("otherAction"), Finding.OtherActionName.ToString());
		Entry->SetStringField(TEXT("otherContext"), Finding.OtherContextName.ToString());
		Entry->SetStringField(TEXT("key"), Finding.Key.IsValid() ? Finding.Key.ToString() : FString());
		Entry->SetStringField(TEXT("missingDevice"), DeviceName(Finding.MissingDevice));
		Entry->SetBoolField(TEXT("crossContext"), Finding.bCrossContext);
		Entry->SetBoolField(TEXT("excludedBySettings"), Finding.bExcluded);
		Entry->SetStringField(TEXT("detail"), Finding.Detail);
		Findings.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Root->SetArrayField(TEXT("findings"), Findings);

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

//~ Blueprint access -----------------------------------------------------------------------------------

FBindGuardReport UBindGuardStatics::ScanNow(const UObject* WorldContextObject)
{
	if (UBindGuardSubsystem* Subsystem = UBindGuardSubsystem::Get(WorldContextObject))
	{
		return Subsystem->Scan();
	}

	return FBindGuardReport();
}

FBindGuardReport UBindGuardStatics::GetLastReport(const UObject* WorldContextObject)
{
	const UBindGuardSubsystem* Subsystem = UBindGuardSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetReport() : FBindGuardReport();
}

TArray<FBindGuardFinding> UBindGuardStatics::GetFindings(const UObject* WorldContextObject)
{
	const UBindGuardSubsystem* Subsystem = UBindGuardSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetReport().Findings : TArray<FBindGuardFinding>();
}

EBindVerdict UBindGuardStatics::GetVerdict(const UObject* WorldContextObject)
{
	const UBindGuardSubsystem* Subsystem = UBindGuardSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetVerdict() : EBindVerdict::Ok;
}

bool UBindGuardStatics::WriteReport(const UObject* WorldContextObject, const FString& Path)
{
	UBindGuardSubsystem* Subsystem = UBindGuardSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->WriteReport(Path) : false;
}

void UBindGuardStatics::SetReportVisible(const UObject* WorldContextObject, const bool bVisible)
{
	if (UBindGuardSubsystem* Subsystem = UBindGuardSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetReportVisible(bVisible);
	}
}

bool UBindGuardStatics::IsReportVisible(const UObject* WorldContextObject)
{
	const UBindGuardSubsystem* Subsystem = UBindGuardSubsystem::Get(WorldContextObject);
	return Subsystem && Subsystem->IsReportVisible();
}

void UBindGuardStatics::SetExemptionsEnabled(const UObject* WorldContextObject, const bool bEnabled)
{
	if (UBindGuardSubsystem* Subsystem = UBindGuardSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetExemptionsEnabled(bEnabled);
	}
}

bool UBindGuardStatics::AreExemptionsEnabled(const UObject* WorldContextObject)
{
	const UBindGuardSubsystem* Subsystem = UBindGuardSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->AreExemptionsEnabled() : UBindGuardSettings::Get().bUseExemptions;
}

TArray<FName> UBindGuardStatics::GetObservedContexts(const UObject* WorldContextObject)
{
	const UBindGuardSubsystem* Subsystem = UBindGuardSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetObservedContexts() : TArray<FName>();
}

//~ Fixing things --------------------------------------------------------------------------------------

bool UBindGuardStatics::AddKeyMapping(UInputMappingContext* Context, UInputAction* Action, const FKey& Key)
{
	if (!IsValid(Context) || !IsValid(Action) || !Key.IsValid())
	{
		UE_LOG(LogBindGuard, Warning, TEXT("AddKeyMapping: needs a context, an action and a valid key."));
		return false;
	}

	Context->MapKey(Action, Key);

	// Without the rebuild the mapping exists on the asset but not in the player's control mappings, so the
	// key would do nothing until something else happened to trigger a rebuild. Forced immediately, because
	// the whole point of the demo's fix button is that the very next scan sees it.
	UEnhancedInputLibrary::RequestRebuildControlMappingsUsingContext(Context, /*bForceImmediately=*/true);

	UE_LOG(LogBindGuard, Log, TEXT("BindGuard: mapped %s to %s in %s."),
		*Action->GetName(), *Key.ToString(), *Context->GetName());
	return true;
}

bool UBindGuardStatics::RemoveKeyMapping(UInputMappingContext* Context, UInputAction* Action, const FKey& Key)
{
	if (!IsValid(Context) || !IsValid(Action) || !Key.IsValid())
	{
		return false;
	}

	Context->UnmapKey(Action, Key);
	UEnhancedInputLibrary::RequestRebuildControlMappingsUsingContext(Context, /*bForceImmediately=*/true);

	UE_LOG(LogBindGuard, Log, TEXT("BindGuard: unmapped %s from %s in %s."),
		*Action->GetName(), *Key.ToString(), *Context->GetName());
	return true;
}
