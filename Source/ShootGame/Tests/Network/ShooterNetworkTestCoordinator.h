// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayAbilitySpecHandle.h"
#include "AI/ShooterNPC.h"
#include "Tests/Network/ShooterRemoteBatchForTest.h"
#include "Weapons/ShooterWeapon.h"
#include "ShooterNetworkTestCoordinator.generated.h"

class AShooterCharacter;
class AShooterWeapon;
class AShooterPlayerState;
class UAbilitySystemComponent;
class UBoxComponent;
class USkeletalMeshComponent;
class UShooterGameplayAbility_Equip;
class UShooterGameplayAbility_Fire;
class UShooterGameplayAbility_Reload;
class UGameplayAbility;
struct FOnAttributeChangeData;
struct FGameplayAbilitySpec;
struct FGameplayTagContainer;

/** Reload identity 定向会话的逐端快照；无效目标保留为无效证据，不折算为零。 */
USTRUCT()
struct FShooterReloadIdentityObservation
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Step = INDEX_NONE;
	UPROPERTY()
	TObjectPtr<AShooterCharacter> Subject = nullptr;
	UPROPERTY()
	TObjectPtr<AShooterWeapon> Weapon = nullptr;
	UPROPERTY()
	uint32 ReloadId = 0;
	UPROPERTY()
	uint32 ObservedReloadId = 0;
	UPROPERTY()
	int32 NewPresentationCount = INDEX_NONE;
	UPROPERTY()
	int32 EntryCount = INDEX_NONE;
	UPROPERTY()
	int32 OwnerActivationCount = 0;
	UPROPERTY()
	int32 OwnerRejectCount = 0;
	UPROPERTY()
	int32 PredictionKey = 0;
	UPROPERTY()
	int32 OwnerFireFeedbackCount = INDEX_NONE;
	UPROPERTY()
	int32 OwnerFireConfirmationCount = INDEX_NONE;
	UPROPERTY()
	int32 MagazineAmmo = INDEX_NONE;
	UPROPERTY()
	int32 ReserveAmmo = INDEX_NONE;
	UPROPERTY()
	FName AnimationState;
	UPROPERTY()
	float AnimationTime = 0.0f;
	UPROPERTY()
	bool bValid = false;
	UPROPERTY()
	bool bOwner = false;
	UPROPERTY()
	bool bActive = false;
	UPROPERTY()
	bool bReloading = false;
	UPROPERTY()
	bool bRecovering = false;
	UPROPERTY()
	bool bSawReloadState = false;
	UPROPERTY()
	bool bSawRecovery = false;
};

/**
 * 夹具读取的 NetConnection 累计网络计数快照（窗口差值口径）。
 *
 * 使用累计系列 `InTotalBytes` / `OutTotalBytes` / `InTotalPackets` / `OutTotalPackets`：
 * 它们在收包 / 发包路径上无条件累加，且**不在** `UNetConnection::Tick` 的 StatPeriod 重置列表内
 * （NetConnection.cpp:4619-4631），因此可以安全做窗口差值。
 *
 * 刻意不使用 `InBytes` / `OutBytes` / `InPackets` / `OutPackets`：它们在
 * `UNetConnection::Tick` 里被"除以统计周期实际时长"后写入 `*PerSecond`，随即清零重置
 * （NetConnection.cpp:4608-4628），是瞬时速率而不是累计值，做差值没有意义。
 * 速率口径请用 FShooterAmmoPredictionNetRateWindow 的采样平均值 / 峰值。
 */
struct FShooterAmmoPredictionNetCounters
{
	/** 连接不存在时为 false；此时所有字段不得被当作 0 使用。 */
	bool bValid = false;
	uint32 InBytes = 0;
	uint32 OutBytes = 0;
	uint32 InPackets = 0;
	uint32 OutPackets = 0;
	/** `UNetDriver::TotalRPCsCalled`：本机累计发起的 RPC 次数（Driver 级，非连接级）。 */
	uint32 RPCsCalled = 0;
};

/**
 * 对一个 NetConnection 的瞬时速率做窗口采样：平均值 + 峰值。
 *
 * 采样对象是 `InBytesPerSecond` / `OutBytesPerSecond` / `InPacketsPerSecond` /
 * `OutPacketsPerSecond`：`UNetConnection::Tick` 每个 StatPeriod（`StatPeriod` 默认 1s）
 * 用"本周期累计值 / 实际时长"刷新它们一次，所以窗口内只能周期采样后取平均与峰值，
 * 不能做差值。采样上限 10Hz（比底层刷新更密只会重复同一个瞬时值，不会失真）。
 * 连接解析失败时 `bConnectionResolved` 保持 false：这是证据缺口，不得用 0 代替。
 */
struct FShooterAmmoPredictionNetRateWindow
{
	static constexpr float SampleIntervalSeconds = 0.1f;

	bool bConnectionResolved = false;
	int32 Samples = 0;
	float LastSampleTime = -1.0f;
	double SumInBytesPerSecond = 0.0;
	double SumOutBytesPerSecond = 0.0;
	double SumInPacketsPerSecond = 0.0;
	double SumOutPacketsPerSecond = 0.0;
	float PeakInBytesPerSecond = 0.0f;
	float PeakOutBytesPerSecond = 0.0f;
	float PeakInPacketsPerSecond = 0.0f;
	float PeakOutPacketsPerSecond = 0.0f;

	void Reset()
	{
		bConnectionResolved = false;
		Samples = 0;
		LastSampleTime = -1.0f;
		SumInBytesPerSecond = 0.0;
		SumOutBytesPerSecond = 0.0;
		SumInPacketsPerSecond = 0.0;
		SumOutPacketsPerSecond = 0.0;
		PeakInBytesPerSecond = 0.0f;
		PeakOutBytesPerSecond = 0.0f;
		PeakInPacketsPerSecond = 0.0f;
		PeakOutPacketsPerSecond = 0.0f;
	}

	bool CanSample(float Now) const
	{
		return LastSampleTime < 0.0f || Now - LastSampleTime >= SampleIntervalSeconds;
	}

	void Accumulate(float Now, float InBytesRate, float OutBytesRate, float InPacketRate, float OutPacketRate)
	{
		bConnectionResolved = true;
		++Samples;
		LastSampleTime = Now;
		SumInBytesPerSecond += InBytesRate;
		SumOutBytesPerSecond += OutBytesRate;
		SumInPacketsPerSecond += InPacketRate;
		SumOutPacketsPerSecond += OutPacketRate;
		PeakInBytesPerSecond = FMath::Max(PeakInBytesPerSecond, InBytesRate);
		PeakOutBytesPerSecond = FMath::Max(PeakOutBytesPerSecond, OutBytesRate);
		PeakInPacketsPerSecond = FMath::Max(PeakInPacketsPerSecond, InPacketRate);
		PeakOutPacketsPerSecond = FMath::Max(PeakOutPacketsPerSecond, OutPacketRate);
	}

	float GetAverageInBytesPerSecond() const
	{
		return Samples > 0 ? static_cast<float>(SumInBytesPerSecond / Samples) : 0.0f;
	}

	float GetAverageOutBytesPerSecond() const
	{
		return Samples > 0 ? static_cast<float>(SumOutBytesPerSecond / Samples) : 0.0f;
	}

	float GetAverageInPacketsPerSecond() const
	{
		return Samples > 0 ? static_cast<float>(SumInPacketsPerSecond / Samples) : 0.0f;
	}

	float GetAverageOutPacketsPerSecond() const
	{
		return Samples > 0 ? static_cast<float>(SumOutPacketsPerSecond / Samples) : 0.0f;
	}
};

/**
 * Ammo Prediction 夹具的拥有端快照；无效目标保留为无效证据，不折算为零。
 *
 * 字段口径与「一次 GA_Fire Activation = 恰好一发 Shot」对齐：
 * - PendingShots 只由这一发的裁决结清（Committed / Rejected），Ammo 复制不参与结清；
 * - PredictedOwnerFeedbackCount 与 OwnerConfirmedReplayCount 分别是本地预测表现与禁止的
 *   Owner historical replay 实际播放次数；OwnerConfirmedReplayRequestCount 是 GA 侧的禁止路径请求次数；
 * - ShotVerdictCount 是拥有端武器收到的单发裁决通知（ClientShotCommitted）次数；
 * - UnresolvedShotRecordCount 是拥有端 GA_Fire 上尚未结清的 Shot 记录数；
 * - bReleaseSettled / ActivationsAtRelease 只在松手后置位，用于断言松手不再产生新激活；
 * - RefireRate 与 Net* 只在射速测量用例里被断言，但每个步骤都会如实上报。
 */
