// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Characters/ShooterCharacter.h"
#include "Weapons/ShooterWeapon.h"
#include "ShooterArchitectureTestTypes.generated.h"

/**
 * 架构基线测试用玩家角色：只用于 Editor Automation 中创建真实 UWorld 场景，
 * 不进入游戏逻辑，也不注册为可放置 Actor。
 */
UCLASS(NotBlueprintable, Transient)
class AShooterArchitectureTestCharacter : public AShooterCharacter
{
	GENERATED_BODY()

public:
	AShooterArchitectureTestCharacter() = default;
};

/**
 * 架构基线测试用武器：基类 AShooterWeapon 为 abstract，
 * 测试需要可 Spawn 的最小具体子类；不配置弹丸、音效或动画。
 */
UCLASS(NotBlueprintable, Transient)
class AShooterArchitectureTestWeapon : public AShooterWeapon
{
	GENERATED_BODY()

public:
	AShooterArchitectureTestWeapon()
	{
		MagazineSize = 10;
	}
};
