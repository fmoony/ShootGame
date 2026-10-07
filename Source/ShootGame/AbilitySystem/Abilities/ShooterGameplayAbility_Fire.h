// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/ShooterGameplayAbility.h"
#include "ShooterGameplayAbility_Fire.generated.h"

class AShooterWeapon;
class UAbilitySystemComponent;

/**
 * 开火动作 Ability：玩家与 NPC 发起开火的唯一 Gameplay 入口。
 *
 * 核心语义：一次 Activation 代表且只代表一次 Shot。
 *
 * - SemiAuto：一次按下沿 → 一次 GA_Fire → 一发 Shot → Ability 结束；
 * - FullAuto：Fire 仍 Held 时，输入层按本地开火节拍反复激活新的 GA_Fire，每一发各自
 *   拥有独立的 Activation、PredictionKey、Accept / Reject 与表现生命周期。
 *
 * 本 Ability 绝不在一次 Activation 内产生第二发：服务器在一次 Activation 里只调用一次
 * AShooterWeapon::CommitSingleShot，Ability 随该发结束。连发由"输入仍按住 + 本地节拍就绪"
 * 下的再次激活表达，不在这里用 Timer 表达。
 *
 * 阅读顺序（Owner 与 Authority 两条控制流分开）：
 * 1. 生命周期入口：CanActivateAbility → ActivateAbility → InputReleased → EndAbility；
 * 2. Authority Path：CanAuthorityStartFire 执行完整校验，ActivateAbility 只做单发提交；
 * 3. Owner Prediction Path：拥有端在同一次 Activation 内至多预测一发，并按 PredictionKey
 *    保留一条 Shot 记录，等待这一发的 Committed / Rejected 裁决；
 * 4. 裁决结清：Committed → 结清预测预算、Pending、Record 与 HUD，不补播历史 Owner 表现；
 *    Rejected → 只退还被拒的那一发，不补播也不回滚已播的瞬时表现。
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

	/**
	 * 按 Spec 查询输入激活策略：连发语义来自"这一份 Spec 对应的武器"。
	 * 每把玩家持有的武器各有一份 GA_Fire Spec，策略因此必须按 Spec 解析。
	 */
	virtual EShooterAbilityActivationPolicy GetActivationPolicyForSpec(const FGameplayAbilitySpec& Spec,
		const FGameplayAbilityActorInfo* ActorInfo) const override;

	/** 输入 Spec 上下文：只有 SourceObject 就是当前武器的那一份 Fire Spec 参与激活。 */
	virtual bool DoesSpecMatchInputContext(const FGameplayAbilitySpec& Spec, const FGameplayAbilityActorInfo* ActorInfo) const override;

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

	/**
	 * Spec 被移除时的实例生命周期收口：引擎在实例 MarkAsGarbage 之前调用，服务器与拥有者客户端都会走。
	 *
	 * 语义是"一个 Fire Spec 生命周期结束时，必须主动终结自己拥有的全部未结本地预测状态"，
	 * 而不是依赖迟到的 ClientActivateAbilityFailed、Owner 变化或 Ammo 复制纠正。
	 */
	virtual void OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;

