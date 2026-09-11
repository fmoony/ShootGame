// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ShooterPoolableActor.generated.h"

/**
 * 通用池化 Actor 契约（实施计划 4.4）。
 *
 * 池负责通用的隐藏/碰撞/Tick/Owner/变换处理；
 * 实现者只通过两个回调处理领域内的绑定与清理（B2 起 WeaponActor 接入）。
 */
UINTERFACE(MinimalAPI, BlueprintType)
class UShooterPoolableActor : public UInterface
{
	GENERATED_BODY()
};

class SHOOTGAME_API IShooterPoolableActor
{
	GENERATED_BODY()

public:
	/** 从池中取出并完成通用复位（变换/Owner/可见性/碰撞/Tick）后调用。 */
	virtual void OnAcquiredFromPool() = 0;

	/** 归还池中、通用隐藏与解绑开始前调用；实现必须幂等。 */
	virtual void OnReleasedToPool() = 0;
};
