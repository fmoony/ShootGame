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
 * 阅读顺序（Owner 与 Authority 两条控制流分开，不互相交错）：
 * 1. 生命周期入口：CanActivateAbility → ActivateAbility → InputReleased → EndAbility；
 * 2. Owner Prediction Path：StartOwnerFirePath → TryOwnerPredictedShot，
 *    全自动另有 StartOwnerFireLoop / HandleOwnerFireTick / StopOwnerFireLoop；
 * 3. Authority Gameplay Path：StartAuthorityFirePath / StopAuthorityFirePath / HandleWeaponOutOfAmmo；
 *    权威每一发仍在 AShooterWeapon 内执行（StartFiring → Fire → ExecuteFireAtTarget → 弹丸与表现），
 *    本 Ability 只负责启动、停止与弹药耗尽收口，不复制第二套权威射击逻辑；
 * 4. 验证层：CanLocallyStartFire（预测资格）与 CanAuthorityStartFire（权威资格），
 *    共享谓词 IsAuthorityFireContextValid / IsAuthorityCadenceReady / IsOwnerFireContextValid。
 *
 * Listen Host 与 Standalone 同时满足 Owner Local 与 Authority，两条路径都有意执行。
 * 拥有者第一人称表现的唯一正常来源是本地预测；服务器 Reject 只做状态收敛，
 * 不回滚已播出的瞬时表现，也不再为 Server Confirm 补播第二次表现。
 */
UCLASS(NotBlueprintable)
class SHOOTGAME_API UShooterGameplayAbility_Fire : public UShooterGameplayAbility
{
	GENERATED_BODY()

public:
	UShooterGameplayAbility_Fire();

	/**
	 * 输入激活策略：当前武器连发时按住持续（WhileInputActive），单发时单次按下沿。
	 * 动态 Gameplay Context 查询，不依赖任何由外部同步的派生状态。
	 */
	virtual EShooterAbilityActivationPolicy GetActivationPolicy(const FGameplayAbilityActorInfo* ActorInfo) const override;

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
	// ---- 验证层：两条 activation 资格 + 三个共享谓词，getter 数量刻意保持少 ----

	/**
	 * 预测客户端资格：只读本地确定性条件（不读可能过期的复制状态），最后交给 Super 做 Tag 门控。
	 *
	 * 判定顺序：ASC 与 Avatar 一致 → 本机拥有者视图 → 当前武器有效且未隐藏 →
	 * 半自动本地节拍 Ready / 全自动仍按住 → Super 的 ActivationBlockedTags 门控。
	 */
	bool CanLocallyStartFire(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const AActor* AvatarActor,
		const FGameplayTagContainer* SourceTags,
		const FGameplayTagContainer* TargetTags,
		FGameplayTagContainer* OptionalRelevantTags) const;

	/** 权威端资格：Super 的 Tag 门控 + 完整权威校验；任何失败都登记权威拒绝计数。 */
	bool CanAuthorityStartFire(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const AActor* AvatarActor,
		const FGameplayTagContainer* SourceTags,
		const FGameplayTagContainer* TargetTags,
		FGameplayTagContainer* OptionalRelevantTags) const;

	/**
	 * 权威校验的共享核心：Avatar 有效且未死亡、当前 WeaponActor 有效且属于该 Avatar、可见、有可消耗弹药。
	 * 起手资格与 Activate 防御复核共用本函数，两侧各自追加自己的额外条件。
	 */
	bool IsAuthorityFireContextValid(const AActor* AvatarActor, const UAbilitySystemComponent* AbilitySystemComponent,
		const AShooterWeapon* Weapon) const;

	/** 权威节拍资格：全自动允许冷却期激活（沿用剩余冷却继续权威射击）；半自动必须越过权威 RefireTimer。 */
	static bool IsAuthorityCadenceReady(const AShooterWeapon& Weapon);

	/** 本地表现上下文是否成立：武器有效、未隐藏、仍是当前装备。 */
	bool IsOwnerFireContextValid();

