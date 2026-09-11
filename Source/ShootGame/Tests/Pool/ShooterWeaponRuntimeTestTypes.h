// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Weapons/ShooterWeapon.h"
#include "ShooterWeaponRuntimeTestTypes.generated.h"

/** WeaponRuntimeSubsystem 测试专用武器：无额外配置，行数据完全来自测试注入表。 */
UCLASS(NotBlueprintable, Transient)
class AShooterRuntimePoolTestWeapon : public AShooterWeapon
{
	GENERATED_BODY()
};
