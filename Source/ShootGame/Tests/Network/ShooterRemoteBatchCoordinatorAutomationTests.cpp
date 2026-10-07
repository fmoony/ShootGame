// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState/ShooterPlayerState.h"
#include "Tests/Equipment/ShooterWeaponPresentationTestTypes.h"
#include "Tests/Network/ShooterNetworkTestCoordinator.h"
#include "Weapons/ShooterWeapon.h"

/**
 * 远端批次在 Coordinator 层的异步到达顺序与 exactly-once。
 *
 * 被验证的对象是真实的 ShooterNetworkTestCoordinator：
 *   - report handler（ServerReportRemoteConfirmedFeedback 的真实实现）；
 *   - NotifyObservedBatchFrozen（Target 冻结后通知观察端的边界）；
 *   - EvaluateRemoteConfirmedBatch（唯一一次比较，且只能在冻结之后发生）。
 *
 * 只回答三件事：
 *   1) report 先到、冻结后到：冻结前不得比较，冻结后恰好比较一次，且用的是冻结增量；
 *   2) 冻结先到、report 后到：report 到达前不得比较，到达后恰好比较一次，用的是同一个冻结增量；
 *   3) exactly-once：重复触发边界通知或直接重复调用比较入口，比较次数与结论都不再变化。
 *
 * 不覆盖：冲突/重复 report 的取舍、reset/rearm、BatchId、生产 RPC、生产状态、容差 getter。
 * 夹具只提供两个玩家各自的 Coordinator 与一把用于身份核对的武器，不启动任何网络会话。
 */
