// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BindGuardHUD.generated.h"

/**
 * Draws the report.
 *
 * On UCanvas and not in UMG, which is the one design decision in this class and it is not a stylistic
 * one. The whole claim of this plugin is a verdict about a build, and the build it matters most in is the
 * cooked one that is about to be submitted. A report drawn in UMG is a report that depends on a widget
 * asset surviving a cook and a UI stack being up; a report drawn on the canvas from AHUD::DrawHUD is a
 * report that is there in a Shipping build with nothing else running.
 *
 * Set this as the HUD class on your game mode, or leave your own HUD alone and turn on
 * "Auto Draw On Any HUD" in Project Settings, which routes the identical panel through
 * AHUD::OnHUDPostRender instead. The two paths know about each other and cannot draw twice.
 */
UCLASS()
class BINDGUARD_API ABindGuardHUD : public AHUD
{
	GENERATED_BODY()

public:
	ABindGuardHUD();

	//~ AHUD interface
	virtual void DrawHUD() override;

	/** Show or hide the panel. The demo panel's button, and BindGuard.Show / BindGuard.Hide. */
	UFUNCTION(BlueprintCallable, Category = "BindGuard")
	void ToggleReport();

	UFUNCTION(BlueprintPure, Category = "BindGuard")
	bool IsReportVisible() const;

	/** Run a scan now and redraw with the result. */
	UFUNCTION(BlueprintCallable, Category = "BindGuard")
	void ScanNow();

	/** Top-left corner of the panel, in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BindGuard")
	FVector2D PanelOrigin = FVector2D(28.0f, 90.0f);

	/** Panel width in pixels. Wide by default: the findings name two assets and a key, and they wrap badly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BindGuard")
	float PanelWidth = 900.0f;
};