private:
	/**
	 * 作废本实例拥有的全部未结预测债务：逐条按"不会再有裁决"结清（退款恰好一次 + HUD 预测结清），
	 * 然后丢弃记录并解绑。清理范围严格限定为"本实例自己的记录"，不动整把武器的预测状态。
	 *
	 * 这里的退休**不是**服务器拒绝：服务器从未对这一发作出 Rejected 裁决，因此本路径不得增加
	 * AuthorityRejectCount 之类的 Reject 统计，日志也必须用 Retired 语义单独标识。
	 */
	void RetireOwnedPredictionDebt(const FGameplayAbilitySpec& Spec);

	// ---- 验证层 ----

	/**
	 * 预测客户端资格：只读本地确定性条件（不读可能过期的复制状态），最后交给 Super 做 Tag 门控。
	 *
	 * 判定顺序：ASC 与 Avatar 一致 → 本机拥有者视图 → 当前武器有效且未隐藏 →
	 * 本地开火节拍已就绪（两种模式共用：半自动是硬失败判据，全自动是请求节流）→
	 * 全自动仍按住 → Super 的 ActivationBlockedTags 门控。
	 *
	 * 本地弹药预算刻意不在这里门控：预算不足只意味着这一次不提前表现，
	 * 服务器仍必须独立裁决这一发（拥有端弹药可能过期，服务器可能已经换弹完成）。
	 */
	bool CanLocallyStartFire(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const AActor* AvatarActor,
		const FGameplayTagContainer* SourceTags,
		const FGameplayTagContainer* TargetTags,
		FGameplayTagContainer* OptionalRelevantTags) const;

	/** 权威端资格：Super 的 Tag 门控 + 完整权威校验 + 权威射速；任何失败都登记权威拒绝计数。 */
	bool CanAuthorityStartFire(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const AActor* AvatarActor,
		const FGameplayTagContainer* SourceTags,
		const FGameplayTagContainer* TargetTags,
		FGameplayTagContainer* OptionalRelevantTags) const;

	/**
	 * 权威校验的共享核心：Avatar 有效且未死亡、本次请求的 RequestedWeapon 有效且属于该 Avatar、
	 * 可见、有可消耗弹药，并且**仍然等于服务器认可的当前装备**。
	 *
	 * RequestedWeapon 与 CurrentWeapon 分开传入：前者是这次 Fire Action 的身份（来自 Spec），
	 * 后者只是"它是否仍然合法"的判据；两者不等时这次请求必须被拒绝，绝不改写成当前武器的 Shot。
	 */
	bool IsAuthorityFireContextValid(const AActor* AvatarActor, const UAbilitySystemComponent* AbilitySystemComponent,
		const AShooterWeapon* RequestedWeapon, const AShooterWeapon* CurrentWeapon) const;

	/** Spec → RequestedWeapon：SourceObject 不是 WeaponActor 时返回 nullptr。 */
	static AShooterWeapon* ResolveRequestedWeaponFromSpec(const FGameplayAbilitySpec& Spec);

	/**
	 * 本次 Activation 的 RequestedWeapon：由当前 AbilitySpec 的 SourceObject 得到。
	 * 这是"这一次 Fire Action 属于哪把枪"的唯一来源，客户端与服务器读取的是同一个事实。
	 */
	AShooterWeapon* ResolveRequestedWeaponForActivation(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo) const;

	/** 本地表现上下文是否成立：武器有效、未隐藏、仍是当前装备。 */
	bool IsOwnerFireContextValid();

	/** 显式上下文版本：AbilitySystemComponent 参数刻意不参与判定，测试依赖「这些状态存在也不门控」这一事实。 */
	bool IsOwnerFireContextValidForContext(AActor* AvatarActor, const AShooterWeapon* Weapon,
		const UAbilitySystemComponent* AbilitySystemComponent);

	// ---- Owner Prediction Path ----

	/**
	 * 一次本地预测 Shot Attempt 的两个独立事实。
	 *
	 * 预算与表现刻意分开：预算一旦消费就必须被裁决结清，而表现提交失败（资产缺失 / 目标失效）
	 * 不应该让这一发的预算永远挂着，也不应该让表现通道失败影响 Server Accept / Reject 结算。
	 */
	struct FOwnerShotAttemptResult
	{
		/** 本次是否消费了本地预测预算（PendingPredictedShots +1）。 */
		bool bBudgetConsumed = false;

		/** 本次是否真的提交了拥有端表现。 */
		bool bFeedbackPlayed = false;
	};

	/**
	 * 一次本地预测 Shot Attempt：上下文 → 预测弹药预算 → Owner 表现。
	 *
	 * 两个输出事实相互独立。上下文失效时两者都不成立；预算不足时只表示这一次不提前表现，
	 * 但**不影响**这次请求已经发给服务器，也不影响本地节拍推进：
	 * 服务器仍会独立裁决这一发，Accept / Reject 只负责结果与状态结算。
	 */
	FOwnerShotAttemptResult TryPredictOwnerShot(AShooterWeapon& Weapon);

	/** 提交一次拥有者纯表现；只有实际播放成功才登记。 */
	bool PlayOwnerShotFeedback(AShooterWeapon& Weapon);

	/**
	 * 一次 Activation 的拥有端 Shot 记录。
	 *
	 * 只按 PredictionKey 保留"这一发的裁决还没回来"所需的三个事实：
	 * 是哪把武器（含预测代次）、是否已经提前表现并占用预算、是否已结清。
	 * 记录必须活过本地 Ability 的结束（本地一发结束得比裁决到达早一个 RTT），
	 * 因此不能放在"当前激活实例状态"里。
	 */
	struct FPredictedShotRecord
	{
		/** 只弱引用 WeaponActor：池化 / 切枪 / 销毁后不得被记录延长生命周期。 */
		TWeakObjectPtr<AShooterWeapon> Weapon;

		/** 本地武器上下文代次；同一 Actor 回池再租用后旧记录不得再动新预算。 */
		uint32 WeaponPredictionGeneration = 0;

		/** 本次 Activation 是否消费了本地预测预算（PendingPredictedShots +1）。 */
		bool bBudgetConsumed = false;

		/** 本次 Activation 是否已经真的提交过拥有端表现。 */
		bool bFeedbackPlayed = false;

		/** 生命周期抑制诊断；不参与 Committed / Rejected 的 Owner 表现或预算结算。 */
		bool bFeedbackSuppressed = false;

		/** 已收到裁决（Committed 或 Rejected），两条路径只处理一次。 */
		bool bResolved = false;
	};

	/** 按 PredictionKey 索引的拥有端 Shot 记录；只在拥有端预测上下文登记。 */
	TMap<int32, FPredictedShotRecord> PendingShotRecords;

	/** 登记本次 Activation 的 Shot 记录；只在 Ammo 预测上下文有效。 */
	void RegisterShotRecord(int32 PredictionKey, AShooterWeapon* Weapon);

	/** 登记实现本体：不做预测上下文判定，供生产入口与定向测试共用。 */
	void RegisterShotRecordCore(int32 PredictionKey, AShooterWeapon* Weapon);

	/** 清理已结清与上下文失效的记录；不按 PredictionKey 大小判断新旧。 */
	void PruneShotRecords();

	/**
	 * 一条 Shot 记录的终结原因。
	 *
	 * 服务器裁决与本地生命周期退休共用同一结清实现，但语义必须分开：Committed / Rejected 是权威结果，
	 * Retired 只表示"这个 Spec 结束时这一发以后不可能再有裁决"。任何 Reject 统计只对 Rejected 成立。
	 */
	enum class EShooterShotOutcome : uint8
	{
		/** 服务器接受这一发：只结清本地预算、Record 与 HUD，不重演历史 Owner 表现。 */
		Committed,

		/** 服务器明确拒绝这一发：退还预算并撤销显示预测。 */
		Rejected,

		/** Spec 生命周期结束导致的本地退休：结清行为与 Rejected 相同，但绝不代表服务器拒绝。 */
		Retired,
	};

	/**
	 * 一次结清实际做了什么。
	 *
	 * 调用方只按这里的事实统计，不按记录字段反推：bBudgetConsumed 只说明"曾经占过预算"，
	 * 代次不匹配时结清会直接丢弃记录，此时退款并没有发生。
	 */
	struct FShotResolution
	{
		/** 是否真的执行了 RefundPredictedAmmo；只有它能让 Refunded 统计增长。 */
		bool bBudgetRefunded = false;

		/** 是否因为武器代次不匹配而只丢弃记录：不退款、不动当前 Pending / HUD / 绑定。 */
		bool bGenerationSkipped = false;
	};

	/** 按终结原因结清记录：Committed 结清预算，Rejected / Retired 退预算并结清显示预测。 */
	FShotResolution ResolveShotRecord(FPredictedShotRecord& Record, int32 PredictionKey, EShooterShotOutcome Outcome);

	/** 终结原因的日志名；只用于日志，不参与任何判定。 */
	static const TCHAR* GetShotOutcomeLogName(EShooterShotOutcome Outcome);

	/** 引擎 Reject 委托的绑定入口：只带 PredictionKey，不持久持有武器指针。 */
	void HandlePredictedShotRejected(int32 PredictionKey);

	/** 本地提交一次 Shot Attempt 前先固定本次 Activation 的身份。 */
	void BeginOwnerActivationIdentity(const FGameplayAbilityActivationInfo& ActivationInfo, AShooterWeapon& Weapon);

	/** 本次拥有端本地激活的 PredictionKey；非拥有端保持 0。 */
	int32 EffectivePredictionKey = 0;

	/** 激活时缓存的武器；权威端控武器，拥有端控表现；EndAbility 只清理仍指向自己的武器。 */
	TWeakObjectPtr<AShooterWeapon> CachedWeapon;

	// ---- 共用查询 ----

	/** Equipment 优先、IShooterWeaponHolder 作为 NPC 兼容回退的当前武器解析。 */
	AShooterWeapon* GetCurrentWeaponForAvatar(AActor* AvatarActor) const;

	/** 输入当前是否仍处于按住状态；由基类按"输入采集层优先、引擎镜像兜底"判定，不维护第二份布尔。 */
	bool IsInputHeld(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo) const;

	/** 权威 CanActivate 拒绝的统一测试观测点；返回值恒为 false。 */
	bool RecordAuthorityRejectAndReturnFalse() const;

	/** P1 统一预测日志标记；仅开发构建输出。 */
	void LogFirePredictionMarker(const TCHAR* Marker, const AShooterWeapon* Weapon) const;

