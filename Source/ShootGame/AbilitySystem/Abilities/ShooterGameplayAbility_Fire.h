// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/ShooterGameplayAbility.h"
#include "ShooterGameplayAbility_Fire.generated.h"

class AShooterWeapon;
class UAbilitySystemComponent;

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
	virtual void ConfirmActivateSucceed() override;
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	/** 权威端：停武器；调用方负责随后结束 Ability。 */
	void StopAuthorityWeapon();

	/** 拥有者本地表现：播一次，并按 bFullAuto 决定是否启动本地表现节拍。 */
	void StartOwnerPredictedFeedback(AShooterWeapon& Weapon);

	/** 提交一次拥有者纯表现；确认回退可旁路纯表现冷却，只有实际播放成功才登记。 */
	bool TryPlayOwnerFeedback(AShooterWeapon& Weapon, const TCHAR* Marker, bool bConfirmedFallback = false);

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
	bool IsOwnerPredictedFeedbackAllowed();

	/** 使用显式上下文执行同一生产门控，供开发测试构造本机已知状态。 */
	bool IsOwnerPredictedFeedbackAllowedForContext(AActor* AvatarActor, const AShooterWeapon* Weapon,
		const UAbilitySystemComponent* AbilitySystemComponent);

	/** P1 统一预测日志标记；仅开发构建输出。 */
	void LogFirePredictionMarker(const TCHAR* Marker, const AShooterWeapon* Weapon, int32 ShotOrdinal) const;

	/** 权威 CanActivate 拒绝的统一测试观测点；返回值恒为 false。 */
	bool RecordAuthorityRejectAndReturnFalse() const;

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

	/** 当前激活是否至少成功提交过一次拥有者本地反馈。 */
	bool bOwnerFeedbackPlayedThisActivation = false;

	/** 服务器确认回退后，迟到阻塞 Tag 首次清除前的短暂表现宽限。 */
	bool bConfirmedBlockerGraceActive = false;

	/**
	 * 半自动在初始预测被本机已知状态挡下后，可能先收到输入释放、后收到服务器确认。
	 * 此时只延后本地 Ability 收口，等待 Confirm / Reject 决定是否补播；不影响服务器输入释放。
	 */
	bool bOwnerReleaseAwaitingConfirmation = false;

#if WITH_DEV_AUTOMATION_TESTS
public:
	/** 测试观察接口：本地表现节拍是否活动。 */
	bool IsPredictedFeedbackActiveForTest() const { return bPredictedFeedbackActive; }

	/** 测试观察接口：本次激活已提交的本地反馈次数。 */
	int32 GetPredictedShotOrdinalForTest() const { return PredictedShotOrdinal; }

	/** 测试观察接口：Reject / EndAbility 后不得继续持有武器。 */
	bool HasCachedWeaponForTest() const { return CachedWeapon.IsValid(); }

	/** 测试观察接口：本实例在权威端明确拒绝的激活次数。 */
	int32 GetAuthorityRejectCountForTest() const { return AuthorityRejectCountForTest; }

	/** 测试观察接口：用显式本地上下文验证生产预测表现门控。 */
	bool IsOwnerPredictedFeedbackAllowedForTest(AActor* AvatarActor, const AShooterWeapon* Weapon,
		const UAbilitySystemComponent* AbilitySystemComponent)
	{
		return IsOwnerPredictedFeedbackAllowedForContext(AvatarActor, Weapon, AbilitySystemComponent);
	}

	/** 测试构造接口：模拟服务器确认已补播后、迟到阻塞 Tag 尚未清除的短暂宽限。 */
	void SetConfirmedBlockerGraceForTest(bool bEnabled) { bConfirmedBlockerGraceActive = bEnabled; }

private:
	mutable int32 AuthorityRejectCountForTest = 0;
#endif
};