USTRUCT()
struct FShooterAmmoPredictionObservation
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Step = INDEX_NONE;
	UPROPERTY()
	TObjectPtr<AShooterCharacter> Subject = nullptr;
	UPROPERTY()
	TObjectPtr<AShooterWeapon> Weapon = nullptr;
	UPROPERTY()
	int32 MagazineAmmo = INDEX_NONE;
	UPROPERTY()
	int32 ReserveAmmo = INDEX_NONE;
	UPROPERTY()
	int32 PendingShots = INDEX_NONE;
	UPROPERTY()
	int32 PredictedMagazineAmmo = INDEX_NONE;
	UPROPERTY()
	int32 HudMagazine = INDEX_NONE;
	UPROPERTY()
	int32 HudReserve = INDEX_NONE;
	UPROPERTY()
	int32 HudPredictedUpdates = INDEX_NONE;
	UPROPERTY()
	int32 HudUnsettledCount = INDEX_NONE;
	UPROPERTY()
	int32 PredictedOwnerFeedbackCount = INDEX_NONE;
	UPROPERTY()
	int32 OwnerConfirmedReplayCount = INDEX_NONE;
	UPROPERTY()
	int32 OwnerConfirmedReplayRequestCount = INDEX_NONE;
	UPROPERTY()
	int32 ShotVerdictCount = INDEX_NONE;
	UPROPERTY()
	int32 OwnerFireActivationCount = 0;
	/**
	 * 拥有端本步骤窗口内自己的 Fire Activation 采样：计数与首末时间来自同一个来源。
	 *
	 * 请求射速必须只用这三个字段算：(数量 - 1) / (末 - 首)。
	 * 计数量与时间跨度必须同域——用累计计数配服务器时间跨度会把 N 与 N-1
	 * 以及两个时钟混在一条公式里。
	 */
	UPROPERTY()
	int32 OwnerWindowActivationCount = 0;
	UPROPERTY()
	float FirstOwnerActivationTime = -1.0f;
	UPROPERTY()
	float LastOwnerActivationTime = -1.0f;
	UPROPERTY()
	int32 OwnerFireRejectCount = 0;
	UPROPERTY()
	int32 MontageCount = INDEX_NONE;
	UPROPERTY()
	int32 MuzzleCount = INDEX_NONE;
	UPROPERTY()
	int32 SoundCount = INDEX_NONE;
	UPROPERTY()
	int32 RecoilCount = INDEX_NONE;
	UPROPERTY()
	int32 UnresolvedShotRecordCount = INDEX_NONE;
	UPROPERTY()
	int32 PeakUnresolvedShotRecords = 0;
	UPROPERTY()
	int32 DistinctPredictionKeyCount = 0;
	UPROPERTY()
	int32 FirstPredictionKey = 0;
	UPROPERTY()
	int32 LastPredictionKey = 0;
	UPROPERTY()
	int32 LastResolvedShotKey = 0;
	UPROPERTY()
	bool bLastResolvedShotCommitted = false;
	UPROPERTY()
	bool bLastResolvedShotRejectedByEngine = false;
	UPROPERTY()
	bool bReleaseSettled = false;
	UPROPERTY()
	int32 ActivationsAtRelease = INDEX_NONE;
	/** 拥有端武器当前的 RefireRate；射速用例要求两端一致，否则测到的是两端节拍之差。 */
	UPROPERTY()
	float RefireRate = -1.0f;
	/** 拥有端网络计数窗口（本步骤起点 → 本次采样）的增量；counter 不可用时保持 INDEX_NONE。 */
	UPROPERTY()
	int32 NetInBytes = INDEX_NONE;
	UPROPERTY()
	int32 NetOutBytes = INDEX_NONE;
	UPROPERTY()
	int32 NetInPackets = INDEX_NONE;
	UPROPERTY()
	int32 NetOutPackets = INDEX_NONE;
	UPROPERTY()
	int32 NetRPCsCalled = INDEX_NONE;
	/** 窗口内夹具自身的采样上报次数（含本条）；从 NetRPCsCalled 扣除即得到玩家动作 RPC 数。 */
	UPROPERTY()
	int32 NetSampleReports = INDEX_NONE;
	/** 拥有端网络计数窗口时长（秒）；窗口未开启时为 -1。 */
	UPROPERTY()
	float NetWindowSeconds = -1.0f;
	/** 拥有端连接是否解析成功；false 表示速率证据存在缺口，不得当成 0 速率。 */
	UPROPERTY()
	bool bNetConnectionResolved = false;
	/** 拥有端连接速率窗口的采样次数（上限 10Hz）与平均值 / 峰值。 */
	UPROPERTY()
	int32 NetRateSamples = 0;
	UPROPERTY()
	float NetInBytesPerSecondAvg = 0.0f;
	UPROPERTY()
	float NetInBytesPerSecondPeak = 0.0f;
	UPROPERTY()
	float NetOutBytesPerSecondAvg = 0.0f;
	UPROPERTY()
	float NetOutBytesPerSecondPeak = 0.0f;
	UPROPERTY()
	float NetInPacketsPerSecondAvg = 0.0f;
	UPROPERTY()
	float NetInPacketsPerSecondPeak = 0.0f;
	UPROPERTY()
	float NetOutPacketsPerSecondAvg = 0.0f;
	UPROPERTY()
	float NetOutPacketsPerSecondPeak = 0.0f;
	UPROPERTY()
	bool bFireActive = false;
	UPROPERTY()
	bool bReloadActive = false;
	UPROPERTY()
	bool bReloading = false;
	UPROPERTY()
	bool bLocalFireCooldownReady = false;
	/** 拥有端当前武器是否全自动；半自动用例据此证明它没有跑在全自动武器上。 */
	UPROPERTY()
	bool bCurrentWeaponIsFullAuto = false;
	/** 拥有端当前武器的 WeaponId；换枪后用它与夹具挑出的行比对，证明两端钉在同一把枪上。 */
	UPROPERTY()
	FName CurrentWeaponId;

	/**
	 * 拥有端本步骤窗口内逐次 Fire Activation 的武器上下文。
	 *
	 * 两个数组等长且同序：ActivationContextKeys[i] 这一次激活用的是 ActivationContextWeaponIds[i]。
	 * 它回答"客户端这一次请求用的是哪把武器"，是判定"服务器是否把这次请求重新解释成另一把武器的
	 * Shot"所必需的另一半；服务器侧同窗口的权威样本按 PredictionKey 与它对齐。
	 */
	UPROPERTY()
	TArray<int32> ActivationContextKeys;
	UPROPERTY()
	TArray<FName> ActivationContextWeaponIds;

	/** 拥有端本步骤起点被钉住的请求上下文武器（A）；换枪后它**不**跟随当前武器。 */
	UPROPERTY()
	FName ContextWeaponId;
	UPROPERTY()
	int32 ContextWeaponPendingShots = INDEX_NONE;
	UPROPERTY()
	int32 ContextWeaponMagazineAmmo = INDEX_NONE;
	UPROPERTY()
	int32 ContextWeaponUnsettledDisplayCount = INDEX_NONE;
	UPROPERTY()
	int32 ContextWeaponUnresolvedRecords = INDEX_NONE;

	/** 上下文武器上"旧 Spec 生命周期结束导致的解绑"次数；证明 Spec Removal 清理确实发生过。 */
	UPROPERTY()
	int32 ContextWeaponSpecRemovalUnbindCount = INDEX_NONE;

	/** 上下文武器上"Spec Removal 清算执行过"的次数（无论当时是否还有未结记录）。 */
	UPROPERTY()
	int32 ContextWeaponSpecRemovalCleanupCount = INDEX_NONE;

	/** 拥有端收到的"发送方不是记录所属武器"的裁决次数；0 表示没有任何跨武器裁决。 */
	UPROPERTY()
	int32 WeaponContextMismatchVerdictCount = INDEX_NONE;

	/** 最近一次带武器来源的裁决发送方 WeaponId；引擎 Reject 通道不带武器时保持 NAME_None。 */
	UPROPERTY()
	FName LastVerdictSourceWeaponId;

	/**
	 * 拥有端可见的 Fire 授予结构：每把持有的武器一份 Spec，SourceObject 指向该武器。
	 *
	 * FireSpecCount 必须等于持有武器数；UnresolvedSourceSpecCount 必须为 0
	 * （复制未解析或孤儿 Spec 既不能参与输入也不能被撤销）。
	 */
	UPROPERTY()
	int32 FireSpecCount = INDEX_NONE;
	UPROPERTY()
	int32 FireInstanceCount = INDEX_NONE;
	UPROPERTY()
	int32 UnresolvedSourceSpecCount = INDEX_NONE;

	/**
	 * 拥有端本地开火节拍取证（本步骤窗口内）。
	 *
	 * 它回答"下一发为什么在此时发生"：ExpectedDeadline 是上一发写下的本地节拍终点，
	 * 实际激活时间与它的差就是被帧边界推迟的量；CadencePhaseErrorMs 把
	 * "每一发的迟到是否被永久写进后续节拍"压缩成一个数。
	 * 无数据项一律保持 -1，不填 0。
	 */
	UPROPERTY()
	int32 CadenceSamples = 0;
	UPROPERTY()
	int32 CadenceIntervalSamples = 0;
	UPROPERTY()
	float CadenceRefireRate = -1.0f;
	UPROPERTY()
	float CadenceMeanIntervalMs = -1.0f;
	UPROPERTY()
	float CadenceMinIntervalMs = -1.0f;
	UPROPERTY()
	float CadenceMaxIntervalMs = -1.0f;
	UPROPERTY()
	float CadenceMeanLagMs = -1.0f;
	UPROPERTY()
	float CadenceMaxLagMs = -1.0f;
	UPROPERTY()
	float CadenceMeanFrameDeltaMs = -1.0f;
	UPROPERTY()
	float CadenceMaxFrameDeltaMs = -1.0f;
	UPROPERTY()
	float CadenceMeanEndToActivationMs = -1.0f;
	UPROPERTY()
	float CadenceSpanMs = -1.0f;
	UPROPERTY()
	float CadencePhaseErrorMs = -1.0f;
	/** 拥有端本步骤的帧率上限设置值（GEngine->GetMaxFPS()）；0 表示不限制。 */
	UPROPERTY()
	float ClientMaxFPS = 0.0f;

	UPROPERTY()
	bool bValid = false;
};

/**
 * 仅用于网络测试的 NPC 子类：验证 ShooterNPC C++ 基类的 ASC 生命周期
 * （Owner = Avatar = NPC），避免依赖 BP_ShooterNPC 的自动占有与武器配置。
 */
UCLASS(NotBlueprintable, Transient)
class AShooterNetworkTestNPC : public AShooterNPC
{
	GENERATED_BODY()

public:
	AShooterNetworkTestNPC();
};

/** 仅用于 SlotFull 测试的额外武器类；不参与开火，只验证 Inventory 授予路径。 */
UCLASS(NotBlueprintable, Transient)
class AShooterNetworkTestWeapon : public AShooterWeapon
{
	GENERATED_BODY()

public:
	AShooterNetworkTestWeapon();
};

/**
 * Drives one owning client through the server-authoritative weapon fire path.
 * Spawned only when the server is launched with -ShootGameNetworkTest.
 */
UCLASS(NotBlueprintable, Transient)
class SHOOTGAME_API AShooterNetworkTestCoordinator : public AActor
{
	GENERATED_BODY()

public:
	AShooterNetworkTestCoordinator();

	/**
	 * OwnerSingleShot 的正向就绪判定：当前武器必须在本地已拥有可解析的 Fire Spec。
	 *
	 * per-Weapon GA_Fire Spec 架构下，"武器 Actor 已复制"与"这把武器能在本端被解释成动作"是两件事：
	 * Spec 晚于武器到达时，同一帧的 Press edge 会在帧末失效，这一枪必丢（普通 Dedicated 全零报告根因）。
	 *
	 * 判定直接复用生产解析入口 AShooterPlayerState::FindFireAbilitySpecForWeapon
	 * （按 AbilityClass + SourceObject 判定武器身份），不在这里重新实现类搜索 / SourceObject 比较 / Tag 比较。
	 *
	 * 放在 public：自动化测试要直接验证 fixture 的这个就绪判定本身（只读，不改任何 Stage）。
	 */
	static const FGameplayAbilitySpec* ResolveOwnerFireSpecForSingleShot(const AShooterPlayerState* ShooterPlayerState,
		const AShooterWeapon* CurrentWeapon);

	// ---- 远端批次异步顺序 / exactly-once 的 TEST-ONLY 入口 ----
	// 放在 public：Coordinator 层自动化测试要直接驱动真实的 report handler /
	// NotifyObservedBatchFrozen / EvaluateRemoteConfirmedBatch 三个入口，
	// 并读取比较次数与结论。它们只暴露既有语义，不新增批次语义，也不出现在生产网络流程里。

	/** 只读取证：本玩家 FullAuto 批次的冻结权威增量；未完成冻结时为 INDEX_NONE。 */
	int32 GetOwnRemoteBatchAuthorityDeltaForTest() const { return OwnRemoteFullAutoBatch.GetExpectedDelta(); }

	/** 只读取证：EvaluateRemoteConfirmedBatch 真正完成比较的次数。 */
	int32 GetRemoteBatchEvaluateCountForTest() const { return RemoteBatchEvaluateCountForTest; }

	/** 只读取证：最近一次比较使用的冻结权威增量。 */
	int32 GetRemoteBatchComparedAuthorityDeltaForTest() const { return RemoteAuthorityShotsForBurst; }

	/** 只读取证：最近一次比较的结论。 */
	bool IsRemoteBatchVerifiedForTest() const { return bRemoteConfirmedVerified; }

	/** 只读取证：观察端批次快照（report）是否已经到达本 Coordinator。 */
	bool HasRemoteBatchReportForTest() const { return bRemoteConfirmedReportReceived; }

	/**
	 * 服务器侧：用「观察端上报的批次快照」与「Target 自己冻结的批次边界」做一次性比较。
	 * 上报到达与批次冻结是两个独立事件：观察端可以在 Target 打完的瞬间就收口上报，
	 * 而权威终点要等静默期验证才冻结，因此比较必须能被推迟到冻结之后再执行，
	 * 绝不能在收到上报时现场读取权威计数。
	 */
	void EvaluateRemoteConfirmedBatch();

	/**
	 * 服务器侧：Target 的批次冻结后，在同一个 tick 内直接通知观察端 Coordinator，
	 * 让观察端拿到该批次的权威边界用于取证与事后核对。
	 */
	void NotifyObservedBatchFrozen(int32 AuthorityStart, int32 AuthorityEnd, int32 ExpectedDelta);

	/** TEST-ONLY：按生产 Arm 语义冻结本玩家批次的起点与统计武器。 */
	bool ArmOwnRemoteBatchForTest(int32 AuthorityStart, AShooterWeapon* Weapon);

	/**
	 * TEST-ONLY：按生产结束语义冻结本玩家批次的终点，并像真实流程那样在同一个 tick
	 * 通知观察端 Coordinator；返回冻结后的 ExpectedDelta。
	 */
	int32 FreezeOwnRemoteBatchAndNotifyForTest(int32 AuthorityEnd);

	/** TEST-ONLY：设置观察端的观测武器（真实流程里由被观测批次的 Arm 解析得到）。 */
	void SetRemoteObservedWeaponForTest(AShooterWeapon* Weapon) { RemoteObservedWeaponAtBurstStart = Weapon; }

	/**
	 * TEST-ONLY：把一次 Observer report 交给真实 handler。
	 * 离线夹具没有 NetDriver，RPC 传输不是本轮被测对象；handler 本身与真实会话完全同一份实现。
	 */
	void SubmitRemoteConfirmedReportForTest(int32 TargetPlayerId, int32 Count, int32 MontageCount, int32 MuzzleCount,
		int32 SoundCount, bool bTargetStable);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	/** 专用 flag 分支复用现有三名玩家的 OwnerOnly Coordinator，不进入旧整体回归。 */
	UPROPERTY(Replicated)
	bool bReloadIdentityMode = false;

	/** Ammo Prediction 复现专用模式；-ShootGameAmmoPredictionTest 开启。只加测试夹具，不改生产语义。 */
	UPROPERTY(Replicated)
	bool bAmmoPredictionMode = false;

	/**
	 * Weapon Context 定向模式（-ShootGameWeaponContextTest）。
	 * 复用 Ammo Prediction 夹具的驱动与观测，只把步骤表换成"按住开火期间换枪"的定向用例。
	 */
	UPROPERTY(Replicated)
	bool bWeaponContextMode = false;

	UFUNCTION(Client, Reliable)
	void ClientPrepareReloadIdentityStep(int32 SubjectPlayerId, int32 Step);

	UFUNCTION(Client, Reliable)
	void ClientSubmitReloadIdentityInput(int32 Step, bool bFire);

	UFUNCTION(Server, Reliable)
	void ServerReportReloadIdentitySample(const FShooterReloadIdentityObservation& Observation);

