// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ParadoxiaCoreRuntime : ModuleRules
{
	public ParadoxiaCoreRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
            }
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
            }
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
                "HTTP",
                "LyraGame",
                "ModularGameplay",
                "CommonGame",
            }
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"InputCore",
                "Slate",
                "SlateCore",
                "RenderCore",
                "DeveloperSettings",
                "EnhancedInput",
				"Engine",
				"NetCore",
				"RHI",
				"UMG",
				"CommonUI",
				"CommonInput",
				"CommonGame",
				"CommonUser",
				"GameSubtitles",
				"GameplayMessageRuntime",
				"EngineSettings",
				"Json",
				"JsonUtilities",
				"HTTP",
				"OWSPlugin",
                "CoreUObject",
            }
			);

		PublicIncludePathModuleNames.AddRange(new string[] {
			"OWSPlugin",
			"LyraGame"
		});

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
