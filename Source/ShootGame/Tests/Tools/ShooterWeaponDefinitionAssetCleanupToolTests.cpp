// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ObjectTools.h"
#include "ShootGame.h"
#include "UObject/ObjectRedirector.h"

namespace ShooterWeaponDefinitionAssetCleanup
{
	/** 纠偏前由 A4 迁移工具生成的 Definition 资产；纠偏后已无任何引用。 */
	const TCHAR* const DefinitionAssetPaths[] = {
		TEXT("/Game/Shooter/Weapons/Definitions/WD_Rifle.WD_Rifle"),
		TEXT("/Game/Shooter/Weapons/Definitions/WD_Pistol.WD_Pistol"),
		TEXT("/Game/Shooter/Weapons/Definitions/WD_AWP.WD_AWP"),
		TEXT("/Game/Shooter/Weapons/Definitions/WD_GrenadeLauncher.WD_GrenadeLauncher"),
		TEXT("/Game/Shooter/Weapons/Definitions/WD_TestAuto.WD_TestAuto"),
	};

	const TCHAR* const DefinitionFolderPath = TEXT("/Game/Shooter/Weapons/Definitions");
}

/**
 * 一次性资产收口工具：通过 Unreal Editor 资产系统删除已无引用的 WD_* Definition 资产，
 * 并确认目录中不残留重定向器。
 *
 * 约束（单表武器配置纠偏小计划 C3）：
 * - 删除前用 Asset Registry 确认没有引用者，存在引用时 fail closed 不删除；
 * - 使用 ObjectTools::DeleteAssets 走编辑器资产删除路径，不直接改 .uasset；
 * - 删除后回读目录，确认资产与重定向器都已消失。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponDefinitionAssetCleanupTool,
	"ShootGame.Tools.WeaponConfig.DeleteDefinitionAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponDefinitionAssetCleanupTool::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponDefinitionAssetCleanup;

	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AssetsToDelete;
	for (const TCHAR* AssetPath : DefinitionAssetPaths)
	{
		const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));
		if (!AssetData.IsValid())
		{
			// 已删除视为通过；本工具是幂等的收口步骤。
			UE_LOG(LogShootGame, Display, TEXT("WEAPONDEFINITIONCLEANUP|absent|%s"), AssetPath);
			continue;
		}

		// 存在引用者时不允许删除：单表纠偏要求先完成全部引用迁移。
		TArray<FName> Referencers;
		AssetRegistry.GetReferencers(AssetData.PackageName, Referencers);
		if (Referencers.Num() > 0)
		{
			AddError(FString::Printf(
				TEXT("Definition asset %s still has %d referencer(s); migrate them before deleting"),
				AssetPath,
				Referencers.Num()));
			for (const FName& Referencer : Referencers)
			{
				AddError(FString::Printf(TEXT("  referencer: %s"), *Referencer.ToString()));
			}
			continue;
		}

		AssetsToDelete.Add(AssetData);
	}

	if (AssetsToDelete.Num() > 0)
	{
		const int32 DeletedCount = ObjectTools::DeleteAssets(AssetsToDelete, /*bShowConfirmation=*/false);
		TestEqual(
			TEXT("All unreferenced Definition assets are deleted"),
			DeletedCount,
			AssetsToDelete.Num());
	}

	// 回读：资产与目录中的重定向器都必须消失。
	for (const TCHAR* AssetPath : DefinitionAssetPaths)
	{
		TestFalse(
			FString::Printf(TEXT("Definition asset %s is gone"), AssetPath).GetCharArray().GetData(),
			AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(AssetPath)).IsValid());
	}

	TArray<FAssetData> RemainingAssets;
	AssetRegistry.GetAssetsByPath(FName(DefinitionFolderPath), RemainingAssets, /*bRecursive=*/true);
	for (const FAssetData& RemainingAsset : RemainingAssets)
	{
		TestFalse(
			FString::Printf(
				TEXT("No redirector or leftover asset remains: %s"),
				*RemainingAsset.GetObjectPathString()).GetCharArray().GetData(),
			RemainingAsset.GetClass() == UObjectRedirector::StaticClass());
	}

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("WEAPONDEFINITIONCLEANUP|done|Deleted=%d|Remaining=%d"),
		AssetsToDelete.Num(),
		RemainingAssets.Num());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