	void RunReloadIdentityServerPhase();
	void SampleReloadIdentityLocalState();
	void StartReloadIdentityStep(int32 Step);
	void CleanupReloadIdentityTest();
	void BindReloadIdentityAbilityObservers(UAbilitySystemComponent* AbilitySystemComponent);
	void HandleReloadIdentityActivated(UGameplayAbility* Ability);
	void HandleReloadIdentityFailed(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureTags);
	void HandleReloadIdentityEnded(UGameplayAbility* Ability);

	UFUNCTION(Client, Reliable)
	void ClientPrepareAmmoPredictionStep(int32 SubjectPlayerId, int32 Step);

	/** 夹具专用：把拥有端 RefireRate 设成夹具指定值，使请求节拍与服务器权威门控使用同一射速。 */
	UFUNCTION(Client, Reliable)
	void ClientSetAmmoPredictionRefireRate(int32 Step, float RefireRate);

	/** 夹具专用：把被试客户端帧率钉在矩阵指定值上（0 = 解除限制），只影响本地帧循环。 */
	UFUNCTION(Client, Reliable)
	void ClientSetAmmoPredictionMaxFPS(int32 Step, float MaxFPS);

	UFUNCTION(Client, Reliable)
	void ClientSubmitAmmoPredictionFire(int32 Step);

	/** HoldSeconds 为按住时长；ReloadAtSeconds < 0 表示本步骤不触发换弹；本地节拍起点在步骤准备时统一复位。 */
	UFUNCTION(Client, Reliable)
	void ClientSubmitAmmoPredictionHoldFire(int32 Step, float HoldSeconds, float LocalCooldownSeconds, float ReloadAtSeconds);

	UFUNCTION(Client, Reliable)
	void ClientVerifyAmmoPredictionObserver(int32 SubjectPlayerId);

	/**
	 * 夹具专用：让拥有端按**生产输入路径**请求切到下一个武器槽位。
	 *
	 * 走 Character 的输入入口 → ASC 输入采集 → GA_Equip（ServerOnly）请求上行，
	 * 不新增任何生产 RPC，也不直接改写 Equipment。拥有端的 CurrentWeaponActor
	 * 只会因 CurrentWeaponActor 的复制而改变，因此请求在途期间它仍然停留在旧武器上。
	 */
	UFUNCTION(Client, Reliable)
	void ClientRequestAmmoPredictionWeaponSwitch(int32 Step);

	UFUNCTION(Server, Reliable)
	void ServerReportAmmoPredictionObserver(bool bValid, int32 PredictedCount, int32 ConfirmedReplayCount, int32 VerdictCount);

	UFUNCTION(Server, Reliable)
	void ServerReportAmmoPredictionSample(const FShooterAmmoPredictionObservation& Observation);

	void RunAmmoPredictionServerPhase();
	void SampleAmmoPredictionLocalState();
	void StartAmmoPredictionStep(int32 Step);
	/** 单发用例的公共编排：就绪门 → 下发输入 → 等待权威结果 → 结算窗口；返回 true 表示可以断言。 */
	bool AdvanceAmmoPredictionSingleFireStep(int32 Step, bool bExpectAuthorityReject);
	void RunAmmoPredictionSemiAutoSingleShotStep();
	void RunAmmoPredictionFullAutoHoldStep();
	void RunAmmoPredictionPredictedAcceptedStep();
	void RunAmmoPredictionPredictedRejectedStep();
	void RunAmmoPredictionFullAutoReleaseStep();
	void RunAmmoPredictionReloadLifecycleStep();
	/** 射速测量用例：Step 必须是两个射速步骤之一，达成射速按权威时钟测量。 */
	void RunAmmoPredictionRateStep(int32 Step);
	/** 帧率 × 射速矩阵用例：Step 必须是矩阵步骤之一；只断言结构性不变量，射速作为数据上报。 */
	void RunAmmoPredictionCadenceMatrixStep(int32 Step);
	/** Weapon Context 定向用例：按住开火期间换枪，Step 必须是本模式的定向步骤之一。 */
	void RunAmmoPredictionWeaponContextStep(int32 Step);

	/** Spec Removal 定向用例：在途请求 + 真实 Inventory Remove（Spec 撤销）的确定性窗口。 */
	void RunAmmoPredictionSpecRemovalStep();

	/** Pool Reuse 定向用例：同一 Owner 立刻复用同一个 pooled 武器，验证新旧 Spec 生命周期互不干扰。 */
	void RunAmmoPredictionPoolReuseStep();

	/** 装备权威变化（服务器）：记录本步骤的精确换枪提交时刻与目标武器。 */
	UFUNCTION()
	void HandleAmmoPredictionEquippedWeaponChanged(AShooterWeapon* PreviousWeapon, AShooterWeapon* CurrentWeapon);
	void RunAmmoPredictionUnpredictedAcceptedStep();
	void RunAmmoPredictionUnpredictedRejectedStep();
	/** 半自动用例的换枪步骤：动态挑出正式半自动行 → 服务器装备 → 等拥有端观察到位。 */
	void RunAmmoPredictionSemiAutoSetupStep();
	void RunAmmoPredictionSemiAutoWeaponSingleShotStep();
	void RunAmmoPredictionSemiAutoHoldStep();
	void ConcludeAmmoPredictionCase(const TCHAR* CaseName, bool bConverged, const FString& Detail);
	bool IsAmmoPredictionClientSampleFresh(int32 Step) const;
	bool IsAmmoPredictionFixtureReady(int32 Step, int32 ExpectedPending) const;
	/** 权威 GA 实例上的拒绝计数探针；只作旁证记录，不作为硬判据。 */
	int32 GetAmmoPredictionAuthorityRejectProbe() const;
	void ClearAmmoPredictionServerTag();
	void CleanupAmmoPredictionTest();
	void BindAmmoPredictionClientObservers(UAbilitySystemComponent* AbilitySystemComponent);
	void BindAmmoPredictionServerObserver(UAbilitySystemComponent* AbilitySystemComponent);
	void HandleAmmoPredictionFireActivated(UGameplayAbility* Ability);
	void HandleAmmoPredictionFireEnded(UGameplayAbility* Ability);
	void HandleAmmoPredictionAuthorityFailed(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureTags);

	TArray<TWeakObjectPtr<AShooterNetworkTestCoordinator>> ReloadIdentityParticipants;
	TWeakObjectPtr<AShooterCharacter> ReloadIdentitySubject;
	TWeakObjectPtr<AShooterWeapon> ReloadIdentityWeapon;
	TWeakObjectPtr<UAbilitySystemComponent> ReloadIdentityObservedASC;
	TWeakObjectPtr<USkeletalMeshComponent> ReloadIdentityTickMesh;
	FDelegateHandle ReloadIdentityActivatedHandle;
	FDelegateHandle ReloadIdentityFailedHandle;
	FDelegateHandle ReloadIdentityEndedHandle;
	FShooterReloadIdentityObservation ReloadIdentityLatest;
	FShooterReloadIdentityObservation ReloadIdentityBefore;
	int32 ReloadIdentityServerStep = 0;
	int32 ReloadIdentityClientStep = INDEX_NONE;
	int32 ReloadIdentitySubjectPlayerId = INDEX_NONE;
	int32 ReloadIdentitySubmittedStep = INDEX_NONE;
	int32 ReloadIdentityOwnerActivations = 0;
	int32 ReloadIdentityOwnerRejects = 0;
	int32 ReloadIdentityPredictionKey = 0;
	int32 ReloadIdentityAuthorityActivations = 0;
	int32 ReloadIdentityAuthorityRejects = 0;
	int32 ReloadIdentityAuthorityActivationsBefore = 0;
	int32 ReloadIdentityAuthorityRejectsBefore = 0;
	int32 ReloadIdentityShotsBefore = 0;
	int32 ReloadIdentityProjectilesBefore = 0;
	int32 ReloadIdentityMagazineBefore = 0;
	int32 ReloadIdentityReserveBefore = 0;
	uint32 ReloadIdentityIdBefore = 0;
	float ReloadIdentityStepStartTime = 0.0f;
	float ReloadIdentityNextReportTime = 0.0f;
	uint8 ReloadIdentityPreviousTickOption = 0;
	bool bReloadIdentityPreviousMeshTick = false;
	bool bReloadIdentityPreviousUpdateOptimization = false;
	bool bReloadIdentitySetup = false;
	bool bReloadIdentityCancelSent = false;
	bool bReloadIdentityFinished = false;
	bool bReloadIdentitySawReloadState = false;
	bool bReloadIdentitySawRecovery = false;

	// ---- Ammo Prediction 复现夹具：只服务 -ShootGameAmmoPredictionTest ----
	TWeakObjectPtr<AShooterCharacter> AmmoPredictionSubject;
	TWeakObjectPtr<AShooterWeapon> AmmoPredictionWeapon;
	TWeakObjectPtr<UAbilitySystemComponent> AmmoPredictionObservedASC;
	FDelegateHandle AmmoPredictionActivatedHandle;
	FDelegateHandle AmmoPredictionEndedHandle;
	FDelegateHandle AmmoPredictionFailedHandle;
	/** 拥有端最近一次上报；服务器只在收到对应 Step 的样本后推进。 */
	FShooterAmmoPredictionObservation AmmoPredictionLatest;
	/** 步骤开始时的拥有端样本，用于计算本地增量。 */
	FShooterAmmoPredictionObservation AmmoPredictionBefore;
	int32 AmmoPredictionClientStep = INDEX_NONE;
	int32 AmmoPredictionServerStep = 0;
	int32 AmmoPredictionSubjectPlayerId = INDEX_NONE;
	int32 AmmoPredictionSubmittedStep = INDEX_NONE;
	/** 拥有端本步骤出现过的 PredictionKey 集合；用于证明每次激活都有独立的 Shot 记录身份。 */
	TSet<int32> AmmoPredictionStepPredictionKeys;
	/** 权威端本步骤被接受的 Activation 的 PredictionKey 集合。 */
	TSet<int32> AmmoPredictionAuthorityActivationKeys;
	int32 AmmoPredictionFirstPredictionKey = 0;
	int32 AmmoPredictionOwnerFireActivations = 0;
	/**
	 * 拥有端本步骤窗口自己的激活采样：计数与首末时间在同一次激活回调里更新。
	 * 请求射速只由它们计算，不与服务器时间跨度或累计计数混用。
	 */
	int32 AmmoPredictionOwnerWindowActivations = 0;
	float AmmoPredictionFirstOwnerActivationTime = -1.0f;
	float AmmoPredictionLastOwnerActivationTime = -1.0f;
	int32 AmmoPredictionOwnerFireRejects = 0;
	int32 AmmoPredictionLastPredictionKey = 0;
	int32 AmmoPredictionAuthorityActivations = 0;
	int32 AmmoPredictionAuthorityRejects = 0;
	int32 AmmoPredictionAuthorityShotsBefore = 0;
	int32 AmmoPredictionAuthorityActivationsBefore = 0;
	int32 AmmoPredictionAuthorityRejectsBefore = 0;
	int32 AmmoPredictionAuthorityRejectProbeBefore = 0;
	int32 AmmoPredictionMagazineBefore = 0;
	int32 AmmoPredictionReserveBefore = 0;
	int32 AmmoPredictionProjectilesBefore = 0;
	int32 AmmoPredictionMismatchCount = 0;
	int32 AmmoPredictionConvergedCount = 0;
	int32 AmmoPredictionPeakUnresolvedShotRecords = 0;
	/** Reload 阻塞窗口起点上的权威 Shot 计数；INDEX_NONE 表示窗口还没被观察到。 */
	int32 AmmoPredictionShotsAtReloadCommit = INDEX_NONE;
	/** 全自动窗口时长与请求数上限，由武器自身 RefireRate 在 setup 时算出。 */
	float AmmoPredictionFullAutoHoldSeconds = 0.0f;
	int32 AmmoPredictionMaxPacedShots = 0;
	/** Reload 用例的 Hold 时长，由武器自身 ReloadDuration 在 setup 时算出，保证松手早于换弹完成。 */
	float AmmoPredictionReloadHoldSeconds = 0.0f;
	/** 动态挑出的正式半自动武器行名；表里没有半自动行时夹具直接失败，不静默跳过用例。 */
	FName AmmoPredictionSemiAutoRowName;