public:
	/** 武器端收到这一发的裁决时转发到拥有端 Shot 记录；由 ShooterWeapon 调用。 */
	void HandleAuthorityShotVerdict(AShooterWeapon* SourceWeapon, int32 ActivationKey, bool bCommitted);

	/** Owner / 租用边界作废该武器的旧记录；武器自身负责整体复位 Pending。 */
	void InvalidateWeaponPredictionContext(AShooterWeapon* Weapon);

	/** 切枪 / 动作取消只抑制旧反馈，不丢弃等待最终裁决的预算账目。 */
	void SuppressWeaponConfirmedFeedback(AShooterWeapon* Weapon);

#if WITH_DEV_AUTOMATION_TESTS
	/**
	 * 一次 Activation 的武器上下文取证样本：本 Ability 处理这次 Activation 时实际使用的武器。
	 *
	 * 拥有端写的是"这次预测所用的武器"，权威端写的是"这次提交所用的武器"。
	 * 两者按 PredictionKey 对齐后即可判定「客户端用 A 发起的请求是否被服务器重新解释成 B 的 Shot」。
	 * 只记录事实，不参与任何判定。
	 */
	struct FWeaponContextSampleForTest
	{
		/** 本条样本的 PredictionKey；拥有端与权威端使用同一个客户端生成的键。 */
		int32 PredictionKey = 0;

		/** 本次 Activation 使用的武器；弱引用，仅用于取名字与身份比对。 */
		TWeakObjectPtr<AShooterWeapon> Weapon;

		/** 该武器的 WeaponId；武器无效时为 NAME_None。 */
		FName WeaponId;

		/** 本端自己的时刻，只用于同一端内部排序，不跨端比较。 */
		float LocalTime = -1.0f;

		/** 本条样本是否来自权威路径。 */
		bool bAuthority = false;
	};

	/** 测试观察接口：本实例（拥有端或权威端）逐次 Activation 的武器上下文样本。 */
	const TArray<FWeaponContextSampleForTest>& GetWeaponContextSamplesForTest() const
	{
		return WeaponContextSamplesForTest;
	}

	/** 测试专用：清空武器上下文取证窗口（每个步骤起点调用）。 */
	void ResetWeaponContextTraceForTest() { WeaponContextSamplesForTest.Reset(); }

	/** 测试观察接口：武器转发的裁决与记录所属武器不一致的次数（跨武器裁决）。 */
	int32 GetWeaponContextMismatchVerdictCountForTest() const { return WeaponContextMismatchVerdictCountForTest; }

	/** 测试观察接口：Spec 移除时作废的未结记录总数（本实例累计）。 */
	int32 GetSpecRemovalRetiredRecordCountForTest() const { return SpecRemovalRetiredRecordCountForTest; }

	/** 测试观察接口：Spec 移除时真正退款的记录数（只统计真的执行了 RefundPredictedAmmo 的那些）。 */
	int32 GetSpecRemovalRefundedCountForTest() const { return SpecRemovalRefundedCountForTest; }

	/** 测试观察接口：Spec 移除时因代次不匹配被丢弃、未退款也未触碰当前上下文的记录数。 */
	int32 GetSpecRemovalGenerationSkippedCountForTest() const { return SpecRemovalGenerationSkippedCountForTest; }

	/**
	 * 测试专用：驱动生产生命周期入口 OnRemoveAbility。
	 *
	 * 它只用于"清理本身的幂等性"单元测试；真实 Spec Removal 必须由网络用例
	 * 经 Inventory Remove → Spec 撤销 → GAS 复制删除 的路径验证，不得用本入口替代。
	 */
	void HandleSpecRemovalForTest(const FGameplayAbilitySpec& Spec) { OnRemoveAbility(nullptr, Spec); }

	/** 测试观察接口：最近一次带武器来源的裁决的发送方；引擎 Reject 通道不带武器，保持原值。 */
	AShooterWeapon* GetLastVerdictSourceWeaponForTest() const { return LastVerdictSourceWeaponForTest.Get(); }

	/** 测试观察接口：本次拥有端激活的 PredictionKey。 */
	int32 GetEffectivePredictionKeyForTest() const { return EffectivePredictionKey; }

	/** 测试观察接口：尚未结清的 Shot 记录数量。 */
	int32 GetUnresolvedShotRecordCountForTest() const;

	/**
	 * 测试观察接口：尚未结清的记录里有多少条属于指定武器。
	 *
	 * 换枪之后"当前武器"已经换人，但旧武器的记录可能仍然挂着；
	 * 只有按武器分别统计才能看到"旧武器的请求没有被结清"。
	 */
	int32 GetUnresolvedShotRecordCountForWeaponForTest(const AShooterWeapon* Weapon) const;

	/** 测试观察接口：指定 PredictionKey 是否已登记。 */
	bool HasShotRecordForTest(int32 PredictionKey) const { return PendingShotRecords.Contains(PredictionKey); }

	/** 测试观察接口：指定记录是否消费了本地预测预算。 */
	bool IsShotRecordBudgetConsumedForTest(int32 PredictionKey) const;

	/** 测试观察接口：指定记录是否已经提交过拥有端表现。 */
	bool IsShotRecordFeedbackPlayedForTest(int32 PredictionKey) const;

	/** 测试观察接口：指定记录是否已结清。 */
	bool IsShotRecordResolvedForTest(int32 PredictionKey) const;

	/** 测试观察接口：最近一次结清的 PredictionKey；没有时返回 INDEX_NONE。 */
	int32 GetLastResolvedShotKeyForTest() const { return LastResolvedShotKeyForTest; }

	/** 测试观察接口：最近一次结清是否为 Committed。 */
	bool WasLastResolvedShotCommittedForTest() const { return bLastResolvedShotCommittedForTest; }

	/** 测试观察接口：最近一次结清是否走了引擎 Reject 通道。 */
	bool WasLastResolvedShotRejectedByEngineForTest() const { return bLastResolvedShotRejectedByEngineForTest; }

	/** 测试观察接口：禁止的 Owner historical cosmetic replay 请求次数，当前生产路径必须恒为 0。 */
	int32 GetOwnerConfirmedReplayRequestCountForTest() const { return OwnerConfirmedReplayRequestCountForTest; }

	/** 测试专用：驱动生产裁决入口，验证 Committed / Rejected 的幂等结清语义。 */
	void HandleAuthorityShotVerdictForTest(int32 PredictionKey, bool bCommitted);

	/** 测试专用：登记记录并写入预测事实。 */
	void RegisterShotRecordForTest(int32 PredictionKey, AShooterWeapon* Weapon, bool bBudgetConsumed, bool bFeedbackPlayed);

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

	/** 测试观察接口：活动期间重复激活是否会重触发实例（单发事务应为 false）。 */
	bool CanRetriggerInstancedAbility() const;

	/** 测试观察接口：是否接受客户端发来的结束命令（必须为 false，权威保留在服务器）。 */
	bool ServerRespectsRemoteAbilityCancellation() const;

	/**
	 * 权威端节拍取证：服务器处理这次 Activation 的时刻与本武器权威射速时钟状态。
	 * 只读、不参与任何判定；接受路径与 Refire 拒绝路径共用同一格式，便于逐发对齐。
	 */
	void LogAuthorityCadenceForTest(const TCHAR* Marker, const AShooterWeapon* Weapon, int32 ActivationKey) const;

	/** 记录一次 Activation 的武器上下文样本；只在开发构建登记，不改动任何 Gameplay 结果。 */
	void RecordWeaponContextForTest(int32 Key, const AShooterWeapon* Weapon, bool bAuthority);

