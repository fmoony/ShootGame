// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "ShootGame.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Weapons/Definitions/ShooterWeaponDefinition.h"

namespace ShooterWeaponDefinitionMigrationTool
{
	/** 通过 Unreal Editor 资产系统创建或更新 Definition 资产并保存到磁盘。 */
	UShooterWeaponDefinition* CreateOrUpdateDefinitionAsset(
		FAutomationTestBase& Test,
		const FString& AssetPath,
		TSubclassOf<AShooterWeapon> WeaponActorClass,
		int32 MagazineSize,
		int32 InitialReserveAmmo,
		bool bFullAuto,
		float RefireRate)
	{
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Test.TestNotNull(TEXT("Definition package created"), Package))
		{
			return nullptr;
		}

		const FName AssetName = *FPackageName::GetShortName(AssetPath);
		UShooterWeaponDefinition* Definition = FindObjectFast<UShooterWeaponDefinition>(Package, AssetName);
		if (!Definition)
		{
			Definition = NewObject<UShooterWeaponDefinition>(
				Package,
				AssetName,
				RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(Definition);
		}

		Definition->WeaponActorClass = WeaponActorClass;
		Definition->AmmoConfig.MagazineSize = MagazineSize;
		Definition->AmmoConfig.InitialReserveAmmo = InitialReserveAmmo;
		Definition->FireConfig.bFullAuto = bFullAuto;
		Definition->FireConfig.RefireRate = RefireRate;
		Definition->MarkPackageDirty();

		const FString FileName = FPackageName::LongPackageNameToFilename(
			AssetPath,
			FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = GError;
		if (!Test.TestTrue(
			TEXT("Definition package saved"),
			UPackage::SavePackage(Package, Definition, *FileName, SaveArgs)))
		{
			return nullptr;
		}

		return Definition;
	}
}

/**
 * A1 工具测试：创建最小测试 Definition 资产 /Game/Shooter/Weapons/Definitions/WD_TestAuto，
 * 证明 Editor 上下文可以通过资产系统发现并保存该类型；
 * 配合 ShootGame.WeaponDefinition.AssetResolution 验证 AssetManager 扫描闭环。
 * 本工具属于实施计划的临时迁移入口，B4 收口时删除。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponDefinitionCreateTestAssetTool,
	"ShootGame.Tools.WeaponDefinition.CreateTestAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponDefinitionCreateTestAssetTool::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponDefinitionMigrationTool;

	UClass* RifleClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C"));
	if (!TestNotNull(TEXT("Rifle weapon class loaded"), RifleClass))
	{
		return false;
	}

	UShooterWeaponDefinition* Definition = CreateOrUpdateDefinitionAsset(
		*this,
		TEXT("/Game/Shooter/Weapons/Definitions/WD_TestAuto"),
		RifleClass,
		30,
		90,
		true,
		0.1f);
	if (!Definition)
	{
		return false;
	}

	TestTrue(TEXT("Test definition is valid for grant"), Definition->IsValidForGrant());
	TestEqual(
		TEXT("Test definition uses the fixed primary asset type"),
		Definition->GetPrimaryAssetId().PrimaryAssetType,
		UShooterWeaponDefinition::GetWeaponDefinitionAssetType());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