	/** 半自动用例切换到的 WeaponActor；用于断言"每把武器各有一份 Fire Spec"的身份映射。 */
	TWeakObjectPtr<AShooterWeapon> AmmoPredictionSemiAutoWeapon;
	/** 半自动 Hold 用例的按住时长，换枪时按该武器 Row 的 RefireRate 算出。 */
	float AmmoPredictionSemiAutoHoldSeconds = 0.0f;
	/** 武器行配置的原始 RefireRate；每个步骤起点都会把两端恢复到这个值。 */
	float AmmoPredictionOriginalRefireRate = 0.0f;
	/** 本步骤两端必须一致的 RefireRate。 */
	float AmmoPredictionStepRefireRate = 0.0f;
	/** 本步骤第一次被服务器接受的 Activation 的服务器时间；射速窗口只用它到最后一发的跨度。 */
	float AmmoPredictionFirstAuthorityActivationTime = -1.0f;
	/** 射速窗口内相邻两次被接受 Activation 的间隔统计，用于判断节拍是否被帧率量化。 */
	float AmmoPredictionRateLastActivationTime = -1.0f;
	float AmmoPredictionRateMinInterval = -1.0f;
	float AmmoPredictionRateMaxInterval = -1.0f;
	float AmmoPredictionRateIntervalSum = 0.0f;
	int32 AmmoPredictionRateIntervalSamples = 0;
	/**
	 * 本步骤窗口内的权威提交采样数：每一次被接受的 Activation 都对应一次真实提交，
	 * 权威提交射速必须用它自己的计数与自己的时间跨度计算。
	 */
	int32 AmmoPredictionAuthorityCommitSamples = 0;
	/** 射速窗口内第一次与最后一次权威提交时间；累计相位误差由它们与间隔数算出。 */
	float AmmoPredictionFirstAuthorityCommitTime = -1.0f;
	float AmmoPredictionLastAuthorityCommitTime = -1.0f;
	/** 服务器下发本步骤 Hold 指令的时间。 */
	float AmmoPredictionHoldStartTime = -1.0f;
	/** 网络计数窗口起点快照与起点时间；两端各自持有自己的一份。 */
	FShooterAmmoPredictionNetCounters AmmoPredictionNetBase;
	float AmmoPredictionNetWindowStartTime = -1.0f;
	/** 连接瞬时速率的窗口采样（平均值 / 峰值）；两端各自持有自己的一份。 */
	FShooterAmmoPredictionNetRateWindow AmmoPredictionNetRateWindow;
	/** 拥有端累计的采样上报次数与窗口起点值（用于把夹具自身流量从 RPC 计数里分离）。 */
	int32 AmmoPredictionClientSampleReports = 0;
	int32 AmmoPredictionNetBaseSampleReports = 0;
	/** 松手时刻的拥有端激活计数，用于断言松手后不再产生新激活。 */
	int32 AmmoPredictionActivationsAtRelease = 0;
	bool bAmmoPredictionObserverVerified = false;
	bool bAmmoPredictionStartupProbeChecked = false;
	float AmmoPredictionStepStartTime = 0.0f;
	float AmmoPredictionSettleStartTime = 0.0f;
	float AmmoPredictionNextReportTime = 0.0f;
	float AmmoPredictionLatestArrivalTime = 0.0f;
	bool bAmmoPredictionSetup = false;
	bool bAmmoPredictionFinished = false;
	/**
	 * 帧率 × 射速矩阵模式（-ShootGameFireCadenceMatrix）。
	 * 复用 Ammo Prediction 夹具的全部驱动与观测，只把步骤表换成矩阵行并按结构性不变量判定。
	 */
	bool bFireCadenceMatrixMode = false;
	bool bAmmoPredictionSettleStarted = false;
	bool bAmmoPredictionStepCommandSent = false;
	bool bAmmoPredictionHoldingFire = false;
	bool bAmmoPredictionReloadSubmitted = false;
	bool bAmmoPredictionReleaseObserved = false;
	bool bAmmoPredictionShotDuringReload = false;
	float AmmoPredictionHoldEndTime = 0.0f;
	/** Hold 中触发 Reload 的时刻；负值表示本步骤不触发换弹。 */
	float AmmoPredictionHoldReloadTime = -1.0f;
	/** 松手后跨过一个完整本地节拍的时刻；早于它不能断言"松手不再产生新激活"。 */
	float AmmoPredictionReleaseSettleTime = 0.0f;
	/** 服务器仅本地持有的阻塞 Tag 是否已挂载。 */
	bool bAmmoPredictionServerTagApplied = false;

	// ---- Weapon Context 定向用例：按住开火期间换枪（只服务 -ShootGameWeaponContextTest） ----

	/** 本步骤起点服务器侧被钉住的请求上下文武器（A）；换枪后**不**跟随当前武器。 */
	TWeakObjectPtr<AShooterWeapon> AmmoPredictionContextWeaponServer;

	/** 本步骤起点拥有端被钉住的请求上下文武器（A）；换枪后**不**跟随当前武器。 */
	TWeakObjectPtr<AShooterWeapon> AmmoPredictionContextWeaponClient;

	/** 服务器下发切枪请求的时刻；负值表示本步骤还没请求过。 */
	float AmmoPredictionSwitchRequestTime = -1.0f;

	/** 服务器观察到 CurrentWeaponActor 真正换人的时刻；负值表示本步骤还没换过。 */
	float AmmoPredictionSwitchCommitTime = -1.0f;

	/**
	 * 装备事务真实提交时刻（Equipment.OnEquippedWeaponChanged 广播时）。
	 *
	 * 它比按 poll 轮询检测到的 AmmoPredictionSwitchCommitTime 精确：轮询间隔 0.1s，
	 * 后者最多晚一个 poll，会让"窗口内被接受的 Activation"被少算。
	 */
	float AmmoPredictionEquipCommitTime = -1.0f;

	/** 装备变化委托是否已绑定；动态委托没有句柄，收口时按绑定标志解绑。 */
	bool bAmmoPredictionEquipDelegateBound = false;

	/** 换枪后服务器的当前武器（B）。 */
	TWeakObjectPtr<AShooterWeapon> AmmoPredictionSwitchTargetWeapon;

	/** 本步骤是否已经下发过切枪请求（只请求一次）。 */
	bool bAmmoPredictionSwitchRequested = false;

	/**
	 * 拥有端是否已经执行过本步骤的切枪输入（只执行一次）。
	 *
	 * 它刻意不复用 AmmoPredictionSubmittedStep：按住指令与切枪指令属于同一个步骤，
	 * 用同一个"已提交"标记会让后到的切枪指令被当成重复指令丢弃。
	 */
	bool bAmmoPredictionSwitchSubmitted = false;

	/** 服务器是否已经观察到"当前武器换人"。 */
	bool bAmmoPredictionSwitchObserved = false;

	/** 服务器第一次从拥有端样本里看到"拥有端已知道换枪"的时刻（= 该样本到达服务器的时刻）。 */
	float AmmoPredictionClientLearnedSwitchTime = -1.0f;

	/** 是否已经观察到拥有端学会了新的当前武器。 */
	bool bAmmoPredictionSwitchLearnedObserved = false;

	/**
	 * 错位窗口内被服务器接受的 Activation 数 = [换枪提交时刻, 拥有端学会换枪时刻] 之间的权威提交数。
	 *
	 * 这是"本用例是否真的验证过目标不变量"的判据：窗口内一次接受都没有时，
	 * 断言"Mismatch == 0"只是没有证据，不能当成通过。
	 */
	int32 AmmoPredictionWindowAcceptedCount = 0;

	/**
	 * 换枪提交之后服务器拒绝掉的权威 Activation 数。
	 *
	 * 服务器拒绝回调不携带 PredictionKey，因此它只作为"错位窗口内确实有请求到达"的旁证：
	 * 换枪提交之后仍然到达的请求只可能来自"拥有端还以为自己在用旧武器"的那段时间。
	 * 它与 WindowAccepted 一起决定本用例是"验证过"还是"没有证据"。
	 */
	int32 AmmoPredictionRejectsAfterSwitchCommit = 0;

	/** 按 PredictionKey 对齐后两端武器上下文不一致的键数；这是本用例的核心失败条件。 */
	int32 AmmoPredictionContextMismatchKeyCount = 0;

	/** 按 PredictionKey 成功对齐的键数；夹具前提要求它 > 0，否则是证据缺口而不是通过。 */
	int32 AmmoPredictionContextMatchedKeyCount = 0;

	/** 只在拥有端出现 / 只在权威端出现的键数，用于区分"没对齐"与"真的不一致"。 */
	int32 AmmoPredictionContextClientOnlyKeyCount = 0;
	int32 AmmoPredictionContextAuthorityOnlyKeyCount = 0;

	// ---- Spec Removal / Pool Reuse 定向用例（StepSpecRemovalInFlight / StepPoolReuseRebind） ----

	/** 被移除的武器与它被撤销的 Fire Spec Handle（H1）。 */
	TWeakObjectPtr<AShooterWeapon> AmmoPredictionRemovedWeapon;
	FGameplayAbilitySpecHandle AmmoPredictionRemovedSpecHandle;
	int32 AmmoPredictionRemovedAuthorityShotsBefore = INDEX_NONE;
	int32 AmmoPredictionRemovedAmmoBefore = INDEX_NONE;

	/** 移除是否已经下发、预定时刻、以及"移除后 Spec 确实不存在"的判据。 */
	bool AmmoPredictionRemovalIssued = false;
	float AmmoPredictionRemovalTime = -1.0f;
	bool AmmoPredictionRemovalSucceeded = false;

	/** 窗口内拥有端最大的本地 Pending：证明移除发生时请求仍在途、债务确实存在。 */
	int32 AmmoPredictionMaxClientPendingDuringWindow = 0;

	/** 复用到的武器（期望就是同一个 pooled Actor）与它的新 Spec Handle（H2）。 */
	TWeakObjectPtr<AShooterWeapon> AmmoPredictionReusedWeapon;
	FGameplayAbilitySpecHandle AmmoPredictionReusedSpecHandle;
	bool AmmoPredictionReusedSameActor = false;
	int32 AmmoPredictionReusedAuthorityShotsBefore = INDEX_NONE;
	int32 AmmoPredictionReusedAmmoBefore = INDEX_NONE;
	bool AmmoPredictionReuseShotSubmitted = false;
	float AmmoPredictionReuseSettleStartTime = 0.0f;

	void PollServerState();
	void PollClientState();
	void HandleActorSpawned(AActor* SpawnedActor);
	void FailTest(const FString& Reason);

	// ---- 普通 Dedicated 回归失败来源取证：只增加身份与只读旁路观测，不改变任何 Stage 时序 ----

	/** 本 Coordinator 的测试身份：Coordinator / Owner / PlayerState / PlayerId / Client 索引 / Pawn。 */
	FString DescribeTestIdentityForTest() const;

	/** 当前 Stage 名；按既有门控只读推导，不新增状态机、不改变推进条件。 */
	const TCHAR* DescribeTestStageForTest() const;
	const TCHAR* DescribeTestStageOnClientForTest() const;
	const TCHAR* DescribeTestStageOnServerForTest() const;

	AShooterPlayerState* GetTestPlayerStateForTest() const;
	int32 GetTestPlayerIdForTest() const;

	/** 服务器侧：对手玩家的 PlayerId。跨端身份只用 PlayerId；本机对象名不可跨端比较。 */
	int32 GetOpponentPlayerIdForTest() const;
	int32 GetTestClientIndexForTest() const;

	/** 拥有端一次开火的只读上下文快照：Spec 解析 / 输入采集 / 本地阻塞 / 预测预算。 */
	void LogOwnerFireAttemptForTest(const TCHAR* Marker, AShooterCharacter* Character, AShooterWeapon* Weapon, int32 AttemptIndex);

	/** 就绪等待只记一次，避免同一 Stage 内重复打印。 */
	bool bClientLoggedOwnerSingleShotNotReady = false;

	/**
	 * Fire 激活 / 失败旁路观测：复用 GAS 既有委托，只记录事实。
	 * 服务器端用它把「服务器是否收到请求 / 是否 Reject / FailureTags」与客户端尝试对齐。
	 */
	void BindTestFireObservers(UAbilitySystemComponent* AbilitySystemComponent);
	void HandleTestFireActivated(UGameplayAbility* Ability);
	void HandleTestFireFailed(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureTags);

	TWeakObjectPtr<UAbilitySystemComponent> TestFireObservedASC;
	FDelegateHandle TestFireActivatedHandle;
	FDelegateHandle TestFireFailedHandle;
	int32 TestFireActivatedCount = 0;
	int32 TestFireFailedCount = 0;
	int32 TestFireLastActivatedKey = 0;
	int32 TestFireSuppressedFailureCount = 0;
	float TestFireLastFailureTime = -1.0f;
	FString TestFireLastFailureTags;

	/** 拥有端开火尝试序号与单次尝试基线：把 Client 尝试与 Server 观测按序号对齐。 */
	int32 OwnerFireAttemptIndex = 0;
	int32 OwnerFireAttemptActivatedBase = 0;
	int32 OwnerFireAttemptFailedBase = 0;
	float OwnerFireAttemptStartTime = -1.0f;
	bool bOwnerFireAttemptResultLogged = false;

	/** Stage 起点：客户端进入 OwnerSingleShot 的时刻；服务器布置单发基线的时刻。 */
	float ClientOwnerSingleShotStageStartTime = -1.0f;
	float ServerOwnerSingleShotArmTime = -1.0f;

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedWeapon();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedProjectile();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedSwitch(AShooterWeapon* ActiveWeapon, AShooterWeapon* CurrentWeapon, bool bRemoteCurrentWeaponVisible);

	UFUNCTION(Server, Reliable)
	void ServerReportOwnerAmmoReplicated();

