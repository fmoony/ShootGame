// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Weapons/Firing/ShooterWeaponFireBehavior.h"
#include "ShooterProjectileFireBehavior.generated.h"

/**
 * 第一版唯一正式开火行为：在服务器上生成弹丸。
 * 行为本身无状态：弹丸类来自本次开火的武器模板行快照，不保存在行为实例上。
 */
UCLASS(EditInlineNew, DefaultToInstanced)
class SHOOTGAME_API UShooterProjectileFireBehavior : public UShooterWeaponFireBehavior
{
	GENERATED_BODY()

public:
	virtual void ExecuteFire(const FShooterWeaponFireContext& Context) override;
};
