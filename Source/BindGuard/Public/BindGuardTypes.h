// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "BindGuardTypes.generated.h"

/**
 * Which family of hardware a key belongs to.
 *
 * Read off the FKey itself - IsGamepadKey, IsTouch, IsMouseButton - and never off the name of the action
 * or the name of the mapping context. A project that calls its action IA_Jump_Gamepad and binds it to the
 * space bar gets told the truth; a project that calls everything IA_01 gets exactly the same answer.
 *
 * Keyboard and mouse are one device on purpose. They are the same pair of hands, they are present or
 * absent together on every platform anybody ships to, and no certification requirement has ever asked for
 * one without the other. Splitting them would produce a report full of findings nobody would act on.
 */
UENUM(BlueprintType)
enum class EBindDevice : uint8
{
	/** Keyboard keys, mouse buttons and mouse axes. */
	KeyboardMouse	UMETA(DisplayName = "Keyboard / Mouse"),

	/** Anything the engine flags as a gamepad key, including the analog sticks and triggers. */
	Gamepad			UMETA(DisplayName = "Gamepad"),

	/** Touch points and touch gestures. */
	Touch			UMETA(DisplayName = "Touch"),

	/** An invalid key, or a key the engine puts in none of the families above. */
	Other			UMETA(DisplayName = "Other"),
};

/** How bad a finding is. The verdict, and the process exit code behind the gate, are decided from these. */
UENUM(BlueprintType)
enum class EBindSeverity : uint8
{
	/** Worth knowing, changes nothing. Never fails a gate. */
	Info		UMETA(DisplayName = "Info"),

	/** Probably wrong. Fails the gate with 1. */
	Warning		UMETA(DisplayName = "Warning"),

	/** Wrong. Fails the gate with 2. */
	Error		UMETA(DisplayName = "Error"),
};

/** The three answers the gate can give, and the three numbers it exits with. */
UENUM(BlueprintType)
enum class EBindVerdict : uint8
{
	/** Nothing above Info. Exit code 0. */
	Ok		UMETA(DisplayName = "Ok"),

	/** At least one Warning and no Error. Exit code 1. */
	Warn	UMETA(DisplayName = "Warn"),

	/** At least one Error. Exit code 2. */
	Fail	UMETA(DisplayName = "Fail"),
};

/**
 * What kind of thing went wrong.
 *
 * The first four are static - they are read out of the assets and they are true whether or not anybody
 * ever presses Play. The last two are observed, and they are only ever as true as the session that
 * produced them. The report keeps that distinction visible; see FBindGuardReport::bObservationAvailable.
 */
UENUM(BlueprintType)
enum class EBindFindingKind : uint8
{
	/** An Input Action asset that appears in no mapping context at all. Static. */
	Unbound				UMETA(DisplayName = "Unbound"),

	/** Bound on keyboard or mouse, and the project requires a gamepad binding it does not have. Static. */
	KeyboardOnly		UMETA(DisplayName = "Keyboard Only"),

	/** Bound on the gamepad, and the project requires a keyboard or mouse binding it does not have. Static. */
	GamepadOnly			UMETA(DisplayName = "Gamepad Only"),

	/**
	 * A required device has no binding, and neither of the two names above describes the shape of it.
	 *
	 * Only reachable with the touch requirement turned on, or for an action bound on three devices and
	 * missing a fourth. It exists so that "keyboard only" and "gamepad only" never have to be stretched to
	 * mean something they do not - a finding that has to lie about its own name is a finding nobody trusts.
	 */
	DeviceGap			UMETA(DisplayName = "Device Gap"),

	/** Two actions on the same key with the same chord layer. Static. */
	Conflict			UMETA(DisplayName = "Conflict"),

	/** A mapping context nothing added during the observed session. Observed. */
	ContextNeverAdded	UMETA(DisplayName = "Context Never Added"),

	/** An action whose every mapping context went unadded, so nothing could ever trigger it. Observed. */
	Unreachable			UMETA(DisplayName = "Unreachable"),
};

