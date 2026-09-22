// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "BindGuardStatics.h"
#include "BindGuardTypes.h"
#include "InputCoreTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace BindGuardTests
{
	// CommandletContext as well as EditorContext. Every rule in this plugin is a place it can be quietly
	// wrong, and a test that only runs when somebody has the editor open is a test that will not be there
	// on the build machine - which is exactly where this plugin is meant to live.
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::EngineFilter;

	FBindGuardMapping Map(const TCHAR* Action, const TCHAR* Context, const FKey Key, const TCHAR* Chord = TEXT(""))
	{
		FBindGuardMapping Mapping(FName(Action), FName(Context), Key);
		Mapping.ChordSignature = Chord;
		return Mapping;
	}

	/** Keyboard and gamepad required, nothing exempt: the shipping default. */
	FBindGuardRequirements DefaultRequirements()
	{
		return FBindGuardRequirements();
	}

	int32 CountOfKind(const TArray<FBindGuardFinding>& Findings, const EBindFindingKind Kind)
	{
		int32 Count = 0;
		for (const FBindGuardFinding& Finding : Findings)
		{
			Count += (Finding.Kind == Kind) ? 1 : 0;
		}
		return Count;
	}

	const FBindGuardFinding* FindByAction(const TArray<FBindGuardFinding>& Findings, const TCHAR* Action)
	{
		const FName Name(Action);
		return Findings.FindByPredicate([Name](const FBindGuardFinding& Finding) { return Finding.ActionName == Name; });
	}
}

//
// (1) The device a key belongs to is read off the key, not off its name.
//
// This is the foundation the whole plugin stands on. If a gamepad key were ever classified as a keyboard
// key, an action bound only on the controller would silently count as keyboard coverage and the report
// would be green on precisely the project it exists to catch.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBindGuardClassifyKeyTest,
	"BindGuard.Rules.ClassifyKeySortsEveryDeviceFamily",
	BindGuardTests::TestFlags)

bool FBindGuardClassifyKeyTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("a keyboard key is keyboard/mouse"),
		UBindGuardStatics::ClassifyKey(EKeys::SpaceBar), EBindDevice::KeyboardMouse);

	TestEqual(TEXT("a letter key is keyboard/mouse"),
		UBindGuardStatics::ClassifyKey(EKeys::E), EBindDevice::KeyboardMouse);

	TestEqual(TEXT("a mouse button is keyboard/mouse"),
		UBindGuardStatics::ClassifyKey(EKeys::LeftMouseButton), EBindDevice::KeyboardMouse);

	TestEqual(TEXT("a mouse axis is keyboard/mouse"),
		UBindGuardStatics::ClassifyKey(EKeys::MouseWheelAxis), EBindDevice::KeyboardMouse);

	TestEqual(TEXT("a face button is gamepad"),
		UBindGuardStatics::ClassifyKey(EKeys::Gamepad_FaceButton_Bottom), EBindDevice::Gamepad);

	// The analog sticks are the case a name-based classifier gets wrong: they are axes, not buttons, and
	// they are still unambiguously gamepad.
	TestEqual(TEXT("a thumbstick axis is gamepad"),
		UBindGuardStatics::ClassifyKey(EKeys::Gamepad_LeftX), EBindDevice::Gamepad);

	TestEqual(TEXT("a trigger axis is gamepad"),
		UBindGuardStatics::ClassifyKey(EKeys::Gamepad_RightTriggerAxis), EBindDevice::Gamepad);

	TestEqual(TEXT("a touch point is touch"),
		UBindGuardStatics::ClassifyKey(EKeys::TouchKeys[0]), EBindDevice::Touch);

	// An invalid key must never satisfy a device requirement. An empty key row in a mapping context is
	// exactly the shape of the bug this plugin looks for, so it cannot be allowed to look like coverage.
	TestEqual(TEXT("an invalid key is other, never a device"),
		UBindGuardStatics::ClassifyKey(EKeys::Invalid), EBindDevice::Other);

	return true;
}

//
// (2) An action nothing binds is reported by name.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBindGuardUnboundTest,
	"BindGuard.Rules.AnActionInNoContextIsUnbound",
	BindGuardTests::TestFlags)

