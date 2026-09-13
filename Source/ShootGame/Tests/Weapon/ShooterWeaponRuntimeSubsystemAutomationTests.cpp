// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "ShootGame.h"
#include "UObject/Package.h"
#include "ShooterWeaponRuntimeTestTypes.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/Data/ShooterWeaponConfigRow.h"
#include "Weapons/Subsystems/ShooterWeaponRuntimeSubsystem.h"

/**
 * S1 定向验证：启动配置快照 + WeaponId 预热池（重构方案 8.S1）。
 *
 * 覆盖边界：
 * - 启动导入：表缺失 / 结构错误导致初始化失败；非法行不建 Bucket；
 * - 预热：每个合法 WeaponId 按 InitialPoolSize 建立配置完成的初始 Actor 池；
 * - Acquire / Release：租用归属、生命周期（InPool <-> Holstered）与归还复位；
 * - 耗尽后只从冻结快照弹性 Spawn 并直接返回 Actor 引用；
 * - 未知 WeaponId 与重复归还 fail closed。
 */
namespace ShooterWeaponRuntimeSubsystemAutomationTests
{
	UWorld* CreateRuntimeTestWorld()
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

	void DestroyRuntimeTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	UDataTable* CreateRuntimeTestTable()
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = FShooterWeaponConfigRow::StaticStruct();
		return Table;
	}

	/** 从测试武器 CDO 导出基础行，只覆盖弹药与预热数量。 */
	FShooterWeaponConfigRow MakeRuntimeTestRow(TSubclassOf<AShooterWeapon> WeaponActorClass, int32 MagazineSize,
		int32 InitialReserveAmmo, int32 InitialPoolSize)
	{
		FShooterWeaponConfigRow Row;
		if (const AShooterWeapon* WeaponDefaults = WeaponActorClass
			? WeaponActorClass->GetDefaultObject<AShooterWeapon>()
			: nullptr)
		{
			Row = WeaponDefaults->CaptureWeaponConfigRow();
		}

		Row.WeaponActorClass = WeaponActorClass;
		Row.MagazineSize = MagazineSize;
		Row.InitialReserveAmmo = InitialReserveAmmo;
		Row.InitialPoolSize = InitialPoolSize;
		return Row;
	}

	void AddRuntimeTestRow(UDataTable* Table, FName RowName, const FShooterWeaponConfigRow& Row)
	{
		Table->AddRow(RowName, Row);
	}

	/** 触发 World BeginPlay：子系统启动导入在 Actor BeginPlay 前执行。
	 *  注意 UE5.6 的 WorldSettings::NotifyBeginPlay 只派发 Actor BeginPlay、不经过
	 *  UWorld::BeginPlay（子系统 OnWorldBeginPlay 所在），测试世界必须直接调用后者；
	 *  真实游戏由 UEngine::LoadMap → World->BeginPlay() 保证同一时序。 */
	bool BeginRuntimeWorldPlay(FAutomationTestBase& Test, UWorld* World)
	{
		if (!Test.TestNotNull(TEXT("Runtime test world valid"), World))
		{
			return false;
		}

		World->BeginPlay();
		return true;
	}
}

