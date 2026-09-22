// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "BindGuardTypes.h"
#include "BindGuardSettings.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * Project-wide settings for BindGuard, under Project Settings -> Plugins -> BindGuard.
 *
 * Two things in here carry more weight than the rest.
 *
 * The first is the pair of device requirements. Keyboard and gamepad are both on by default because the
 * default has to be the one that catches the failure this plugin exists for - an action that only exists
 * on the keyboard, in a build that is about to be submitted to a console platform.
 *
 * The second is ExemptActions, and it is not a convenience. Every real project has actions that are
 * deliberately keyboard-only: the console key, the debug camera, the screenshot key. Without a way to say
 * so, this tool reports twenty errors on its first run, the buyer decides the tool is noisy, and it gets
 * switched off in the first hour. What is exempted is still counted and still printed as "excluded by
 * settings", so the list can never quietly make a project look clean.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "BindGuard"))
class BINDGUARD_API UBindGuardSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UBindGuardSettings();

	//~ UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	/** The settings object, never null. */
	static const UBindGuardSettings& Get();

	/** The settings flattened into the struct the rules take. Applies the exemption master switch. */
	FBindGuardRequirements MakeRequirements() const;

	//~ What the project requires --------------------------------------------------------------------

	/**
	 * Every action must be reachable on a keyboard or a mouse.
	 *
	 * On by default. A gamepad-only action in a PC build is a feature a mouse-and-keyboard player cannot
	 * use, and it is usually an accident that happened while somebody was testing with a controller.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Required Devices")
	bool bRequireKeyboardMouse = true;

	/**
	 * Every action must be reachable on a gamepad.
	 *
	 * On by default, and this is the one that matters. A console submission is checked against exactly
	 * this question and it is checked by a person with a controller and no keyboard.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Required Devices")
	bool bRequireGamepad = true;

	/** Every action must be reachable by touch. Off by default; turn it on for a handset build. */
	UPROPERTY(config, EditAnywhere, Category = "Required Devices")
	bool bRequireTouch = false;

	//~ The exemption list --------------------------------------------------------------------------

	/**
	 * Use the exemption list at all.
	 *
	 * Turning it off is how you find out what the list is actually hiding, which is a thing worth doing
	 * once a milestone. The report never stops showing the count either way.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Exemptions")
	bool bUseExemptions = true;

	/**
	 * Actions that are allowed to break the device requirements and are allowed to be unbound, by asset
	 * name - Debug_OpenConsole, IA_ToggleDebugCamera, IA_Screenshot.
	 *
	 * The name is matched against the asset's name, not its path, because that is the name that appears
	 * in the report and the name a person will copy out of it.
	 *
	 * An exempt action is not skipped. It is checked, its findings are produced, and then every one of
	 * them is forced down to Info and counted as excluded. You can always see what the list is costing
	 * you.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Exemptions")
	TArray<FName> ExemptActions;

	/**
	 * Contexts that are allowed never to be added, by asset name.
	 *
	 * The honest use for this is a context that only a cheat menu or a platform-specific build ever adds.
	 * The dishonest use is silencing the check, and the excluded counter is what keeps that visible.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Exemptions")
	TArray<FName> ExemptContexts;

	//~ Severities ----------------------------------------------------------------------------------

	/** An action in no context at all. Error by default: it cannot possibly work. */
	UPROPERTY(config, EditAnywhere, Category = "Severity")
	EBindSeverity UnboundSeverity = EBindSeverity::Error;

	/** A required device with no binding. Error by default: this is the certification failure. */
	UPROPERTY(config, EditAnywhere, Category = "Severity")
	EBindSeverity MissingDeviceSeverity = EBindSeverity::Error;

	/** Two actions on one key inside one context. Error by default: one of them will not fire the way you meant. */
	UPROPERTY(config, EditAnywhere, Category = "Severity")
	EBindSeverity ConflictSeverity = EBindSeverity::Error;

	/**
	 * Two actions on one key in two different contexts.
	 *
	 * Info by default and clamped to Warning at worst, never Error, because reusing a key across contexts
	 * is exactly what contexts are for. It is reported at all because when two contexts are active at the
	 * same time it is still worth a look - and only you know whether they are.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Severity")
	EBindSeverity CrossContextConflictSeverity = EBindSeverity::Info;

	/** A context nothing added during the observed session. Warning by default, because it is observed, not proven. */
	UPROPERTY(config, EditAnywhere, Category = "Severity")
	EBindSeverity ContextNeverAddedSeverity = EBindSeverity::Warning;

	/** An action whose every context went unadded. Warning, for the same reason. */
	UPROPERTY(config, EditAnywhere, Category = "Severity")
	EBindSeverity UnreachableSeverity = EBindSeverity::Warning;

	//~ The report ----------------------------------------------------------------------------------

	/** Draw the report from the first frame. BindGuard.Show and BindGuard.Hide flip it. */
	UPROPERTY(config, EditAnywhere, Category = "Report")
	bool bShowReportByDefault = true;

	/**
	 * Run a scan automatically a moment after the first world has begun play.
	 *
	 * The delay is not laziness: contexts are added in BeginPlay, and a scan that ran in the same frame
	 * would report every one of them as never added. See AutoScanDelaySeconds.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Report")
	bool bScanOnBeginPlay = true;

	/** How long after begin play the automatic scan waits for the game to add its contexts. */
	UPROPERTY(config, EditAnywhere, Category = "Report", meta = (ClampMin = "0.0", UIMax = "10.0", Units = "Seconds"))
	float AutoScanDelaySeconds = 1.0f;

	/**
	 * Draw the report even when the project's HUD is not an ABindGuardHUD.
	 *
	 * A project with its own HUD class does not have to reparent it: turn this on and the same panel is
	 * drawn through AHUD::OnHUDPostRender instead. The two paths know about each other and cannot stack.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Report")
	bool bAutoDrawOnAnyHUD = false;

	/** How many findings the on-screen panel lists before it says how many more there are. */
	UPROPERTY(config, EditAnywhere, Category = "Report", meta = (ClampMin = "1", UIMax = "40"))
	int32 MaxReportRows = 16;

	/** Where BindGuard.Report and BindGuard.Gate write, relative to the project directory. */
	UPROPERTY(config, EditAnywhere, Category = "Report")
	FString ReportPath = TEXT("Saved/BindGuard/report.json");

	//~ Where to look ---------------------------------------------------------------------------------

	/**
	 * Content paths the editor module's asset registry walk covers. Empty means /Game.
	 *
	 * Narrow this if your project is enormous and your input assets live in one place; the scan cost is
	 * printed on the report either way, so you can see whether it was worth it.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Scanning", meta = (ContentDir))
	TArray<FDirectoryPath> ScanPaths;

	/**
	 * Mapping contexts to load and check even if nothing else has loaded them.
	 *
	 * Only needed in a cooked build, where there is no asset registry walk and BindGuard can otherwise
	 * only see the contexts something already loaded. In the editor this is redundant and harmless.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Scanning")
	TArray<TSoftObjectPtr<UInputMappingContext>> ExtraContexts;

	/** Input actions to load and check even if nothing else has loaded them. Same reason as above. */
	UPROPERTY(config, EditAnywhere, Category = "Scanning")
	TArray<TSoftObjectPtr<UInputAction>> ExtraActions;
};
