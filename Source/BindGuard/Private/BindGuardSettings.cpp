// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "BindGuardSettings.h"

UBindGuardSettings::UBindGuardSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("BindGuard");
}

FName UBindGuardSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FName UBindGuardSettings::GetSectionName() const
{
	return TEXT("BindGuard");
}

const UBindGuardSettings& UBindGuardSettings::Get()
{
	const UBindGuardSettings* Settings = GetDefault<UBindGuardSettings>();
	check(Settings);
	return *Settings;
}

FBindGuardRequirements UBindGuardSettings::MakeRequirements() const
{
	FBindGuardRequirements Requirements;

	Requirements.bRequireKeyboardMouse = bRequireKeyboardMouse;
	Requirements.bRequireGamepad = bRequireGamepad;
	Requirements.bRequireTouch = bRequireTouch;

	Requirements.bUseExemptions = bUseExemptions;
	Requirements.ExemptActions = ExemptActions;
	Requirements.ExemptContexts = ExemptContexts;

	Requirements.UnboundSeverity = UnboundSeverity;
	Requirements.MissingDeviceSeverity = MissingDeviceSeverity;
	Requirements.ConflictSeverity = ConflictSeverity;
	Requirements.ContextNeverAddedSeverity = ContextNeverAddedSeverity;
	Requirements.UnreachableSeverity = UnreachableSeverity;

	// Clamped here rather than trusted from the settings page. A key shared between two contexts is
	// legitimate design, and a project that set this to Error would be a project whose gate fails on
	// working input. Warning is as loud as this finding is ever allowed to be.
	Requirements.CrossContextConflictSeverity = (CrossContextConflictSeverity == EBindSeverity::Error)
		? EBindSeverity::Warning
		: CrossContextConflictSeverity;

	return Requirements;
}