bool FBindGuardUnboundTest::RunTest(const FString& Parameters)
{
	using namespace BindGuardTests;

	const TArray<FBindGuardMapping> Mappings =
	{
		Map(TEXT("IA_Jump"), TEXT("IMC_Default"), EKeys::SpaceBar),
		Map(TEXT("IA_Jump"), TEXT("IMC_Default"), EKeys::Gamepad_FaceButton_Bottom),
	};

	const TArray<FName> Actions = { TEXT("IA_Jump"), TEXT("IA_Whistle") };

	const TArray<FBindGuardFinding> Findings = UBindGuardStatics::FindUnbound(Mappings, Actions, DefaultRequirements());

	TestEqual(TEXT("exactly one action is unbound"), Findings.Num(), 1);

	if (Findings.Num() == 1)
	{
		TestEqual(TEXT("and it is the one nothing maps"), Findings[0].ActionName, FName(TEXT("IA_Whistle")));
		TestEqual(TEXT("reported as an error by default"), Findings[0].Severity, EBindSeverity::Error);
		TestFalse(TEXT("the sentence is not empty"), Findings[0].Detail.IsEmpty());
	}

	// A mapping row with an invalid key is not a binding. Without this, an unfinished row in the editor
	// would silently mark an action as bound.
	const TArray<FBindGuardMapping> EmptyKeyRow = { Map(TEXT("IA_Whistle"), TEXT("IMC_Default"), EKeys::Invalid) };
	const TArray<FBindGuardFinding> StillUnbound =
		UBindGuardStatics::FindUnbound(EmptyKeyRow, { TEXT("IA_Whistle") }, DefaultRequirements());

	TestEqual(TEXT("a row with no key does not count as a binding"), StillUnbound.Num(), 1);

	return true;
}

//
// (3) One context is a conflict; two contexts are not.
//
// The honest half of the conflict rule and the reason it can be trusted. Reusing a key across contexts is
// the entire purpose of contexts, so a checker that called it an error would be a checker whose first run
// on a real project produces a page of findings that are all wrong.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBindGuardConflictTest,
	"BindGuard.Rules.OneContextIsAConflictTwoContextsAreNot",
	BindGuardTests::TestFlags)

bool FBindGuardConflictTest::RunTest(const FString& Parameters)
{
	using namespace BindGuardTests;

	{
		const TArray<FBindGuardMapping> SameContext =
		{
			Map(TEXT("IA_Jump"), TEXT("IMC_Default"), EKeys::SpaceBar),
			Map(TEXT("IA_Interact"), TEXT("IMC_Default"), EKeys::SpaceBar),
		};

		const TArray<FBindGuardFinding> Findings = UBindGuardStatics::FindConflicts(SameContext, DefaultRequirements());

		TestEqual(TEXT("two actions on one key in one context is one finding"), Findings.Num(), 1);
		if (Findings.Num() == 1)
		{
			TestEqual(TEXT("and it is an error"), Findings[0].Severity, EBindSeverity::Error);
			TestFalse(TEXT("and it is not marked cross-context"), Findings[0].bCrossContext);
			TestTrue(TEXT("and it names both actions"),
				!Findings[0].ActionName.IsNone() && !Findings[0].OtherActionName.IsNone());
		}
	}

	{
		const TArray<FBindGuardMapping> TwoContexts =
		{
			Map(TEXT("IA_Jump"), TEXT("IMC_Default"), EKeys::SpaceBar),
			Map(TEXT("IA_MenuAccept"), TEXT("IMC_Menu"), EKeys::SpaceBar),
		};

		const TArray<FBindGuardFinding> Findings = UBindGuardStatics::FindConflicts(TwoContexts, DefaultRequirements());

		TestEqual(TEXT("the same key in two contexts is reported once, as a hint"), Findings.Num(), 1);
		if (Findings.Num() == 1)
		{
			TestTrue(TEXT("marked as cross-context"), Findings[0].bCrossContext);
			TestEqual(TEXT("and only information, never an error"), Findings[0].Severity, EBindSeverity::Info);
		}

		TestEqual(TEXT("a cross-context hint alone is a pass"),
			UBindGuardStatics::Judge(Findings), EBindVerdict::Ok);
	}

	{
		// A chord is the one thing that genuinely separates two bindings on one key. Shift+E and E are two
		// bindings, and calling them a collision would be a false positive on a perfectly ordinary setup.
		const TArray<FBindGuardMapping> Chorded =
		{
			Map(TEXT("IA_Use"), TEXT("IMC_Default"), EKeys::E),
			Map(TEXT("IA_UseAlternate"), TEXT("IMC_Default"), EKeys::E, TEXT("IA_Modifier")),
		};

		TestEqual(TEXT("different chord layers are not a conflict"),
			UBindGuardStatics::FindConflicts(Chorded, DefaultRequirements()).Num(), 0);
	}

	return true;
}

