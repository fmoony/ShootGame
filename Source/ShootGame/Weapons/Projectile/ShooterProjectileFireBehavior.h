// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Weapons/Firing/ShooterWeaponFireBehavior.h"
#include "ShooterProjectileFireBehavior.generated.h"

/**
 * 第一版唯一正式开火行为：在服务器上生成弹丸。
 * 行为本身无状态：弹丸类只来自本次开火的上下文（Context.ProjectileClass），不保存在行为实例上。
 * 当前处于休眠状态：WeaponActor 不再实例化行为，直接生成自己的 ProjectileClass；
 * 本定义保留，供后续重新接入开火结果边界时使用。
 */
UCLASS(EditInlineNew, DefaultToInstanced)
class SHOOTGAME_API UShooterProjectileFireBehavior : public UShooterWeaponFireBehavior
{
	GENERATED_BODY()

public:
	virtual void ExecuteFire(const FShooterWeaponFireContext& Context) override;
};
