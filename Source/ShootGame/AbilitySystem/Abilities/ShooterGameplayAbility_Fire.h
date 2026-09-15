// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/ShooterGameplayAbility.h"
#include "ShooterGameplayAbility_Fire.generated.h"

class AShooterWeapon;

/**
 * 开火事务 Ability：玩家与 NPC 发起开火的唯一 Gameplay 入口。
 *
 * LocalPredicted：拥有者客户端立即播放第一人称表现，
 * 同时把同一次激活按 PredictionKey 请求服务器；服务器仍独占武器、弹药、弹丸与伤害结果。
 * 本地路径只提交纯表现，不写任何权威字段，也不建立预测 Gameplay 状态。
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

	/** 测试观察接口：State.Reloading 是否阻塞本 Ability 激活。 */
	bool IsBlockedByStateReloading() const;

	/** 测试观察接口：State.Equipping 是否阻塞本 Ability 激活。 */
	bool IsBlockedByStateEquipping() const;

	/** 测试观察接口：Ability 活动期间是否向拥有者挂载 State.Firing。 */
	bool OwnsStateFiringWhileActive() const;

	/** 测试观察接口：活动期间重复激活是否会重触发实例（单事务应为 false）。 */
	bool CanRetriggerInstancedAbility() const;

	/** 测试观察接口：是否接受客户端发来的结束命令（必须为 false，权威保留在服务器）。 */
	bool ServerRespectsRemoteAbilityCancellation() const;

protected:
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags,
		const FGameplayTagContainer* TargetTags,
		FGameplayTagContainer* OptionalRelevantTags) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	/** 权威端：停武器；调用方负责随后结束 Ability。 */
	void StopAuthorityWeapon();

	/** 拥有者本地表现：播一次，并按 bFullAuto 决定是否启动本地表现节拍。 */
	void StartOwnerPredictedFeedback(AShooterWeapon& Weapon);

	/** 幂等停止本地表现节拍与标志；Reject、释放、取消共用。 */
	void StopOwnerPredictedFeedback();

	/** 本地表现节拍回调：每次只播纯表现，并在播放前复核武器仍是当前装备。 */
	void HandlePredictedFeedbackTick();

	/** 本地表现是否仍可用：武器有效、未隐藏，且仍是当前装备。 */
	bool IsCurrentWeaponStillValidForFeedback(AActor* AvatarActor) const;

	/**
	 * 本机是否允许播放预测表现。
	 * 在武器可用基础上再排除本机已知的阻塞态：State.Reloading / State.Equipping / State.Dead。
	 * 该判定只影响本地预测表现，不影响激活请求：请求照常发给服务器，由服务器完整校验并 Reject。
	 */
	bool IsOwnerPredictedFeedbackAllowed() const;

	/** P1 统一预测日志标记；仅开发构建输出。 */
	void LogFirePredictionMarker(const TCHAR* Marker, const AShooterWeapon* Weapon, int32 ShotOrdinal) const;

	/** WeaponActor 在 Fire 中确认弹药耗尽时回调。 */
	void HandleWeaponOutOfAmmo(AShooterWeapon* Weapon);

	/** Equipment 优先、IShooterWeaponHolder 作为 NPC 兼容回退的当前武器解析。 */
	AShooterWeapon* GetCurrentWeaponForAvatar(AActor* AvatarActor) const;

	/** 激活时缓存的武器；权威端控武器，拥有端控表现；EndAbility 只清理仍指向自己的武器。 */
	TWeakObjectPtr<AShooterWeapon> CachedWeapon;

	/** 全自动本地表现节拍 Timer；归 Ability，不归 Weapon。 */
	FTimerHandle PredictedFeedbackTimer;

	/** 本次激活内的本地反馈序号；不得假设每发都有新的 PredictionKey。 */
	int32 PredictedShotOrdinal = 0;

	/** 本地表现节拍是否活动；幂等停止与测试观察共用。 */
	bool bPredictedFeedbackActive = false;

#if WITH_DEV_AUTOMATION_TESTS
public:
	/** 测试观察接口：本地表现节拍是否活动。 */
	bool IsPredictedFeedbackActiveForTest() const { return bPredictedFeedbackActive; }

	/** 测试观察接口：本次激活已提交的本地反馈次数。 */
	int32 GetPredictedShotOrdinalForTest() const { return PredictedShotOrdinal; }
#endif
};
