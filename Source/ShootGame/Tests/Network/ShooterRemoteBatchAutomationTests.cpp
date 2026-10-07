// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Tests/Network/ShooterRemoteBatchForTest.h"

/**
 * 远端确认批次的 fixture 语义测试。
 *
 * 本文件不启动网络会话，只驱动普通网络夹具真正使用的批次类型
 * （Tests/Network/ShooterRemoteBatchForTest.h），用来锁住两条边界语义：
 *   1) 批次终点一旦由 Target 的 Action Stage 冻结，后续阶段的任何新权威 Shot 都不能污染它；
 *   2) 观察端基线就绪之前，Target 不得开始本批次。
 *
 * 这两条正是 2026-10-07 普通 Dedicated 假失败（Confirmed=1 / Auth=2）的结构性来源；
 * 真实的跨进程时序由普通 Dedicated 会话的批次日志另行取证，本文件只回答夹具语义本身是否正确。
 */
namespace ShooterRemoteBatchAutomationTests
{
	FShooterRemotePresentationDeltasForTest MakeDeltas(int32 Confirmed, int32 Montage, int32 Muzzle, int32 Sound)
	{
		FShooterRemotePresentationDeltasForTest Deltas;
		Deltas.Confirmed = Confirmed;
		Deltas.Montage = Montage;
		Deltas.Muzzle = Muzzle;
		Deltas.Sound = Sound;
		return Deltas;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterRemoteBatchFrozenEndTest,
	"ShootGame.Network.RemoteBatch.FrozenAuthorityEndSurvivesNextStageShot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterRemoteBatchFrozenEndTest::RunTest(const FString& Parameters)
{
	using namespace ShooterRemoteBatchAutomationTests;

	FShooterRemoteFullAutoBatchForTest Batch;
	TestFalse(TEXT("a fresh batch is not armed"), Batch.IsArmed());
	TestFalse(TEXT("a fresh batch is not frozen"), Batch.IsFrozen());
	TestEqual(TEXT("an unarmed batch has no authority delta"), Batch.GetExpectedDelta(), INDEX_NONE);
	TestEqual(TEXT("freezing without arming must not fabricate an end"), Batch.FreezeEnd(11), INDEX_NONE);
	TestFalse(TEXT("freezing without arming leaves the batch unfrozen"), Batch.IsFrozen());

	// AuthorityStart：Target 的 FullAuto Action Stage 开始时冻结，之后重复 Arm 不得改写。
	const int32 AuthorityStart = 10;
	TestTrue(TEXT("arming establishes the authority start"), Batch.Arm(AuthorityStart));
	TestFalse(TEXT("arming twice must not overwrite the established start"), Batch.Arm(AuthorityStart + 5));
	TestEqual(TEXT("the frozen start is the batch start"), Batch.AuthorityStart, AuthorityStart);
	TestEqual(TEXT("an armed but unfrozen batch still has no delta"), Batch.GetExpectedDelta(), INDEX_NONE);

	// AuthorityEnd：Action Stage 真正结束时冻结；本批次只有 1 发权威 Shot。
	const int32 AuthorityEnd = AuthorityStart + 1;
	TestEqual(TEXT("the action stage end freezes the expected delta"), Batch.FreezeEnd(AuthorityEnd), 1);
	TestTrue(TEXT("the batch is frozen"), Batch.IsFrozen());
	TestEqual(TEXT("the frozen end is the batch end"), Batch.AuthorityEnd, AuthorityEnd);

	// Observer 对这一批次收到四路表现各 1 份，比较成立。
	const FShooterRemotePresentationDeltasForTest BatchAObservation = MakeDeltas(1, 1, 1, 1);
	TestTrue(TEXT("batch A observation matches the frozen authority batch"),
		BatchAObservation.EqualsAuthorityDelta(Batch.GetExpectedDelta()));

	// 旧 race：report 已经形成、服务器尚未执行比较时，Target 进入下一阶段（SwitchCancel）再 Commit 一发。
	// 服务器此后即使再读一次权威计数（= 12），也不得改变已经冻结的批次终点与 ExpectedDelta。
	TestEqual(TEXT("re-freezing returns the frozen delta"), Batch.FreezeEnd(AuthorityEnd + 1), 1);
	TestEqual(TEXT("the frozen authority end is still the batch boundary"), Batch.AuthorityEnd, AuthorityEnd);
	TestEqual(TEXT("the frozen authority start is unchanged"), Batch.AuthorityStart, AuthorityStart);

	// 这一发新 Shot 属于下一批次：把它算进来就会得到 Confirmed=1 / Auth=2 的旧失败形态。
	const FShooterRemotePresentationDeltasForTest ContaminatedObservation = MakeDeltas(2, 2, 2, 2);
	TestFalse(TEXT("a next-stage shot stays outside the previous batch"), ContaminatedObservation.EqualsAuthorityDelta(1));
	TestTrue(TEXT("the previous batch stays exact after the next stage shot"),
		BatchAObservation.EqualsAuthorityDelta(Batch.GetExpectedDelta()));

	// 下一阶段自己的批次是另一份状态，不能与上一批次共用终点。
	FShooterRemoteFullAutoBatchForTest NextBatch;
	TestTrue(TEXT("the next stage can establish its own batch start"), NextBatch.Arm(AuthorityEnd + 1));
	TestEqual(TEXT("the next batch freezes its own delta"), NextBatch.FreezeEnd(AuthorityEnd + 2), 1);
	TestEqual(TEXT("the next batch end is independent"), NextBatch.AuthorityEnd, AuthorityEnd + 2);
	TestEqual(TEXT("the previous batch end is untouched by the next batch"), Batch.AuthorityEnd, AuthorityEnd);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterRemoteBatchStartGateTest,
	"ShootGame.Network.RemoteBatch.StartGateBlocksUntilObserverBaselineReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterRemoteBatchStartGateTest::RunTest(const FString& Parameters)
{
	// 没有观察端路径（Listen 主机自己不被任何人观测）：不存在"基线尚未建立"的问题，允许直接开始。
	FShooterRemoteBatchStartGateForTest Gate;
	TestFalse(TEXT("a fresh gate has no observation path"), Gate.bObserverHasObservationPath);
	TestTrue(TEXT("no observer path allows the batch to start immediately"), Gate.IsStartAllowed());

	// 存在远端观察端：AuthorityStart 可以已经建立，但在收到基线就绪 ack 之前绝不允许开始，
	// 这正是 Auth=N / Confirmed=N-1 镜像 race 的入口。
	Gate.bObserverHasObservationPath = true;
	Gate.bObserverBaselineReady = false;
	TestFalse(TEXT("the batch must not start before the observer baseline"), Gate.IsStartAllowed());

	// 只有观察端明确报告"四条基线已经建立"之后才放开。
	Gate.bObserverBaselineReady = true;
	TestTrue(TEXT("the batch starts only after the observer baseline ready ack"), Gate.IsStartAllowed());

	// ack 是单向闸门：基线不会因为后续任何原因"重新变回未就绪"。
	TestTrue(TEXT("the ready ack stays effective"), Gate.IsStartAllowed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterRemoteBatchChannelSemanticsTest,
	"ShootGame.Network.RemoteBatch.PresentationChannelsKeepOwnMeaning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterRemoteBatchChannelSemanticsTest::RunTest(const FString& Parameters)
{
	using namespace ShooterRemoteBatchAutomationTests;

	constexpr int32 ExpectedDelta = 2;
	const FShooterRemotePresentationDeltasForTest Exact = MakeDeltas(2, 2, 2, 2);
	TestTrue(TEXT("four channels equal to the batch delta are exact"), Exact.EqualsAuthorityDelta(ExpectedDelta));
	TestTrue(TEXT("four exact channels are within authority"), Exact.WithinAuthority(ExpectedDelta));

	// 单路少一份：聚合看着够、玩家却少看到一份表现，必须不成立。
	FShooterRemotePresentationDeltasForTest MissingSound = MakeDeltas(2, 2, 2, 1);
	TestFalse(TEXT("a missing sound presentation is not exact"), MissingSound.EqualsAuthorityDelta(ExpectedDelta));
	TestTrue(TEXT("a missing sound is still within authority"), MissingSound.WithinAuthority(ExpectedDelta));

	// 单路多一份：重复表现必须不成立。
	FShooterRemotePresentationDeltasForTest DuplicatedMuzzle = MakeDeltas(2, 2, 3, 2);
	TestFalse(TEXT("a duplicated muzzle presentation is not exact"), DuplicatedMuzzle.EqualsAuthorityDelta(ExpectedDelta));
	TestFalse(TEXT("a duplicated muzzle is not within authority"), DuplicatedMuzzle.WithinAuthority(ExpectedDelta));

	// 某一路完全没有提交：聚合计数不得掩盖单路静默失效。
	const FShooterRemotePresentationDeltasForTest SilentMontage = MakeDeltas(2, 0, 2, 2);
	TestFalse(TEXT("a silent channel is not exact"), SilentMontage.EqualsAuthorityDelta(ExpectedDelta));
	TestFalse(TEXT("a silent channel is not within authority"), SilentMontage.WithinAuthority(ExpectedDelta));

	// 生产计数器不存在时不得把 INDEX_NONE 当作 0 或当作通过。
	const FShooterRemotePresentationDeltasForTest Unmeasurable;
	TestFalse(TEXT("unmeasured channels are not measurable"), Unmeasurable.IsMeasurable());
	TestFalse(TEXT("unmeasured channels are not exact"), Unmeasurable.EqualsAuthorityDelta(ExpectedDelta));
	TestFalse(TEXT("unmeasured channels are not within authority"), Unmeasurable.WithinAuthority(ExpectedDelta));
	return true;
}

#endif
