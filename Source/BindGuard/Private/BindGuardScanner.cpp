// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "BindGuardScanner.h"

#include "BindGuardAssetSource.h"
#include "BindGuardLog.h"
#include "BindGuardSettings.h"
#include "BindGuardStatics.h"
#include "EnhancedActionKeyMapping.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace BindGuardScannerPrivate
{
	/** Collects the chord actions off one trigger array into a name list. */
	static void CollectChords(const TArray<TObjectPtr<UInputTrigger>>& Triggers, TArray<FString>& OutChords)
	{
		for (const TObjectPtr<UInputTrigger>& Trigger : Triggers)
		{
			const UInputTriggerChordAction* Chord = Cast<UInputTriggerChordAction>(Trigger.Get());
			if (Chord == nullptr)
			{
				continue;
			}

			// A chord blocker is a chord action the engine instantiates by itself to stop the chording key
			// from also firing its own mapping. It is machinery, not authored intent, and counting it
			// would make two bindings look distinct because the engine added something behind your back.
			if (Chord->IsA<UInputTriggerChordBlocker>())
			{
				continue;
			}

			if (const UInputAction* ChordAction = Chord->ChordAction.Get())
			{
				OutChords.AddUnique(ChordAction->GetName());
			}
		}
	}

	/** One flattened mapping, filled in from the asset. */
	static void AppendMapping(
		const UInputMappingContext* Context,
		const FEnhancedActionKeyMapping& Mapping,
		const FString& ProfileId,
		TArray<FBindGuardMapping>& OutMappings)
	{
		const UInputAction* Action = Mapping.Action.Get();

		// A mapping row with no action is an unfinished row in the editor, not a binding. It cannot be
		// unbound, cannot collide and cannot cover a device, so there is nothing true to say about it.
		if (Action == nullptr)
		{
			return;
		}

		FBindGuardMapping Flat;
		Flat.ActionName = FName(*Action->GetName());
		Flat.ContextName = FName(*Context->GetName());
		Flat.Key = Mapping.Key;
		Flat.ChordSignature = FBindGuardScanner::MakeChordSignature(Mapping, Action);
		Flat.ProfileId = ProfileId;
		Flat.ActionPath = Action->GetPathName();
		Flat.ContextPath = Context->GetPathName();

		OutMappings.Add(MoveTemp(Flat));
	}
}

FString FBindGuardScanner::MakeChordSignature(const FEnhancedActionKeyMapping& Mapping, const UInputAction* Action)
{
	using namespace BindGuardScannerPrivate;

	TArray<FString> Chords;

	CollectChords(Mapping.Triggers, Chords);

	if (Action != nullptr)
	{
		// Triggers on the action itself apply to every one of its mappings, so they belong in the
		// signature too - otherwise an action chorded once on the asset would look unchorded everywhere.
		CollectChords(Action->Triggers, Chords);
	}

	// Sorted so that two mappings with the same chords in a different order produce the same signature and
	// are correctly seen as colliding.
	Chords.Sort();
	return FString::Join(Chords, TEXT(","));
}

void FBindGuardScanner::FlattenContext(const UInputMappingContext* Context, TArray<FBindGuardMapping>& OutMappings)
{
	using namespace BindGuardScannerPrivate;

	if (!IsValid(Context))
	{
		return;
	}

	for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
	{
		AppendMapping(Context, Mapping, FString(), OutMappings);
	}

	// The player-mappable key profiles. Two actions sharing a key in two different profiles are not in
	// conflict - only one profile is ever active - which is why the profile id travels with the mapping
	// and ends up in the grouping key the conflict rule uses.
	for (const FString& ProfileId : Context->GetProfilesWithOverridenMappings())
	{
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappingsForProfile(ProfileId))
		{
			AppendMapping(Context, Mapping, ProfileId, OutMappings);
		}
	}
}