/** 启动导入：合法行建立快照 Bucket 并按 InitialPoolSize 预热；Actor 携带 WeaponId 与静态配置。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponRuntimeStartupSnapshotTest,
	"ShootGame.WeaponRuntime.Startup.BuildsSnapshotAndPrewarms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponRuntimeStartupSnapshotTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponRuntimeSubsystemAutomationTests;

	UWorld* World = CreateRuntimeTestWorld();
	if (!TestNotNull(TEXT("Runtime test world created"), World))
	{
		return false;
	}

	UShooterWeaponRuntimeSubsystem* Runtime = World->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
	if (!TestNotNull(TEXT("Weapon runtime subsystem exists"), Runtime))
	{
		DestroyRuntimeTestWorld(World);
		return false;
	}

	UDataTable* Table = CreateRuntimeTestTable();
	AddRuntimeTestRow(Table, TEXT("TestRifle"),
		MakeRuntimeTestRow(AShooterRuntimePoolTestWeapon::StaticClass(), /*MagazineSize*/ 5, /*Reserve*/ 7, /*Pool*/ 2));
	AddRuntimeTestRow(Table, TEXT("TestPistol"),
		MakeRuntimeTestRow(AShooterRuntimePoolTestWeapon::StaticClass(), /*MagazineSize*/ 3, /*Reserve*/ -1, /*Pool*/ 3));
	Runtime->SetWeaponTableOverride(Table);

	if (!BeginRuntimeWorldPlay(*this, World))
	{
		DestroyRuntimeTestWorld(World);
		return false;
	}

	TestTrue(TEXT("Runtime initialization succeeds"), Runtime->IsRuntimeInitialized());
	TestTrue(TEXT("Runtime has TestRifle"), Runtime->HasWeaponId(TEXT("TestRifle")));
	TestTrue(TEXT("Runtime has TestPistol"), Runtime->HasWeaponId(TEXT("TestPistol")));
	TestFalse(TEXT("Runtime rejects unknown WeaponId"), Runtime->HasWeaponId(TEXT("Unknown")));
	TestEqual(TEXT("TestRifle prewarms 2 actors"), Runtime->GetAvailableCount(TEXT("TestRifle")), 2);
	TestEqual(TEXT("TestPistol prewarms 3 actors"), Runtime->GetAvailableCount(TEXT("TestPistol")), 3);
	TestEqual(TEXT("TestRifle leases nothing at startup"), Runtime->GetLeasedCount(TEXT("TestRifle")), 0);

	const FShooterWeaponConfigRow* RifleConfig = Runtime->FindRuntimeConfig(TEXT("TestRifle"));
	if (!TestNotNull(TEXT("TestRifle runtime config resolvable"), RifleConfig))
	{
		DestroyRuntimeTestWorld(World);
		return false;
	}
	TestEqual(TEXT("Snapshot keeps magazine size"), RifleConfig->MagazineSize, 5);
	TestEqual(TEXT("Snapshot keeps initial reserve"), RifleConfig->InitialReserveAmmo, 7);
	TestEqual(TEXT("Snapshot keeps pool size"), RifleConfig->InitialPoolSize, 2);
	TestTrue(TEXT("Snapshot keeps actor class"),
		RifleConfig->WeaponActorClass == AShooterRuntimePoolTestWeapon::StaticClass());

	// 预热 Actor 的身份与静态配置：取出两把必须互不相同且都携带该 WeaponId 的配置。
	AActor* Owner = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	AShooterWeapon* First = Runtime->AcquireWeapon(TEXT("TestRifle"), Owner, nullptr);
	AShooterWeapon* Second = Runtime->AcquireWeapon(TEXT("TestRifle"), Owner, nullptr);
	if (!TestNotNull(TEXT("First prewarmed weapon acquired"), First) ||
		!TestNotNull(TEXT("Second prewarmed weapon acquired"), Second))
	{
		DestroyRuntimeTestWorld(World);
		return false;
	}

	TestFalse(TEXT("Prewarmed weapons are distinct actors"), First == Second);
	TestEqual(TEXT("Prewarmed weapon carries WeaponId"), First->GetWeaponId(), FName(TEXT("TestRifle")));
	TestEqual(TEXT("Prewarmed weapon applies snapshot magazine size"), First->GetMagazineSize(), 5);
	TestEqual(TEXT("Prewarmed weapon applies snapshot initial reserve"), First->GetInitialReserveAmmo(), 7);
	TestTrue(TEXT("Prewarmed weapon is owned by acquirer"), First->GetOwner() == Owner);
	TestEqual(TEXT("Acquire leases first weapon"), Runtime->GetLeasedCount(TEXT("TestRifle")), 2);
	TestEqual(TEXT("Acquire drains available pool"), Runtime->GetAvailableCount(TEXT("TestRifle")), 0);

	DestroyRuntimeTestWorld(World);
	return true;
}

