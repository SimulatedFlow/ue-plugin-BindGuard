// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "BindGuardLog.h"
#include "BindGuardScanner.h"
#include "BindGuardSettings.h"
#include "BindGuardStatics.h"
#include "BindGuardSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"

/**
 * The console surface.
 *
 * Every one of these works with a game running and without one. With a game, the scan uses the live
 * subsystem and therefore has the observed half; without a game - in the editor, or from a commandlet -
 * it runs the static half and the report says in as many words that the observed checks did not run.
 *
 * That split is the whole reason these commands do not simply require a subsystem. A build server has no
 * player, and a build server is exactly where the three static checks pay for themselves.
 */
namespace BindGuardCommands
{
	static bool ParseBool(const TArray<FString>& Args, const bool bDefault)
	{
		if (Args.Num() == 0)
		{
			return bDefault;
		}

		return Args[0].ToBool() || Args[0] == TEXT("1");
	}

	/** The live subsystem, if any game instance anywhere has one. */
	static UBindGuardSubsystem* FindSubsystem()
	{
		if (GEngine == nullptr)
		{
			return nullptr;
		}

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			// Game and PIE only. An editor world has no game instance and no player, and picking one up
			// would produce a report that claims to have observed a session that never happened.
			if (Context.WorldType != EWorldType::Game && Context.WorldType != EWorldType::PIE)
			{
				continue;
			}

			if (UGameInstance* Instance = Context.OwningGameInstance)
			{
				if (UBindGuardSubsystem* Subsystem = Instance->GetSubsystem<UBindGuardSubsystem>())
				{
					return Subsystem;
				}
			}
		}

		return nullptr;
	}

	/** Scan through the subsystem when there is one, statically when there is not. */
	static FBindGuardReport RunScan()
	{
		if (UBindGuardSubsystem* Subsystem = FindSubsystem())
		{
			return Subsystem->Scan();
		}

		return FBindGuardScanner::RunWithProjectSettings(TArray<FName>(), /*bObservationAvailable=*/false);
	}

	static FAutoConsoleCommand GScan(
		TEXT("BindGuard.Scan"),
		TEXT("BindGuard.Scan - check every input action and mapping context now, and print the headline."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			FBindGuardScanner::LogReport(RunScan(), /*bAllFindings=*/false);
		}));

	static FAutoConsoleCommand GDump(
		TEXT("BindGuard.Dump"),
		TEXT("BindGuard.Dump - the whole report to the log: every finding, its sentence, and what was checked."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			FBindGuardScanner::LogReport(RunScan(), /*bAllFindings=*/true);
		}));

	static FAutoConsoleCommand GShow(
		TEXT("BindGuard.Show"),
		TEXT("BindGuard.Show [0|1] - show the on-screen report."),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			if (UBindGuardSubsystem* Subsystem = FindSubsystem())
			{
				Subsystem->SetReportVisible(ParseBool(Args, true));
			}
			else
			{
				UE_LOG(LogBindGuard, Warning, TEXT("BindGuard.Show needs a running game. Use BindGuard.Dump instead."));
			}
		}));

	static FAutoConsoleCommand GHide(
		TEXT("BindGuard.Hide"),
		TEXT("BindGuard.Hide - hide the on-screen report."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			if (UBindGuardSubsystem* Subsystem = FindSubsystem())
			{
				Subsystem->SetReportVisible(false);
			}
		}));

	static FAutoConsoleCommand GExempt(
		TEXT("BindGuard.Exempt"),
		TEXT("BindGuard.Exempt [0|1] - use the exemption list, or do not, for this session. Rescans immediately."),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			UBindGuardSubsystem* Subsystem = FindSubsystem();
			if (Subsystem == nullptr)
			{
				UE_LOG(LogBindGuard, Warning, TEXT("BindGuard.Exempt needs a running game."));
				return;
			}

			Subsystem->SetExemptionsEnabled(ParseBool(Args, true));
			UE_LOG(LogBindGuard, Display, TEXT("BindGuard: exemption list %s."),
				Subsystem->AreExemptionsEnabled() ? TEXT("on") : TEXT("off"));
		}));

	static FAutoConsoleCommand GReset(
		TEXT("BindGuard.ResetObservation"),
		TEXT("BindGuard.ResetObservation - forget which contexts were added, so you can judge one section of the game."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			if (UBindGuardSubsystem* Subsystem = FindSubsystem())
			{
				Subsystem->ResetObservation();
			}
		}));

	static FAutoConsoleCommand GReport(
		TEXT("BindGuard.Report"),
		TEXT("BindGuard.Report [path] - scan and write the JSON report. Default: the path in Project Settings."),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			const FBindGuardReport Report = RunScan();

			FString FullPath;
			FBindGuardScanner::WriteReportFile(Report, Args.Num() > 0 ? Args[0] : FString(), FullPath);
		}));

	/**
	 * The gate.
	 *
	 * Scans, writes Saved/BindGuard/report.json and ends the process with 0 when there is nothing to say,
	 * 1 when there are only warnings and 2 when there is an error. Those are the same three numbers
	 * LocaleGuard, AssetWarden, WidgetLedger, LoadLens and HeapCensus return, and they mean the same three
	 * things - one convention, six tools, nothing new to learn for a project that already owns one.
	 *
	 * Run from a build step with no game, this is the static half: unbound actions, device gaps and key
	 * conflicts. Run from a game, it also carries the observed half. Both write the same file and the file
	 * says which one it was, so a build script can refuse to accept a report that was not allowed to check
	 * everything it claims to.
	 *
	 * -noexit reports without ending the process, which is what you want when you are typing this into the
	 * console rather than running it from a script.
	 */
	static void RunGate(const TArray<FString>& Args)
	{
		bool bExitWhenDone = true;
		FString Path;

		for (const FString& Arg : Args)
		{
			if (Arg.Equals(TEXT("-noexit"), ESearchCase::IgnoreCase))
			{
				bExitWhenDone = false;
			}
			else if (!Arg.StartsWith(TEXT("-")))
			{
				Path = Arg;
			}
		}

		const FBindGuardReport Report = RunScan();
		FBindGuardScanner::LogReport(Report, /*bAllFindings=*/true);

		FString FullPath;
		const bool bWritten = FBindGuardScanner::WriteReportFile(Report, Path, FullPath);

		const int32 ExitCode = UBindGuardStatics::VerdictExitCode(Report.Verdict);

		UE_LOG(LogBindGuard, Display, TEXT("BindGuard.Gate: %s - errors %d, warnings %d, %d excluded by settings. Exit code %d."),
			*UBindGuardStatics::VerdictName(Report.Verdict).ToUpper(),
			Report.ErrorCount, Report.WarningCount, Report.ExcludedCount, ExitCode);

		if (!bWritten)
		{
			UE_LOG(LogBindGuard, Error, TEXT("BindGuard.Gate: the report could not be written. Treating that as a failure."));
		}

		if (!bExitWhenDone)
		{
			return;
		}

		// A report that could not be written is a gate that did not run, and a gate that did not run must
		// never be allowed to look like a gate that passed.
		const uint8 Status = bWritten ? static_cast<uint8>(ExitCode) : 2;
		FPlatformMisc::RequestExitWithStatus(/*Force=*/true, Status, TEXT("BindGuard.Gate"));
	}

	static FAutoConsoleCommand GGate(
		TEXT("BindGuard.Gate"),
		TEXT("BindGuard.Gate [path] [-noexit] - scan, write the report, exit 0 clean / 1 warnings / 2 errors."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&RunGate));
}
