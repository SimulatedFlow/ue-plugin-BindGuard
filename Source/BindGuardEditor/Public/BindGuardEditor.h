// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Editor module for BindGuard.
 *
 * Two jobs, both small on purpose. It registers the asset registry walk into FBindGuardAssetSource so the
 * static checks can see the whole project, and it puts one entry under Tools so a scan does not require
 * remembering a console command.
 *
 * There is deliberately no rule code in here. Every rule lives in the runtime module, is a static function
 * over plain structs, and is called identically by this menu entry, by the console commands and by the
 * in-game report - so there is no second implementation that could drift from the one the tests cover.
 */
class FBindGuardEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();

	/** Scan and show the result as a toast, with the whole report in the log. */
	void RunScanFromMenu();

	/** Scan, write the JSON report, and say where it went. */
	void RunReportFromMenu();
};
