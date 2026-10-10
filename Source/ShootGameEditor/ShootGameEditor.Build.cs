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
			"Core", "CoreUObject", "Engine", "ShootGame", "AnimGraph", "AnimGraphRuntime", "BlueprintGraph"
		});

			// AnimationDataController：UAnimSequenceBase::GetController 内部按模块名取控制器实现，
			// Rifle 动画派生的原生裁切与逐骨骼轨道写入依赖它已加载。
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "BlueprintEditorLibrary", "Kismet",
				"Slate", "SlateCore", "AssetTools", "AnimationDataController" });
	}
}
