// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "BindGuardHUD.h"

#include "BindGuardSubsystem.h"
#include "Engine/Canvas.h"

ABindGuardHUD::ABindGuardHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ABindGuardHUD::ToggleReport()
{
	if (UBindGuardSubsystem* Subsystem = UBindGuardSubsystem::Get(this))
	{
		Subsystem->SetReportVisible(!Subsystem->IsReportVisible());
	}
}

bool ABindGuardHUD::IsReportVisible() const
{
	const UBindGuardSubsystem* Subsystem = UBindGuardSubsystem::Get(this);
	return Subsystem && Subsystem->IsReportVisible();
}

void ABindGuardHUD::ScanNow()
{
	if (UBindGuardSubsystem* Subsystem = UBindGuardSubsystem::Get(this))
	{
		Subsystem->Scan();
	}
}

void ABindGuardHUD::DrawHUD()
{
	Super::DrawHUD();

	if (Canvas == nullptr)
	{
		return;
	}

	// Read from the subsystem on the frame it is drawn. Nothing is cached here, so the panel cannot claim
	// one verdict while the last scan says another.
	const UBindGuardSubsystem* Subsystem = UBindGuardSubsystem::Get(this);
	if (Subsystem == nullptr || !Subsystem->IsReportVisible())
	{
		return;
	}

	Subsystem->DrawReport(Canvas, PanelOrigin, PanelWidth);
}
