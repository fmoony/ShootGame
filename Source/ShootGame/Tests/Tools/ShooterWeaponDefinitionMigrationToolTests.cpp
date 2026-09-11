// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "NiagaraSystem.h"
#include "Engine/DataTable.h"
#include "Misc/PackageName.h"
#include "ShootGame.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "Weapons/Definitions/ShooterWeaponDefinition.h"
#include "Weapons/ShooterProjectileFireBehavior.h"
#include "Weapons/ShooterProjectile.h"
#include "Weapons/ShooterPickup.h"
#include "Weapons/ShooterWeapon.h"

namespace ShooterWeaponDefinitionMigrationTool
{
	/** 保存资产包到磁盘的统一入口。 */
	bool SaveAssetPackage(FAutomationTestBase& Test, UPackage* Package, UObject* Asset)
	{
		const FString FileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		return Test.TestTrue(
			TEXT("Asset package saved"),
			UPackage::SavePackage(Package, Asset, *FileName, SaveArgs));
	}

	/**
	 * 通过 Unreal Editor 资产系统创建或更新 Definition 资产。
	 * 已存在的资产必须先完整加载再改写；在未加载包上直接 NewObject 会因
	 * “partially loaded”在保存时触发 Critical error。
	 */
	UShooterWeaponDefinition* CreateOrUpdateDefinitionAsset(
		FAutomationTestBase& Test,
		const FString& AssetPath,
		TSubclassOf<AShooterWeapon> WeaponActorClass)
	{
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
		return Definition;
	}

	// ---- 反射读取 WeaponActor CDO 配置（迁移后 CDO 不再是生产数据源） ----

	int32 ReadInt(const UClass* Class, const TCHAR* Name)
	{
		const FIntProperty* Property = FindFProperty<FIntProperty>(Class, Name);
		return Property ? Property->GetPropertyValue_InContainer(Class->GetDefaultObject()) : 0;
	}

	float ReadFloat(const UClass* Class, const TCHAR* Name)
	{
		const FFloatProperty* Property = FindFProperty<FFloatProperty>(Class, Name);
		return Property ? Property->GetPropertyValue_InContainer(Class->GetDefaultObject()) : 0.0f;
	}

	bool ReadBool(const UClass* Class, const TCHAR* Name)
	{
		const FBoolProperty* Property = FindFProperty<FBoolProperty>(Class, Name);
		return Property ? Property->GetPropertyValue_InContainer(Class->GetDefaultObject()) : false;
	}

	FName ReadFName(const UClass* Class, const TCHAR* Name)
	{
		const FNameProperty* Property = FindFProperty<FNameProperty>(Class, Name);
		return Property ? Property->GetPropertyValue_InContainer(Class->GetDefaultObject()) : NAME_None;
	}

	UObject* ReadObject(const UClass* Class, const TCHAR* Name)
	{
		const FObjectProperty* Property = FindFProperty<FObjectProperty>(Class, Name);
		return Property ? Property->GetPropertyValue_InContainer(Class->GetDefaultObject()).Get() : nullptr;
	}

	UClass* ReadClass(const UClass* Class, const TCHAR* Name)
	{
		const FClassProperty* Property = FindFProperty<FClassProperty>(Class, Name);
		return Property
			? Cast<UClass>(Property->GetPropertyValue_InContainer(Class->GetDefaultObject()).Get())
			: nullptr;
	}
}