	/** 显式上下文版本：AbilitySystemComponent 参数刻意不参与判定，测试依赖「这些状态存在也不门控」这一事实。 */
	bool IsOwnerFireContextValidForContext(AActor* AvatarActor, const AShooterWeapon* Weapon,
		const UAbilitySystemComponent* AbilitySystemComponent);

	// ---- Owner Prediction Path ----

	/** 本地预测路径入口：一次首拍，并按 Semi / FullAuto 决定是否建立连续预测循环。 */
	void StartOwnerFirePath(AShooterWeapon& Weapon);

	/** 建立全自动本地预测循环；Semi 不建立，下一发由下一次输入驱动。 */
	void StartOwnerFireLoop(AShooterWeapon& Weapon);

	/** 本地预测循环每一拍：上下文失效则结束 Ability，否则再走一次本地 Shot Attempt。 */
	void HandleOwnerFireTick();

	/** 停止本地预测循环；幂等，由 EndAbility 统一调用。 */
	void StopOwnerFireLoop();

	/**
	 * 一次本地预测 Shot Attempt：上下文 → 预测弹药预算 → 本地节拍 → Owner 表现。
	 *
	 * 上下文失效或预算不足时本次不成立，但不影响激活、服务器请求与 Ability 生命周期。
	 */
	bool TryOwnerPredictedShot(AShooterWeapon& Weapon);

	/** 提交一次拥有者纯表现；只有实际播放成功才登记。 */
	bool PlayOwnerShotFeedback(AShooterWeapon& Weapon);

	// ---- Authority Gameplay Path ----

	/** 启动权威开火：绑定弹药耗尽回调并让武器接管权威事务（权威每一发在 Weapon 内执行）。 */
	void StartAuthorityFirePath(AShooterWeapon& Weapon);

	/** 停止权威开火；权威连发的 Timer 停与清理由 Weapon::StopFiring 内部完成。 */
	void StopAuthorityFirePath();

	/** WeaponActor 在 Fire 中确认弹药耗尽时回调；幂等结束 Ability。 */
	void HandleWeaponOutOfAmmo(AShooterWeapon* Weapon);

	// ---- 共用查询与状态 ----

	/** Equipment 优先、IShooterWeaponHolder 作为 NPC 兼容回退的当前武器解析。 */
	AShooterWeapon* GetCurrentWeaponForAvatar(AActor* AvatarActor) const;

	/** 输入当前是否仍处于按住状态；读引擎 Spec 状态，不维护第二份布尔。 */
	bool IsInputHeld(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo) const;

	/** 权威 CanActivate 拒绝的统一测试观测点；返回值恒为 false。 */
	bool RecordAuthorityRejectAndReturnFalse() const;

	/** P1 统一预测日志标记；仅开发构建输出。 */
	void LogFirePredictionMarker(const TCHAR* Marker, const AShooterWeapon* Weapon, int32 ShotOrdinal) const;

	/**
	 * 一次本地预测激活的退款上下文：在 Reject 结果到达前按 PredictionKey 保留。
	 *
	 * 拥有端释放输入会在 Reject 到达前结束实例并清空 CachedWeapon；
	 * 只靠 EndAbility 的 Rejected 分支会丢失原消费的武器与发数。
	 * 本上下文不复制任何权威状态，只记本地预测消费，供迟到 Reject 精确退还。
	 */
	struct FPredictedFireRefundContext
	{
		/** 只弱引用 WeaponActor：池化 / 切枪 / 销毁后不得被退款路径延长生命周期。 */
		TWeakObjectPtr<AShooterWeapon> Weapon;

		/** 本次激活已预测消费的发数。 */
		int32 PredictedShots = 0;

		/** 表示本次激活已经结清（已退还或无需退还），保证两条路径只处理一次。 */
		bool bResolved = false;
	};

	/** 按 PredictionKey 索引的迟到 Reject 退款上下文；只在拥有端本地预测路径写入。 */
	TMap<int32, FPredictedFireRefundContext> PendingRefundContexts;