/**
 * One key bound to one action inside one context: the flattened form of an FEnhancedActionKeyMapping.
 *
 * This is the only thing the rules ever see. It carries names and an FKey and nothing else - no UObject
 * pointers, no asset registry, no world - which is what lets FindUnbound, FindDeviceGaps, FindConflicts
 * and Judge be static functions covered by automation tests rather than something you have to stand a
 * game up to exercise.
 */
USTRUCT(BlueprintType)
struct BINDGUARD_API FBindGuardMapping
{
	GENERATED_BODY()

	/** The Input Action asset's name, e.g. IA_Jump. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	FName ActionName;

	/** The Input Mapping Context asset's name, e.g. IMC_Default. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	FName ContextName;

	/** The key. The device family is derived from this and from nothing else. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	FKey Key;

	/**
	 * The chord layer, as a sorted, comma-joined list of the actions that must also be held.
	 *
	 * Empty for a plain binding. This is what makes Shift+E and E two different bindings rather than a
	 * conflict, and it is deliberately built only from Chorded Action triggers - not from every trigger
	 * and not from modifiers. Pressed and Held on the same key really do both fire, so calling them
	 * "different" would be a checker lying to make its own report shorter.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	FString ChordSignature;

	/**
	 * The player-mappable key profile these mappings override, or empty for the default set.
	 *
	 * Two actions that share a key in two different profiles are not in conflict - only one profile is
	 * ever active - so the profile is part of the grouping key, exactly like the context is.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	FString ProfileId;

	/** Package path of the action asset, so a report can point at the thing it is complaining about. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	FString ActionPath;

	/** Package path of the context asset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	FString ContextPath;

	FBindGuardMapping() = default;

	FBindGuardMapping(const FName InAction, const FName InContext, const FKey InKey)
		: ActionName(InAction)
		, ContextName(InContext)
		, Key(InKey)
	{
	}
};

/**
 * One thing that is wrong, or one thing that was deliberately allowed to be wrong.
 *
 * Every finding carries the names, not the counts. "3 actions have no gamepad binding" is a number a
 * person cannot act on; "IA_Crouch is bound to C and to nothing on the gamepad" is a fix.
 */
USTRUCT(BlueprintType)
struct BINDGUARD_API FBindGuardFinding
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	EBindFindingKind Kind = EBindFindingKind::Unbound;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	EBindSeverity Severity = EBindSeverity::Error;

	/** The action this is about. Empty only for ContextNeverAdded, which is about a context. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	FName ActionName;

	/** The context this is about, or the context the finding was found in. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	FName ContextName;

	/** For a conflict: the other action fighting over the key. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	FName OtherActionName;

	/** For a cross-context conflict: the context the other action lives in. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	FName OtherContextName;

	/** The key involved, where there is one. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	FKey Key;

	/**
	 * For a device finding: the device family that has no binding.
	 *
	 * Carried separately from Kind because Kind describes the shape a person recognises ("keyboard only")
	 * while this is the machine-readable half a build script filters on.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	EBindDevice MissingDevice = EBindDevice::Other;

	/**
	 * True when the two actions are in different contexts.
	 *
	 * Different contexts are allowed to reuse a key - that is what contexts are for - so this is never an
	 * error on its own, and its severity comes from a separate setting that defaults to Info.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	bool bCrossContext = false;

	/**
	 * True when the exemption list in Project Settings took the teeth out of this finding.
	 *
	 * An excluded finding is forced down to Info, is counted separately as "excluded by settings" and is
	 * still printed. An exemption list that could hide its own effect would be a way to make a report
	 * green by editing a settings page, which is the opposite of what this plugin is for.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	bool bExcluded = false;

	/** One sentence saying what to do about it. Filled in by UBindGuardStatics::Explain. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	FString Detail;
};

/**
 * What the project requires, flattened out of the settings so the rules never read a UDeveloperSettings.
 *
 * A test can build one of these in three lines. That is the point.
 */
USTRUCT(BlueprintType)
struct BINDGUARD_API FBindGuardRequirements
{
	GENERATED_BODY()

