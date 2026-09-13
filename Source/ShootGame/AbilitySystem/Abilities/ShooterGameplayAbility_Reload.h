// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/ShooterGameplayAbility.h"
#include "ShooterGameplayAbility_Reload.generated.h"

class AShooterCharacter;
class AShooterWeapon;
class UAbilityTask_WaitDelay;

/**
 * 服务器权威换弹事务 Ability。
 *
 * 5B 闭环：激活时取消 Fire、缓存目标 WeaponActor，
 * 以 WeaponActor.ReloadDuration 为服务器时钟等待，提交前二次确认目标仍是当前装备，
 * 最后调用 WeaponActor.ReloadFromReserve 完成唯一一次原子转移。
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
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	/** 服务器完整校验与目标解析：目标必须是 Equipment 当前装备、可见且弹药需要转移的 WeaponActor。 */
	bool ResolveReloadTarget(const FGameplayAbilityActorInfo* ActorInfo, AShooterWeapon*& OutWeapon) const;

	/** 提交前二次校验：目标 Actor 仍在背包、仍是当前装备且归属未变。 */
	bool IsReloadTargetStillCurrent() const;

	/** WaitDelay 到期：原子提交或失败结束；bReloadCommitted 保证一次 Ability 最多提交一次。 */
	UFUNCTION()
	void HandleReloadWaitFinished();

	/** 结束前清理 WaitDelay、Weapon 引用与事务快照。 */
	void CleanupReloadTransaction();

	/** 本次事务对应的当前 WeaponActor；EndAbility 只清理仍属于自己的引用。 */
	TWeakObjectPtr<AShooterWeapon> CachedWeapon;

	/** 服务器权威时钟任务；等待期间被取消时由 EndAbility 清理。 */
	TWeakObjectPtr<UAbilityTask_WaitDelay> ReloadWaitTask;

	/** 是否已经提交 ReloadFromReserve；重复回调不得再次提交。 */
	bool bReloadCommitted = false;
};