	/**
	 * 登记本次本地预测激活的退款上下文。
	 * 返回后由调用方在对应 PredictionKey 上绑定 Rejected 委托。
	 */
	void RegisterPredictedActivationRefund(int32 PredictionKey, AShooterWeapon* Weapon);

	/** 记录一次本地预测消费，使迟到 Reject 能退还准确发数。 */
	void RecordPredictedShotForRefund(int32 PredictionKey);

	/** PredictionKey Rejected 委托：不依赖 Ability 实例是否仍然活动。 */
	void HandlePredictedActivationRejected(int32 PredictionKey);

	/** PredictionKey CaughtUp 委托：确认到达后本次激活不再可能被拒，可回收上下文。 */
	void HandlePredictedActivationCaughtUp(int32 PredictionKey);

	/**
	 * Reject 到达时的幂等退还入口。
	 * 上下文存在时按上下文退还；不存在时保留 EndAbility 的原退还语义。
	 */
	void RefundPredictedActivation(int32 PredictionKey, AShooterWeapon* FallbackWeapon, int32 FallbackShots);

	/** 新激活开始时清理已结清与超额上下文，避免拒绝 / 确认通道异常时无限累积。 */
	void PrunePredictedRefundContexts(int32 NewPredictionKey);

	/** 本次拥有端本地预测激活的 PredictionKey；非拥有端保持 0。 */
	int32 EffectivePredictionKey = 0;

	/** 激活时缓存的武器；权威端控武器，拥有端控表现；EndAbility 只清理仍指向自己的武器。 */
	TWeakObjectPtr<AShooterWeapon> CachedWeapon;

	/** 全自动本地表现节拍 Timer；归 Ability，不归 Weapon。 */
	FTimerHandle PredictedFeedbackTimer;

	/** 本次激活内的本地反馈序号；不得假设每发都有新的 PredictionKey。 */
	int32 PredictedShotOrdinal = 0;

	/**
	 * 本次 activation 已预测消费的本地发数。
	 *
	 * 只在 Ammo Prediction 上下文的客户端增长；Rejected 时按它一次性退还 PendingPredictedShots，
	 * 因为被拒的预测发数永远等不到服务器扣弹复制。
	 */
	int32 PredictedShotsThisActivation = 0;

	/** 本地表现节拍是否活动；幂等停止与测试观察共用。 */
	bool bPredictedFeedbackActive = false;

public:
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

#if WITH_DEV_AUTOMATION_TESTS
	/** 测试观察接口：本地表现节拍是否活动。 */
	bool IsPredictedFeedbackActiveForTest() const { return bPredictedFeedbackActive; }

	/** 测试观察接口：本次激活已提交的本地反馈次数。 */
	int32 GetPredictedShotOrdinalForTest() const { return PredictedShotOrdinal; }

	/** 测试观察接口：尚未结清的迟到 Reject 退款上下文数量。 */
	int32 GetPendingRefundContextCountForTest() const { return PendingRefundContexts.Num(); }

	/** 测试观察接口：指定预测身份是否已经结清。 */
	bool IsPredictionRefundResolvedForTest(int32 PredictionKey) const
	{
		const FPredictedFireRefundContext* Context = PendingRefundContexts.Find(PredictionKey);
		return Context != nullptr && Context->bResolved;
	}

	/** 测试观察接口：Reject / EndAbility 后不得继续持有武器。 */
	bool HasCachedWeaponForTest() const { return CachedWeapon.IsValid(); }

	/** 测试观察接口：本实例在权威端明确拒绝的激活次数。 */
	int32 GetAuthorityRejectCountForTest() const { return AuthorityRejectCountForTest; }

	/** 测试观察接口：用显式本地上下文验证生产表现门控。 */
	bool IsOwnerFireContextValidForTest(AActor* AvatarActor, const AShooterWeapon* Weapon,
		const UAbilitySystemComponent* AbilitySystemComponent)
	{
		return IsOwnerFireContextValidForContext(AvatarActor, Weapon, AbilitySystemComponent);
	}

private:
	mutable int32 AuthorityRejectCountForTest = 0;
#endif
};
