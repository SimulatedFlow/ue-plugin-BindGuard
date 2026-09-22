// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "BindGuardEditor.h"

#include "BindGuardAssetScan.h"
#include "BindGuardLog.h"
#include "BindGuardScanner.h"
#include "BindGuardStatics.h"
#include "Framework/Notifications/NotificationManager.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FBindGuardEditorModule"

void FBindGuardEditorModule::StartupModule()
{
	FBindGuardAssetScan::Register();

	// Deferred rather than done here. Tool menus are not necessarily up at module startup, and
	// RegisterStartupCallback is the engine's own answer to that - it runs the callback immediately if
	// menus are already available and queues it if they are not.
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBindGuardEditorModule::RegisterMenus));

	UE_LOG(LogBindGuard, Log, TEXT("BindGuardEditor started; the static scan now covers the whole project."));
}

void FBindGuardEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FBindGuardAssetScan::Unregister();
}

void FBindGuardEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	if (ToolsMenu == nullptr)
	{
		return;
	}

	FToolMenuSection& Section = ToolsMenu->FindOrAddSection(
		TEXT("BindGuard"), LOCTEXT("BindGuardSection", "BindGuard"));

	Section.AddMenuEntry(
		TEXT("BindGuardScan"),
		LOCTEXT("BindGuardScanLabel", "Check Input Bindings"),
		LOCTEXT("BindGuardScanTooltip",
			"Check every Input Action and Mapping Context in the project: actions nobody bound, actions bound on only one device, and two actions on one key. The full report goes to the Output Log."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FBindGuardEditorModule::RunScanFromMenu)));

	Section.AddMenuEntry(
		TEXT("BindGuardReport"),
		LOCTEXT("BindGuardReportLabel", "Write Input Binding Report"),
		LOCTEXT("BindGuardReportTooltip",
			"Check, then write the JSON report to the path in Project Settings. The same file BindGuard.Gate writes on a build server."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FBindGuardEditorModule::RunReportFromMenu)));
}

void FBindGuardEditorModule::RunScanFromMenu()
{
	// No observed contexts and observation explicitly unavailable. An editor scan has not watched a
	// session, so it must not be allowed to say anything at all about which contexts get added - the
	// report will print that limitation in words rather than quietly reporting nothing.
	const FBindGuardReport Report = FBindGuardScanner::RunWithProjectSettings(TArray<FName>(), /*bObservationAvailable=*/false);
	FBindGuardScanner::LogReport(Report, /*bAllFindings=*/true);

	FNotificationInfo Info(FText::FromString(FString::Printf(
		TEXT("BindGuard: %s\n%s\nStatic checks only - the full report is in the Output Log."),
		*UBindGuardStatics::VerdictName(Report.Verdict).ToUpper(),
		*UBindGuardStatics::FormatHeadline(Report))));

	Info.ExpireDuration = 8.0f;
	Info.bFireAndForget = true;

	const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
	if (Notification.IsValid())
	{
		Notification->SetCompletionState(Report.Verdict == EBindVerdict::Fail
			? SNotificationItem::CS_Fail
			: SNotificationItem::CS_Success);
	}
}

void FBindGuardEditorModule::RunReportFromMenu()
{
	const FBindGuardReport Report = FBindGuardScanner::RunWithProjectSettings(TArray<FName>(), /*bObservationAvailable=*/false);
	FBindGuardScanner::LogReport(Report, /*bAllFindings=*/true);

	FString FullPath;
	const bool bWritten = FBindGuardScanner::WriteReportFile(Report, FString(), FullPath);

	FNotificationInfo Info(FText::FromString(bWritten
		? FString::Printf(TEXT("BindGuard report written:\n%s"), *FullPath)
		: FString::Printf(TEXT("BindGuard could not write the report to\n%s"), *FullPath)));

	Info.ExpireDuration = 8.0f;
	Info.bFireAndForget = true;

	const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
	if (Notification.IsValid())
	{
		Notification->SetCompletionState(bWritten ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBindGuardEditorModule, BindGuardEditor)