/** 启动失败边界：表结构错误导致初始化失败；单行非法只跳过该行、不影响其余 Bucket。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponRuntimeStartupInvalidTableTest,
	"ShootGame.WeaponRuntime.Startup.InvalidRowAndTableFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponRuntimeStartupInvalidTableTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponRuntimeSubsystemAutomationTests;

	// 初始化失败的 Error 与非法行跳过的 Error 都是该路径的预期输出，显式声明避免误判。
	AddExpectedError(TEXT("WeaponRuntime initialization failed: table"),
		EAutomationExpectedErrorFlags::Contains, /*Count*/ 1);
	AddExpectedError(TEXT("WeaponRuntime skipped invalid row"),
		EAutomationExpectedErrorFlags::Contains, /*Count*/ 2);

	// 1) 结构错误的表：RowStruct 不是 FShooterWeaponConfigRow，初始化必须失败。
	UWorld* BadStructWorld = CreateRuntimeTestWorld();
	if (TestNotNull(TEXT("Bad struct world created"), BadStructWorld))
	{
		UShooterWeaponRuntimeSubsystem* Runtime = BadStructWorld->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
		if (TestNotNull(TEXT("Bad struct runtime exists"), Runtime))
		{
			UDataTable* BadTable = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
			// RowStruct 保持为空：与正式行结构不符。
			Runtime->SetWeaponTableOverride(BadTable);
			if (BeginRuntimeWorldPlay(*this, BadStructWorld))
			{
				TestFalse(TEXT("Wrong row struct fails initialization"), Runtime->IsRuntimeInitialized());
				TestFalse(TEXT("Wrong row struct builds no bucket"), Runtime->HasWeaponId(TEXT("Any")));
			}
		}
		DestroyRuntimeTestWorld(BadStructWorld);
	}

	// 2) 混合行表：abstract ActorClass 行与 InitialPoolSize=0 行不建 Bucket，合法行照常预热。
	UWorld* World = CreateRuntimeTestWorld();
	if (!TestNotNull(TEXT("Mixed row world created"), World))
	{
		return false;
	}

	UShooterWeaponRuntimeSubsystem* Runtime = World->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
	if (!TestNotNull(TEXT("Mixed row runtime exists"), Runtime))
	{
		DestroyRuntimeTestWorld(World);
		return false;
	}

	UDataTable* Table = CreateRuntimeTestTable();
	AddRuntimeTestRow(Table, TEXT("AbstractClass"),
		MakeRuntimeTestRow(AShooterWeapon::StaticClass(), /*MagazineSize*/ 10, /*Reserve*/ -1, /*Pool*/ 1));
	AddRuntimeTestRow(Table, TEXT("ZeroPoolSize"),
		MakeRuntimeTestRow(AShooterRuntimePoolTestWeapon::StaticClass(), /*MagazineSize*/ 10, /*Reserve*/ -1, /*Pool*/ 0));
	AddRuntimeTestRow(Table, TEXT("ValidRow"),
		MakeRuntimeTestRow(AShooterRuntimePoolTestWeapon::StaticClass(), /*MagazineSize*/ 6, /*Reserve*/ -1, /*Pool*/ 1));
	Runtime->SetWeaponTableOverride(Table);

	if (!BeginRuntimeWorldPlay(*this, World))
	{
		DestroyRuntimeTestWorld(World);
		return false;
	}

	TestTrue(TEXT("Mixed table still initializes from the valid row"), Runtime->IsRuntimeInitialized());
	TestFalse(TEXT("Abstract ActorClass row builds no bucket"), Runtime->HasWeaponId(TEXT("AbstractClass")));
	TestFalse(TEXT("Zero pool size row builds no bucket"), Runtime->HasWeaponId(TEXT("ZeroPoolSize")));
	TestTrue(TEXT("Valid row builds its bucket"), Runtime->HasWeaponId(TEXT("ValidRow")));
	TestEqual(TEXT("Valid row prewarms exactly once"), Runtime->GetAvailableCount(TEXT("ValidRow")), 1);

	DestroyRuntimeTestWorld(World);
	return true;
}

