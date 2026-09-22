// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "BindGuardSubsystem.h"

#include "BindGuardHUD.h"
#include "BindGuardLog.h"
#include "BindGuardScanner.h"
#include "BindGuardSettings.h"
#include "BindGuardStatics.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/HUD.h"
#include "GlobalRenderResources.h"
#include "InputMappingContext.h"
#include "Misc/StringBuilder.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

namespace BindGuardPanel
{
	static constexpr float LineHeight = 15.0f;
	static constexpr float BoxPadding = 8.0f;

	static const FLinearColor PanelBackground(0.0f, 0.0f, 0.0f, 0.68f);
	static const FLinearColor HeadingColor(0.42f, 0.78f, 1.0f, 1.0f);
	static const FLinearColor BodyColor(0.90f, 0.90f, 0.90f, 1.0f);
	static const FLinearColor GoodColor(0.42f, 0.95f, 0.48f, 1.0f);
	static const FLinearColor WarnColor(0.98f, 0.78f, 0.35f, 1.0f);
	static const FLinearColor ErrorColor(1.0f, 0.40f, 0.36f, 1.0f);
	static const FLinearColor DimColor(0.62f, 0.62f, 0.62f, 1.0f);

	static const FLinearColor& SeverityColor(const EBindSeverity Severity)
	{
		switch (Severity)
		{
		case EBindSeverity::Error:		return ErrorColor;
		case EBindSeverity::Warning:	return WarnColor;
		default:						return DimColor;
		}
	}

	static void DrawFilledRect(UCanvas* Canvas, const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color)
	{
		FCanvasTileItem Tile(Position, GWhiteTexture, Size, Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}
}

//~ Lifetime -------------------------------------------------------------------------------------------

void UBindGuardSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UBindGuardSettings& Settings = UBindGuardSettings::Get();
	bReportVisible = Settings.bShowReportByDefault;
	bExemptionsEnabled = Settings.bUseExemptions;

	if (UGameInstance* Instance = GetGameInstance())
	{
		LocalPlayerAddedHandle = Instance->OnLocalPlayerAddedEvent.AddUObject(this, &UBindGuardSubsystem::HandleLocalPlayerAdded);
		LocalPlayerRemovedHandle = Instance->OnLocalPlayerRemovedEvent.AddUObject(this, &UBindGuardSubsystem::HandleLocalPlayerRemoved);

		// A local player can already exist by the time a game instance subsystem initialises - notably on
		// a second PIE run - so the existing ones are picked up rather than waited for.
		for (ULocalPlayer* LocalPlayer : Instance->GetLocalPlayers())
		{
			ObserveLocalPlayer(LocalPlayer);
		}
	}

	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UBindGuardSubsystem::HandlePostLoadMap);

	if (Settings.bAutoDrawOnAnyHUD)
	{
		HudPostRenderHandle = AHUD::OnHUDPostRender.AddUObject(this, &UBindGuardSubsystem::HandleHUDPostRender);
	}

	UE_LOG(LogBindGuard, Log, TEXT("BindGuard: watching for mapping contexts."));
}

void UBindGuardSubsystem::Deinitialize()
{
	if (UGameInstance* Instance = GetGameInstance())
	{
		Instance->OnLocalPlayerAddedEvent.Remove(LocalPlayerAddedHandle);
		Instance->OnLocalPlayerRemovedEvent.Remove(LocalPlayerRemovedHandle);
	}
	LocalPlayerAddedHandle.Reset();
	LocalPlayerRemovedHandle.Reset();

	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	PostLoadMapHandle.Reset();

	if (HudPostRenderHandle.IsValid())
	{
		AHUD::OnHUDPostRender.Remove(HudPostRenderHandle);
		HudPostRenderHandle.Reset();
	}

	// Unbound one at a time rather than with RemoveAll on this object, because these are dynamic delegates
	// on objects that may already be gone - a local player subsystem outlives nothing.
	for (const TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem>& Weak : WatchedSubsystems)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Input = Weak.Get())
		{
			Input->OnMappingContextAdded.RemoveDynamic(this, &UBindGuardSubsystem::HandleMappingContextAdded);
			Input->OnMappingContextRemoved.RemoveDynamic(this, &UBindGuardSubsystem::HandleMappingContextRemoved);
		}
	}
	WatchedSubsystems.Reset();
	bObserving = false;

	Super::Deinitialize();
}