//
// (4) Keyboard-only is found, and adding the gamepad binding makes it go away.
//
// The headline check, tested in both directions. Finding the fault matters; so does the finding
// disappearing once the fault is fixed, because a report that never goes green is a report nobody uses.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBindGuardDeviceGapTest,
	"BindGuard.Rules.KeyboardOnlyIsFoundAndFixable",
	BindGuardTests::TestFlags)

bool FBindGuardDeviceGapTest::RunTest(const FString& Parameters)
{
	using namespace BindGuardTests;

	const TArray<FBindGuardMapping> KeyboardOnly = { Map(TEXT("IA_Crouch"), TEXT("IMC_Default"), EKeys::C) };

	const TArray<FBindGuardFinding> Findings = UBindGuardStatics::FindDeviceGaps(KeyboardOnly, DefaultRequirements());

	TestEqual(TEXT("one missing device, one finding"), Findings.Num(), 1);
	if (Findings.Num() == 1)
	{
		TestEqual(TEXT("named for the shape a person recognises"), Findings[0].Kind, EBindFindingKind::KeyboardOnly);
		TestEqual(TEXT("and it says which device is missing"), Findings[0].MissingDevice, EBindDevice::Gamepad);
		TestEqual(TEXT("an error, because this is the certification failure"), Findings[0].Severity, EBindSeverity::Error);
	}

	// Add the gamepad binding and the finding must be gone - not downgraded, gone.
	TArray<FBindGuardMapping> Fixed = KeyboardOnly;
	Fixed.Add(Map(TEXT("IA_Crouch"), TEXT("IMC_Default"), EKeys::Gamepad_FaceButton_Right));

	TestEqual(TEXT("with a gamepad binding there is nothing to report"),
		UBindGuardStatics::FindDeviceGaps(Fixed, DefaultRequirements()).Num(), 0);

	// The mirror image, so the rule is not accidentally one-directional.
	const TArray<FBindGuardMapping> GamepadOnly = { Map(TEXT("IA_Taunt"), TEXT("IMC_Default"), EKeys::Gamepad_FaceButton_Top) };
	const TArray<FBindGuardFinding> Mirror = UBindGuardStatics::FindDeviceGaps(GamepadOnly, DefaultRequirements());

	TestEqual(TEXT("gamepad-only is found too"), Mirror.Num(), 1);
	if (Mirror.Num() == 1)
	{
		TestEqual(TEXT("and named the other way round"), Mirror[0].Kind, EBindFindingKind::GamepadOnly);
	}

	return true;
}

//
// (5) An exempt action produces no error - and is still counted and still printed.
//
// The rule that stops the exemption list from becoming a way to make a project look clean from a settings
// page. If this test ever goes red in the direction of "excluded findings vanish", the gate is worthless.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBindGuardExemptionTest,
	"BindGuard.Rules.ExemptActionsAreSilencedButStillCounted",
	BindGuardTests::TestFlags)

