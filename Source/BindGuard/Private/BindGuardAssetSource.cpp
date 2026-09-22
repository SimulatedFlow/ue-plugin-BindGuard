// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "BindGuardAssetSource.h"

#include "BindGuardLog.h"
#include "BindGuardSettings.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace BindGuardAssetSourcePrivate
{
	static FBindGuardAssetSource::FGatherDelegate GGatherDelegate;
	static FString GSourceName(TEXT("loaded objects"));

	/** A class default object is not an asset, and neither is anything living in the transient package. */
	static bool IsRealAsset(const UObject* Object)
	{
		return IsValid(Object)
			&& !Object->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)
			&& Object->GetOutermost() != GetTransientPackage();
	}
}

FBindGuardAssetSource::FGatherDelegate& FBindGuardAssetSource::OnGather()
{
	return BindGuardAssetSourcePrivate::GGatherDelegate;
}

void FBindGuardAssetSource::SetSourceName(const FString& InSourceName)
{
	BindGuardAssetSourcePrivate::GSourceName = InSourceName;
}

const FString& FBindGuardAssetSource::GetSourceName()
{
	return BindGuardAssetSourcePrivate::GSourceName;
}

void FBindGuardAssetSource::Gather(TArray<const UInputMappingContext*>& OutContexts, TArray<const UInputAction*>& OutActions)
{
	if (OnGather().IsBound())
	{
		OnGather().Execute(OutContexts, OutActions);
	}
	else
	{
		GatherFromLoadedObjects(OutContexts, OutActions);
	}

	// Always merged in on top, whichever provider ran. In the editor this is redundant; in a cooked build
	// it is the only way to check a context nothing has loaded yet.
	GatherFromSettings(OutContexts, OutActions);

	// A context can name actions the action sweep never saw - an action in a plugin folder outside the
	// scan paths, for instance. Adding them here means "unbound" is only ever said about actions BindGuard
	// genuinely knows the whole story for.
	for (const UInputMappingContext* Context : OutContexts)
	{
		if (!IsValid(Context))
		{
			continue;
		}

		Context->ForEachKeyMapping([&OutActions](const FEnhancedActionKeyMapping& Mapping)
		{
			if (const UInputAction* Action = Mapping.Action.Get())
			{
				OutActions.AddUnique(Action);
			}
		});
	}
}

void FBindGuardAssetSource::GatherFromLoadedObjects(TArray<const UInputMappingContext*>& OutContexts, TArray<const UInputAction*>& OutActions)
{
	using namespace BindGuardAssetSourcePrivate;

	for (TObjectIterator<UInputMappingContext> It; It; ++It)
	{
		if (IsRealAsset(*It))
		{
			OutContexts.AddUnique(*It);
		}
	}

	for (TObjectIterator<UInputAction> It; It; ++It)
	{
		if (IsRealAsset(*It))
		{
			OutActions.AddUnique(*It);
		}
	}
}

void FBindGuardAssetSource::GatherFromSettings(TArray<const UInputMappingContext*>& OutContexts, TArray<const UInputAction*>& OutActions)
{
	const UBindGuardSettings& Settings = UBindGuardSettings::Get();

	// LoadSynchronous, and on purpose. This runs on demand from a console command, a menu entry or a
	// button - never in a tick - and an asynchronous load here would mean the scan reported on assets that
	// had not arrived yet, which is a scan that is wrong in a way nobody would notice.
	for (const TSoftObjectPtr<UInputMappingContext>& Soft : Settings.ExtraContexts)
	{
		if (Soft.IsNull())
		{
			continue;
		}

		if (const UInputMappingContext* Context = Soft.LoadSynchronous())
		{
			OutContexts.AddUnique(Context);
		}
		else
		{
			UE_LOG(LogBindGuard, Warning, TEXT("BindGuard: could not load the extra mapping context %s."), *Soft.ToString());
		}
	}

	for (const TSoftObjectPtr<UInputAction>& Soft : Settings.ExtraActions)
	{
		if (Soft.IsNull())
		{
			continue;
		}

		if (const UInputAction* Action = Soft.LoadSynchronous())
		{
			OutActions.AddUnique(Action);
		}
		else
		{
			UE_LOG(LogBindGuard, Warning, TEXT("BindGuard: could not load the extra input action %s."), *Soft.ToString());
		}
	}
}