UBindGuardSubsystem* UBindGuardSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	return Instance ? Instance->GetSubsystem<UBindGuardSubsystem>() : nullptr;
}

//~ Observation ----------------------------------------------------------------------------------------

void UBindGuardSubsystem::ObserveLocalPlayer(ULocalPlayer* LocalPlayer)
{
	if (!IsValid(LocalPlayer))
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (Input == nullptr)
	{
		// A project that has not enabled Enhanced Input has nothing for BindGuard to observe. That is a
		// legitimate state, not an error, and the static half of the report still works.
		return;
	}

	if (WatchedSubsystems.ContainsByPredicate([Input](const TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem>& Weak)
		{ return Weak.Get() == Input; }))
	{
		return;
	}

	Input->OnMappingContextAdded.AddDynamic(this, &UBindGuardSubsystem::HandleMappingContextAdded);
	Input->OnMappingContextRemoved.AddDynamic(this, &UBindGuardSubsystem::HandleMappingContextRemoved);

	WatchedSubsystems.Add(Input);
	bObserving = true;

	UE_LOG(LogBindGuard, Log, TEXT("BindGuard: observing %s."), *LocalPlayer->GetName());
}

void UBindGuardSubsystem::HandleLocalPlayerAdded(ULocalPlayer* LocalPlayer)
{
	ObserveLocalPlayer(LocalPlayer);
}

void UBindGuardSubsystem::HandleLocalPlayerRemoved(ULocalPlayer* LocalPlayer)
{
	if (!IsValid(LocalPlayer))
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
	{
		Input->OnMappingContextAdded.RemoveDynamic(this, &UBindGuardSubsystem::HandleMappingContextAdded);
		Input->OnMappingContextRemoved.RemoveDynamic(this, &UBindGuardSubsystem::HandleMappingContextRemoved);

		WatchedSubsystems.RemoveAll([Input](const TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem>& Weak)
		{
			return !Weak.IsValid() || Weak.Get() == Input;
		});
	}

	// bObserving deliberately stays true. A player that has left is not a session that never happened, and
	// what was recorded while they were here is still what was recorded.
}

void UBindGuardSubsystem::HandleMappingContextAdded(const UInputMappingContext* MappingContext)
{
	if (!IsValid(MappingContext))
	{
		return;
	}

	const FName Name(*MappingContext->GetName());

	ObservedContexts.AddUnique(Name);
	ActiveContexts.AddUnique(Name);

	UE_LOG(LogBindGuard, Verbose, TEXT("BindGuard: %s added."), *Name.ToString());
}

void UBindGuardSubsystem::HandleMappingContextRemoved(const UInputMappingContext* MappingContext)
{
	if (!IsValid(MappingContext))
	{
		return;
	}

	// Removed from the active list, never from the observed list. "It was added and then taken away again"
	// and "nothing ever added it" are two different facts and the report is only useful if it can tell
	// them apart.
	ActiveContexts.Remove(FName(*MappingContext->GetName()));
}

TArray<FName> UBindGuardSubsystem::GetActiveContexts() const
{
	return ActiveContexts;
}

void UBindGuardSubsystem::ResetObservation()
{
	ObservedContexts.Reset();
	ActiveContexts.Reset();

	// What is currently applied is still applied, so it goes straight back into both lists. Otherwise a
	// reset would immediately produce a report claiming the context the player is holding was never added.
	for (const TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem>& Weak : WatchedSubsystems)
	{
		const UEnhancedInputLocalPlayerSubsystem* Input = Weak.Get();
		if (Input == nullptr)
		{
			continue;
		}

		for (TObjectIterator<UInputMappingContext> It; It; ++It)
		{
			if (IsValid(*It) && !It->HasAnyFlags(RF_ClassDefaultObject) && Input->HasMappingContext(*It))
			{
				const FName Name(*It->GetName());
				ObservedContexts.AddUnique(Name);
				ActiveContexts.AddUnique(Name);
			}
		}
	}

	UE_LOG(LogBindGuard, Display, TEXT("BindGuard: observation reset; %d context(s) currently applied."), ActiveContexts.Num());
}

void UBindGuardSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (!IsValid(LoadedWorld) || LoadedWorld->GetGameInstance() != GetGameInstance())
	{
		return;
	}

	// A map load can bring a local player with it, and can happen before this subsystem ever saw one.
	if (UGameInstance* Instance = GetGameInstance())
	{
		for (ULocalPlayer* LocalPlayer : Instance->GetLocalPlayers())
		{
			ObserveLocalPlayer(LocalPlayer);
		}
	}

	const UBindGuardSettings& Settings = UBindGuardSettings::Get();
	if (!Settings.bScanOnBeginPlay)
	{
		return;
	}

	// Delayed, and the delay is the whole reason this works. Mapping contexts are added in BeginPlay, so a
	// scan in the same frame as the map load would find an empty observed list and report every context in
	// the project as never added - a tool that is loudly wrong on its very first frame.
	const float Delay = FMath::Max(Settings.AutoScanDelaySeconds, 0.01f);
	LoadedWorld->GetTimerManager().SetTimer(AutoScanTimer, FTimerDelegate::CreateUObject(this, &UBindGuardSubsystem::HandleAutoScan), Delay, false);
}

void UBindGuardSubsystem::HandleAutoScan()
{
	Scan();
}

//~ Scanning -------------------------------------------------------------------------------------------

FBindGuardRequirements UBindGuardSubsystem::MakeRequirements() const
{
	FBindGuardRequirements Requirements = UBindGuardSettings::Get().MakeRequirements();

	// The session override sits on top of the settings, so BindGuard.Exempt 0 - and the demo's toggle -
	// can show the same project both ways without editing a config file.
	Requirements.bUseExemptions = bExemptionsEnabled;
	return Requirements;
}

FBindGuardReport UBindGuardSubsystem::Scan()
{
	Report = FBindGuardScanner::Run(ObservedContexts, bObserving, MakeRequirements());

	FBindGuardScanner::LogReport(Report, /*bAllFindings=*/false);

	OnFindings.Broadcast(Report);
	return Report;
}

bool UBindGuardSubsystem::WriteReport(const FString& Path)
{
	if (!Report.bHasRun)
	{
		Scan();
	}

	FString FullPath;
	return FBindGuardScanner::WriteReportFile(Report, Path, FullPath);
}

//~ The panel ------------------------------------------------------------------------------------------

void UBindGuardSubsystem::SetReportVisible(const bool bVisible)
{
	bReportVisible = bVisible;
}

void UBindGuardSubsystem::SetExemptionsEnabled(const bool bEnabled)
{
	if (bExemptionsEnabled == bEnabled)
	{
		return;
	}

	bExemptionsEnabled = bEnabled;

	// Rescanned immediately, because the point of the switch is to see the difference. A toggle that only
	// took effect on the next scan would look like it had done nothing.
	if (Report.bHasRun)
	{
		Scan();
	}
}

void UBindGuardSubsystem::HandleHUDPostRender(AHUD* HUD, UCanvas* Canvas)
{
	if (!IsValid(HUD) || Canvas == nullptr || !bReportVisible)
	{
		return;
	}

	// The two drawing paths must not stack. An ABindGuardHUD draws the panel itself, so this one stands
	// down for it - otherwise a project that both reparented its HUD and turned on the setting would get
	// the same panel twice, half a pixel apart.
	if (HUD->IsA<ABindGuardHUD>())
	{
		return;
	}

	DrawReport(Canvas, FVector2D(28.0f, 90.0f), 900.0f);
}

