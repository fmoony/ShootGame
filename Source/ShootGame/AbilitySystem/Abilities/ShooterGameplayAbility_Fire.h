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
 * LocalPredicted：拥有者客户端在本地开火资格通过后立即播放第一人称表现，并把这次激活
 * 按 PredictionKey 请求服务器；服务器独立完成权威射速与 Gameplay 校验，并独占
 * Shot / Ammo / Projectile / Hit / Damage 结果。
 *
 * 拥有者第一人称表现的唯一正常来源就是本地预测：
 * 本地未通过节拍检查时既不表现也不发请求；服务器 Reject 只做状态收敛，不回滚已播出的瞬时表现，
 * 也不再为 Server Confirm 补播第二次表现。
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

	/** 输入缓冲上下文：按下时对应的当前武器；换枪提交后旧输入不得在新武器上生效。 */
	virtual const UObject* GetInputBufferContext() const override;

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

	/** 拥有者本地表现：全自动按"剩余 cooldown"安排首拍，并按 bFullAuto 决定是否启动本地表现节拍。 */
	void StartOwnerPredictedFeedback(AShooterWeapon& Weapon);

	/** 提交一次拥有者纯表现；只有实际播放成功才登记。 */
	bool TryPlayOwnerFeedback(AShooterWeapon& Weapon, const TCHAR* Marker);

	/** 幂等停止本地表现节拍与标志；Reject、释放、取消共用。 */
	void StopOwnerPredictedFeedback();

	/** 本地表现节拍回调：每次只播纯表现，并在播放前复核武器仍是当前装备。 */
	void HandlePredictedFeedbackTick();

	/** 本地表现是否仍可用：武器有效、未隐藏，且仍是当前装备。 */
	bool IsCurrentWeaponStillValidForFeedback(AActor* AvatarActor) const;

	/**
	 * 本机是否允许播放预测表现。
	 *
	 * 唯一判据是"表现目标是否成立"：武器有效 / 未隐藏 / 仍是当前装备。
	 * 刻意**不读取** Ammo / Reloading / Equipping / Dead 等可能过期的复制状态：
	 * 用它们二次否决首次 Owner 表现，会让服务器已经接受并生成弹丸的那一发永久没有反馈。
	 * 反之，客户端以为有弹而服务器实际无弹时允许播一次 cosmetic，由服务器 Reject 收敛。
	 */
	bool IsOwnerPredictedFeedbackAllowed();

	/**
	 * 使用显式上下文执行同一生产判定，供开发测试构造表现目标状态。
	 *
	 * AbilitySystemComponent 参数刻意保留且刻意不读：测试用它证明「即使这些复制状态存在，
	 * 也不参与 Owner 表现门控」。任何把它接回判定的改动都会让该测试失败。
	 */
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

	/** 输入当前是否仍处于按住状态；读引擎 Spec 状态，不维护第二份布尔。 */
	bool IsInputHeld(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo) const;

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

	/** 测试观察接口：Reject / EndAbility 后不得继续持有武器。 */
	bool HasCachedWeaponForTest() const { return CachedWeapon.IsValid(); }

	/** 测试观察接口：本实例在权威端明确拒绝的激活次数。 */
	int32 GetAuthorityRejectCountForTest() const { return AuthorityRejectCountForTest; }

	/** 测试观察接口：用显式本地上下文验证生产表现门控。 */
	bool IsOwnerPredictedFeedbackAllowedForTest(AActor* AvatarActor, const AShooterWeapon* Weapon,
		const UAbilitySystemComponent* AbilitySystemComponent)
	{
		return IsOwnerPredictedFeedbackAllowedForContext(AvatarActor, Weapon, AbilitySystemComponent);
	}

private:
	mutable int32 AuthorityRejectCountForTest = 0;
#endif
};
