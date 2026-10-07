// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterNetworkObservationTypes.generated.h"

class AShooterCharacter;
class AShooterWeapon;

/**
 * 网络测试的纯观测 / 统计类型集合。
 *
 * 边界（只放数据形状）：
 * - 没有 Actor 生命周期、没有 World 访问、不注册委托、不发 RPC、不推进 Stage；
 * - 只有采样快照、计数快照、速率窗口，以及作为 RPC 载荷的逐端观测快照
 *   （USTRUCT，只声明字段，不携带任何流程语义）。
 *
 * 生命周期与编排（Timer、Stage 状态机、复制字段、RPC 收发、PlayerId 身份解析）
 * 仍然全部留在 Tests/Network/ShooterNetworkTestCoordinator.h：
 * Coordinator 自己持有本文件类型的实例，但不把这些类型的语义收归自己。
 */

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
