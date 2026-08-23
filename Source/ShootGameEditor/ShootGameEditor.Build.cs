// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ShootGameEditor : ModuleRules
{
	public ShootGameEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"ShootGame",
			"AnimGraph",
			"AnimGraphRuntime",
			"BlueprintGraph"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"UnrealEd",
			"Slate",
			"SlateCore"
		});
	}
}
