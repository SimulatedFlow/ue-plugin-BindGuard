// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BindGuardTypes.h"

// The delayed automatic scan holds a timer handle. Named explicitly rather than picked up from a shared
// PCH: a plugin built with RunUAT BuildPlugin gets strict include validation and no engine PCH, and a
// header that only compiles inside a host project is a header that fails on the packaging machine.
#include "Engine/TimerHandle.h"

#include "Subsystems/GameInstanceSubsystem.h"
#include "BindGuardSubsystem.generated.h"

class AHUD;
class UCanvas;
class UEnhancedInputLocalPlayerSubsystem;
class UInputMappingContext;
class ULocalPlayer;

/** Fired every time a scan finishes, with the report it produced. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBindGuardOnFindings, const FBindGuardReport&, Report);

/**
 * Watches, scans, judges and draws.
 *
 * The watching is the part that could not be done any other way. Three of BindGuard's five questions are
 * answerable from assets on disk; two of them - "which context does nobody ever add" and "which action is
 * unreachable because its context is never active" - are not, because whether AddMappingContext gets
 * called is a decision made by Blueprint logic somewhere in a game mode or a pawn or a UI stack. No
 * amount of static analysis can settle it.
 *
 * So this subsystem binds to UEnhancedInputLocalPlayerSubsystem::OnMappingContextAdded on every local
 * player and writes down what actually happens. That gives an honest answer to a question that otherwise
 * gets a confident wrong one - and it comes with a limit that the report states in plain words every time
 * it is drawn: a context missing from the observed list was not added *in this session*. It is not proof
 * that nothing ever adds it. A line that pretended otherwise would be worse than no line at all.
 *
 * It is a game instance subsystem rather than a world subsystem because the observation has to survive a
 * map change. Mapping contexts are added and removed across level transitions, and a record that reset
 * every time a level loaded would be a record that always looks empty.
 */
UCLASS()
class BINDGUARD_API UBindGuardSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The subsystem for the world context's game instance, or null outside a game. */
	static UBindGuardSubsystem* Get(const UObject* WorldContextObject);

	//~ Scanning ----------------------------------------------------------------------------------------

	/** Load the assets, run every rule, keep the report and broadcast OnFindings. */
	UFUNCTION(BlueprintCallable, Category = "BindGuard")
	FBindGuardReport Scan();

	/** The last report. bHasRun is false until Scan has been called at least once. */
	UFUNCTION(BlueprintPure, Category = "BindGuard")
	FBindGuardReport GetReport() const { return Report; }

	/** The findings from the last scan, errors first. */
	UFUNCTION(BlueprintPure, Category = "BindGuard")
	TArray<FBindGuardFinding> GetFindings() const { return Report.Findings; }

	/** Ok, Warn or Fail. Ok before the first scan, because nothing has been found - not because it is clean. */
	UFUNCTION(BlueprintPure, Category = "BindGuard")
	EBindVerdict GetVerdict() const { return Report.Verdict; }

	/** Write the JSON report. An empty path means the one in Project Settings. */
	UFUNCTION(BlueprintCallable, Category = "BindGuard")
	bool WriteReport(const FString& Path);

	/** Fired after every scan. */
	UPROPERTY(BlueprintAssignable, Category = "BindGuard")
	FBindGuardOnFindings OnFindings;

	//~ Observation -------------------------------------------------------------------------------------

	/** The contexts seen going into AddMappingContext, in the order they were first seen. */
	UFUNCTION(BlueprintPure, Category = "BindGuard")
	TArray<FName> GetObservedContexts() const { return ObservedContexts; }

	/** The contexts applied right now, as opposed to ever. */
	UFUNCTION(BlueprintPure, Category = "BindGuard")
	TArray<FName> GetActiveContexts() const;

	/** True once at least one enhanced input subsystem is being watched. Gates the two observed checks. */
	UFUNCTION(BlueprintPure, Category = "BindGuard")
	bool IsObserving() const { return bObserving; }

	/** Forget what was observed, so a tester can play one section of the game and judge just that section. */
	UFUNCTION(BlueprintCallable, Category = "BindGuard")
	void ResetObservation();

	//~ The panel ---------------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "BindGuard")
	void SetReportVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "BindGuard")
	bool IsReportVisible() const { return bReportVisible; }

	/** Turn the exemption list on and off for this session only. The settings asset is not touched. */
	UFUNCTION(BlueprintCallable, Category = "BindGuard")
	void SetExemptionsEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "BindGuard")
	bool AreExemptionsEnabled() const { return bExemptionsEnabled; }

	/**
	 * Draw the report onto a canvas. Called by ABindGuardHUD, and by AHUD::OnHUDPostRender for projects
	 * that already have their own HUD class.
	 */
	void DrawReport(UCanvas* Canvas, const FVector2D& Origin, float Width) const;

private:
	UFUNCTION()
	void HandleMappingContextAdded(const UInputMappingContext* MappingContext);

	UFUNCTION()
	void HandleMappingContextRemoved(const UInputMappingContext* MappingContext);

	void HandleLocalPlayerAdded(ULocalPlayer* LocalPlayer);
	void HandleLocalPlayerRemoved(ULocalPlayer* LocalPlayer);
	void HandlePostLoadMap(UWorld* LoadedWorld);
	void HandleHUDPostRender(AHUD* HUD, UCanvas* Canvas);
	void HandleAutoScan();

	/** Bind to one local player's enhanced input subsystem, if it has one and is not already watched. */
	void ObserveLocalPlayer(ULocalPlayer* LocalPlayer);

	/** The settings, with the session's exemption override applied on top. */
	FBindGuardRequirements MakeRequirements() const;

	/** The last scan. */
	UPROPERTY(Transient)
	FBindGuardReport Report;

	/** Every context ever handed to AddMappingContext while watching, in first-seen order. */
	UPROPERTY(Transient)
	TArray<FName> ObservedContexts;

	/** The ones applied right now. Kept apart from the list above: removed is not the same as never added. */
	UPROPERTY(Transient)
	TArray<FName> ActiveContexts;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem>> WatchedSubsystems;

	bool bObserving = false;
	bool bReportVisible = true;
	bool bExemptionsEnabled = true;

	FDelegateHandle LocalPlayerAddedHandle;
	FDelegateHandle LocalPlayerRemovedHandle;
	FDelegateHandle PostLoadMapHandle;
	FDelegateHandle HudPostRenderHandle;
	FTimerHandle AutoScanTimer;
};
