// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BindGuardTypes.h"
#include "BindGuardStatics.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * The rules, and the Blueprint entry points.
 *
 * Everything above the divider is a static function over plain structs. No world, no subsystem, no asset
 * registry, no player. That is not tidiness for its own sake - it is the reason the automation tests can
 * put four mappings in an array and assert on the exact finding that comes back, and it is the reason
 * there is exactly one copy of each rule. The subsystem, the console commands and the editor menu all
 * call these functions; none of them contains a second, slightly different implementation that could
 * drift.
 *
 * Everything below the divider is what a Blueprint calls: one node per button on the demo panel.
 */
UCLASS(meta = (DisplayName = "Bind Guard Statics"))
class BINDGUARD_API UBindGuardStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//~ The rules ---------------------------------------------------------------------------------------

	/**
	 * Which device family a key belongs to.
	 *
	 * Decided by asking the FKey - IsTouch, IsGamepadKey, IsMouseButton - in that order, because touch
	 * points and gamepad keys are mutually exclusive in the engine's own registration and asking in a
	 * fixed order makes the answer reproducible. A key the engine does not know is Other, and an Other
	 * key never satisfies a device requirement: an unknown key is not evidence of coverage.
	 */
	UFUNCTION(BlueprintPure, Category = "BindGuard|Rules")
	static EBindDevice ClassifyKey(const FKey& Key);

	/**
	 * Actions that appear in no mapping context at all.
	 *
	 * Needs the full list of action assets, not just the mapped ones - an action that is bound nowhere
	 * leaves no trace in any context, so the only way to notice it is to know it exists. This is what the
	 * editor module's asset registry walk is for, and it is why a scan run purely off loaded objects says
	 * so on the report.
	 */
	UFUNCTION(BlueprintCallable, Category = "BindGuard|Rules")
	static TArray<FBindGuardFinding> FindUnbound(const TArray<FBindGuardMapping>& Mappings, const TArray<FName>& Actions, const FBindGuardRequirements& Requirements);

	/**
	 * Actions that are bound, but not on every device the project requires.
	 *
	 * One finding per action per missing device. An action bound only on the keyboard in a project that
	 * requires a gamepad comes back as KeyboardOnly, which is the finding this whole plugin exists for.
	 */
	UFUNCTION(BlueprintCallable, Category = "BindGuard|Rules")
	static TArray<FBindGuardFinding> FindDeviceGaps(const TArray<FBindGuardMapping>& Mappings, const FBindGuardRequirements& Requirements);

	/**
	 * Two actions on one key.
	 *
	 * Inside one context and one chord layer that is an error: both actions will be evaluated and both
	 * will fire, and whichever one the designer did not mean is a bug that only shows up when somebody
	 * presses the key. Between two contexts it is not an error and is not reported as one - reusing a key
	 * across contexts is the entire reason contexts exist - so it comes back as a separate finding with
	 * bCrossContext set and a severity that is Info by default and can never be raised past Warning.
	 *
	 * The chord layer is part of the grouping key, so Shift+E and E are two bindings, not a collision.
	 * Trigger types are deliberately not: Pressed and Held on one key really do both fire.
	 */
	UFUNCTION(BlueprintCallable, Category = "BindGuard|Rules")
	static TArray<FBindGuardFinding> FindConflicts(const TArray<FBindGuardMapping>& Mappings, const FBindGuardRequirements& Requirements);

	/**
	 * The two observed checks: contexts nothing added, and actions left unreachable by them.
	 *
	 * Returns nothing at all when Input.bObservationAvailable is false. That is the important line in this
	 * class. A scan with no session behind it has not proved that every context gets added - it has proved
	 * nothing whatsoever about that question - and returning an empty array that the report then prints as
	 * "clean" would be the single most dishonest thing this plugin could do.
	 */
	UFUNCTION(BlueprintCallable, Category = "BindGuard|Rules")
	static TArray<FBindGuardFinding> FindContextGaps(const FBindGuardScanInput& Input, const FBindGuardRequirements& Requirements);

	/** Fail with at least one Error, Warn with at least one Warning and no Error, otherwise Ok. */
	UFUNCTION(BlueprintPure, Category = "BindGuard|Rules")
	static EBindVerdict Judge(const TArray<FBindGuardFinding>& Findings);

	/**
	 * The plain sentence: what is wrong, and what to do about it.
	 *
	 * Every finding carries one of these, generated once when the finding is made, so the on-screen panel,
	 * the log and the JSON all say the same words. "3 actions have no gamepad binding" is a number nobody
	 * can act on. "IA_Crouch is bound to C and to nothing on the gamepad - map it in IMC_Default" is a fix.
	 */
	UFUNCTION(BlueprintPure, Category = "BindGuard|Rules")
	static FString Explain(const FBindGuardFinding& Finding);

	/** Run every rule, sort, count and judge. This is the whole scan, minus the part that loads assets. */
	UFUNCTION(BlueprintCallable, Category = "BindGuard|Rules")
	static FBindGuardReport Analyze(const FBindGuardScanInput& Input, const FBindGuardRequirements& Requirements);

	//~ Formatting --------------------------------------------------------------------------------------

	/** `actions 24 | contexts 5 | errors 2  warnings 3  info 1 | scan 12.4 ms` */
	UFUNCTION(BlueprintPure, Category = "BindGuard|Report")
	static FString FormatHeadline(const FBindGuardReport& Report);

	/** `[error] keyboard only   IA_Jump   in IMC_Default   SpaceBar` */
	UFUNCTION(BlueprintPure, Category = "BindGuard|Report")
	static FString FormatFinding(const FBindGuardFinding& Finding);

	UFUNCTION(BlueprintPure, Category = "BindGuard|Report")
	static FString DeviceName(EBindDevice Device);

	UFUNCTION(BlueprintPure, Category = "BindGuard|Report")
	static FString KindName(EBindFindingKind Kind);

	UFUNCTION(BlueprintPure, Category = "BindGuard|Report")
	static FString SeverityName(EBindSeverity Severity);

	UFUNCTION(BlueprintPure, Category = "BindGuard|Report")
	static FString VerdictName(EBindVerdict Verdict);

	/** 0 for Ok, 1 for Warn, 2 for Fail - the same three numbers the gate exits with. */
	UFUNCTION(BlueprintPure, Category = "BindGuard|Report")
	static int32 VerdictExitCode(EBindVerdict Verdict);

	/** The report as JSON, exactly as BindGuard.Report writes it. */
	static FString ReportToJson(const FBindGuardReport& Report);

	//~ Blueprint access --------------------------------------------------------------------------------

	/** Scan now and return the report. The "check" button on the demo panel. */
	UFUNCTION(BlueprintCallable, Category = "BindGuard", meta = (WorldContext = "WorldContextObject"))
	static FBindGuardReport ScanNow(const UObject* WorldContextObject);

	/** The report from the last scan. bHasRun is false if there has not been one. */
	UFUNCTION(BlueprintPure, Category = "BindGuard", meta = (WorldContext = "WorldContextObject"))
	static FBindGuardReport GetLastReport(const UObject* WorldContextObject);

	/** The findings from the last scan, already sorted errors first. */
	UFUNCTION(BlueprintPure, Category = "BindGuard", meta = (WorldContext = "WorldContextObject"))
	static TArray<FBindGuardFinding> GetFindings(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "BindGuard", meta = (WorldContext = "WorldContextObject"))
	static EBindVerdict GetVerdict(const UObject* WorldContextObject);

	/** Write the JSON report. An empty path means the one in Project Settings. */
	UFUNCTION(BlueprintCallable, Category = "BindGuard", meta = (WorldContext = "WorldContextObject"))
	static bool WriteReport(const UObject* WorldContextObject, const FString& Path);

	UFUNCTION(BlueprintCallable, Category = "BindGuard", meta = (WorldContext = "WorldContextObject"))
	static void SetReportVisible(const UObject* WorldContextObject, bool bVisible);

	UFUNCTION(BlueprintPure, Category = "BindGuard", meta = (WorldContext = "WorldContextObject"))
	static bool IsReportVisible(const UObject* WorldContextObject);

	/**
	 * Turn the exemption list on and off for this session, without touching the settings asset.
	 *
	 * The demo panel's fourth button. Watching the same project go from "clean" to four errors and back
	 * is the fastest way to understand what an exemption list actually costs you.
	 */
	UFUNCTION(BlueprintCallable, Category = "BindGuard", meta = (WorldContext = "WorldContextObject"))
	static void SetExemptionsEnabled(const UObject* WorldContextObject, bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "BindGuard", meta = (WorldContext = "WorldContextObject"))
	static bool AreExemptionsEnabled(const UObject* WorldContextObject);

	/** The contexts that were actually added during this session, in the order they were first seen. */
	UFUNCTION(BlueprintPure, Category = "BindGuard", meta = (WorldContext = "WorldContextObject"))
	static TArray<FName> GetObservedContexts(const UObject* WorldContextObject);

	//~ Fixing things -----------------------------------------------------------------------------------

	/**
	 * Add a key mapping to a context at runtime and rebuild the control mappings so it takes effect now.
	 *
	 * This is the demo's "fix it" button, and it is a real fix, not a fake one: the binding genuinely
	 * exists afterwards, the next scan genuinely finds it, and the report genuinely goes from red to
	 * green in front of you. It is also a legitimate shipping API - a rebinding screen does exactly this.
	 *
	 * Note that it edits the context asset in memory. In the editor that will mark the asset dirty, which
	 * is correct and visible rather than hidden.
	 */
	UFUNCTION(BlueprintCallable, Category = "BindGuard|Fix")
	static bool AddKeyMapping(UInputMappingContext* Context, UInputAction* Action, const FKey& Key);

	/** The other half, so the demo can put the mistake back and show the red report again. */
	UFUNCTION(BlueprintCallable, Category = "BindGuard|Fix")
	static bool RemoveKeyMapping(UInputMappingContext* Context, UInputAction* Action, const FKey& Key);
};