	UFUNCTION(Server, Reliable)
	void ServerReportNonOwnerAmmoHidden();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedDamage();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedDeath();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedRespawn();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedMatchState(uint8 TeamId, int32 Kills, int32 Deaths, int32 TeamScore);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedRemoteAim(float PitchN, float ExpectedPitchN);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedGasLifecycle();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedFireAbilityGrant(int32 OwnerFireSpecCount, int32 OwnerHeldWeaponCount, bool bRemoteFireSpecsHidden);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedReloadEquipAbilityGrant(int32 OwnerReloadSpecCount, bool bRemoteReloadSpecsHidden,
		int32 OwnerEquipSpecCount, bool bRemoteEquipSpecsHidden);

	UFUNCTION(Server, Reliable)
	void ServerReportClientTriggeredReload(int32 RequestId);

	UFUNCTION(Server, Reliable)
	void ServerReportClientTriggeredReloadSwitch();

	UFUNCTION(Server, Reliable)
	void ServerReportClientTriggeredReloadSwitchBack();

	UFUNCTION(Server, Reliable)
	void ServerReportClientTriggeredFireAfterReload(bool bReloadingTagPresentAtInput);

	UFUNCTION(Server, Reliable)
	void ServerReportClientStoppedFireAfterReload(int32 OwnerFeedbackDelta, int32 OwnerConfirmationDelta);

	UFUNCTION(Server, Reliable)
	void ServerReportClientTriggeredEquipSingleReject();

	/**
	 * P1 换弹中开火报告：客户端在换弹状态下按下开火后回报本地观测。
	 * FireCase：1 = 客户端已知 State.Reloading（8A，要求本地预测增量为 0）；
	 *           2 = 客户端尚未收到 State.Reloading（8B，允许有限的纯本地预测表现）。
	 */
	UFUNCTION(Server, Reliable)
	void ServerReportReloadFireResult(int32 RequestId, int32 FireCase, int32 PredictedDelta,
		bool bKnownBlockerObserved, bool bTargetStable, bool bClientConverged);

	UFUNCTION(Server, Reliable)
	void ServerReportFullAutoReleased(int32 BulletCountAfterRelease, int32 OwnerFeedbackDelta,
		float MinimumFeedbackInterval, bool bLocalFeedbackSettled, bool bTargetStable);

	/**
	 * Invariant 2 半自动快速连点证据：客户端在真实按下 / 释放输入下跑完一整轮连点后上报本机观测。
	 * 服务器用同窗口的权威弹药、弹丸与权威射击增量做一一对应比较，
	 * 并要求本地可见表现的最小间隔不低于武器 RefireRate。
	 */
	UFUNCTION(Server, Reliable)
	void ServerReportSemiAutoRapidClick(int32 ClicksAttempted, int32 OwnerFeedbackDelta,
		float MinimumFeedbackInterval, float RefireRate, bool bTargetStable);

	/**
	 * 半自动连点窗口的分通道表现证据。
	 * 聚合计数只能证明"至少提交了一项表现"，无法发现枪口 / 声音 / 后坐力 / Montage 中
	 * 某一路在本机静默失效；因此四路各自上报增量，服务器逐路断言。
	 */
	UFUNCTION(Server, Reliable)
	void ServerReportSemiAutoRapidClickChannels(int32 MontageDelta, int32 MuzzleDelta, int32 SoundDelta,
		int32 RecoilDelta, bool bTargetStable);

	UFUNCTION(Server, Reliable)
	void ServerReportOwnerAcceptedShotEvidence(
		int32 OwnerFeedbackDelta,
		int32 MontageDelta,
		int32 MuzzleDelta,
		int32 SoundDelta,
		int32 RecoilDelta,
		int32 ConfirmationDelta,
		bool bFeedbackBeforeConfirmation,
		bool bTargetStable);

	/**
	 * P1-D 远端第三人称确认表现证据：观测端上报“另一名玩家武器”的确认表现增量。
	 * 观测源是生产计数器 AShooterWeapon::RemoteConfirmedFeedbackCount，不在协调器里复制实现。
	 *
	 * 上报只在「被观测批次已被权威冻结」之后发生，参数里的两份身份（TargetPlayerId 与四条增量）
	 * 描述的都是同一批 Shot；服务器不再在收到上报的这一刻现场读取权威射击计数。
	 */
	UFUNCTION(Server, Reliable)
	void ServerReportRemoteConfirmedFeedback(int32 TargetPlayerId, int32 Count, int32 MontageCount, int32 MuzzleCount,
		int32 SoundCount, bool bTargetStable);

	/**
	 * 批次开始边界 ack（TEST-ONLY 就绪握手，不进入生产协议）：观测端已经为对手当前武器的
	 * 确认表现建立 Confirmed / Montage / Muzzle / Sound 四条基线，并声明该基线早于对手批次第一发。
	 * 服务器在该 ack 到达后才放开对手的 FullAuto 开火许可。
	 */
	UFUNCTION(Server, Reliable)
	void ServerReportRemoteBaselineReady(int32 TargetPlayerId);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedCancelSwitch(AShooterWeapon* CurrentWeapon);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedGasRespawn();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedInventory(int32 WeaponCount, AShooterWeapon* ActiveWeapon,
		bool bRemoteInventoryHidden, bool bInventoryComponentInitialized);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedPickupAuthority();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedInventoryDeathClear();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedInventoryRespawnEmpty();

	/** 记录角色 OnDamaged 事件值（HUD 事件链证据）。 */
	UFUNCTION()
	void HandleDamagedEvent(float LifePercent);

	/** 记录 ASC Health 属性变化（HUD 事件链源头，跨重生无竞态）。 */
	void HandleClientHealthAttributeChanged(const FOnAttributeChangeData& ChangeData);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedGasHealthInit();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedGasHealthDamage();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedGasHealthRespawn(bool bFullHealthHudEvent);

	AShooterCharacter* GetShooterCharacter() const;
	AShooterWeapon* GetCurrentWeapon(AShooterCharacter* Character) const;
	int32 CountProjectilesForInstigator(APawn* ProjectileInstigator) const;
	AController* GetOpponentController() const;

	/** P1-D 观测端：解析另一名玩家当前装备的武器，作为第三人称确认表现的观测源。 */
	AShooterWeapon* FindRemoteObservedWeapon(AShooterCharacter* LocalCharacter) const;

	/** P1-D 服务器侧：解析对手玩家，用于读取其武器在同一批次内的权威射击计数。 */
	AShooterCharacter* GetOpponentCharacter() const;

	/**
	 * 服务器侧：解析另一名玩家的 Coordinator。两名玩家的 Coordinator 都在同一台服务器上，
	 * 跨端身份只用 PlayerId，对象引用只用于服务器内部的批次状态读取（冻结起点 / 终点）。
	 */
	AShooterNetworkTestCoordinator* FindOpponentCoordinator() const;

	/** 服务器侧：对手是否存在真实的远端观察路径（远端客户端才会运行 PollClientState 的观测分支）。 */
	bool OpponentHasRemoteObservationPath() const;

	/** 服务器侧：推进本玩家批次的开始边界；观察端基线未就绪时不得放开 FullAuto 开火许可。 */
	void RefreshRemoteBatchStartGate();

	// ---- 远端批次异步顺序 / exactly-once 的 TEST-ONLY 入口（public 区已声明对应 API）----

	/**
	 * 服务器侧：Target 的批次 Arm 后，在同一个 tick 内直接通知观察端 Coordinator 建立四路基线。
	 * 观察端基线因此锚定在"被观测批次的起点"而不是"本机自己的起点"。
	 */
	void NotifyObservedBatchArmed();

	/**
	 * 服务器侧：Target 的射击动作结束时（收到拥有者释放上报），在同一个 tick 内通知观察端
	 * Coordinator 收口窗口。该信号必须早于下一阶段（SwitchCancel）的第一发表现。
	 */
	void NotifyObservedBatchFireEnded();

	/** 观测端四路表现增量（相对 Arm 时建立的基线）；任一生产计数器不可用时保留为 INDEX_NONE。 */
	FShooterRemotePresentationDeltasForTest CaptureRemotePresentationDeltas(AShooterCharacter* LocalCharacter) const;

	/** 5B 测试辅助：把指定 WeaponActor 的权威弹药直接设置为测试起点值。 */
	bool SetReloadTestAmmo(AShooterWeapon* Weapon, int32 MagazineAmmo, int32 ReserveAmmo);

	/** 5B 测试辅助：返回当前 PlayerState 是否有一个活动 GA_Fire。 */
	bool HasActiveFireAbility(AShooterCharacter* Character) const;

	/**
	 * 一把武器对应的 Fire Ability 授予快照（DEV 观察）。
	 * 它把"武器 → Spec → 主实例"显式暴露出来，供测试断言 SourceObject / Handle / 实例隔离。
	 */
	struct FShooterFireSpecGrantForTest
	{
		/** Spec.SourceObject 指向的 WeaponActor；不是武器时为空。 */
		TWeakObjectPtr<const AShooterWeapon> Weapon;

		/** 该武器那份 Fire Spec 的 Handle；同一 ASC 内不同武器必须不同。 */
		FGameplayAbilitySpecHandle Handle;

		/** InstancedPerActor 主实例；未激活过时为空（此时按 Spec->Ability CDO 回落）。 */
		const UShooterGameplayAbility_Fire* PrimaryInstance = nullptr;
	};

	/** 收集该 ASC 上所有 GA_Fire Spec 的授予快照；ASC 为空时返回 false。 */
	bool CollectFireSpecGrantsForTest(const UAbilitySystemComponent* AbilitySystemComponent,
		TArray<FShooterFireSpecGrantForTest>& OutGrants) const;

	/**
	 * 一次 Fire Activation 的武器上下文（跨 Fire 实例聚合后的扁平形式）。
	 * 实例现在是"每把武器一份"，因此"全局唯一"的观测必须先按实例收集再求和 / 对齐。
	 */
	struct FShooterFireActivationContextForTest
	{
		int32 PredictionKey = 0;
		TWeakObjectPtr<const AShooterWeapon> Weapon;
		FName WeaponId;
		float LocalTime = -1.0f;
		bool bAuthority = false;
	};

	/** 一个角色全部 Fire 实例上的 DEV 观测聚合。 */
	struct FShooterFireContextAggregateForTest
	{
		/** Fire Spec 总数（含 SourceObject 未解析的）。 */
		int32 SpecCount = 0;

		/** SourceObject 为空或不是 WeaponActor 的 Spec 数：复制未解析或孤儿 Spec，必须为 0。 */
		int32 UnresolvedSourceSpecCount = 0;

		int32 InstanceCount = 0;
		int32 UnresolvedRecordCount = INDEX_NONE;
		int32 OwnerConfirmedReplayRequestCount = INDEX_NONE;
		int32 WeaponContextMismatchVerdictCount = INDEX_NONE;
		int32 AuthorityRejectCount = INDEX_NONE;
		TArray<FShooterFireActivationContextForTest> ActivationContexts;
	};

	/** 聚合该角色全部 Fire 实例的 DEV 观测；没有任何 Fire Spec 时返回 false。 */
	bool AggregateFireContextForTest(AShooterCharacter* Character, FShooterFireContextAggregateForTest& OutAggregate) const;

	/** 指定武器对应的 Fire Ability 主实例；没有对应 Spec 时返回 nullptr。 */
	const UShooterGameplayAbility_Fire* GetFireAbilityInstanceForWeaponForTest(
		const AShooterPlayerState* ShooterPlayerState, const AShooterWeapon* Weapon) const;

	/** 当前武器对应的 Fire Ability 主实例（每把武器各有一份 Spec，因此必须按武器解析）。 */
	const UShooterGameplayAbility_Fire* GetFireAbilityInstanceForTest(AShooterCharacter* Character) const;

	/**
	 * 4C 测试辅助：查询服务器对 GA_Fire 的权威激活结论。
	 * GA_Fire 为 LocalPredicted 后，服务器对非本机控制玩家调用公开 TryActivateAbility
	 * 会经 bAllowRemoteActivation 把请求转回拥有者客户端并返回 true，拿不到校验结论；
	 * 因此直接调用服务器实例的 CanActivateAbility。返回 true 表示服务器允许激活。
	 */
	bool CanServerActivateFireAbility(UAbilitySystemComponent* AbilitySystemComponent) const;

	/** 5C 测试辅助：返回当前 PlayerState 是否有一个活动 GA_Equip。 */
	bool HasActiveEquipAbility(AShooterCharacter* Character) const;

	/** 5B 测试辅助：返回当前 PlayerState 是否有一个活动 GA_Reload。 */
	bool HasActiveReloadAbility(AShooterCharacter* Character) const;

	/** DisconnectCleanup 专用：在断线前主动激活一次 GA_Reload。 */
	void TriggerDisconnectReload();