FBindGuardScanInput FBindGuardScanner::Gather(
	const TArray<FName>& ObservedContexts,
	const bool bObservationAvailable,
	float& OutGatherMilliseconds)
{
	const double Start = FPlatformTime::Seconds();

	TArray<const UInputMappingContext*> Contexts;
	TArray<const UInputAction*> Actions;
	FBindGuardAssetSource::Gather(Contexts, Actions);

	FBindGuardScanInput Input;
	Input.bObservationAvailable = bObservationAvailable;
	Input.ObservedContexts = ObservedContexts;
	Input.SourceName = FBindGuardAssetSource::GetSourceName();

	Input.Contexts.Reserve(Contexts.Num());
	for (const UInputMappingContext* Context : Contexts)
	{
		if (!IsValid(Context))
		{
			continue;
		}

		Input.Contexts.AddUnique(FName(*Context->GetName()));
		FlattenContext(Context, Input.Mappings);
	}

	Input.Actions.Reserve(Actions.Num());
	for (const UInputAction* Action : Actions)
	{
		if (IsValid(Action))
		{
			Input.Actions.AddUnique(FName(*Action->GetName()));
		}
	}

	OutGatherMilliseconds = static_cast<float>((FPlatformTime::Seconds() - Start) * 1000.0);
	return Input;
}

FBindGuardReport FBindGuardScanner::Run(
	const TArray<FName>& ObservedContexts,
	const bool bObservationAvailable,
	const FBindGuardRequirements& Requirements)
{
	float GatherMilliseconds = 0.0f;
	const FBindGuardScanInput Input = Gather(ObservedContexts, bObservationAvailable, GatherMilliseconds);

	FBindGuardReport Report = UBindGuardStatics::Analyze(Input, Requirements);
	Report.GatherMilliseconds = GatherMilliseconds;
	return Report;
}

FBindGuardReport FBindGuardScanner::RunWithProjectSettings(const TArray<FName>& ObservedContexts, const bool bObservationAvailable)
{
	return Run(ObservedContexts, bObservationAvailable, UBindGuardSettings::Get().MakeRequirements());
}

bool FBindGuardScanner::WriteReportFile(const FBindGuardReport& Report, const FString& Path, FString& OutFullPath)
{
	FString Target = Path.IsEmpty() ? UBindGuardSettings::Get().ReportPath : Path;
	if (Target.IsEmpty())
	{
		Target = TEXT("Saved/BindGuard/report.json");
	}

	OutFullPath = FPaths::IsRelative(Target)
		? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / Target)
		: Target;

	if (!FFileHelper::SaveStringToFile(UBindGuardStatics::ReportToJson(Report), *OutFullPath))
	{
		UE_LOG(LogBindGuard, Error, TEXT("BindGuard: could not write the report to %s."), *OutFullPath);
		return false;
	}

	UE_LOG(LogBindGuard, Display, TEXT("BindGuard: report written to %s (verdict %s, exit code %d)."),
		*OutFullPath,
		*UBindGuardStatics::VerdictName(Report.Verdict),
		UBindGuardStatics::VerdictExitCode(Report.Verdict));
	return true;
}

void FBindGuardScanner::LogReport(const FBindGuardReport& Report, const bool bAllFindings)
{
	UE_LOG(LogBindGuard, Display, TEXT("BindGuard: %s | mappings %d | source %s | verdict %s"),
		*UBindGuardStatics::FormatHeadline(Report),
		Report.MappingCount,
		*Report.SourceName,
		*UBindGuardStatics::VerdictName(Report.Verdict));

	if (!Report.bObservationAvailable)
	{
		UE_LOG(LogBindGuard, Display,
			TEXT("  observed checks did NOT run - no session was watched. Nothing here says anything about which contexts get added."));
	}

	if (bAllFindings)
	{
		for (const FBindGuardFinding& Finding : Report.Findings)
		{
			UE_LOG(LogBindGuard, Display, TEXT("  %s"), *UBindGuardStatics::FormatFinding(Finding));
			UE_LOG(LogBindGuard, Display, TEXT("      %s"), *Finding.Detail);
		}

		for (const FString& Check : Report.ChecksRun)
		{
			UE_LOG(LogBindGuard, Display, TEXT("  checked: %s"), *Check);
		}
	}
}
