// Copyright 2026 Silvan Teufel. All Rights Reserved.

using UnrealBuildTool;

public class BindGuard : ModuleRules
{
	public BindGuard(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Everything that decides anything lives here, in a Runtime module, and that is the whole
		// architecture of this plugin in one sentence.
		//
		// Two of the five checks - "nobody ever added this context" and "this action is unreachable
		// because its only context is never active" - cannot be answered from assets on disk. Whether
		// AddMappingContext is called is a decision made by Blueprint logic at runtime, so the only
		// honest way to answer it is to watch it happen. That means the observer has to be in a module
		// that exists in a cooked Shipping build, and it means the report has to be drawn by something
		// that also exists there - hence UCanvas from an AHUD, and no UMG.
		//
		// The editor module next door adds one thing and one thing only: the asset registry, so the
		// three static checks can run with no game standing. It depends on this module. This module
		// must never depend on it.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",

			// AHUD, UCanvas, UGameInstanceSubsystem, ULocalPlayer.
			"Engine",

			// FKey and, more to the point, FKey::IsGamepadKey / IsMouseButton / IsTouch. The device a
			// binding lives on is read off the key itself and never off the name of the action, which
			// is why renaming IA_Jump_KBM to IA_Jump does not change a single line of the report.
			"InputCore",

			// UInputAction, UInputMappingContext, FEnhancedActionKeyMapping, and the two delegates
			// this plugin's entire "observed" half is built on:
			// UEnhancedInputLocalPlayerSubsystem::OnMappingContextAdded / OnMappingContextRemoved.
			"EnhancedInput",

			// UBindGuardSettings is a UDeveloperSettings, so the required devices and the exemption
			// list appear under Project Settings > Plugins > BindGuard with no editor module involved.
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// GWhiteTexture - the one-pixel texture the report panel is tiled from.
			"RenderCore",

			// Saved/BindGuard/report.json. Hand-rolled string building would have saved a dependency
			// and cost correct escaping, and the entire value of the gate is that a build server can
			// parse the result without guessing.
			//
			// JsonUtilities is deliberately not here: FJsonObjectConverter would name the fields after
			// the C++ members, and the report's field names are a published interface that build
			// scripts grep. They are spelled out by hand for that reason.
			"Json",
		});

		// Deliberately NOT here:
		//   UMG      - the report is drawn on UCanvas so it survives a cooked Shipping build. A plugin
		//              whose entire claim is a verdict cannot afford that verdict to be stripped in the
		//              build that ships. The demo panel is a UMG asset in Content that calls the
		//              Blueprint library, exactly as a project would.
		//   UnrealEd / AssetRegistry - see above. The static scan is fed to this module through a
		//              delegate the editor module fills in; nothing here knows the editor exists.
	}
}
