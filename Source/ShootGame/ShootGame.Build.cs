// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ShootGame : ModuleRules
{
	public ShootGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"NetCore",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"AnimGraphRuntime",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "AnimationCore", "Niagara" });

		if (Target.bBuildEditor)
		{
			// 单表武器配置纠偏的一次性资产收口工具需要编辑器资产删除 API（ObjectTools）。
			// 只在编辑器目标链接，Game 目标不引入 UnrealEd。
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

		PublicIncludePaths.AddRange(new string[] {
			"ShootGame",
			"ShootGame/GameFramework",
			"ShootGame/Characters",
			"ShootGame/Characters/Animation",
			"ShootGame/Characters/Animation/AnimNodes",
            "ShootGame/AI",
            "ShootGame/Weapons",
            "ShootGame/Pool",
            "ShootGame/Inventory",
			"ShootGame/Tests/Inventory",
		"ShootGame/Tests/Equipment",
			"ShootGame/UI",
			"ShootGame/AbilitySystem",
			"ShootGame/AbilitySystem/Abilities"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
