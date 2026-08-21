// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterGameplayAbility.h"
#include "ShooterGameplayAbility_Equip.generated.h"

class AShooterCharacter;
class AShooterWeapon;
class UAbilityTask_WaitDelay;

/**
 * 服务器权威装备事务 Ability。
 *
 * 5C 闭环：激活时按 Slot 顺序计算下一个合法 InstanceId，取消 Fire / Reload，
 * 以 WeaponActor.EquipDuration 为服务器时钟等待，提交前二次确认目标仍存在，
 * 最后通过 Character.CommitActiveWeapon 原子提交 ActiveWeaponInstanceId 与 CurrentWeapon。
 */
UCLASS(NotBlueprintable)
class SHOOTGAME_API UShooterGameplayAbility_Equip : public UShooterGameplayAbility
{
	GENERATED_BODY()

public:
	UShooterGameplayAbility_Equip();

	/** 测试观察接口：Ability 的资产标签是否包含 Input.Equip.Next。 */
	bool HasInputEquipNextTag() const;

	/** 测试观察接口：State.Dead 是否阻塞本 Ability 激活。 */
	bool IsBlockedByStateDead() const;

	/** 测试观察接口：State.Equipping 是否阻塞重复激活。 */
	bool IsBlockedByStateEquipping() const;

	/** 测试观察接口：Ability 活动期间是否向拥有者挂载 State.Equipping。 */
	bool OwnsStateEquippingWhileActive() const;

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
	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	/** 服务器完整校验：至少两把武器、当前实例有效且能按 Slot 顺序找到下一个目标。 */
	bool ResolveEquipTarget(
		const FGameplayAbilityActorInfo* ActorInfo,
		AShooterWeapon*& OutWeapon,
		FGuid& OutInstanceId) const;

	/** 提交前二次校验：目标实例与 WeaponActor 仍有效，且没有其他事务改写当前武器。 */
	bool IsEquipTargetStillValid() const;

	/** WaitDelay 到期：原子提交 ActiveWeaponInstanceId 与 CurrentWeapon。 */
	UFUNCTION()
	void HandleEquipWaitFinished();

	/** 结束前清理 WaitDelay、Weapon 引用与事务快照。 */
	void CleanupEquipTransaction();

	/** 本次事务的目标实例快照。 */
	FGuid TargetInstanceId;

	/** 激活前的 Active Instance 快照；提交前确认没有第三方切换。 */
	FGuid PreviousInstanceId;

	/** 激活前的 CurrentWeapon；EndAbility 只清理仍属于自己的引用。 */
	TWeakObjectPtr<AShooterWeapon> CachedPreviousWeapon;

	/** 等待提交的目标 WeaponActor。 */
	TWeakObjectPtr<AShooterWeapon> CachedTargetWeapon;

	/** 服务器权威时钟任务；等待期间被取消时由 EndAbility 清理。 */
	TWeakObjectPtr<UAbilityTask_WaitDelay> EquipWaitTask;

	/** 是否已经提交 CurrentWeapon；重复回调不得再次提交。 */
	bool bEquipCommitted = false;
};
