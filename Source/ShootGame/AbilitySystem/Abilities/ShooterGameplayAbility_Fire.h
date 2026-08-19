// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterGameplayAbility.h"
#include "ShooterGameplayAbility_Fire.generated.h"

class AShooterCharacter;
class AShooterNPC;
class AShooterWeapon;

/**
 * 开火事务 Ability 壳体：4A 只建立输入标签、网络执行策略与授予入口。
 * 玩家与 NPC 的真实 StartFiring / StopFiring 调用在 4B / 4C 接入。
 */
UCLASS(NotBlueprintable)
class SHOOTGAME_API UShooterGameplayAbility_Fire : public UShooterGameplayAbility
{
	GENERATED_BODY()

public:
	UShooterGameplayAbility_Fire();

	/** 测试观察接口：Ability 的资产标签是否包含 Input.Fire。 */
	bool HasInputFireTag() const;

	/** 测试观察接口：State.Dead 是否阻塞本 Ability 激活。 */
	bool IsBlockedByStateDead() const;

	/** 测试观察接口：Ability 活动期间是否向拥有者挂载 State.Firing。 */
	bool OwnsStateFiringWhileActive() const;

protected:
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags,
		const FGameplayTagContainer* TargetTags,
		FGameplayTagContainer* OptionalRelevantTags) const override;
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

private:
	AShooterCharacter* GetShooterCharacter() const;
	AShooterNPC* GetShooterNPC() const;
};
