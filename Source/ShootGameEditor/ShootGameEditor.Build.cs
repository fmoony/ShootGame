// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ShootGameEditor : ModuleRules
{
	public ShootGameEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// 与引擎 AnimGraph 模块（EngineDeveloper）一致：AnimGraphNode 放在 Developer 包中，
		// 避免 AnimBP 编译时出现 "node is from an Editor Only module" 警告。
		OverridePackageType = PackageOverrideType.GameDeveloper;

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
