// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterWeaponDefinition.h"

#include "Engine/AssetManager.h"
#include "ShootGame.h"
#include "Weapons/ShooterWeapon.h"

const FPrimaryAssetType& UShooterWeaponDefinition::GetWeaponDefinitionAssetType()
{
	// 固定类型名：与 DefaultGame.ini 的 AssetManager 扫描规则保持一致，
	// 不使用 GetClass()->GetFName()，避免未来子类把类型漂移到别处。
	static FPrimaryAssetType WeaponDefinitionType(TEXT("ShooterWeaponDefinition"));
	return WeaponDefinitionType;
}

FPrimaryAssetId UShooterWeaponDefinition::GetPrimaryAssetId() const
{
	// CDO 与其它类型级对象没有资产身份。
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return FPrimaryAssetId();
	}

	// 本地 UE 5.6 实现核对（DataAsset.cpp）：实例化 DataAsset 默认返回 (类名, 资产名)；
	// 这里显式收敛到固定类型，保证 DefinitionId 不依赖任何蓝图类名。
	return FPrimaryAssetId(GetWeaponDefinitionAssetType(), GetFName());
}

bool UShooterWeaponDefinition::IsValidForGrant() const
{
	if (!GetPrimaryAssetId().IsValid())
	{
		return false;
	}

	if (!WeaponActorClass || !WeaponActorClass->IsChildOf<AShooterWeapon>())
	{
		return false;
	}

	// 非法弹匣容量必须 fail closed，不能回落到 Weapon CDO 或默认值。
	if (AmmoConfig.MagazineSize <= 0)
	{
		return false;
	}

	if (AmmoConfig.InitialReserveAmmo < -1)
	{
		return false;
	}

	return true;
}

UShooterWeaponDefinition* UShooterWeaponDefinition::ResolveDefinitionSync(const FPrimaryAssetId& DefinitionId)
{
	if (!DefinitionId.IsValid() ||
		DefinitionId.PrimaryAssetType != GetWeaponDefinitionAssetType())
	{
		return nullptr;
	}

	// 第一版同步解析：未注册（含 Cook 配置缺失）或加载失败统一返回 nullptr。
	const FSoftObjectPath AssetPath = UAssetManager::Get().GetPrimaryAssetPath(DefinitionId);
	if (!AssetPath.IsValid())
	{
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("WeaponDefinition resolve failed: %s is not registered with the Asset Manager"),
			*DefinitionId.ToString());
		return nullptr;
	}

	UObject* LoadedObject = AssetPath.TryLoad();
	UShooterWeaponDefinition* Definition = Cast<UShooterWeaponDefinition>(LoadedObject);
	if (!Definition)
	{
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("WeaponDefinition resolve failed: %s loaded non-definition object %s"),
			*DefinitionId.ToString(),
			*GetNameSafe(LoadedObject));
		return nullptr;
	}

	return Definition;
}
