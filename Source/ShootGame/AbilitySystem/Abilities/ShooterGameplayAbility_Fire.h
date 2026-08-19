// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterGameplayAbility.h"
#include "ShooterGameplayAbility_Fire.generated.h"

class AShooterCharacter;
class AShooterNPC;
class AShooterWeapon;

/**
 * 开火事务 Ability：玩家与 NPC 发起开火的唯一 Gameplay 入口。
 * ServerOnly：输入只提交意图，武器、弹药、弹丸和伤害结果全部在服务器执行。
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
	virtual void InputReleased(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;
	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	/** 停止当前 Weapon 并幂等结束 Ability。 */
	void StopWeaponAndEndAbility();

	/** WeaponActor 在 Fire 中确认弹药耗尽时回调。 */
	void HandleWeaponOutOfAmmo(AShooterWeapon* Weapon);

	AShooterCharacter* GetShooterCharacter() const;
	AShooterNPC* GetShooterNPC() const;
	AShooterWeapon* GetCurrentWeaponForAvatar(AActor* AvatarActor) const;

	/** 激活时缓存的武器；EndAbility 只清理仍指向自己的武器。 */
	TWeakObjectPtr<AShooterWeapon> CachedWeapon;
};
