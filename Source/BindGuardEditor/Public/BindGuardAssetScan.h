// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UInputAction;
class UInputMappingContext;

/**
 * The asset registry walk.
 *
 * This is the only thing the editor module contributes, and it is the thing that turns a partial check
 * into a complete one. "Which action did nobody bind" is a question about the assets nothing has loaded,
 * so it can only be answered by something that can see assets on disk - and that is the asset registry,
 * which exists in the editor and not in a shipped game.
 *
 * Registered into FBindGuardAssetSource at module startup, which is why the runtime module gets the good
 * answer in the editor without ever knowing this module exists.
 */
class BINDGUARDEDITOR_API FBindGuardAssetScan
{
public:
	/** Bind this into FBindGuardAssetSource. Called from the editor module's startup. */
	static void Register();
	static void Unregister();

	/** Load every UInputMappingContext and UInputAction under the configured scan paths. */
	static void Gather(TArray<const UInputMappingContext*>& OutContexts, TArray<const UInputAction*>& OutActions);
};