/** Acquire / Release：租用复位、归属改写与归还后回到可复用状态；重复归还 fail closed。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponRuntimeAcquireReleaseTest,
	"ShootGame.WeaponRuntime.AcquireRelease.LeasesAndRestores",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponRuntimeAcquireReleaseTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponRuntimeSubsystemAutomationTests;

	UWorld* World = CreateRuntimeTestWorld();
	if (!TestNotNull(TEXT("Acquire release world created"), World))
	{
		return false;
	}

	UShooterWeaponRuntimeSubsystem* Runtime = World->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
	if (!TestNotNull(TEXT("Acquire release runtime exists"), Runtime))
	{
		DestroyRuntimeTestWorld(World);
		return false;
	}

	UDataTable* Table = CreateRuntimeTestTable();
	AddRuntimeTestRow(Table, TEXT("TestRifle"),
		MakeRuntimeTestRow(AShooterRuntimePoolTestWeapon::StaticClass(), /*MagazineSize*/ 5, /*Reserve*/ 7, /*Pool*/ 2));
	Runtime->SetWeaponTableOverride(Table);

	if (!BeginRuntimeWorldPlay(*this, World))
	{
		DestroyRuntimeTestWorld(World);
		return false;
	}

	AActor* FirstOwner = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	AShooterWeapon* Weapon = Runtime->AcquireWeapon(TEXT("TestRifle"), FirstOwner, nullptr);
	if (!TestNotNull(TEXT("Weapon acquired"), Weapon))
	{
		DestroyRuntimeTestWorld(World);
		return false;
	}

	TestTrue(TEXT("Acquired weapon is leased"), Runtime->IsLeased(Weapon));
	TestEqual(TEXT("Acquire leases one"), Runtime->GetLeasedCount(TEXT("TestRifle")), 1);
	TestEqual(TEXT("Acquire consumes one available"), Runtime->GetAvailableCount(TEXT("TestRifle")), 1);
	TestEqual(TEXT("Acquired weapon waits at Holstered"), static_cast<int32>(Weapon->GetLifecycleState()),
		static_cast<int32>(EShooterWeaponLifecycleState::Holstered));
	TestTrue(TEXT("Holstered weapon stays hidden until equipped"), Weapon->IsHidden());

	TestTrue(TEXT("Release returns the weapon"), Runtime->ReleaseWeapon(Weapon));
	TestEqual(TEXT("Release restores availability"), Runtime->GetAvailableCount(TEXT("TestRifle")), 2);
	TestEqual(TEXT("Release clears lease"), Runtime->GetLeasedCount(TEXT("TestRifle")), 0);
	TestFalse(TEXT("Released weapon is no longer leased"), Runtime->IsLeased(Weapon));
	TestEqual(TEXT("Released weapon returns to InPool"), static_cast<int32>(Weapon->GetLifecycleState()),
		static_cast<int32>(EShooterWeaponLifecycleState::InPool));
	TestNull(TEXT("Released weapon loses its owner"), Weapon->GetOwner());
	TestTrue(TEXT("Released weapon is hidden"), Weapon->IsHidden());
	TestFalse(TEXT("Released weapon is not destroyed"), Weapon->IsActorBeingDestroyed());

	// WeaponId 与静态配置永久保留：复用租用不得重新选择配置。
	TestEqual(TEXT("Released weapon keeps its WeaponId"), Weapon->GetWeaponId(), FName(TEXT("TestRifle")));
	TestEqual(TEXT("Released weapon keeps static config"), Weapon->GetMagazineSize(), 5);

	// 复用：再次取出必须得到同一 Actor（单实体池）并重新绑定新 Owner。
	AActor* SecondOwner = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	AShooterWeapon* Reacquired = Runtime->AcquireWeapon(TEXT("TestRifle"), SecondOwner, nullptr);
	if (!TestNotNull(TEXT("Weapon reacquired"), Reacquired))
	{
		DestroyRuntimeTestWorld(World);
		return false;
	}
	TestTrue(TEXT("Reuse returns the pooled actor"), Reacquired == Weapon);
	TestTrue(TEXT("Reuse rebinds the new owner"), Reacquired->GetOwner() == SecondOwner);
	TestEqual(TEXT("Reacquired weapon waits at Holstered"), static_cast<int32>(Reacquired->GetLifecycleState()),
		static_cast<int32>(EShooterWeaponLifecycleState::Holstered));

	// 重复归还 / 未知 WeaponId 全部 fail closed。
	TestTrue(TEXT("Second release succeeds"), Runtime->ReleaseWeapon(Reacquired));
	TestFalse(TEXT("Repeated release is rejected"), Runtime->ReleaseWeapon(Reacquired));
	TestNull(TEXT("Unknown WeaponId acquires nothing"), Runtime->AcquireWeapon(TEXT("Unknown"), FirstOwner, nullptr));
	TestNull(TEXT("None WeaponId acquires nothing"), Runtime->AcquireWeapon(NAME_None, FirstOwner, nullptr));

	DestroyRuntimeTestWorld(World);
	return true;
}

