// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BindGuardTypes.h"

struct FEnhancedActionKeyMapping;
class UInputAction;
class UInputMappingContext;

/**
 * The part that turns assets into the plain structs the rules work on, and back into a file.
 *
 * Everything here is static and world-free, so the console commands, the subsystem and the editor menu
 * all reach the same scan. It is kept apart from UBindGuardStatics for one reason: this half touches
 * UObjects and the settings, and the half next door must not, because the half next door is the half the
 * automation tests can drive with an array literal.
 */
struct BINDGUARD_API FBindGuardScanner
{
	/**
	 * Load the assets, flatten every mapping, and hand back the input the rules take.
	 *
	 * OutGatherMilliseconds is measured separately from the rules because one of the two numbers is your
	 * disk and the other one is arithmetic, and a buyer deciding whether to put this in a build step is
	 * entitled to see which is which.
	 */
	static FBindGuardScanInput Gather(const TArray<FName>& ObservedContexts, bool bObservationAvailable, float& OutGatherMilliseconds);

	/** Gather, then analyse. The whole scan, with no world involved. */
	static FBindGuardReport Run(const TArray<FName>& ObservedContexts, bool bObservationAvailable, const FBindGuardRequirements& Requirements);

	/** Gather and analyse using whatever Project Settings currently say. What the console commands call. */
	static FBindGuardReport RunWithProjectSettings(const TArray<FName>& ObservedContexts, bool bObservationAvailable);

	/** Flatten one context - default mappings and every profile override - into plain structs. */
	static void FlattenContext(const UInputMappingContext* Context, TArray<FBindGuardMapping>& OutMappings);

	/**
	 * The chord layer of a mapping, as a sorted comma-joined list of the actions that must also be held.
	 *
	 * Built only from Chorded Action triggers, on the mapping and on the action, because a chord is the
	 * one trigger type that genuinely makes two bindings on one key distinct. Pressed and Held do not:
	 * both of those fire from the same key press, so treating them as different would let a checker
	 * declare a real collision harmless.
	 */
	static FString MakeChordSignature(const FEnhancedActionKeyMapping& Mapping, const UInputAction* Action);

	/**
	 * Write the JSON report. An empty path means the one in Project Settings; a relative path is taken
	 * relative to the project directory. OutFullPath comes back absolute so a build script can find it.
	 */
	static bool WriteReportFile(const FBindGuardReport& Report, const FString& Path, FString& OutFullPath);

	/** The headline and, optionally, every finding, to the log. */
	static void LogReport(const FBindGuardReport& Report, bool bAllFindings);
};