void UBindGuardSubsystem::DrawReport(UCanvas* Canvas, const FVector2D& Origin, const float Width) const
{
	using namespace BindGuardPanel;

	if (Canvas == nullptr)
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (Font == nullptr)
	{
		return;
	}

	const UBindGuardSettings& Settings = UBindGuardSettings::Get();
	const int32 MaxRows = FMath::Max(Settings.MaxReportRows, 1);

	// Worked out before anything is drawn, so the background is exactly as tall as the text and a report
	// with two findings does not sit in a panel sized for twenty.
	const int32 ShownFindings = FMath::Min(Report.Findings.Num(), MaxRows);
	const bool bTruncated = Report.Findings.Num() > ShownFindings;
	const bool bClean = Report.bHasRun && Report.ErrorCount == 0 && Report.WarningCount == 0;

	int32 LineCount = 3;											// title, headline, source line
	LineCount += ShownFindings;
	LineCount += bTruncated ? 1 : 0;
	LineCount += bClean ? (1 + Report.ChecksRun.Num()) : 0;			// "nothing to report" plus what was checked
	LineCount += Report.bHasRun ? 0 : 1;							// "no scan yet"

	const float BoxHeight = LineCount * LineHeight + BoxPadding * 2.0f;
	DrawFilledRect(Canvas,
		FVector2D(Origin.X - BoxPadding, Origin.Y - BoxPadding),
		FVector2D(Width, BoxHeight),
		PanelBackground);

	float LineY = static_cast<float>(Origin.Y);
	auto DrawLine = [&](FStringView Line, const FLinearColor& Color)
	{
		FCanvasTextStringViewItem Item(FVector2D(Origin.X, LineY), Line, Font, Color);
		Canvas->DrawItem(Item);
		LineY += LineHeight;
	};

	TStringBuilder<512> Line;

	// The title carries the verdict and the colour, so the answer is readable from across the room and
	// before a single finding has been read.
	const FLinearColor& VerdictColor =
		(Report.Verdict == EBindVerdict::Fail) ? ErrorColor :
		(Report.Verdict == EBindVerdict::Warn) ? WarnColor : GoodColor;

	const FString VerdictLabel = Report.bHasRun
		? UBindGuardStatics::VerdictName(Report.Verdict).ToUpper()
		: FString(TEXT("NOT RUN"));

	Line.Reset();
	Line.Appendf(TEXT("BindGuard   %s"), *VerdictLabel);
	DrawLine(Line.ToView(), Report.bHasRun ? VerdictColor : HeadingColor);

	DrawLine(*UBindGuardStatics::FormatHeadline(Report), BodyColor);

	// The honesty line. It says where the assets came from and, more importantly, whether anybody was
	// watching - because "0 contexts never added" means nothing at all if nothing was observed.
	Line.Reset();
	Line.Appendf(TEXT("source %s   |   %s"),
		Report.SourceName.IsEmpty() ? TEXT("unknown") : *Report.SourceName,
		Report.bObservationAvailable
			? TEXT("observed: contexts added in THIS session only - not proof that nothing ever adds them")
			: TEXT("observed checks did NOT run - nothing was watched, so nothing is claimed about them"));
	DrawLine(Line.ToView(), Report.bObservationAvailable ? DimColor : WarnColor);

	if (!Report.bHasRun)
	{
		DrawLine(TEXT("no scan yet - run BindGuard.Scan, or press the check button"), DimColor);
		return;
	}

	for (int32 Index = 0; Index < ShownFindings; ++Index)
	{
		const FBindGuardFinding& Finding = Report.Findings[Index];
		DrawLine(*UBindGuardStatics::FormatFinding(Finding), SeverityColor(Finding.Severity));
	}

	if (bTruncated)
	{
		Line.Reset();
		Line.Appendf(TEXT("... and %d more - BindGuard.Dump writes all of them to the log"),
			Report.Findings.Num() - ShownFindings);
		DrawLine(Line.ToView(), DimColor);
	}

	// Green is only allowed to mean something if it also says what it looked at. A checker that goes quiet
	// when it is happy is indistinguishable from a checker that never ran, and that is how a broken gate
	// stays broken for a milestone.
	if (bClean)
	{
		DrawLine(TEXT("nothing to report. checked:"), GoodColor);

		for (const FString& Check : Report.ChecksRun)
		{
			Line.Reset();
			Line.Appendf(TEXT("   %s"), *Check);
			DrawLine(Line.ToView(), Check.Contains(TEXT("NOT CHECKED")) ? WarnColor : DimColor);
		}
	}
}
