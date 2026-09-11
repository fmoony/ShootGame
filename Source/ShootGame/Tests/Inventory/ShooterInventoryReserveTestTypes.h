// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Weapons/ShooterWeapon.h"
#include "ShooterInventoryReserveTestTypes.generated.h"

/** 备弹声明测试武器：保持 InitialReserveAmmo = -1 自动默认，验证 MagazineSize×3 兼容基线。 */
UCLASS(NotBlueprintable, Transient)
class AShooterInventoryAutoReserveTestWeapon : public AShooterWeapon
{
	GENERATED_BODY()

public:
	AShooterInventoryAutoReserveTestWeapon()
	{
		MagazineSize = 4;
	}
};

/** 备弹声明测试武器：显式声明有限备弹值。 */
UCLASS(NotBlueprintable, Transient)
class AShooterInventoryDeclaredReserveTestWeapon : public AShooterWeapon
{
	GENERATED_BODY()

public:
	AShooterInventoryDeclaredReserveTestWeapon()
	{
		MagazineSize = 4;
		InitialReserveAmmo = 7;
	}
};

/** 备弹声明测试武器：显式声明零备弹（纯弹匣经济）。 */
UCLASS(NotBlueprintable, Transient)
class AShooterInventoryZeroReserveTestWeapon : public AShooterWeapon
{
	GENERATED_BODY()

public:
	AShooterInventoryZeroReserveTestWeapon()
	{
		MagazineSize = 4;
		InitialReserveAmmo = 0;
	}
};

/**
 * 换弹事务测试武器（S2 起事务收敛在 WeaponActor::ReloadFromReserve）：
 * 暴露受保护的弹药与容量写入口，构造任意弹匣/备弹状态。
 */
UCLASS(NotBlueprintable, Transient)
class AShooterInventoryReloadTestWeapon : public AShooterWeapon
{
	GENERATED_BODY()

public:
	void SetMagazineSizeForTest(int32 InMagazineSize)
	{
		MagazineSize = InMagazineSize;
	}

	void SetAmmoForTest(int32 InMagazineAmmo, int32 InReserveAmmo)
	{
		MagazineAmmo = InMagazineAmmo;
		ReserveAmmo = InReserveAmmo;
	}
};