	/**
	 * DisconnectCleanup 专用：请拥有者客户端按生产输入路径发起换弹。
	 * GA_Reload 是 LocalPredicted，服务器不能直接为远端 Pawn 激活它。
	 */
	UFUNCTION(Client, Reliable)
	void ClientTriggerDisconnectReload();

	/** DisconnectCleanup 专用：在有限窗口内重按换弹，直到本地预测窗口成立或放弃。 */
	void TrySubmitDisconnectReloadInput();
	/** DisconnectCleanup 专用：延长目标武器 EquipDuration 后激活 GA_Equip。 */
	bool TriggerLongEquip(AShooterCharacter* Character, const TCHAR* Context);

	/** Equip 清理会话：一名玩家保持活动 GA_Equip，等待脚本主动断线。 */
	void TriggerDisconnectEquip();

	/** Equip 清理会话：另一名玩家在活动 GA_Equip 提交前受到致死伤害。 */
	void TriggerEquipDeath();
	void VerifyEquipDeathCleanup();

	FTimerHandle PollTimer;
	FTimerHandle CleanupAbilityTimer;
	FTimerHandle EquipDeathVerifyTimer;
	bool bCleanupAbilityScheduled = false;
	/** 断线前换弹前置条件的有限重试计数；预测换弹需要等服务器实例或复制到达。 */
	int32 DisconnectReloadRetryCount = 0;
	/** 是否已经请拥有者客户端发起断线换弹；只请求一次。 */
	bool bDisconnectReloadRequestedToClient = false;
	/** 拥有者客户端的断线换弹重按状态。 */
	bool bDisconnectReloadInputActive = false;
	int32 DisconnectReloadInputAttempts = 0;
	float DisconnectReloadInputDeadline = 0.0f;
	FDelegateHandle ActorSpawnedHandle;

	UPROPERTY(Replicated)
	int32 ReloadInputRequestId = 0;

	UPROPERTY(Replicated)
	bool bServerReadyForReloadSwitch = false;

	UPROPERTY(Replicated)
	bool bServerReadyForReloadSwitchBack = false;

	UPROPERTY(Replicated)
	bool bServerReadyForFireAfterReload = false;

	UPROPERTY(Replicated)
	bool bServerReadyForStopFireAfterReload = false;

	UPROPERTY(Replicated)
	bool bServerReadyForEquipSingleReject = false;

	/** P1 换弹中开火阶段：0=未开始，2=同帧换弹加开火（8B），1=等到本地 State.Reloading 再开火（8A）。 */
	UPROPERTY(Replicated)
	int32 ReloadFirePhase = 0;

	UPROPERTY(Replicated)
	int32 ReloadFireRequestId = 0;

	UPROPERTY(Replicated)
	bool bServerReadyToSwitch = false;

	UPROPERTY(Replicated)
	bool bServerReadyToFire = false;

	UPROPERTY(Replicated)
	bool bServerReadyForFullAuto = false;

	/** 半自动快速连点阶段的起跑许可：客户端收到后开始真实连点输入。 */
	UPROPERTY(Replicated)
	bool bServerReadyForSemiAutoRapidClick = false;

	/**
	 * 批次开始边界（TEST-ONLY）：批次起点已建立，但观察端四路基线还没就绪时保持 false，
	 * 客户端不得开始本次 FullAuto 批次。它表达的是"观察端已经准备好"，不是"等一会儿"。
	 */
	UPROPERTY(Replicated)
	bool bServerOwnBatchStartAllowed = false;

	/**
	 * 观察端基线许可（TEST-ONLY）：本客户端正在观测的那一批次已经 Arm。
	 * 基线必须锚定在「被观测批次的 Arm」而不是本机自己的 Arm：本机的 Arm 可能早于对手
	 * 上一阶段（单发）表现的到达，那样会把上一发算进本批次。Arm 由服务器在对手单发证据
	 * 落地之后才发出，因此该时刻一定晚于对手上一发表现的组播送达。
	 */
	UPROPERTY(Replicated)
	bool bServerObservedBatchArmed = false;

	/**
	 * 观察端窗口收口许可（TEST-ONLY）：本客户端正在观测的对手批次，其射击动作已经结束
	 * （服务器收到拥有者释放上报），观察端可以就地冻结四路增量。
	 *
	 * 为什么边界落在"释放上报"而不是"静默期验证之后"：下一阶段（SwitchCancel）的开火许可
	 * 在静默期验证的同一拍放行，若等到那时才通知，下一发的表现可能已经先到达观察端。
	 * 释放上报比批次最后一发的权威提交至少晚一个"释放等待"，因此批次自己的表现一定已经到达，
	 * 而下一阶段的第一发至少还要再等一个静默期——两端都留有结构性余量，不依赖固定等待。
	 * 权威终点仍在静默期验证后冻结，两者由残留检查保证相等。
	 */
	UPROPERTY(Replicated)
	bool bServerObservedBatchFireEnded = false;

	/**
	 * 被观测批次的冻结边界（同一事实的复制镜像，唯一 Source of Truth 在 Target 的服务器实例上）。
	 * 它比收口许可晚一个静默期，因此只用于取证与事后核对，不参与观察端窗口的关闭时刻。
	 */
	UPROPERTY(Replicated)
	bool bServerObservedBatchFrozen = false;

	/** 被观测批次的冻结边界（同一事实的复制镜像，唯一 Source of Truth 在 Target 的服务器实例上）。 */
	UPROPERTY(Replicated)
	int32 ServerObservedBatchAuthorityStart = INDEX_NONE;

	UPROPERTY(Replicated)
	int32 ServerObservedBatchAuthorityEnd = INDEX_NONE;

	UPROPERTY(Replicated)
	int32 ServerObservedBatchExpectedDelta = INDEX_NONE;

	UPROPERTY(Replicated)
	bool bServerReadyForSwitchCancel = false;

	UPROPERTY(Replicated)
	TObjectPtr<AShooterWeapon> WeaponBeforeSwitch;

	UPROPERTY(Replicated)
	bool bRequireRemoteMontage = true;

	UPROPERTY(Replicated)
	bool bRequireRemoteCurrentWeapon = true;

	float TestStartTime = 0.0f;
	int32 InitialBulletCount = INDEX_NONE;
	int32 BulletCountAfterFire = INDEX_NONE;
	bool bClientObservedWeapon = false;
	bool bClientObservedProjectile = false;
	bool bClientObservedSwitch = false;
	bool bClientObservedOwnerAmmo = false;
	bool bClientObservedNonOwnerAmmoHidden = false;
	bool bClientObservedDamage = false;
	bool bClientObservedDeath = false;
	bool bClientObservedRespawn = false;
	bool bClientObservedMatchState = false;
	bool bClientObservedRemoteAim = false;
	bool bClientObservedRemoteMontage = false;
	bool bClientTriggeredFire = false;
	bool bClientReportedOwnerAcceptedShot = false;
	float ClientOwnerAcceptedShotStartTime = 0.0f;
	/** 拥有者本地动作互斥导致本次开火输入等待的起始时间；0 表示没有等待。 */
	float ClientOwnerAcceptedShotLocalWaitStart = 0.0f;
	TWeakObjectPtr<AShooterWeapon> ClientOwnerAcceptedShotWeapon;
	int32 ClientOwnerFeedbackBefore = INDEX_NONE;
	int32 ClientOwnerMontageBefore = INDEX_NONE;
	int32 ClientOwnerMuzzleBefore = INDEX_NONE;
	int32 ClientOwnerSoundBefore = INDEX_NONE;
	int32 ClientOwnerRecoilBefore = INDEX_NONE;
	int32 ClientOwnerConfirmationBefore = INDEX_NONE;
	bool bClientTriggeredSwitch = false;
	int32 LastObservedReloadInputRequestId = 0;
	bool bClientTriggeredReloadSwitch = false;
	bool bClientTriggeredReloadSwitchBack = false;
	bool bClientReportedProjectile = false;
	bool bClientTriggeredFireAfterReload = false;
	bool bClientStoppedFireAfterReload = false;
	int32 FireAfterReloadOwnerFeedbackBefore = 0;
	int32 FireAfterReloadOwnerConfirmationBefore = 0;
	float FireAfterReloadStopReadyTime = 0.0f;
	bool bClientTriggeredEquipSingleReject = false;

	// ---- P1 换弹中开火：客户端侧跟踪 ----
	int32 LastObservedReloadFirePhase = 0;
	bool bClientReloadFireInputSent = false;
	bool bClientReloadFireTagSeen = false;
	bool bClientReportedReloadFire = false;
	float ClientReloadFireSettleTime = 0.0f;
	int32 ClientReloadFirePredictedBefore = 0;
	TWeakObjectPtr<AShooterWeapon> ClientReloadFireTargetWeapon;
	bool bClientReportedSwitch = false;
	bool bClientReportedOwnerAmmo = false;
	bool bClientReportedNonOwnerAmmoHidden = false;
	bool bClientReportedDamage = false;
	bool bClientReportedDeath = false;
	bool bClientReportedRespawn = false;
	bool bClientReportedMatchState = false;
	bool bClientSetAimPitch = false;
	bool bClientReportedRemoteAim = false;
	bool bServerGasLifecycleChecked = false;
	bool bServerGasOwnerOk = false;
	bool bServerGasAvatarOk = false;
	bool bServerGasConnectionOk = false;
	bool bNpcGasLifecycleChecked = false;
	bool bNpcGasLifecycleOk = false;
	bool bNpcAiSuppressed = false;
	bool bServerGasRespawnChecked = false;
	bool bServerGasRespawnOk = false;
	bool bClientObservedGasLifecycle = false;
	bool bClientObservedGasRespawn = false;
	bool bClientReportedGasLifecycle = false;
	bool bClientReportedGasRespawn = false;
	bool bServerGasHealthInitChecked = false;
	bool bServerGasHealthInitOk = false;
	float InitialAttributeHealth = 0.0f;
	float ExpectedPartialHealth = 0.0f;
	bool bServerGasDamageChecked = false;
	bool bServerGasDamageOk = false;
	bool bServerGasDeathChecked = false;
	bool bServerGasDeathOk = false;
	bool bNpcGasHealthInitOk = false;
	bool bNpcGasDeathOk = false;
	bool bClientObservedGasHealthInit = false;
	bool bClientObservedGasHealthDamage = false;
	bool bClientObservedGasHealthRespawn = false;
	bool bClientReportedGasHealthInit = false;
	bool bClientReportedGasHealthDamage = false;
	bool bClientReportedGasHealthRespawn = false;
	bool bClientObservedFullHealthHudEvent = false;

	/** 4A 观测：服务器 / NPC / 重生三个生命周期中 Fire Ability Spec 均有且只有一个。 */
	bool bServerFireGrantChecked = false;
	bool bServerFireGrantOk = false;
	bool bNpcFireGrantOk = false;
	bool bServerFireRespawnGrantOk = false;
	bool bClientObservedFireGrant = false;
	bool bClientReportedFireGrant = false;
	FGameplayAbilitySpecHandle ServerFireAbilityHandle;
	int32 ServerFireAbilityCount = INDEX_NONE;

	/** 5A 观测：玩家 Reload / Equip Ability 在出生与重生后均有且只有一个 Spec，重复授予不增长。 */
	bool bServerReloadEquipGrantChecked = false;
	bool bServerReloadEquipGrantOk = false;
	bool bServerReloadEquipRespawnGrantOk = false;
	bool bClientObservedReloadEquipGrant = false;
	bool bClientReportedReloadEquipGrant = false;
	FGameplayAbilitySpecHandle ServerReloadAbilityHandle;
	FGameplayAbilitySpecHandle ServerEquipAbilityHandle;
	int32 ServerReloadAbilityCount = INDEX_NONE;
	int32 ServerEquipAbilityCount = INDEX_NONE;

	/**
	 * 5C 观测：GA_Equip 在初始切枪、取消 Reload 切枪与切回阶段均被服务器激活，
	 * 提交后 CurrentWeaponActor 与 Inventory Entry 一致。
	 */
	bool bEquipInitialCommitConsistent = false;
	bool bEquipCancelReloadActiveObserved = false;
	bool bEquipSwitchBackActiveObserved = false;

	bool bEquipSingleRejectPhaseTriggered = false;
	bool bEquipSingleRejectVerified = false;
	float EquipSingleRejectCheckTime = 0.0f;
	bool bEquipRejectDeadVerified = false;

	/** 5B 观测：FullMagazine / Transfer / EquipCancel / NoReserve / DeathCancel 五个换弹事务边界。 */
	bool bReloadFullRejectPhaseTriggered = false;
	bool bReloadFullRejectVerified = false;
	bool bClientTriggeredReload = false;
	int32 ReloadMagazineBeforeFullReject = INDEX_NONE;
	int32 ReloadReserveBeforeFullReject = INDEX_NONE;
	float ReloadFullRejectCheckTime = 0.0f;