bool FBindGuardExemptionTest::RunTest(const FString& Parameters)
{
	using namespace BindGuardTests;

	FBindGuardScanInput Input;
	Input.Mappings =
	{
		Map(TEXT("Debug_OpenConsole"), TEXT("IMC_Debug"), EKeys::Tilde),
		Map(TEXT("IA_Fire"), TEXT("IMC_Default"), EKeys::LeftMouseButton),
		Map(TEXT("IA_Fire"), TEXT("IMC_Default"), EKeys::Gamepad_RightTrigger),
	};
	Input.Actions = { TEXT("Debug_OpenConsole"), TEXT("IA_Fire") };
	Input.Contexts = { TEXT("IMC_Debug"), TEXT("IMC_Default") };

	// Without the exemption: one error, and the verdict fails.
	{
		const FBindGuardReport Report = UBindGuardStatics::Analyze(Input, DefaultRequirements());

		TestEqual(TEXT("the debug key is an error while nothing exempts it"), Report.ErrorCount, 1);
		TestEqual(TEXT("so the gate fails"), Report.Verdict, EBindVerdict::Fail);
		TestEqual(TEXT("and nothing is excluded"), Report.ExcludedCount, 0);
	}

	// With it: no error, verdict clean - and the finding is still there, still named, and counted.
	{
		FBindGuardRequirements Requirements = DefaultRequirements();
		Requirements.ExemptActions.Add(TEXT("Debug_OpenConsole"));

		const FBindGuardReport Report = UBindGuardStatics::Analyze(Input, Requirements);

		TestEqual(TEXT("no errors once it is exempt"), Report.ErrorCount, 0);
		TestEqual(TEXT("the gate passes"), Report.Verdict, EBindVerdict::Ok);
		TestEqual(TEXT("and exactly one finding is counted as excluded"), Report.ExcludedCount, 1);

		const FBindGuardFinding* Finding = FindByAction(Report.Findings, TEXT("Debug_OpenConsole"));
		TestNotNull(TEXT("the finding is still in the report, by name"), Finding);
		if (Finding != nullptr)
		{
			TestTrue(TEXT("flagged as excluded"), Finding->bExcluded);
			TestEqual(TEXT("and forced down to information"), Finding->Severity, EBindSeverity::Info);
			TestTrue(TEXT("and the sentence says so"), Finding->Detail.Contains(TEXT("Excluded by settings")));
		}
	}

	// Turning the master switch off must bring the error back, or the demo's toggle is a lie.
	{
		FBindGuardRequirements Requirements = DefaultRequirements();
		Requirements.ExemptActions.Add(TEXT("Debug_OpenConsole"));
		Requirements.bUseExemptions = false;

		const FBindGuardReport Report = UBindGuardStatics::Analyze(Input, Requirements);
		TestEqual(TEXT("with exemptions off the error is back"), Report.ErrorCount, 1);
		TestEqual(TEXT("and nothing is counted as excluded"), Report.ExcludedCount, 0);
	}

	return true;
}

//
// (6) Judge fails only on an error and warns only on a warning.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBindGuardJudgeTest,
	"BindGuard.Rules.JudgeFailsOnlyOnAnError",
	BindGuardTests::TestFlags)

bool FBindGuardJudgeTest::RunTest(const FString& Parameters)
{
	auto MakeFinding = [](const EBindSeverity Severity, const bool bExcluded = false)
	{
		FBindGuardFinding Finding;
		Finding.Severity = Severity;
		Finding.bExcluded = bExcluded;
		return Finding;
	};

	TestEqual(TEXT("nothing at all is a pass"),
		UBindGuardStatics::Judge(TArray<FBindGuardFinding>()), EBindVerdict::Ok);

	TestEqual(TEXT("information alone is a pass"),
		UBindGuardStatics::Judge({ MakeFinding(EBindSeverity::Info) }), EBindVerdict::Ok);

	TestEqual(TEXT("warnings alone are a warn"),
		UBindGuardStatics::Judge({ MakeFinding(EBindSeverity::Info), MakeFinding(EBindSeverity::Warning) }),
		EBindVerdict::Warn);

	TestEqual(TEXT("one error is a fail, whatever else is there"),
		UBindGuardStatics::Judge({ MakeFinding(EBindSeverity::Warning), MakeFinding(EBindSeverity::Error) }),
		EBindVerdict::Fail);

	// An exempt finding must never move the verdict, which is what makes the exemption list safe to use.
	TestEqual(TEXT("an excluded finding cannot fail a gate"),
		UBindGuardStatics::Judge({ MakeFinding(EBindSeverity::Error, /*bExcluded=*/true) }),
		EBindVerdict::Ok);

	TestEqual(TEXT("ok exits 0"), UBindGuardStatics::VerdictExitCode(EBindVerdict::Ok), 0);
	TestEqual(TEXT("warn exits 1"), UBindGuardStatics::VerdictExitCode(EBindVerdict::Warn), 1);
	TestEqual(TEXT("fail exits 2"), UBindGuardStatics::VerdictExitCode(EBindVerdict::Fail), 2);

	return true;
}