	/** Every action must have at least one keyboard or mouse binding. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	bool bRequireKeyboardMouse = true;

	/** Every action must have at least one gamepad binding. The console requirement. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	bool bRequireGamepad = true;

	/** Every action must have at least one touch binding. Off unless you ship to a handset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	bool bRequireTouch = false;

	/** Master switch for the exemption list, so the demo - and a tester - can see both answers. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	bool bUseExemptions = true;

	/** Actions that are allowed to break the device and binding requirements, by name. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	TArray<FName> ExemptActions;

	/** Contexts that are allowed never to be added, by name. Loading screens and cheat overlays live here. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	TArray<FName> ExemptContexts;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	EBindSeverity UnboundSeverity = EBindSeverity::Error;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	EBindSeverity MissingDeviceSeverity = EBindSeverity::Error;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	EBindSeverity ConflictSeverity = EBindSeverity::Error;

	/** Two contexts sharing a key. Info or Warning; never an error, because it is legitimate design. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	EBindSeverity CrossContextConflictSeverity = EBindSeverity::Info;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	EBindSeverity ContextNeverAddedSeverity = EBindSeverity::Warning;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	EBindSeverity UnreachableSeverity = EBindSeverity::Warning;
};

/**
 * Everything the rules need, in one struct: what the assets say, and what the session saw.
 *
 * The two halves are kept apart deliberately. Mappings, Actions and Contexts are facts about the project
 * and do not change between runs. ObservedContexts is a fact about one session on one machine, and
 * bObservationAvailable is what stops the report from treating an empty observation as proof of anything.
 */
USTRUCT(BlueprintType)
struct BINDGUARD_API FBindGuardScanInput
{
	GENERATED_BODY()

	/** Every key mapping in every context that was found. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	TArray<FBindGuardMapping> Mappings;

	/** Every Input Action asset that was found, bound or not. This is what makes Unbound answerable. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	TArray<FName> Actions;

	/** Every Input Mapping Context asset that was found, used or not. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	TArray<FName> Contexts;

	/** The contexts that were actually handed to AddMappingContext while somebody was watching. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	TArray<FName> ObservedContexts;

	/**
	 * True only when an observer was running.
	 *
	 * False means the two observed checks are skipped entirely rather than reported as clean - a scan run
	 * from the editor with no game standing has not proved that every context gets added, it has proved
	 * nothing at all about that question, and saying so is the single most important line in the report.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	bool bObservationAvailable = false;

	/** Where the assets came from, for the report header: "asset registry" or "loaded objects". */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	FString SourceName;
};

/** The whole answer: the findings, the counts behind the header line, and the verdict. */
USTRUCT(BlueprintType)
struct BINDGUARD_API FBindGuardReport
{
	GENERATED_BODY()

	/** Errors first, then warnings, then info; stable within a severity so a screenshot means one thing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	TArray<FBindGuardFinding> Findings;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	int32 ActionCount = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	int32 ContextCount = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	int32 MappingCount = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	int32 ErrorCount = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	int32 WarningCount = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	int32 InfoCount = 0;

	/** How many findings the exemption list took the teeth out of. Always shown, never hidden. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	int32 ExcludedCount = 0;

	/** How many distinct contexts were seen going into AddMappingContext. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	int32 ObservedContextCount = 0;

	/** False when the two observed checks did not run. See FBindGuardScanInput::bObservationAvailable. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	bool bObservationAvailable = false;

	/** Measured, wall clock, over the rules only - the asset gather is timed separately below. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	float ScanMilliseconds = 0.0f;

	/** Measured, wall clock, over loading the assets. Separate because one of these is your disk. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	float GatherMilliseconds = 0.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	EBindVerdict Verdict = EBindVerdict::Ok;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	FString SourceName;

	/**
	 * The checks that actually ran, in words.
	 *
	 * This exists so a green report can never be mistaken for a report that did not run - which is the
	 * failure mode of every quiet checker ever written. When there is nothing to say, BindGuard says what
	 * it looked for instead of saying nothing.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	TArray<FString> ChecksRun;

	/** True once a scan has actually been run. A default-constructed report is not a clean report. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BindGuard")
	bool bHasRun = false;
};