/** 可用池耗尽：只从冻结快照弹性 Spawn，成功直接返回 Actor 引用；扩容 Actor 留在原 Bucket 复用。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponRuntimeElasticSpawnTest,
	"ShootGame.WeaponRuntime.AcquireRelease.ExhaustedPoolSpawnsFromSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponRuntimeElasticSpawnTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponRuntimeSubsystemAutomationTests;

	UWorld* World = CreateRuntimeTestWorld();
	if (!TestNotNull(TEXT("Elastic spawn world created"), World))
	{
		return false;
	}

	UShooterWeaponRuntimeSubsystem* Runtime = World->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
	if (!TestNotNull(TEXT("Elastic spawn runtime exists"), Runtime))
	{
		DestroyRuntimeTestWorld(World);
		return false;
	}

	UDataTable* Table = CreateRuntimeTestTable();
	AddRuntimeTestRow(Table, TEXT("SinglePool"),
		MakeRuntimeTestRow(AShooterRuntimePoolTestWeapon::StaticClass(), /*MagazineSize*/ 4, /*Reserve*/ -1, /*Pool*/ 1));
	Runtime->SetWeaponTableOverride(Table);

	if (!BeginRuntimeWorldPlay(*this, World))
	{
		DestroyRuntimeTestWorld(World);
		return false;
	}

	AActor* Owner = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);

	// 预热 1 个：第一次取出池内 Actor，第二、三次耗尽后弹性 Spawn。
	AShooterWeapon* Pooled = Runtime->AcquireWeapon(TEXT("SinglePool"), Owner, nullptr);
	AShooterWeapon* ElasticA = Runtime->AcquireWeapon(TEXT("SinglePool"), Owner, nullptr);
	AShooterWeapon* ElasticB = Runtime->AcquireWeapon(TEXT("SinglePool"), Owner, nullptr);
	if (!TestNotNull(TEXT("Pooled weapon acquired"), Pooled) ||
		!TestNotNull(TEXT("Elastic weapon A acquired"), ElasticA) ||
		!TestNotNull(TEXT("Elastic weapon B acquired"), ElasticB))
	{
		DestroyRuntimeTestWorld(World);
		return false;
	}

	TestFalse(TEXT("Elastic weapon A differs from pooled"), ElasticA == Pooled);
	TestFalse(TEXT("Elastic weapon B differs from pooled"), ElasticB == Pooled);
	TestFalse(TEXT("Elastic weapons are distinct"), ElasticA == ElasticB);
	TestEqual(TEXT("Exhausted acquire leases all three"), Runtime->GetLeasedCount(TEXT("SinglePool")), 3);
	TestEqual(TEXT("Exhausted acquire leaves no available"), Runtime->GetAvailableCount(TEXT("SinglePool")), 0);

	// 弹性 Spawn 的 Actor 与预热 Actor 同源同配置：同一 WeaponId、同一快照配置。
	TestEqual(TEXT("Elastic weapon A carries WeaponId"), ElasticA->GetWeaponId(), FName(TEXT("SinglePool")));
	TestEqual(TEXT("Elastic weapon applies snapshot config"), ElasticA->GetMagazineSize(), 4);

	// 全部归还：扩容 Actor 留在该 WeaponId Bucket 复用，不销毁也不流失。
	TestTrue(TEXT("Pooled weapon released"), Runtime->ReleaseWeapon(Pooled));
	TestTrue(TEXT("Elastic weapon A released"), Runtime->ReleaseWeapon(ElasticA));
	TestTrue(TEXT("Elastic weapon B released"), Runtime->ReleaseWeapon(ElasticB));
	TestEqual(TEXT("All three actors wait for reuse"), Runtime->GetAvailableCount(TEXT("SinglePool")), 3);
	TestEqual(TEXT("No actor stays leased"), Runtime->GetLeasedCount(TEXT("SinglePool")), 0);
	TestFalse(TEXT("Elastic actor survives release"), ElasticA->IsActorBeingDestroyed());

	// 复用不回退预热规模：再次取出仍来自扩容后的 Bucket。
	AShooterWeapon* Reused = Runtime->AcquireWeapon(TEXT("SinglePool"), Owner, nullptr);
	if (TestNotNull(TEXT("Weapon reused after elastic growth"), Reused))
	{
		TestTrue(TEXT("Reuse draws from the grown bucket"), Reused == Pooled || Reused == ElasticA || Reused == ElasticB);
		TestEqual(TEXT("Grown bucket still has two available"), Runtime->GetAvailableCount(TEXT("SinglePool")), 2);
	}

	DestroyRuntimeTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