/**
 * A1 工具测试：创建最小测试 Definition 资产 /Game/Shooter/Weapons/Definitions/WD_TestAuto。
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
		RifleClass);
	if (!Definition)
	{
		return false;
	}

	// 幂等短路：磁盘内容已是目标配置时跳过保存；该文件可能被同时打开的
	// 编辑器进程锁定，重复写盘会在文件被占用时失败。
	const bool bTargetConfig =
		Definition->AmmoConfig.MagazineSize == 7 &&
		Definition->AmmoConfig.InitialReserveAmmo == 21 &&
		Cast<UShooterProjectileFireBehavior>(Definition->FireBehavior) &&
		Cast<UShooterProjectileFireBehavior>(Definition->FireBehavior)->ProjectileClass != nullptr;
	if (bTargetConfig)
	{
		TestTrue(TEXT("Test definition is valid for grant"), Definition->IsValidForGrant());
		return true;
	}

	Definition->AmmoConfig.MagazineSize = 7;
	Definition->AmmoConfig.InitialReserveAmmo = 21;
	Definition->FireConfig.bFullAuto = true;
	Definition->FireConfig.RefireRate = 0.1f;

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
		TestTrue(TEXT("Pistol bullet class loaded"), PistolBulletClass != nullptr);
		ProjectileBehavior->ProjectileClass = PistolBulletClass;
	}
	Definition->MarkPackageDirty();

	if (!SaveAssetPackage(*this, Definition->GetOutermost(), Definition))
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

/**
 * A4 工具测试：为所有当前可达武器创建正式 Definition 资产，并把 Pickup DataTable
 * 迁移到 Definition 软引用。配置从各武器蓝图 CDO 一次性拷贝（反射读取），此后
 * WeaponActor CDO 的授予相关配置退出生产路径。本工具属于临时迁移入口，B4 删除。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponDefinitionMigrateProductionTool,
	"ShootGame.Tools.WeaponDefinition.MigrateProductionAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponDefinitionMigrateProductionTool::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponDefinitionMigrationTool;

	struct FWeaponMigration
	{
		const TCHAR* RowName;
		const TCHAR* WeaponClassPath;
	};
	static const FWeaponMigration Migrations[] = {
		{ TEXT("Rifle"), TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C") },
		{ TEXT("Pistol"), TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Pistol.BP_ShooterWeapon_Pistol_C") },
		{ TEXT("AWP"), TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_AWP.BP_ShooterWeapon_AWP_C") },
		{ TEXT("GrenadeLauncher"), TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_GrenadeLauncher.BP_ShooterWeapon_GrenadeLauncher_C") },
	};

	UDataTable* WeaponTable = LoadObject<UDataTable>(
		nullptr,
		TEXT("/Game/Shooter/Data/DT_WeaponData"));
	if (!TestNotNull(TEXT("Weapon DataTable loaded"), WeaponTable))
	{
		return false;
	}

	// 幂等短路：所有行已指向正确 Definition 且预览网格已归档时直接通过，
	// 不再写盘（运行中的编辑器可能持有 DataTable 文件句柄，重复保存会失败）。
	bool bAlreadyMigrated = WeaponTable->GetRowNames().Num() > 0;
	for (const FName& RowName : WeaponTable->GetRowNames())
	{
		const FWeaponTableRow* Row = WeaponTable->FindRow<FWeaponTableRow>(RowName, TEXT("WeaponDefinitionMigration"));
		UShooterWeaponDefinition* Existing = Row ? Row->WeaponDefinition.LoadSynchronous() : nullptr;
		if (!Row ||
			!Existing ||
			Existing->GetFName() != *FString::Printf(TEXT("WD_%s"), *RowName.ToString()) ||
			Existing->PresentationConfig.PickupPreviewMesh.Get() != Row->StaticMesh.Get())
		{
			bAlreadyMigrated = false;
			break;
		}
	}
	if (bAlreadyMigrated)
	{
		UE_LOG(LogShootGame, Display, TEXT("WeaponDefinition migration skipped: DataTable already migrated"));
		return true;
	}

	TMap<FName, TObjectPtr<UShooterWeaponDefinition>> MigratedDefinitions;

	for (const FWeaponMigration& Migration : Migrations)
	{
		UClass* WeaponClass = LoadObject<UClass>(nullptr, Migration.WeaponClassPath);
		if (!TestNotNull(
			FString::Printf(TEXT("Weapon class %s loaded"), Migration.RowName).GetCharArray().GetData(),
			WeaponClass))
		{
			return false;
		}

		const FString DefinitionPath = FString::Printf(
			TEXT("/Game/Shooter/Weapons/Definitions/WD_%s"),
			Migration.RowName);
		UShooterWeaponDefinition* Definition = CreateOrUpdateDefinitionAsset(*this, DefinitionPath, WeaponClass);
		if (!Definition)
		{
			return false;
		}

		// 授予相关配置：弹匣 / 备弹 / 开火节奏 / 射击噪声。
		Definition->AmmoConfig.MagazineSize = ReadInt(WeaponClass, TEXT("MagazineSize"));
		Definition->AmmoConfig.InitialReserveAmmo = ReadInt(WeaponClass, TEXT("InitialReserveAmmo"));
		Definition->FireConfig.bFullAuto = ReadBool(WeaponClass, TEXT("bFullAuto"));
		Definition->FireConfig.RefireRate = ReadFloat(WeaponClass, TEXT("RefireRate"));
		Definition->FireConfig.ShotLoudness = ReadFloat(WeaponClass, TEXT("ShotLoudness"));
		Definition->FireConfig.ShotNoiseRange = ReadFloat(WeaponClass, TEXT("ShotNoiseRange"));
		Definition->FireConfig.ShotNoiseTag = ReadFName(WeaponClass, TEXT("ShotNoiseTag"));

		// 表现配置归档：动画类 / Montage / 特效 / 音效 / 后坐力 / 散布。
		Definition->PresentationConfig.FirstPersonAnimInstanceClass = ReadClass(WeaponClass, TEXT("FirstPersonAnimInstanceClass"));
		Definition->PresentationConfig.ThirdPersonAnimInstanceClass = ReadClass(WeaponClass, TEXT("ThirdPersonAnimInstanceClass"));
		Definition->PresentationConfig.FiringMontage = Cast<UAnimMontage>(ReadObject(WeaponClass, TEXT("FiringMontage")));
		Definition->PresentationConfig.MuzzleFlash = Cast<UNiagaraSystem>(ReadObject(WeaponClass, TEXT("MuzzleFlash")));
		Definition->PresentationConfig.FireSound = Cast<USoundBase>(ReadObject(WeaponClass, TEXT("FireSound")));
		Definition->PresentationConfig.ReloadMagazineOutSound = Cast<USoundBase>(ReadObject(WeaponClass, TEXT("ReloadMagazineOutSound")));
		Definition->PresentationConfig.ReloadMagazineInSound = Cast<USoundBase>(ReadObject(WeaponClass, TEXT("ReloadMagazineInSound")));
		Definition->PresentationConfig.ReloadCockingSound = Cast<USoundBase>(ReadObject(WeaponClass, TEXT("ReloadCockingSound")));
		Definition->PresentationConfig.FiringRecoil = ReadFloat(WeaponClass, TEXT("FiringRecoil"));
		Definition->PresentationConfig.AimVariance = ReadFloat(WeaponClass, TEXT("AimVariance"));

		// 网格归档：来自 CDO 组件模板。
		const AShooterWeapon* WeaponDefaults = WeaponClass->GetDefaultObject<AShooterWeapon>();
		if (const USkeletalMeshComponent* FirstPersonMesh = WeaponDefaults->GetFirstPersonMesh())
		{
			Definition->PresentationConfig.FirstPersonMesh = FirstPersonMesh->GetSkeletalMeshAsset();
		}
		if (const USkeletalMeshComponent* ThirdPersonMesh = WeaponDefaults->GetThirdPersonMesh())
		{
			Definition->PresentationConfig.ThirdPersonMesh = ThirdPersonMesh->GetSkeletalMeshAsset();
		}

		// 弹丸行为：ProjectileClass 从 CDO 一次性迁入行为实例。
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
			ProjectileBehavior->ProjectileClass = ReadClass(WeaponClass, TEXT("ProjectileClass"));
		}

		Definition->MarkPackageDirty();
		if (!SaveAssetPackage(*this, Definition->GetOutermost(), Definition))
		{
			return false;
		}
		TestTrue(
			FString::Printf(TEXT("Definition %s is valid for grant"), Migration.RowName).GetCharArray().GetData(),
			Definition->IsValidForGrant());

		MigratedDefinitions.Add(FName(Migration.RowName), Definition);
	}

	// DataTable 迁移：行名 -> Definition 软引用。
	bool bAllRowsMigrated = true;
	for (const FName& RowName : WeaponTable->GetRowNames())
	{
		FWeaponTableRow* Row = WeaponTable->FindRow<FWeaponTableRow>(RowName, TEXT("WeaponDefinitionMigration"));
		TObjectPtr<UShooterWeaponDefinition>* DefinitionPtr = MigratedDefinitions.Find(RowName);
		if (!Row || !DefinitionPtr || !*DefinitionPtr)
		{
			AddError(FString::Printf(TEXT("Weapon row %s has no matching definition"), *RowName.ToString()));
			bAllRowsMigrated = false;
			continue;
		}

		UClass* ExpectedWeaponClass = (*DefinitionPtr)->WeaponActorClass;
		if (!ExpectedWeaponClass)
		{
			AddError(FString::Printf(
				TEXT("Definition %s has no WeaponActorClass"),
				*RowName.ToString()));
			bAllRowsMigrated = false;
			continue;
		}

		Row->WeaponDefinition = TSoftObjectPtr<UShooterWeaponDefinition>(*DefinitionPtr);
		(*DefinitionPtr)->PresentationConfig.PickupPreviewMesh = Row->StaticMesh;
	}

	if (!bAllRowsMigrated)
	{
		return false;
	}

	// 网格归档写回 Definition 后再保存一次定义资产。
	for (auto& Pair : MigratedDefinitions)
	{
		Pair.Value->MarkPackageDirty();
		if (!SaveAssetPackage(*this, Pair.Value->GetOutermost(), Pair.Value))
		{
			return false;
		}
	}

	WeaponTable->MarkPackageDirty();
	return SaveAssetPackage(*this, WeaponTable->GetOutermost(), WeaponTable);
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
