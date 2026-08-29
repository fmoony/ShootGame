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

		PublicIncludePaths.AddRange(new string[] {
			"ShootGame",
			"ShootGame/GameFramework",
			"ShootGame/Characters",
			"ShootGame/Characters/Animation",
			"ShootGame/Characters/Animation/AnimNodes",
			"ShootGame/AI",
			"ShootGame/Weapons",
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