	bool bReloadTransferPhaseTriggered = false;
	bool bReloadTransferActiveObserved = false;
	bool bReloadTransferVerified = false;
	int32 ReloadMagazineBeforeTransfer = INDEX_NONE;
	int32 ReloadReserveBeforeTransfer = INDEX_NONE;
	int32 ExpectedReloadTransfer = INDEX_NONE;
	int32 ReloadMagazineAfterTransfer = INDEX_NONE;
	int32 ReloadReserveAfterTransfer = INDEX_NONE;
	float ReloadTransferCheckTime = 0.0f;

	/** 5B 弱网 Fire-after-Reload：Reload 完成后单次 Fire 必须到达服务器且只激活 / 射击 / 扣弹一次。 */
	bool bFireAfterReloadPhaseTriggered = false;
	bool bFireAfterReloadActiveObserved = false;
	bool bFireAfterReloadSingleShotVerified = false;
	bool bClientTriggeredStopFireAfterReload = false;
	bool bFireAfterReloadQuiescentVerified = false;
	bool bFireAfterReloadStaleTagObserved = false;
	bool bFireAfterReloadOwnerFeedbackVerified = false;
	int32 FireAfterReloadMagazineBefore = INDEX_NONE;
	int32 FireAfterReloadProjectileBefore = INDEX_NONE;
	float FireAfterReloadQuiescenceCheckTime = 0.0f;

	bool bReloadCancelEquipPhaseTriggered = false;
	bool bReloadCancelEquipActiveObserved = false;
	bool bReloadCancelEquipVerified = false;
	int32 ReloadMagazineBeforeCancelEquip = INDEX_NONE;
	int32 ReloadReserveBeforeCancelEquip = INDEX_NONE;

	bool bReloadSwitchBackPhaseTriggered = false;
	bool bReloadSwitchBackVerified = false;
	float ReloadCancelEquipCheckTime = 0.0f;
	float ReloadSwitchBackCheckTime = 0.0f;

	bool bReloadNoReservePhaseTriggered = false;
	bool bReloadNoReserveVerified = false;
	int32 ReloadMagazineBeforeNoReserve = INDEX_NONE;
	int32 ReloadReserveBeforeNoReserve = INDEX_NONE;
	float ReloadNoReserveCheckTime = 0.0f;

	bool bReloadCancelDeathPhaseTriggered = false;
	bool bReloadCancelDeathActiveObserved = false;
	bool bReloadCancelDeathAmmoUnchanged = false;
	bool bReloadCancelDeathVerified = false;
	int32 ReloadMagazineBeforeCancelDeath = INDEX_NONE;
	int32 ReloadReserveBeforeCancelDeath = INDEX_NONE;

	/** 4B 观测：单次按下只生成一颗弹丸，全自动保持期间只有一个活动 GA_Fire，释放后计时器无残留。 */
	int32 ProjectileSpawnCount = 0;
	bool bSingleProjectileVerified = false;
	bool bOwnerAcceptedShotEvidenceVerified = false;
	int32 AuthorityShotsBeforeSingleFire = INDEX_NONE;
	int32 ProjectileCountBeforeSingleFire = INDEX_NONE;
	bool bFullAutoPhaseTriggered = false;
	bool bFullAutoActiveObserved = false;
	bool bClientReportedFullAutoRelease = false;
	bool bFullAutoReleaseVerified = false;
	bool bFullAutoLocalCadenceVerified = false;
	bool bFullAutoAuthorityExactlyOnceVerified = false;
	bool bFullAutoQuiescentConfirmed = false;
	bool bClientTriggeredFullAuto = false;
	bool bClientStoppedFullAuto = false;
	bool bClientReportedFullAuto = false;
	float FullAutoReleaseWaitStartTime = 0.0f;
	int32 BulletCountBeforeFullAuto = INDEX_NONE;
	int32 ProjectileCountBeforeFullAuto = INDEX_NONE;
	int32 ProjectileCountAfterRelease = INDEX_NONE;
	int32 AmmoAfterRelease = INDEX_NONE;
	int32 ClientBulletCountAfterRelease = INDEX_NONE;
	float FullAutoReleaseCheckTime = 0.0f;
	TWeakObjectPtr<AShooterWeapon> ClientFullAutoTargetWeapon;
	int32 ClientFullAutoOwnerFeedbackBefore = INDEX_NONE;

	// ---- Invariant 2 半自动快速连点：服务器侧窗口快照与验收 ----
	bool bSemiAutoRapidClickPhaseTriggered = false;
	bool bClientReportedSemiAutoRapidClick = false;
	bool bSemiAutoRapidClickVerified = false;
	bool bSemiAutoRapidInputVerified = false;
	bool bSemiAutoLocalCadenceVerified = false;
	bool bSemiAutoAuthorityExactlyOnceVerified = false;
	int32 SemiAutoRapidAuthorityShotsBefore = INDEX_NONE;
	int32 SemiAutoRapidRejectsBefore = INDEX_NONE;
	int32 SemiAutoRapidProjectilesBefore = INDEX_NONE;
	int32 SemiAutoRapidAmmoBefore = INDEX_NONE;
	int32 SemiAutoRapidClicksObserved = INDEX_NONE;
	int32 SemiAutoRapidAuthorityShotDelta = INDEX_NONE;
	int32 SemiAutoRapidFeedbackDelta = INDEX_NONE;
	float SemiAutoRapidMinFeedbackInterval = -1.0f;
	float SemiAutoRapidClickStartTime = 0.0f;
	bool bSemiAutoRapidChannelsVerified = false;
	int32 SemiAutoRapidMontageDelta = INDEX_NONE;
	int32 SemiAutoRapidMuzzleDelta = INDEX_NONE;
	int32 SemiAutoRapidSoundDelta = INDEX_NONE;
	int32 SemiAutoRapidRecoilDelta = INDEX_NONE;
	TWeakObjectPtr<AShooterWeapon> SemiAutoRapidTargetWeapon;

	// ---- Invariant 2 半自动快速连点：客户端侧真实输入与观测窗口 ----
	bool bClientSemiAutoBurstStarted = false;
	bool bClientSemiAutoBurstReported = false;
	int32 SemiAutoBurstClicksDone = 0;
	int32 SemiAutoBurstTargetClicks = 0;
	int32 SemiAutoBurstFeedbackBefore = INDEX_NONE;
	int32 SemiAutoBurstMontageBefore = INDEX_NONE;
	int32 SemiAutoBurstMuzzleBefore = INDEX_NONE;
	int32 SemiAutoBurstSoundBefore = INDEX_NONE;
	int32 SemiAutoBurstRecoilBefore = INDEX_NONE;
	float SemiAutoBurstNextClickTime = 0.0f;
	float SemiAutoBurstSettleStartTime = 0.0f;
	TWeakObjectPtr<AShooterWeapon> ClientSemiAutoBurstWeapon;

	// ---- P1-D 远端第三人称确认表现：被比较的权威 Shot 与观察端四路表现必须描述严格同一批 Shot ----
	/**
	 * 服务器侧：本玩家作为 Target 的 FullAuto 批次（起点复用 AuthorityShotsBeforeFullAuto，
	 * 终点在 Action Stage 真正结束时冻结）。ExpectedDelta 冻结后不再受任何后续阶段影响。
	 */
	FShooterRemoteFullAutoBatchForTest OwnRemoteFullAutoBatch;
	/** 服务器侧：全自动窗口起点，本玩家武器的权威射击计数（同时是批次的 AuthorityStart 来源）。 */
	int32 AuthorityShotsBeforeFullAuto = INDEX_NONE;
	/** 批次身份：本批次统计的是这把武器（Arm 时固定），用于与观察端观测到的武器交叉核对。 */
	TWeakObjectPtr<AShooterWeapon> OwnRemoteBatchWeapon;
	/** 服务器侧：批次开始边界状态与关键时序证据。 */
	FShooterRemoteBatchStartGateForTest OwnRemoteBatchStartGate;
	float RemoteBatchArmServerTime = 0.0f;
	float RemoteBatchStartAllowedServerTime = 0.0f;
	bool bOwnBatchObserverBaselineReadySeen = false;
	float OwnBatchObserverBaselineReadyServerTime = 0.0f;
	bool bOwnBatchFirstAuthorityShotSeen = false;
	float OwnBatchFirstAuthorityShotServerTime = 0.0f;
	/** 下一阶段第一发权威 Shot：用于证明它落在批次结束之后，且没有改写批次终点。 */
	bool bPostBatchFirstAuthorityShotLogged = false;
	float PostBatchFirstAuthorityShotServerTime = 0.0f;
	/** 服务器侧：批次终点被冻结的时刻（Action Stage 真正结束、静默期验证通过的时刻）。 */
	float OwnRemoteBatchFrozenServerTime = 0.0f;
	/** 服务器侧：收到拥有者释放上报那一刻的权威 Shot 数（收口许可的依据）。 */
	int32 OwnBatchReleaseSignalCount = INDEX_NONE;
	float OwnBatchReleaseSignalServerTime = 0.0f;
	/** 开始边界只用于把"观察端基线永远不到"变成明确失败原因，不参与批次语义判定。 */
	bool bRemoteBatchStartGateFailureReported = false;
	/** 服务器侧：本玩家批次 Arm 时是否已经把"已 Arm"通知到观察端 Coordinator。 */
	bool bObservedBatchArmedNotified = false;
	/** 服务器侧：观察端（对手）为「本玩家批次」建立基线的 ack 是否已到达，以及它到达的时刻。 */
	float RemoteObserverBaselineReadyServerTime = 0.0f;
	/** 观察端上报的远端确认表现增量，与同一批次冻结的权威射击增量比较。 */
	int32 RemoteConfirmedDeltaObserved = INDEX_NONE;
	int32 RemoteMontageDeltaObserved = INDEX_NONE;
	int32 RemoteMuzzleDeltaObserved = INDEX_NONE;
	int32 RemoteSoundDeltaObserved = INDEX_NONE;
	int32 RemoteAuthorityShotsForBurst = INDEX_NONE;
	bool bRemoteConfirmedVerified = false;
	/** 观察端批次快照是否已经到达，以及它是否已经完成比较（两者都是服务器侧状态）。 */
	bool bRemoteConfirmedReportReceived = false;
	bool bRemoteConfirmedEvaluated = false;
	/** 只读取证：真正完成比较的次数（供 Coordinator 层自动化测试断言 exactly-once）。 */
	int32 RemoteBatchEvaluateCountForTest = 0;
	/** 观察端上报里携带的观测源稳定性与目标身份，供推迟后的比较使用。 */
	bool bRemoteConfirmedReportTargetStable = false;
	int32 RemoteConfirmedReportedTargetPlayerId = INDEX_NONE;
	/**
	 * 服务器侧：观察端在本玩家批次期间观测到的武器引用，只用于核对"双方看的是同一把武器"。
	 * 权威增量本身来自该 Target 自己冻结的批次，不再由本引用现场读取。
	 */
	TWeakObjectPtr<AShooterWeapon> RemoteObservedWeaponAtBurstStart;
	/** 客户端侧：观测源、Arm 时建立的四条基线，以及窗口收口状态。 */
	TWeakObjectPtr<AShooterWeapon> ClientObservedRemoteWeapon;
	TWeakObjectPtr<AShooterCharacter> ClientObservedRemoteCharacter;
	/** 客户端侧：被观测玩家的 PlayerId（跨端身份只用 PlayerId，不用本机对象名）。 */
	int32 ClientObservedRemotePlayerId = INDEX_NONE;
	/** 客户端本地：四条基线建立的时刻，用于观测窗口的耗时取证。 */
	float ClientRemoteBaselineTime = 0.0f;
	int32 ClientRemoteConfirmedBefore = INDEX_NONE;
	int32 ClientRemoteMontageBefore = INDEX_NONE;
	int32 ClientRemoteMuzzleBefore = INDEX_NONE;
	int32 ClientRemoteSoundBefore = INDEX_NONE;
	/** 客户端侧：本玩家作为观察端是否已经建立基线并上报就绪 ack。 */
	bool bClientCapturedRemoteBaseline = false;
	/** 服务器侧：观察端 ack 是否已经到达（对手据此才允许开始它自己的批次）。 */
	bool bRemoteObserverBaselineReady = false;
	/** 客户端本地：四路增量快照与最后一次变化时刻，只用于记录边界之前的到达余量。 */
	FShooterRemotePresentationDeltasForTest ClientRemoteDeltasLast;
	float ClientRemotePresentationStableTime = 0.0f;
	/** 客户端本地：上报快照里的四路增量，用于与"权威边界到达时"的四路增量做事后核对。 */
	FShooterRemotePresentationDeltasForTest ClientRemoteDeltasAtSnapshot;
	bool bClientLoggedRemoteBatchFrozenCorroboration = false;
	/** 观测窗口在等权威冻结期间只留一条低频取证，避免每帧刷屏。 */
	bool bClientLoggedRemoteBatchAwait = false;
	/** 观测源尚未复制到本机时只留一条低频取证。 */
	bool bClientLoggedRemoteBaselinePending = false;
	/** 客户端侧：上报之后继续观测到的表现增量，用来证明下一发确实到达、但没有进入上一批次。 */
	bool bClientLoggedRemotePostFreeze = false;
	bool bClientReportedRemoteConfirmed = false;
	/**
	 * Unreliable 纯表现 / 确认通道是否要求精确送达。
	 * 无丢包无延迟（Dedicated / Listen）时要求"四路逐路等于同一冻结批次的权威 Shot 数"；
	 * Emulated（PktLag / PktLoss）下契约不承诺 Unreliable 表现必达，只要求不重复。
	 * 注意：Exact 是受控测试环境下的强诊断不变量，不是生产网络对 Unreliable 纯表现的投递承诺。
	 */
	bool bRequireExactRemoteConfirmed = true;