private:
	/** 本次取证窗口内逐次 Activation 的武器上下文；容量上限防止长时间测试无界增长。 */
	TArray<FWeaponContextSampleForTest> WeaponContextSamplesForTest;

	/** 跨武器裁决次数：武器转发的裁决命中了另一把武器的 Shot 记录。 */
	int32 WeaponContextMismatchVerdictCountForTest = 0;

	/**
	 * Spec 移除时作废 / 真实退款 / 代次跳过的记录计数；
	 * 用于验证"生命周期结束时债务清零、退款恰好一次、旧代次记录不得动当前状态"。
	 */
	int32 SpecRemovalRetiredRecordCountForTest = 0;
	int32 SpecRemovalRefundedCountForTest = 0;
	int32 SpecRemovalGenerationSkippedCountForTest = 0;

	/** 最近一次带武器来源的裁决发送方。 */
	TWeakObjectPtr<AShooterWeapon> LastVerdictSourceWeaponForTest;

	mutable int32 AuthorityRejectCountForTest = 0;
	int32 LastResolvedShotKeyForTest = INDEX_NONE;
	bool bLastResolvedShotCommittedForTest = false;
	bool bLastResolvedShotRejectedByEngineForTest = false;
	int32 OwnerConfirmedReplayRequestCountForTest = 0;
#endif
};