namespace ShooterRemoteBatchCoordinatorAutomationTests
{
	UWorld* CreateBatchTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!World || !GEngine)
		{
			return World;
		}

		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		return World;
	}

	void DestroyBatchTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	/** 两名玩家各自的 Coordinator + 一把用于武器身份核对的武器。 */
	struct FBatchCoordinatorFixture
	{
		UWorld* World = nullptr;
		AShooterNetworkTestCoordinator* TargetCoordinator = nullptr;
		AShooterNetworkTestCoordinator* ObserverCoordinator = nullptr;
		AShooterWeapon* BatchWeapon = nullptr;
		int32 TargetPlayerId = INDEX_NONE;
		int32 ObserverPlayerId = INDEX_NONE;

		bool IsValid() const
		{
			return World && TargetCoordinator && ObserverCoordinator && BatchWeapon;
		}
	};

	/**
	 * 生成一个由指定 PlayerId 的 PlayerController 拥有的 Coordinator。
	 * 跨端身份只看 PlayerId；PlayerController 必须进入 World 的 PlayerController 列表，
	 * 因为 GetOpponentController / FindOpponentCoordinator 依赖该列表解析对手。
	 */
	AShooterNetworkTestCoordinator* SpawnOwnedCoordinator(FAutomationTestBase& Test, UWorld* World, const TCHAR* Label, int32 PlayerId)
	{
		const FString Prefix = FString::Printf(TEXT("remote batch %s"), Label);
		APlayerController* PlayerController = World->SpawnActor<APlayerController>(FVector::ZeroVector,
			FRotator::ZeroRotator);
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s PlayerController spawned"), *Prefix), PlayerController))
		{
			return nullptr;
		}

		FActorSpawnParameters PlayerStateParameters;
		PlayerStateParameters.Owner = PlayerController;
		PlayerStateParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AShooterPlayerState* PlayerState = World->SpawnActor<AShooterPlayerState>(AShooterPlayerState::StaticClass(),
			PlayerStateParameters);
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s PlayerState spawned"), *Prefix), PlayerState))
		{
			return nullptr;
		}

		PlayerState->SetPlayerId(PlayerId);
		PlayerController->PlayerState = PlayerState;
		World->AddController(PlayerController);

		FActorSpawnParameters CoordinatorParameters;
		CoordinatorParameters.Owner = PlayerController;
		CoordinatorParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AShooterNetworkTestCoordinator* Coordinator = World->SpawnActor<AShooterNetworkTestCoordinator>(
			AShooterNetworkTestCoordinator::StaticClass(), CoordinatorParameters);
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s Coordinator spawned"), *Prefix), Coordinator))
		{
			return nullptr;
		}

		// 身份不额外断言：比较会用 report 携带的 TargetPlayerId 与 Target Coordinator 的
		// 身份交叉核对（GetTestPlayerIdForTest 是私有实现细节），PlayerId 不匹配时 verdict 必然为 false。
		return Coordinator;
	}

	FBatchCoordinatorFixture CreateBatchCoordinatorFixture(FAutomationTestBase& Test)
	{
		FBatchCoordinatorFixture Fixture;
		Fixture.World = CreateBatchTestWorld();
		if (!Test.TestNotNull(TEXT("remote batch world created"), Fixture.World))
		{
			return Fixture;
		}

		Fixture.TargetPlayerId = 257;
		Fixture.ObserverPlayerId = 256;
		Fixture.TargetCoordinator = SpawnOwnedCoordinator(Test, Fixture.World, TEXT("target"), Fixture.TargetPlayerId);
		Fixture.ObserverCoordinator = SpawnOwnedCoordinator(Test, Fixture.World, TEXT("observer"),
			Fixture.ObserverPlayerId);
		Fixture.BatchWeapon = Fixture.World->SpawnActor<AShooterWeaponLifecycleTestWeapon>(FVector::ZeroVector,
			FRotator::ZeroRotator);
		Test.TestNotNull(TEXT("remote batch weapon spawned"), Fixture.BatchWeapon);
		return Fixture;
	}

	void DestroyBatchCoordinatorFixture(const FBatchCoordinatorFixture& Fixture)
	{
		DestroyBatchTestWorld(Fixture.World);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterRemoteBatchCoordinatorReportFirstTest,
	"ShootGame.Network.RemoteBatch.CoordinatorReportBeforeFreezeEvaluatesOnceOnFreeze",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterRemoteBatchCoordinatorReportFirstTest::RunTest(const FString& Parameters)
{
	using namespace ShooterRemoteBatchCoordinatorAutomationTests;

	const FBatchCoordinatorFixture Fixture = CreateBatchCoordinatorFixture(*this);
	if (!Fixture.IsValid())
	{
		DestroyBatchCoordinatorFixture(Fixture);
		return false;
	}

	AShooterNetworkTestCoordinator* Target = Fixture.TargetCoordinator;
	AShooterNetworkTestCoordinator* Observer = Fixture.ObserverCoordinator;
	constexpr int32 AuthorityStart = 10;
	constexpr int32 AuthorityEnd = 12;
	constexpr int32 BatchShotCount = AuthorityEnd - AuthorityStart;

	// 武器从未开火：权威 Shot 数为 0，因此任何"现场读取权威计数"的实现都不可能得到批次增量。
	TestEqual(TEXT("the batch weapon never fired"), Fixture.BatchWeapon->GetAuthorityShotCountForAutomationTest(), 0);

	// ---- report 先到：此时 Target 的批次还没有 FreezeEnd ----
	Observer->SetRemoteObservedWeaponForTest(Fixture.BatchWeapon);
	Observer->SubmitRemoteConfirmedReportForTest(Fixture.TargetPlayerId, BatchShotCount, BatchShotCount,
		BatchShotCount, BatchShotCount, true);
	TestTrue(TEXT("the report is registered before the authority end exists"), Observer->HasRemoteBatchReportForTest());
	TestEqual(TEXT("a report without a frozen authority end must not evaluate"),
		Observer->GetRemoteBatchEvaluateCountForTest(), 0);
	TestFalse(TEXT("no verdict exists before the authority end"), Observer->IsRemoteBatchVerifiedForTest());

	// ---- 冻结后到：Arm + FreezeEnd（生产顺序：冻结终点后同 tick 通知观察端）----
	TestTrue(TEXT("the target arms its batch"), Target->ArmOwnRemoteBatchForTest(AuthorityStart, Fixture.BatchWeapon));
	TestEqual(TEXT("freezing yields the frozen expected delta"),
		Target->FreezeOwnRemoteBatchAndNotifyForTest(AuthorityEnd), BatchShotCount);
	TestEqual(TEXT("the deferred report is evaluated exactly once on freeze"),
		Observer->GetRemoteBatchEvaluateCountForTest(), 1);
	TestTrue(TEXT("the deferred verdict is valid"), Observer->IsRemoteBatchVerifiedForTest());
	TestEqual(TEXT("the comparison uses the frozen authority delta"),
		Observer->GetRemoteBatchComparedAuthorityDeltaForTest(), BatchShotCount);

	DestroyBatchCoordinatorFixture(Fixture);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterRemoteBatchCoordinatorEndFirstTest,
	"ShootGame.Network.RemoteBatch.CoordinatorAuthorityEndBeforeReportEvaluatesOnceOnReport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterRemoteBatchCoordinatorEndFirstTest::RunTest(const FString& Parameters)
{
	using namespace ShooterRemoteBatchCoordinatorAutomationTests;

	const FBatchCoordinatorFixture Fixture = CreateBatchCoordinatorFixture(*this);
	if (!Fixture.IsValid())
	{
		DestroyBatchCoordinatorFixture(Fixture);
		return false;
	}

	AShooterNetworkTestCoordinator* Target = Fixture.TargetCoordinator;
	AShooterNetworkTestCoordinator* Observer = Fixture.ObserverCoordinator;
	constexpr int32 AuthorityStart = 20;
	constexpr int32 AuthorityEnd = 23;
	constexpr int32 BatchShotCount = AuthorityEnd - AuthorityStart;

	TestEqual(TEXT("the batch weapon never fired"), Fixture.BatchWeapon->GetAuthorityShotCountForAutomationTest(), 0);

	// ---- 权威边界先到：Arm + FreezeEnd 完成时 observation report 还没有到 ----
	TestTrue(TEXT("the target arms its batch"), Target->ArmOwnRemoteBatchForTest(AuthorityStart, Fixture.BatchWeapon));
	TestEqual(TEXT("the target freezes its own authority end"),
		Target->FreezeOwnRemoteBatchAndNotifyForTest(AuthorityEnd), BatchShotCount);
	TestFalse(TEXT("no report has arrived yet"), Observer->HasRemoteBatchReportForTest());
	TestEqual(TEXT("a frozen batch without a report must not evaluate"),
		Observer->GetRemoteBatchEvaluateCountForTest(), 0);
	TestFalse(TEXT("no verdict exists before the report"), Observer->IsRemoteBatchVerifiedForTest());

	// ---- report 后到：到达即在同一个比较入口完成唯一一次比较 ----
	Observer->SetRemoteObservedWeaponForTest(Fixture.BatchWeapon);
	Observer->SubmitRemoteConfirmedReportForTest(Fixture.TargetPlayerId, BatchShotCount, BatchShotCount,
		BatchShotCount, BatchShotCount, true);
	TestEqual(TEXT("the late report is evaluated exactly once"), Observer->GetRemoteBatchEvaluateCountForTest(), 1);
	TestTrue(TEXT("the late report verdict is valid"), Observer->IsRemoteBatchVerifiedForTest());
	TestEqual(TEXT("the comparison uses the same frozen authority delta"),
		Observer->GetRemoteBatchComparedAuthorityDeltaForTest(), BatchShotCount);

	DestroyBatchCoordinatorFixture(Fixture);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterRemoteBatchCoordinatorExactlyOnceTest,
	"ShootGame.Network.RemoteBatch.CoordinatorEvaluateIsExactlyOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterRemoteBatchCoordinatorExactlyOnceTest::RunTest(const FString& Parameters)
{
	using namespace ShooterRemoteBatchCoordinatorAutomationTests;

	const FBatchCoordinatorFixture Fixture = CreateBatchCoordinatorFixture(*this);
	if (!Fixture.IsValid())
	{
		DestroyBatchCoordinatorFixture(Fixture);
		return false;
	}

	AShooterNetworkTestCoordinator* Target = Fixture.TargetCoordinator;
	AShooterNetworkTestCoordinator* Observer = Fixture.ObserverCoordinator;
	constexpr int32 AuthorityStart = 30;
	constexpr int32 AuthorityEnd = 32;
	constexpr int32 BatchShotCount = AuthorityEnd - AuthorityStart;

	// 先跑完一次完整场景（冻结先到、report 后到）。
	TestTrue(TEXT("the target arms its batch"), Target->ArmOwnRemoteBatchForTest(AuthorityStart, Fixture.BatchWeapon));
	Target->FreezeOwnRemoteBatchAndNotifyForTest(AuthorityEnd);
	Observer->SetRemoteObservedWeaponForTest(Fixture.BatchWeapon);
	Observer->SubmitRemoteConfirmedReportForTest(Fixture.TargetPlayerId, BatchShotCount, BatchShotCount,
		BatchShotCount, BatchShotCount, true);
	if (!TestEqual(TEXT("the completed scenario evaluated exactly once"),
			Observer->GetRemoteBatchEvaluateCountForTest(), 1) ||
		!TestTrue(TEXT("the completed scenario produced a valid verdict"), Observer->IsRemoteBatchVerifiedForTest()))
	{
		DestroyBatchCoordinatorFixture(Fixture);
		return false;
	}

	const int32 FrozenDeltaBeforeRepeat = Target->GetOwnRemoteBatchAuthorityDeltaForTest();
	const int32 ComparedDeltaBeforeRepeat = Observer->GetRemoteBatchComparedAuthorityDeltaForTest();

	// ---- 重复触发边界通知：比较次数与结论都不得变化 ----
	Observer->NotifyObservedBatchFrozen(AuthorityStart, AuthorityEnd, BatchShotCount);
	TestEqual(TEXT("a repeated boundary notification must not evaluate again"),
		Observer->GetRemoteBatchEvaluateCountForTest(), 1);
	TestTrue(TEXT("the verdict is unchanged by a repeated boundary notification"),
		Observer->IsRemoteBatchVerifiedForTest());

	// ---- 直接重复调用比较入口：同样不得再比较 ----
	Observer->EvaluateRemoteConfirmedBatch();
	TestEqual(TEXT("a repeated evaluate entry must not evaluate again"),
		Observer->GetRemoteBatchEvaluateCountForTest(), 1);
	TestTrue(TEXT("the verdict is unchanged by a repeated evaluate entry"), Observer->IsRemoteBatchVerifiedForTest());

	// ---- 下一阶段继续推进批次终点：冻结值与已用增量都不得改变 ----
	TestEqual(TEXT("re-freezing after the next stage shot returns the frozen delta"),
		Target->FreezeOwnRemoteBatchAndNotifyForTest(AuthorityEnd + 1), BatchShotCount);
	TestEqual(TEXT("the frozen batch end is unchanged"), Target->GetOwnRemoteBatchAuthorityDeltaForTest(),
		FrozenDeltaBeforeRepeat);
	TestEqual(TEXT("the compared authority delta is unchanged"),
		Observer->GetRemoteBatchComparedAuthorityDeltaForTest(), ComparedDeltaBeforeRepeat);
	TestEqual(TEXT("the evaluate count is still exactly one"), Observer->GetRemoteBatchEvaluateCountForTest(), 1);
	TestTrue(TEXT("the verdict survives the next stage shot"), Observer->IsRemoteBatchVerifiedForTest());

	DestroyBatchCoordinatorFixture(Fixture);
	return true;
}

#endif