	/** 4C 观测：死亡 / 无武器 / 无弹药拒绝、切枪取消、重生 Tag 清理与 NPC Ability 链路。 */
	bool bSwitchCancelPhaseTriggered = false;
	bool bSwitchCancelActiveObserved = false;
	bool bClientObservedSwitchCancel = false;
	bool bClientReportedSwitchCancel = false;
	bool bSwitchCancelVerified = false;
	bool bSwitchCancelQuiescentConfirmed = false;
	bool bNoAmmoRejectVerified = false;

	// ---- P1 换弹中开火：服务器侧记录与验收 ----
	bool bReloadFireImmediateVerified = false;
	bool bReloadFireAfterTagVerified = false;
	int32 ReloadFireActiveRequestId = 0;
	/** 阶段起点武器引用：归还池后 GetCurrentWeapon 会变空，但该 Actor 仍可用于读取权威计数。 */
	TWeakObjectPtr<AShooterWeapon> ReloadFireTargetWeapon;
	int32 ReloadFireAmmoBefore = INDEX_NONE;
	int32 ReloadFireAuthorityShotsBefore = INDEX_NONE;
	int32 ReloadFireAuthorityRejectsBefore = INDEX_NONE;
	int32 ReloadFireProjectilesBefore = INDEX_NONE;
	float ReloadFirePhaseStartTime = 0.0f;
	bool bFireRejectDeadVerified = false;
	bool bFireRejectNoWeaponVerified = false;
	bool bRespawnTagCleanupVerified = false;
	bool bClientTriggeredSwitchCancel = false;
	bool bClientSwitchCancelRequested = false;
	TWeakObjectPtr<AShooterWeapon> ClientWeaponBeforeSwitchCancel;
	int32 BulletCountBeforeClientSwitchCancel = INDEX_NONE;
	int32 ProjectileCountBeforeSwitchCancel = INDEX_NONE;
	int32 RifleAmmoBeforeSwitchCancel = INDEX_NONE;
	int32 ProjectileCountAfterSwitchCancel = INDEX_NONE;
	int32 RifleAmmoAfterSwitchCancel = INDEX_NONE;

	/** 切枪取消阶段起点：旧武器的权威 Shot 计数；用它证明"切枪前确实开过火"。 */
	int32 AuthorityShotsBeforeSwitchCancel = INDEX_NONE;
	float SwitchCancelCheckTime = 0.0f;

	bool bNpcFireActivated = false;
	bool bNpcFireStopOk = false;
	bool bNpcFireQuiescenceConfirmed = false;
	int32 NpcProjectileCountAtStop = INDEX_NONE;
	float NpcFireStopCheckTime = 0.0f;
	TWeakObjectPtr<AShooterNPC> NpcFireTestNpc;

	/** Inventory 2A 观测：服务器插入两把测试武器，Owner 完整收到，远端不收到完整列表。 */
	bool bServerInventoryPrepared = false;
	bool bClientObservedPickupAuthority = false;
	bool bClientReportedPickupAuthority = false;
	int32 InitialRifleMagazineAmmo = INDEX_NONE;
	int32 InitialPistolMagazineAmmo = INDEX_NONE;
	bool bAmmoIsolationVerified = false;
	bool bServerDeathInventoryCleared = false;
	bool bClientObservedDeathInventoryClear = false;
	bool bClientReportedDeathInventoryClear = false;
	bool bServerRespawnInventoryEmpty = false;
	bool bClientObservedRespawnInventoryEmpty = false;
	bool bClientReportedRespawnInventoryEmpty = false;
	TWeakObjectPtr<AShooterWeapon> ServerInventoryFirstWeapon;
	TWeakObjectPtr<AShooterWeapon> ServerInventorySecondWeapon;
	TWeakObjectPtr<AShooterWeapon> ServerInventoryActiveWeapon;
	bool bClientObservedOwnerInventory = false;
	bool bClientReportedInventory = false;
	bool bClientObservedRemoteInventoryHidden = false;

	float LastDamagedLifePercent = -1.0f;
	float LastClientAttributeHealth = -1.0f;
	float ClientMaxHealthAttributeValue = 0.0f;
	bool bClientHealthAttributeDelegateBound = false;
	TWeakObjectPtr<AShooterCharacter> HudBoundCharacter;
	TWeakObjectPtr<UAbilitySystemComponent> ObservedAbilitySystemComponent;
	bool bServerObservedProjectile = false;
	bool bAimDirectionValid = false;
	bool bPartialDamageApplied = false;
	bool bLethalDamageApplied = false;
	bool bSecondaryWeaponGranted = false;
	bool bOpponentKilledForStats = false;
	bool bDisconnectCleanupMode = false;
	bool bDisconnectEquipMode = false;

	/** B1 瞄准表现基线模式：-ShootGameAimRotationTest 开启。只测量，不新增网络字段。 */
	bool bAimRotationMode = false;

	/** 快慢转向 CSV 诊断模式：-ShootGameAimTurnCsvTest 开启；正常玩法与常规自动化均不运行。 */
	bool bAimTurnCsvMode = false;

	/** CSV 模式下只存在于拥有者进程的 Visibility 薄墙，用来稳定复现近命中/远端回退切换。 */
	UPROPERTY(VisibleAnywhere, Category = "Automation|Aim Turn CSV")
	TObjectPtr<UBoxComponent> AimTurnCsvObstacleComponent;

	struct FAimTurnCsvPreviousSample
	{
		bool bValid = false;
		FVector TraceTarget = FVector::ZeroVector;
		FVector RawTarget = FVector::ZeroVector;
		FVector SmoothedTarget = FVector::ZeroVector;
		FVector AimDirection = FVector::ZeroVector;
		FVector MuzzleLocation = FVector::ZeroVector;
		FVector MuzzleForward = FVector::ZeroVector;
		FVector FinalizedMuzzleLocation = FVector::ZeroVector;
		FVector FeedbackDirection = FVector::ZeroVector;
		FVector ReferenceDirection = FVector::ZeroVector;
		float AimPitchN = 0.0f;
		FString TraceKind;
		FString HitIdentity;
	};

	/** 骨骼求值完成后的只读采样；只在 AimTurnCsv 诊断模式注册。 */
	struct FAimTurnCsvPoseProbe
	{
		TWeakObjectPtr<AShooterCharacter> Subject;
		TWeakObjectPtr<USkeletalMeshComponent> Mesh;
		FDelegateHandle DelegateHandle;
		FTransform FinalizedHandWorld = FTransform::Identity;
		FTransform FinalizedMuzzleWorld = FTransform::Identity;
		FTransform ReferenceMuzzleInMeshSpace = FTransform::Identity;
		uint64 FinalizedFrame = 0;
		bool bHasFinalizedSample = false;
		bool bHasReferenceMuzzle = false;
		bool bLoggedAnimGraphClass = false;
	};

	bool bAimTurnCsvStarted = false;
	bool bAimTurnCsvWritten = false;
	float AimTurnCsvStartTime = 0.0f;
	FRotator AimTurnCsvStartRotation = FRotator::ZeroRotator;
	FString AimTurnCsvBuffer;
	FString AimTurnCsvOutputPath;
	int32 AimTurnCsvRowCount = 0;
	FAimTurnCsvPreviousSample AimTurnCsvOwnerPrevious;
	FAimTurnCsvPreviousSample AimTurnCsvObserverPrevious;
	FAimTurnCsvPoseProbe AimTurnCsvOwnerPoseProbe;
	FAimTurnCsvPoseProbe AimTurnCsvObserverPoseProbe;

	void RunAimTurnCsvFrame(float DeltaSeconds);
	void CaptureAimTurnCsvSubject(const TCHAR* SampleRole, AShooterCharacter* Subject, float PhaseTime,
		float DeltaSeconds, FAimTurnCsvPreviousSample& PreviousSample, FAimTurnCsvPoseProbe& PoseProbe);
	void EnsureAimTurnCsvPoseProbe(AShooterCharacter* Subject, FAimTurnCsvPoseProbe& PoseProbe);
	void CaptureAimTurnCsvFinalizedPose(FAimTurnCsvPoseProbe* PoseProbe);
	void UnregisterAimTurnCsvPoseProbe(FAimTurnCsvPoseProbe& PoseProbe);
	void FlushAimTurnCsv();

	/** 服务器开启瞄准旋转阶段后复制给客户端。 */
	UPROPERTY(Replicated)
	bool bAimRotationPhaseActive = false;

	/** 服务器完成跟踪验证后复制给客户端。 */
	UPROPERTY(Replicated)
	bool bAimRotationServerDone = false;

	/** 服务器完成表现目标复制验证后复制给客户端（B2）。 */
	UPROPERTY(Replicated)
	bool bAimRotationServerPresentationVerified = false;

	/** B1 服务器侧阶段状态（服务器时间）。 */
	float AimRotationServerPhaseStartTime = 0.0f;
	float AimRotationLastSampleTime = -1.0f;
	int32 AimRotationServerSampleCount = 0;
	int32 AimRotationServerYawGoodSamples = 0;
	int32 AimRotationServerPitchGoodSamples = 0;
	bool bAimRotationServerYawTracked = false;
	bool bAimRotationServerPitchTracked = false;
	float AimRotationServerMuzzleAngleMin = 180.0f;
	float AimRotationServerMuzzleAngleMax = 0.0f;

	/** B1 客户端侧阶段状态（客户端时间）。 */
	float AimRotationClientPhaseStartTime = 0.0f;
	bool bClientAimRotationStarted = false;
	bool bClientAimRotationReported = false;
	FRotator AimRotationClientStartRotation = FRotator::ZeroRotator;
	float AimRotationLastObserverSampleTime = -1.0f;
	float AimRotationLastObserverRemoteYaw = 0.0f;
	int32 AimRotationObserverSampleCount = 0;
	int32 AimRotationObserverYawGoodSamples = 0;
	float AimRotationObserverMaxPitch = -90.0f;
	float AimRotationObserverMinPitch = 90.0f;
	float AimRotationObserverMuzzleAngleMin = 180.0f;
	float AimRotationObserverMuzzleAngleMax = 0.0f;
	TWeakObjectPtr<AShooterCharacter> AimRotationObservedCharacter;

	// ---- B2 表现目标复制验证 ----
	int32 AimRotationServerPresentationGoodSamples = 0;
	int32 AimRotationServerPresentationChanges = 0;
	FVector AimRotationServerPresentationPrevTarget = FVector::ZeroVector;
	bool bAimRotationServerPresentationPrevValid = false;
	float AimRotationServerMaxPresentationMagnitude = 0.0f;
	bool bAimRotationOwnerPresentationUntouched = false;
	int32 AimRotationObserverPresentationGoodSamples = 0;
	int32 AimRotationObserverQuantizedSamples = 0;
	bool bAimRotationObserverPresentationSeen = false;

	// ---- B3 观察端平滑与局部角度契约验证 ----
	float AimRotationObserverMinSmoothGap = FLT_MAX;
	int32 AimRotationObserverPitchContractSamples = 0;
	int32 AimRotationObserverYawStableSamples = 0;
	float AimRotationObserverAimYawFirst = 0.0f;
	bool bAimRotationObserverAimYawFirstSet = false;
	bool bAimRotationObserverSmoothingSeen = false;

	/** B1 服务器侧瞄准旋转阶段（PollServerState 专用分支）。 */
	void RunAimRotationServerPhase();

	/** B1 客户端侧瞄准旋转阶段（PollClientState 专用分支）。 */
	void RunAimRotationClientPhase();

	/** B1 观察端采样与验证：远端角色方向 + 枪口 Forward 夹角。 */
	void RunAimRotationObserverPhase();
	int32 InitialClientBulletCount = INDEX_NONE;
	float InitialHP = 0.0f;
	float ObservedAimDot = -1.0f;
	uint8 ObservedTeamId = 0;
	int32 ObservedKills = 0;
	int32 ObservedDeaths = 0;
	int32 ObservedTeamScore = 0;
	float ObservedRemotePitchN = 0.0f;
	float ExpectedRemotePitchN = 0.0f;
	TWeakObjectPtr<AShooterWeapon> InitialClientWeapon;
	TWeakObjectPtr<AShooterCharacter> CharacterBeforeDeath;
};
