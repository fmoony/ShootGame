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
