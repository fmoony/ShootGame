// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 远端表现的批次语义（TEST-ONLY，供普通网络夹具与自动化测试共用）。
 *
 * 背景（2026-10-07 取证）：旧口径把「Authority baseline 到 Observer report 抵达服务器时为止的累计量」
 * 当作权威增量，但 report 的到达时刻与 Observer 冻结时刻之间没有任何上界：Target 完全可以在这段间隙里
 * 进入 SwitchCancel 并再 Commit 一发，服务器随后现场读取权威计数就把这一发算进了上一批次，
 * 得到 Confirmed=1 / Auth=2 的假失败。失败原因不是生产 Remote presentation 漏播，而是
 * 不同 observation batch 的计数被拿来做 Exact 比较。
 *
 * 因此本文件把「批次」显式定义成两个冻结边界：
 *   - Start：Target（被观察方）的 FullAuto Action Stage 开始时，该武器冻结的权威 Shot 计数；
 *   - End：该 Action Stage 真正结束时（服务器已证明不再有属于本批次的 Shot）冻结的权威 Shot 计数。
 *
 * ExpectedDelta = End - Start 一旦冻结，之后任何阶段（SwitchCancel / Reload / 其它 Fire）产生的
 * 新 Shot 都不能改变它，Observer 上报晚到多久也不能污染它。
 *
 * 这三个类型是夹具里对应事实的唯一 Source of Truth：生产代码不引用它们，
 * 协调器直接持有它们，自动化测试直接驱动它们。
 */

/** Target 侧：一次 FullAuto Action Stage 对应的权威 Shot 批次。 */
struct FShooterRemoteFullAutoBatchForTest
{
	/** 批次起点；未 Arm 时为 INDEX_NONE。 */
	int32 AuthorityStart = INDEX_NONE;

	/** 批次终点；只在第一次 FreezeEnd 时写入，此后不得再改写。 */
	int32 AuthorityEnd = INDEX_NONE;

	bool bArmed = false;
	bool bFrozen = false;

	/** 冻结批次起点。重复 Arm 不覆盖已建立的起点，返回是否本次建立了起点。 */
	bool Arm(int32 InAuthorityStart)
	{
		if (bArmed || InAuthorityStart == INDEX_NONE)
		{
			return false;
		}

		AuthorityStart = InAuthorityStart;
		bArmed = true;
		return true;
	}

	/**
	 * 冻结批次终点。只有已 Arm 且尚未冻结时才写入，重复调用一律返回当前冻结值，
	 * 因此"下一阶段又打了一发之后再调用一次"不会改变任何已成立的结论。
	 */
	int32 FreezeEnd(int32 InAuthorityEnd)
	{
		if (bArmed && !bFrozen && InAuthorityEnd != INDEX_NONE)
		{
			AuthorityEnd = InAuthorityEnd;
			bFrozen = true;
		}

		return GetExpectedDelta();
	}

	bool IsArmed() const { return bArmed; }
	bool IsFrozen() const { return bFrozen; }

	/** 冻结后的批次权威增量；未完成冻结时为 INDEX_NONE。 */
	int32 GetExpectedDelta() const { return (bArmed && bFrozen) ? AuthorityEnd - AuthorityStart : INDEX_NONE; }
};

/**
 * Target 侧：批次开始边界。
 *
 * AuthorityStart 建立之后 Target 仍不得开火，直到「观察该 Target 的那一侧」已经建立四条表现基线。
 * 没有观察端路径时（例如 Listen 主机自己不被任何人观测）允许直接开始；
 * 只要该 Target 的对手是一个真正的远端客户端，就必须等到它的基线就绪 ack 到达服务器。
 * 这条边界排除的是镜像 race：Target 的第一发发生在 Observer 基线之前，导致 Auth=N / Confirmed=N-1。
 */
struct FShooterRemoteBatchStartGateForTest
{
	/** 对手是否存在一条真实的远端观察路径（远端客户端才会运行观测分支）。 */
	bool bObserverHasObservationPath = false;

	/** 观察端是否已经建立四条基线并把就绪 ack 送到服务器。 */
	bool bObserverBaselineReady = false;

	bool IsStartAllowed() const { return !bObserverHasObservationPath || bObserverBaselineReady; }
};

/**
 * Observer 侧：同一批次内四条表现通道的增量。
 *
 * 四路各自保持自己的真实含义，任何一路都不允许改写成 Authority 计数：
 *   - Confirmed：remote FX presentation 的提交证据；
 *   - Montage：TP Montage_Play 被接受次数；
 *   - Muzzle：remote muzzle spawn 请求次数；
 *   - Sound：remote sound spawn 请求次数。
 */
struct FShooterRemotePresentationDeltasForTest
{
	int32 Confirmed = INDEX_NONE;
	int32 Montage = INDEX_NONE;
	int32 Muzzle = INDEX_NONE;
	int32 Sound = INDEX_NONE;

	/** 四路是否都可测量（生产计数器不存在时为 INDEX_NONE）。 */
	bool IsMeasurable() const
	{
		return Confirmed != INDEX_NONE && Montage != INDEX_NONE && Muzzle != INDEX_NONE && Sound != INDEX_NONE;
	}

	bool HasAtLeastOnePerChannel() const
	{
		return Confirmed >= 1 && Montage >= 1 && Muzzle >= 1 && Sound >= 1;
	}

	/** 四路都提交过，且每一路都不超过该批次的权威 Shot 数。 */
	bool WithinAuthority(int32 ExpectedDelta) const
	{
		return ExpectedDelta >= 1 && HasAtLeastOnePerChannel() && Confirmed <= ExpectedDelta &&
			Montage <= ExpectedDelta && Muzzle <= ExpectedDelta && Sound <= ExpectedDelta;
	}

	/** 四路逐路等于同一批次的权威 Shot 数：多一份少一份都不成立。 */
	bool EqualsAuthorityDelta(int32 ExpectedDelta) const
	{
		return ExpectedDelta >= 0 && Confirmed == ExpectedDelta && Montage == ExpectedDelta &&
			Muzzle == ExpectedDelta && Sound == ExpectedDelta;
	}

	bool operator==(const FShooterRemotePresentationDeltasForTest& Other) const
	{
		return Confirmed == Other.Confirmed && Montage == Other.Montage && Muzzle == Other.Muzzle && Sound == Other.Sound;
	}

	bool operator!=(const FShooterRemotePresentationDeltasForTest& Other) const { return !(*this == Other); }
};
