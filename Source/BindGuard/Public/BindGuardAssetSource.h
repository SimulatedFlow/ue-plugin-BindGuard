// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"

class UInputAction;
class UInputMappingContext;

/**
 * Where the assets to check come from - and the seam that keeps the runtime module free of the editor.
 *
 * The runtime module cannot walk the asset registry, and it must not try. In a cooked build the only
 * mapping contexts and input actions that exist in memory are the ones something already loaded, and the
 * single most valuable static check - "which action did nobody bind" - is a question about the assets
 * nobody loaded. So the good answer lives in the editor module, and it is pushed in here through a
 * delegate at module startup. The dependency arrow points editor -> runtime and never back, which is what
 * keeps the report working in the packaged build, which is the build you most want it in.
 *
 * With no provider registered there is still a real answer, just a smaller one: every mapping context and
 * input action currently loaded, plus anything named in Project Settings > Plugins > BindGuard. The
 * report always says which of the two it got, on the header line, because a scan that could only see
 * loaded assets and a scan that saw the whole project are not the same claim.
 */
class BINDGUARD_API FBindGuardAssetSource
{
public:
	/** Fill the two arrays with every context and every action the project owns. */
	DECLARE_DELEGATE_TwoParams(FGatherDelegate, TArray<const UInputMappingContext*>& /*OutContexts*/, TArray<const UInputAction*>& /*OutActions*/);

	/** Bind this from an editor module to replace the loaded-objects fallback with a real asset walk. */
	static FGatherDelegate& OnGather();

	/** What the report calls the source it used: "asset registry", "loaded objects". */
	static void SetSourceName(const FString& InSourceName);
	static const FString& GetSourceName();

	/**
	 * Gather, using the registered provider if there is one and the fallback if there is not, then merge
	 * in whatever the settings name explicitly.
	 *
	 * The returned pointers are raw and are not kept alive. That is deliberate and safe: gathering and
	 * analysing happen inside one synchronous call, nothing yields, and the analysis keeps only names and
	 * keys. Nothing in this plugin holds a UObject pointer across a frame boundary.
	 */
	static void Gather(TArray<const UInputMappingContext*>& OutContexts, TArray<const UInputAction*>& OutActions);

private:
	/** The fallback: everything currently in memory. Honest, and clearly labelled as partial. */
	static void GatherFromLoadedObjects(TArray<const UInputMappingContext*>& OutContexts, TArray<const UInputAction*>& OutActions);

	/** Whatever ExtraContexts and ExtraActions name, loaded synchronously. The cooked-build escape hatch. */
	static void GatherFromSettings(TArray<const UInputMappingContext*>& OutContexts, TArray<const UInputAction*>& OutActions);
};
