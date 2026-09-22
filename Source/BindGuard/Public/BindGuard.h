// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Runtime module for BindGuard.
 *
 * Loads at PreDefault, ahead of the game modules, for one reason: the observer has to be listening
 * before the first AddMappingContext call happens. A player controller that adds its mapping context in
 * BeginPlay does it very early, and a context that was added before BindGuard started watching is a
 * context BindGuard would then wrongly report as never added. The whole "observed" half of this plugin
 * is only worth having if it cannot miss the first frame.
 */
class FBindGuardModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