//
// (7) With nothing observed, the observed checks are skipped rather than reported clean.
//
// The most important test in the file, because the failure it guards against is not a crash - it is a
// report that quietly claims to have checked something it could not check. A static-only scan that
// returned "0 contexts never added" would be the single most misleading line this plugin could print.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBindGuardObservationTest,
	"BindGuard.Rules.ObservedChecksAreSkippedNotPassedWhenNothingWasWatched",
	BindGuardTests::TestFlags)

bool FBindGuardObservationTest::RunTest(const FString& Parameters)
{
	using namespace BindGuardTests;

	FBindGuardScanInput Input;
	Input.Mappings =
	{
		Map(TEXT("IA_Fire"), TEXT("IMC_Combat"), EKeys::LeftMouseButton),
		Map(TEXT("IA_Fire"), TEXT("IMC_Combat"), EKeys::Gamepad_RightTrigger),
	};
	Input.Actions = { TEXT("IA_Fire") };
	Input.Contexts = { TEXT("IMC_Combat"), TEXT("IMC_Vehicle") };

	// No session: nothing may be said about which contexts get added.
	{
		Input.bObservationAvailable = false;
		const FBindGuardReport Report = UBindGuardStatics::Analyze(Input, DefaultRequirements());

		TestEqual(TEXT("no context findings without a session"),
			CountOfKind(Report.Findings, EBindFindingKind::ContextNeverAdded), 0);
		TestEqual(TEXT("no unreachable findings without a session"),
			CountOfKind(Report.Findings, EBindFindingKind::Unreachable), 0);
		TestFalse(TEXT("the report says observation was unavailable"), Report.bObservationAvailable);

		const bool bSaysSo = Report.ChecksRun.ContainsByPredicate([](const FString& Check)
		{
			return Check.Contains(TEXT("NOT CHECKED"));
		});
		TestTrue(TEXT("and it says so in words rather than going quiet"), bSaysSo);
	}

	// A session that only ever added IMC_Combat: the vehicle context is now genuinely reportable.
	{
		Input.bObservationAvailable = true;
		Input.ObservedContexts = { TEXT("IMC_Combat") };

		const FBindGuardReport Report = UBindGuardStatics::Analyze(Input, DefaultRequirements());

		TestEqual(TEXT("the context nothing added is reported"),
			CountOfKind(Report.Findings, EBindFindingKind::ContextNeverAdded), 1);

		// IA_Fire lives in a context that WAS added, so it is reachable and must not be reported.
		TestEqual(TEXT("an action in an added context is not unreachable"),
			CountOfKind(Report.Findings, EBindFindingKind::Unreachable), 0);

		TestEqual(TEXT("an observed-only finding is a warning, not an error"), Report.ErrorCount, 0);
		TestEqual(TEXT("so the verdict is warn"), Report.Verdict, EBindVerdict::Warn);
	}

	// An action bound only inside the context nobody added is unreachable.
	{
		Input.Mappings.Add(Map(TEXT("IA_Honk"), TEXT("IMC_Vehicle"), EKeys::H));
		Input.Mappings.Add(Map(TEXT("IA_Honk"), TEXT("IMC_Vehicle"), EKeys::Gamepad_FaceButton_Left));
		Input.Actions.Add(TEXT("IA_Honk"));

		const FBindGuardReport Report = UBindGuardStatics::Analyze(Input, DefaultRequirements());

		TestEqual(TEXT("the action stranded in an unadded context is reported"),
			CountOfKind(Report.Findings, EBindFindingKind::Unreachable), 1);

		// It is bound, so it must not also be reported as unbound - one problem, one line.
		TestEqual(TEXT("and it is not also called unbound"),
			CountOfKind(Report.Findings, EBindFindingKind::Unbound), 0);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
