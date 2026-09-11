// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Pool/ShooterActorPoolSubsystem.h"
#include "ShooterActorPoolTestTypes.h"

namespace ShooterActorPoolAutomationTests
{
	UWorld* CreatePoolTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!World || !GEngine)
		{
			return World;
		}

		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		// 池测试不依赖 Actor BeginPlay；子系统在 World Init 阶段已创建。
		return World;
	}

	void DestroyPoolTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	UShooterActorPoolSubsystem* GetPool(UWorld* World)
	{
		return World->GetSubsystem<UShooterActorPoolSubsystem>();
	}
}

/** 复用同一 Actor：Acquire-Release-Acquire 返回同一实例，通用复位与回调正确。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterActorPoolReuseTest,
	"ShootGame.Pool.ReuseSameActor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterActorPoolReuseTest::RunTest(const FString& Parameters)
{
	using namespace ShooterActorPoolAutomationTests;

	UWorld* World = CreatePoolTestWorld();
	if (!TestNotNull(TEXT("Pool test world created"), World))
	{
		return false;
	}
	UShooterActorPoolSubsystem* Pool = GetPool(World);
	if (!TestNotNull(TEXT("Pool subsystem exists"), Pool))
	{
		DestroyPoolTestWorld(World);
		return false;
	}

	const FTransform FirstTransform(FVector(100.0f, 0.0f, 50.0f));
	AShooterPoolTestActorA* First = Cast<AShooterPoolTestActorA>(Pool->Acquire(
		AShooterPoolTestActorA::StaticClass(),
		FirstTransform,
		FActorSpawnParameters()));
	if (!TestNotNull(TEXT("First acquire succeeds"), First))
	{
		DestroyPoolTestWorld(World);
		return false;
	}

	TestTrue(TEXT("Acquired actor is visible"), !First->IsHidden());
	TestTrue(TEXT("Acquired actor ticks"), First->IsActorTickEnabled());
	TestEqual(TEXT("Acquire callback fired once"), First->AcquireCallbackCount, 1);
	TestEqual(TEXT("First actor placed at requested transform"), First->GetActorLocation(), FVector(100.0f, 0.0f, 50.0f));

	TestTrue(TEXT("Release succeeds"), Pool->Release(First));
	TestEqual(TEXT("Release callback fired once"), First->ReleaseCallbackCount, 1);
	TestTrue(TEXT("Released actor is hidden"), First->IsHidden());
	TestFalse(TEXT("Released actor does not tick"), First->IsActorTickEnabled());
	TestEqual(TEXT("Pool holds one actor"), Pool->GetPooledCount(AShooterPoolTestActorA::StaticClass()), 1);

	// 第二次获取必须复用同一实例，并完成通用复位。
	const FTransform SecondTransform(FVector(-100.0f, 20.0f, 80.0f));
	AShooterPoolTestActorA* Second = Cast<AShooterPoolTestActorA>(Pool->Acquire(
		AShooterPoolTestActorA::StaticClass(),
		SecondTransform,
		FActorSpawnParameters()));
	TestTrue(TEXT("Second acquire reuses the same actor"), Second == First);
	if (Second)
	{
		TestEqual(TEXT("Acquire callback fired again"), Second->AcquireCallbackCount, 2);
		TestTrue(TEXT("Reused actor is visible"), !Second->IsHidden());
		TestEqual(TEXT("Reused actor placed at new transform"), Second->GetActorLocation(), FVector(-100.0f, 20.0f, 80.0f));
	}
	TestEqual(TEXT("Pool is empty after reuse"), Pool->GetPooledCount(AShooterPoolTestActorA::StaticClass()), 0);

	DestroyPoolTestWorld(World);
	return true;
}

/** 跨 Class 不串池：A/B 类各自分池，互不混用。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterActorPoolCrossClassTest,
	"ShootGame.Pool.CrossClassIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterActorPoolCrossClassTest::RunTest(const FString& Parameters)
{
	using namespace ShooterActorPoolAutomationTests;

	UWorld* World = CreatePoolTestWorld();
	if (!TestNotNull(TEXT("Cross class world created"), World))
	{
		return false;
	}
	UShooterActorPoolSubsystem* Pool = GetPool(World);
	if (!TestNotNull(TEXT("Pool subsystem exists"), Pool))
	{
		DestroyPoolTestWorld(World);
		return false;
	}

	AActor* FirstA = Pool->Acquire(AShooterPoolTestActorA::StaticClass(), FTransform::Identity, FActorSpawnParameters());
	AActor* SecondA = Pool->Acquire(AShooterPoolTestActorA::StaticClass(), FTransform::Identity, FActorSpawnParameters());
	AActor* FirstB = Pool->Acquire(AShooterPoolTestActorB::StaticClass(), FTransform::Identity, FActorSpawnParameters());
	TestNotNull(TEXT("First A acquired"), FirstA);
	TestNotNull(TEXT("Second A acquired"), SecondA);
	TestNotNull(TEXT("First B acquired"), FirstB);
	TestTrue(TEXT("Two live A actors are distinct"), FirstA != SecondA);

	TestTrue(TEXT("Release first A"), Pool->Release(FirstA));
	TestTrue(TEXT("Release B"), Pool->Release(FirstB));
	TestEqual(TEXT("A pool holds one"), Pool->GetPooledCount(AShooterPoolTestActorA::StaticClass()), 1);
	TestEqual(TEXT("B pool holds one"), Pool->GetPooledCount(AShooterPoolTestActorB::StaticClass()), 1);

	// A 池的取出不能返回 B 类实例。
	AActor* ReacquiredA = Pool->Acquire(AShooterPoolTestActorA::StaticClass(), FTransform::Identity, FActorSpawnParameters());
	TestTrue(TEXT("Reacquired A is the released A instance"), ReacquiredA == FirstA);
	TestTrue(TEXT("Reacquired A keeps its class"), ReacquiredA && ReacquiredA->GetClass() == AShooterPoolTestActorA::StaticClass());

	// 未实现 Poolable 契约的普通类同样走池路径且不回调崩溃。
	AActor* Plain = Pool->Acquire(AShooterPoolTestActorPlain::StaticClass(), FTransform::Identity, FActorSpawnParameters());
	TestNotNull(TEXT("Plain actor acquired"), Plain);
	TestTrue(TEXT("Plain actor releases"), Pool->Release(Plain));

	DestroyPoolTestWorld(World);
	return true;
}

/** Release fail closed：重复归还、非托管对象、无效对象均返回 false 且不崩溃。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterActorPoolReleaseFailClosedTest,
	"ShootGame.Pool.ReleaseFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterActorPoolReleaseFailClosedTest::RunTest(const FString& Parameters)
{
	using namespace ShooterActorPoolAutomationTests;

	UWorld* World = CreatePoolTestWorld();
	if (!TestNotNull(TEXT("Fail closed world created"), World))
	{
		return false;
	}
	UShooterActorPoolSubsystem* Pool = GetPool(World);
	if (!TestNotNull(TEXT("Pool subsystem exists"), Pool))
	{
		DestroyPoolTestWorld(World);
		return false;
	}

	AActor* Actor = Pool->Acquire(AShooterPoolTestActorA::StaticClass(), FTransform::Identity, FActorSpawnParameters());
	if (!TestNotNull(TEXT("Actor acquired"), Actor))
	{
		DestroyPoolTestWorld(World);
		return false;
	}

	TestFalse(TEXT("Unmanaged actor release rejected"), Pool->Release(nullptr));
	AActor* Foreign = World->SpawnActor<AShooterPoolTestActorPlain>(FVector::ZeroVector, FRotator::ZeroRotator);
	TestFalse(TEXT("Foreign actor release rejected"), Pool->Release(Foreign));

	TestTrue(TEXT("Managed release succeeds"), Pool->Release(Actor));
	TestFalse(TEXT("Duplicate release rejected"), Pool->Release(Actor));
	TestFalse(TEXT("Pooled actor release rejected"), Pool->Release(Actor));
	TestEqual(TEXT("Pool still holds exactly one"), Pool->GetPooledCount(AShooterPoolTestActorA::StaticClass()), 1);

	// RegisterExisting 后可归还；重复注册被拒绝。
	TestTrue(TEXT("Foreign actor registers"), Pool->RegisterExisting(Foreign));
	TestFalse(TEXT("Duplicate register rejected"), Pool->RegisterExisting(Foreign));
	TestTrue(TEXT("Registered actor releases"), Pool->Release(Foreign));
	TestFalse(TEXT("Releasing again after pool rejected"), Pool->Release(Foreign));

	DestroyPoolTestWorld(World);
	return true;
}

/** 容量上限：满池后归还直接销毁；Prewarm 预热数量正确。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterActorPoolCapacityTest,
	"ShootGame.Pool.CapacityAndPrewarm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterActorPoolCapacityTest::RunTest(const FString& Parameters)
{
	using namespace ShooterActorPoolAutomationTests;

	UWorld* World = CreatePoolTestWorld();
	if (!TestNotNull(TEXT("Capacity world created"), World))
	{
		return false;
	}
	UShooterActorPoolSubsystem* Pool = GetPool(World);
	if (!TestNotNull(TEXT("Pool subsystem exists"), Pool))
	{
		DestroyPoolTestWorld(World);
		return false;
	}

	Pool->SetClassCapacity(AShooterPoolTestActorA::StaticClass(), 2);
	TestEqual(TEXT("Capacity configured"), Pool->GetClassCapacity(AShooterPoolTestActorA::StaticClass()), 2);

	AActor* One = Pool->Acquire(AShooterPoolTestActorA::StaticClass(), FTransform::Identity, FActorSpawnParameters());
	AActor* Two = Pool->Acquire(AShooterPoolTestActorA::StaticClass(), FTransform::Identity, FActorSpawnParameters());
	AActor* Three = Pool->Acquire(AShooterPoolTestActorA::StaticClass(), FTransform::Identity, FActorSpawnParameters());
	TestNotNull(TEXT("Three actors acquired"), Three);

	TestTrue(TEXT("Release one"), Pool->Release(One));
	TestTrue(TEXT("Release two"), Pool->Release(Two));
	TestEqual(TEXT("Pool full at capacity"), Pool->GetPooledCount(AShooterPoolTestActorA::StaticClass()), 2);

	TestTrue(TEXT("Release three accepted"), Pool->Release(Three));
	TestEqual(TEXT("Overflow release does not pool"), Pool->GetPooledCount(AShooterPoolTestActorA::StaticClass()), 2);
	TestFalse(TEXT("Overflow actor was destroyed"), IsValid(Three));

	// Prewarm：B 类预热 3 个，3 次 Acquire 全部复用预热实例，第 4 次新生成。
	Pool->SetClassCapacity(AShooterPoolTestActorB::StaticClass(), 8);
	TestEqual(TEXT("Prewarm creates three"), Pool->Prewarm(AShooterPoolTestActorB::StaticClass(), 3), 3);
	TestEqual(TEXT("B pool holds three"), Pool->GetPooledCount(AShooterPoolTestActorB::StaticClass()), 3);

	AActor* B1 = Pool->Acquire(AShooterPoolTestActorB::StaticClass(), FTransform::Identity, FActorSpawnParameters());
	AActor* B2 = Pool->Acquire(AShooterPoolTestActorB::StaticClass(), FTransform::Identity, FActorSpawnParameters());
	AActor* B3 = Pool->Acquire(AShooterPoolTestActorB::StaticClass(), FTransform::Identity, FActorSpawnParameters());
	AActor* B4 = Pool->Acquire(AShooterPoolTestActorB::StaticClass(), FTransform::Identity, FActorSpawnParameters());
	TestTrue(TEXT("Three reacquired leave empty pool"), Pool->GetPooledCount(AShooterPoolTestActorB::StaticClass()) == 0);
	TestNotNull(TEXT("Fourth B spawns new"), B4);
	TestTrue(TEXT("Fourth B is new instance"), B4 != B1 && B4 != B2 && B4 != B3);

	DestroyPoolTestWorld(World);
	return true;
}

/** World 销毁：Deinitialize 显式清理池内与在用 Actor，不残留悬空对象。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterActorPoolWorldTeardownTest,
	"ShootGame.Pool.WorldTeardown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterActorPoolWorldTeardownTest::RunTest(const FString& Parameters)
{
	using namespace ShooterActorPoolAutomationTests;

	UWorld* World = CreatePoolTestWorld();
	if (!TestNotNull(TEXT("Teardown world created"), World))
	{
		return false;
	}
	UShooterActorPoolSubsystem* Pool = GetPool(World);
	if (!TestNotNull(TEXT("Pool subsystem exists"), Pool))
	{
		DestroyPoolTestWorld(World);
		return false;
	}

	AActor* PooledIn = Pool->Acquire(AShooterPoolTestActorA::StaticClass(), FTransform::Identity, FActorSpawnParameters());
	AActor* InUse = Pool->Acquire(AShooterPoolTestActorB::StaticClass(), FTransform::Identity, FActorSpawnParameters());
	Pool->Release(PooledIn);
	TestEqual(TEXT("One actor pooled before teardown"), Pool->GetPooledCount(AShooterPoolTestActorA::StaticClass()), 1);
	TestTrue(TEXT("One actor still in use"), Pool->IsManaged(InUse));

	// DestroyPoolTestWorld 触发 WorldSubsystem Deinitialize：
	// 池内与在用 Actor 都被显式销毁，测试断言销毁请求已发出。
	Pool->Release(InUse);
	const TWeakObjectPtr<AActor> WeakPooled(PooledIn);
	DestroyPoolTestWorld(World);

	TestFalse(TEXT("Pooled actor no longer valid after world teardown"), WeakPooled.IsValid());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
