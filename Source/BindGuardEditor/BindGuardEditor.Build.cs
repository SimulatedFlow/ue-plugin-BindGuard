// Copyright 2026 Silvan Teufel. All Rights Reserved.

using UnrealBuildTool;

public class BindGuardEditor : ModuleRules
{
	public BindGuardEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// One job: hand the runtime module every UInputAction and UInputMappingContext the project owns,
		// so the three static checks can run before anybody presses Play.
		//
		// The runtime module cannot do this itself. In a cooked build the only mapping contexts that
		// exist are the ones something has already loaded, and the whole point of "which action did
		// nobody bind" is that it asks about the assets nobody loaded. So the asset registry walk lives
		// here and is pushed into the runtime module through FBindGuardAssetSource, a delegate. The
		// dependency arrow points editor -> runtime and never back.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",

			// The rules, the report, the settings and the delegate this module fills in.
			"BindGuard",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Where the assets come from.
			"AssetRegistry",

			// UInputAction / UInputMappingContext class paths for the registry filter.
			"EnhancedInput",

			// GEditor and the editor's notion of when it is safe to load an asset.
			"UnrealEd",

			// The entry under Tools, and the toast that says how the scan went.
			"ToolMenus",
			"Slate",
			"SlateCore",
			"InputCore",
		});
	}
}
