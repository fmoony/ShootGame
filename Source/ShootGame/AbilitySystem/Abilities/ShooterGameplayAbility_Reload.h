// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterGameplayAbility.h"
#include "ShooterGameplayAbility_Reload.generated.h"

/**
 * 换弹事务 Ability 壳体：5A 只建立 Input.Reload 标签、ServerOnly 执行策略、
 * 状态互斥合同与授予入口；真实 Inventory 原子提交在 5B 接入。
 */
UCLASS(NotBlueprintable)
class SHOOTGAME_API UShooterGameplayAbility_Reload : public UShooterGameplayAbility
{
	GENERATED_BODY()

public:
	UShooterGameplayAbility_Reload();

	/** 测试观察接口：Ability 的资产标签是否包含 Input.Reload。 */
	bool HasInputReloadTag() const;

	/** 测试观察接口：State.Dead 是否阻塞本 Ability 激活。 */
	bool IsBlockedByStateDead() const;

	/** 测试观察接口：State.Reloading 是否阻塞重复激活。 */
	bool IsBlockedByStateReloading() const;

	/** 测试观察接口：State.Equipping 是否阻塞本 Ability 激活。 */
	bool IsBlockedByStateEquipping() const;

	/** 测试观察接口：Ability 活动期间是否向拥有者挂载 State.Reloading。 */
	bool OwnsStateReloadingWhileActive() const;

	/** 测试观察接口：活动期间重复激活是否会重触发实例（ServerOnly 单事务应为 false）。 */
	bool CanRetriggerInstancedAbility() const;

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
};
