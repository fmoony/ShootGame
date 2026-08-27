// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ShootGameEditor : ModuleRules
{
	public ShootGameEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// 自定义 AnimGraphNode 只服务未烹饪资产的编辑与独立测试；正式烹饪后仅保留 Runtime AnimNode。
		// 与 UE 动画插件的 GraphNode 模块一致，允许 UnrealEditor -game 反序列化节点绑定。
		OverridePackageType = PackageOverrideType.GameUncookedOnly;

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
			"BlueprintEditorLibrary",
			"Kismet",
			"Slate",
			"SlateCore"
		});
	}
}
