// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "ShootGame.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Weapons/Definitions/ShooterWeaponDefinition.h"
#include "Weapons/ShooterProjectileFireBehavior.h"

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
		// 已存在的资产必须先完整加载再改写；在未加载包上直接 NewObject 会因
		// “partially loaded”在保存时触发 Critical error。
		const FString ObjectPath = AssetPath + TEXT(".") + FPackageName::GetShortName(AssetPath);
		UShooterWeaponDefinition* Definition = LoadObject<UShooterWeaponDefinition>(nullptr, *ObjectPath);
		UPackage* Package = Definition
			? Definition->GetOutermost()
			: CreatePackage(*AssetPath);
		if (!Test.TestNotNull(TEXT("Definition package resolved"), Package))
		{
			return nullptr;
		}

		if (!Definition)
		{
			const FName AssetName = *FPackageName::GetShortName(AssetPath);
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

		// A3：为测试 Definition 配置弹丸行为；弹丸类与 Rifle 常规弹丸刻意不同，
		// 供 FireBehavior 测试证明弹丸类来自 Definition 行为而非 WeaponActor CDO。
		if (!Definition->FireBehavior)
		{
			Definition->FireBehavior = NewObject<UShooterProjectileFireBehavior>(
				Definition,
				NAME_None,
				RF_Transactional);
		}
		if (UShooterProjectileFireBehavior* ProjectileBehavior =
			Cast<UShooterProjectileFireBehavior>(Definition->FireBehavior))
		{
			UClass* PistolBulletClass = LoadObject<UClass>(
				nullptr,
				TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterProjectile_Bullet_Pistol.BP_ShooterProjectile_Bullet_Pistol_C"));
			Test.TestTrue(TEXT("Pistol bullet class loaded"), PistolBulletClass != nullptr);
			ProjectileBehavior->ProjectileClass = PistolBulletClass;
		}
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

	// 弹匣 7 / 备弹 21：与 BP_ShooterWeapon_Rifle 的 CDO 配置刻意不同，
	// 供 Inventory 换弹容量测试证明容量来自 Definition 而非 WeaponActor CDO。
	UShooterWeaponDefinition* Definition = CreateOrUpdateDefinitionAsset(
		*this,
		TEXT("/Game/Shooter/Weapons/Definitions/WD_TestAuto"),
		RifleClass,
		7,
		21,
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
