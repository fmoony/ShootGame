// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterWeaponFireBehavior.h"
#include "ShooterProjectileFireBehavior.generated.h"

class AShooterProjectile;

/**
 * 第一版唯一正式开火行为：在服务器上生成弹丸。
 * 弹丸类配置在本行为上，不再读取 WeaponActor CDO。
 */
UCLASS(EditInlineNew, DefaultToInstanced)
class SHOOTGAME_API UShooterProjectileFireBehavior : public UShooterWeaponFireBehavior
{
	GENERATED_BODY()

public:
	virtual void ExecuteFire(const FShooterWeaponFireContext& Context) override;

	/** 该行为生成的弹丸类。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectile")
	TSubclassOf<AShooterProjectile> ProjectileClass;
};
