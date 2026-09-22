// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "BindGuardAssetScan.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "BindGuardAssetSource.h"
#include "BindGuardLog.h"
#include "BindGuardSettings.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Modules/ModuleManager.h"

void FBindGuardAssetScan::Register()
{
	FBindGuardAssetSource::OnGather().BindStatic(&FBindGuardAssetScan::Gather);

	// The name goes on the report header. "asset registry" and "loaded objects" are two different claims
	// about how complete a scan was, and a reader is entitled to know which one they are looking at.
	FBindGuardAssetSource::SetSourceName(TEXT("asset registry"));
}

void FBindGuardAssetScan::Unregister()
{
	FBindGuardAssetSource::OnGather().Unbind();
	FBindGuardAssetSource::SetSourceName(TEXT("loaded objects"));
}

void FBindGuardAssetScan::Gather(TArray<const UInputMappingContext*>& OutContexts, TArray<const UInputAction*>& OutActions)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// A scan that started before the registry finished its first pass would report a project with three
	// input actions in it. Waiting is the difference between a checker and a race condition.
	if (AssetRegistry.IsLoadingAssets())
	{
		UE_LOG(LogBindGuard, Log, TEXT("BindGuard: waiting for the asset registry to finish its scan."));
		AssetRegistry.WaitForCompletion();
	}

	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;
	Filter.ClassPaths.Add(UInputMappingContext::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(UInputAction::StaticClass()->GetClassPathName());

	const UBindGuardSettings& Settings = UBindGuardSettings::Get();
	for (const FDirectoryPath& Path : Settings.ScanPaths)
	{
		if (!Path.Path.IsEmpty())
		{
			Filter.PackagePaths.Add(FName(*Path.Path));
		}
	}

	// No paths configured means the whole game content tree, which is the answer that is right for most
	// projects and is the one a person expects when they have not been asked a question.
	if (Filter.PackagePaths.Num() == 0)
	{
		Filter.PackagePaths.Add(TEXT("/Game"));
	}

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(Filter, Assets);

	for (const FAssetData& Asset : Assets)
	{
		// GetAsset loads the package. That is the cost of this check and it is paid on purpose: the
		// mappings, the keys and the chord triggers live inside the asset and are not in the registry tags,
		// so there is no cheaper way to read them that is also correct. The scan cost is measured and
		// printed on the report so nobody has to guess what it is.
		UObject* Object = Asset.GetAsset();

		if (const UInputMappingContext* Context = Cast<UInputMappingContext>(Object))
		{
			OutContexts.AddUnique(Context);
		}
		else if (const UInputAction* Action = Cast<UInputAction>(Object))
		{
			OutActions.AddUnique(Action);
		}
	}

	UE_LOG(LogBindGuard, Verbose, TEXT("BindGuard: asset registry gave %d context(s) and %d action(s)."),
		OutContexts.Num(), OutActions.Num());
}
