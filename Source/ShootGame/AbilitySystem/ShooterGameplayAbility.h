// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "ShooterGameplayAbility.generated.h"

/**
 * ShootGame 项目通用 Ability 最小基类。
 * 只在出现第二个真实共享需求时扩展；不向基类塞入武器、Inventory、UI 或 Projectile 逻辑。
 */
UCLASS(Abstract)
class SHOOTGAME_API UShooterGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	/** 安全取得当前 Avatar Actor；AbilityActorInfo 尚未初始化时为 nullptr。 */
	AActor* GetShooterAvatarActor() const;

	/** 判断当前执行端是否拥有权威 Avatar；客户端预检可据此把完整校验留给服务器。 */
	bool IsAvatarAuthoritative() const;
};
