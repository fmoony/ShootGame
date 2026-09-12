// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/ShooterWeaponRuntimeSubsystem.h"
#include "ShooterWeaponPresentationTestTypes.h"
#include "../Weapon/ShooterWeaponTestTableTypes.h"

/**
 * S3 定向验证：Inventory 通过 WeaponRuntimeSubsystem 授予与归还 WeaponActor。
 *
 * 覆盖边界：
 * - 授予 = Runtime.Acquire + Inventory.AddWeapon；移除 / 装备失败回滚 / 死亡清理 = Runtime.Release；
 * - 同一池化 Actor 归还后复用：重新租用绑定新 Owner，静态配置与 WeaponId 不变、弹药复位；
 * - 拥有者销毁（断线 / 世界 teardown）走同一幂等归还边界。
 */
namespace ShooterWeaponPoolIntegrationAutomationTests
{
	UWorld* CreatePoolIntegrationTestWorld()
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

	void DestroyPoolIntegrationTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	AShooterWeaponPresentationTestCharacter* SpawnPoolIntegrationCharacter(FAutomationTestBase& Test, UWorld* World)
	{
		AShooterWeaponPresentationTestCharacter* Character = World->SpawnActor<AShooterWeaponPresentationTestCharacter>(
				FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("Pool integration character spawned"), Character))
		{
			return nullptr;
		}

		// 测试 World 没有 GameMode；直接驱动 WorldSettings，让组件订阅与后续 Actor BeginPlay 生效。
		if (AWorldSettings* WorldSettings = World->GetWorldSettings())
		{
			WorldSettings->NotifyBeginPlay();
		}
		return Character;
	}
}

/** 授予走运行时池 Acquire；移除走 Release；重新授予复用同一 Actor 且弹药复位。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponPoolGrantReuseTest, "ShootGame.Pool.WeaponGrant.AcquireBindsAndReuses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponPoolGrantReuseTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponPoolIntegrationAutomationTests;

	UWorld* World = CreatePoolIntegrationTestWorld();
	if (!TestNotNull(TEXT("Pool integration world created"), World))
	{
		return false;
	}

	UShooterWeaponRuntimeSubsystem* Runtime = World->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
	if (!TestNotNull(TEXT("Weapon runtime subsystem exists"), Runtime))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnPoolIntegrationCharacter(*this, World);
	if (!Character)
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	if (!TestNotNull(TEXT("Character owns Inventory"), Inventory) || !TestNotNull(TEXT("Character owns Equipment"), Equipment))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	// 第一次授予：弹匣 10 + 自动备弹 30。
	EShooterInventoryAddResult AddResult = EShooterInventoryAddResult::NotAuthoritative;
	AShooterWeapon* FirstWeapon = GrantTestWeapon(World, Inventory, AShooterInventoryOrderTestWeapon::StaticClass(),
		/*MagazineSize*/ 10,
		/*InitialReserveAmmo*/ -1,
		&AddResult);
	TestEqual(TEXT("First WeaponId grant succeeds"), static_cast<int32>(AddResult),
		static_cast<int32>(EShooterInventoryAddResult::Added));
	if (!TestNotNull(TEXT("Granted WeaponActor exists"), FirstWeapon))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	const FName FirstWeaponId = FirstWeapon->GetWeaponId();

	// 授予后的池状态：Actor 由池租出，不在池内待复用。
	TestTrue(TEXT("Granted WeaponActor is leased from the runtime"), Runtime->IsLeased(FirstWeapon));
	TestFalse(TEXT("Granted WeaponActor is not sitting in the pool"), Runtime->IsPooled(FirstWeapon));
	TestTrue(TEXT("Granted WeaponActor is owned by the character"), FirstWeapon->GetOwner() == Character);
	TestEqual(TEXT("Granted WeaponActor waits at Holstered"), static_cast<int32>(FirstWeapon->GetLifecycleState()),
		static_cast<int32>(EShooterWeaponLifecycleState::Holstered));

	// 装备后移除：Equipment 先清空当前装备，再把 Actor 归还池。
	TestTrue(TEXT("Granted weapon is equipped"), Equipment->EquipWeapon(FirstWeapon));
	TestTrue(TEXT("Equipped weapon is visible"), !FirstWeapon->IsHidden());

	// 消耗弹药后再移除：归还必须恢复初始弹药，不把上一持有者的弹药串给下一租用。
	TestTrue(TEXT("Consume one round before remove"), FirstWeapon->ConsumeAmmo(1));

	TestTrue(TEXT("Granted weapon is removed"), Inventory->RemoveWeapon(FirstWeapon));
	TestTrue(TEXT("Removed WeaponActor is returned to the pool"), Runtime->IsPooled(FirstWeapon));
	TestFalse(TEXT("Removed WeaponActor is no longer leased"), Runtime->IsLeased(FirstWeapon));
	TestFalse(TEXT("Removed WeaponActor is not destroyed while pooled"), FirstWeapon->IsActorBeingDestroyed());
	TestNull(TEXT("Removed WeaponActor has no owner"), FirstWeapon->GetOwner());
	TestTrue(TEXT("Removed WeaponActor is hidden"), FirstWeapon->IsHidden());
	TestEqual(TEXT("Removed WeaponActor returns to InPool"), static_cast<int32>(FirstWeapon->GetLifecycleState()),
		static_cast<int32>(EShooterWeaponLifecycleState::InPool));
	TestFalse(TEXT("Remove unbinds the WeaponActor from Inventory"), Inventory->ContainsWeapon(FirstWeapon));

	// 重新授予同一 WeaponId：必须复用同一 Actor，并重新绑定新租用状态。
	AShooterWeapon* SecondWeapon = Runtime->AcquireWeapon(FirstWeaponId, Character, nullptr);
	if (!TestNotNull(TEXT("Re-granted WeaponActor exists"), SecondWeapon))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}
	TestTrue(TEXT("Inventory accepts the reused actor"), Inventory->AddWeapon(SecondWeapon) == EShooterInventoryAddResult::Added);

	TestTrue(TEXT("Re-grant reuses the pooled WeaponActor"), SecondWeapon == FirstWeapon);
	TestTrue(TEXT("Reused WeaponActor is out of the pool again"), !Runtime->IsPooled(SecondWeapon));
	TestTrue(TEXT("Reused WeaponActor is leased again"), Runtime->IsLeased(SecondWeapon));
	TestEqual(TEXT("Reused WeaponActor keeps its WeaponId"), SecondWeapon->GetWeaponId(), FirstWeaponId);
	TestTrue(TEXT("Reused WeaponActor rebinds the owner"), SecondWeapon->GetOwner() == Character);
	TestEqual(TEXT("Reused WeaponActor restores the initial magazine"), SecondWeapon->GetBulletCount(), 10);
	TestEqual(TEXT("Reused WeaponActor restores the initial reserve"), SecondWeapon->GetReserveAmmo(), 30);
	TestEqual(TEXT("Reused WeaponActor waits at Holstered"), static_cast<int32>(SecondWeapon->GetLifecycleState()),
		static_cast<int32>(EShooterWeaponLifecycleState::Holstered));

	// 复用后的武器必须能正常装备与开火前置判定。
	TestTrue(TEXT("Reused weapon can be equipped"), Equipment->EquipWeapon(SecondWeapon));
	TestTrue(TEXT("Reused equipped weapon is visible"), !SecondWeapon->IsHidden());
	TestTrue(TEXT("Reused equipped weapon can consume ammo"), SecondWeapon->CanConsumeAmmo());

	DestroyPoolIntegrationTestWorld(World);
	return true;
}

/** 拥有者销毁（断线 / teardown）把租出的 WeaponActor 归还池，而不是留下 PendingKill 引用。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponPoolOwnerDestroyedReleaseTest,
	"ShootGame.Pool.WeaponRelease.OwnerDestroyReturnsWeaponToPool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponPoolOwnerDestroyedReleaseTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponPoolIntegrationAutomationTests;

	UWorld* World = CreatePoolIntegrationTestWorld();
	if (!TestNotNull(TEXT("Owner destroy world created"), World))
	{
		return false;
	}

	UShooterWeaponRuntimeSubsystem* Runtime = World->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
	if (!TestNotNull(TEXT("Weapon runtime subsystem exists"), Runtime))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnPoolIntegrationCharacter(*this, World);
	if (!Character)
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	// 注入行并重建快照，使 OwnerDestroyWeapon 具备可租用的 Bucket。
	const FName OwnerDestroyRow = AddTestWeaponRow(GetOrInjectRuntimeTestTable(World),
		MakeTestWeaponRow(AShooterInventoryOrderTestWeapon::StaticClass()));
	Runtime->InitializeWeaponRuntimeForTest();

	AShooterInventoryOrderTestWeapon* Weapon = Cast<AShooterInventoryOrderTestWeapon>(
		Runtime->AcquireWeapon(OwnerDestroyRow, Character, nullptr));
	if (!TestNotNull(TEXT("Weapon acquired for owner destroy test"), Weapon))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	TestTrue(TEXT("Weapon is leased before owner destruction"), Runtime->IsLeased(Weapon));
	TestTrue(TEXT("Weapon owner destroy handler is bound"), Character->OnDestroyed.IsBound());

	// 直接驱动拥有者销毁回调：Editor 自动化测试世界不投递 Actor 销毁通知
	// （实测 EndPlay 与 OnDestroyed 都不到达），通知投递本身由网络阶段 DisconnectCleanup 覆盖，
	// 这里验证「通知到达后的归还边界」。
	Weapon->SimulateOwnerDestroyedForTest();

	TestTrue(TEXT("Owner destruction returns the weapon to the pool"), Runtime->IsPooled(Weapon));
	TestFalse(TEXT("Owner destruction is no longer leased"), Runtime->IsLeased(Weapon));
	TestFalse(TEXT("Owner destruction does not destroy the pooled weapon"), Weapon->IsActorBeingDestroyed());
	TestNull(TEXT("Pooled weapon loses its owner"), Weapon->GetOwner());
	TestEqual(TEXT("Pooled weapon returns to InPool"), static_cast<int32>(Weapon->GetLifecycleState()),
		static_cast<int32>(EShooterWeaponLifecycleState::InPool));

	// 非池出生（NPC 兼容路径 / 测试直接 Spawn）无法归还，必须保留原有销毁语义。
	AShooterInventoryOrderTestWeapon* DirectlySpawned = World->SpawnActor<AShooterInventoryOrderTestWeapon>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Directly spawned weapon exists"), DirectlySpawned))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	TestFalse(TEXT("Directly spawned weapon is not pool managed"), Runtime->IsLeased(DirectlySpawned));
	DirectlySpawned->SimulateOwnerDestroyedForTest();
	TestTrue(TEXT("Non-pooled weapon falls back to destroy"), DirectlySpawned->IsActorBeingDestroyed());

	DestroyPoolIntegrationTestWorld(World);
	return true;
}

/** 死亡 / EndPlay 清理：ClearInventory 归还全部 WeaponActor 到对应 WeaponId Bucket。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponPoolDeathCleanupReleaseTest,
	"ShootGame.Pool.WeaponRelease.DeathClearReturnsAllWeaponsToPool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponPoolDeathCleanupReleaseTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponPoolIntegrationAutomationTests;

	UWorld* World = CreatePoolIntegrationTestWorld();
	if (!TestNotNull(TEXT("Death cleanup world created"), World))
	{
		return false;
	}

	UShooterWeaponRuntimeSubsystem* Runtime = World->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
	if (!TestNotNull(TEXT("Weapon runtime subsystem exists"), Runtime))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnPoolIntegrationCharacter(*this, World);
	if (!Character)
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	if (!TestNotNull(TEXT("Character owns Inventory"), Inventory) || !TestNotNull(TEXT("Character owns Equipment"), Equipment))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	EShooterInventoryAddResult AddResult = EShooterInventoryAddResult::NotAuthoritative;
	AShooterWeapon* PrimaryWeapon = GrantTestWeapon(World, Inventory, AShooterInventoryOrderTestWeapon::StaticClass(),
		10, -1, &AddResult);
	AShooterWeapon* SecondaryWeapon = GrantTestWeapon(World, Inventory,
		AShooterWeaponPresentationTestWeaponPrimary::StaticClass(), 10, -1, &AddResult);
	if (!TestNotNull(TEXT("Primary WeaponActor exists"), PrimaryWeapon) ||
		!TestNotNull(TEXT("Secondary WeaponActor exists"), SecondaryWeapon))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	TestTrue(TEXT("Primary weapon is equipped before death"), Equipment->EquipWeapon(PrimaryWeapon));

	// ClearInventory 是 Character::Die 与 Character::EndPlay（断线 / teardown）共用的清理入口；
	// Editor 自动化测试世界不投递 EndPlay，因此这里直接驱动该入口，
	// 死亡链路的通知投递由网络阶段的死亡 / DisconnectCleanup 场景覆盖。
	Inventory->ClearInventory();

	TestEqual(TEXT("Death cleanup empties Inventory entries"), Inventory->GetWeaponCount(), 0);
	TestNull(TEXT("Death cleanup clears Equipment CurrentWeaponActor"), Equipment->GetCurrentWeaponActor());

	TestTrue(TEXT("Death cleanup pools the equipped weapon"), Runtime->IsPooled(PrimaryWeapon));
	TestTrue(TEXT("Death cleanup pools the holstered weapon"), Runtime->IsPooled(SecondaryWeapon));
	TestFalse(TEXT("Death cleanup does not destroy the equipped weapon"), PrimaryWeapon->IsActorBeingDestroyed());
	TestFalse(TEXT("Death cleanup does not destroy the holstered weapon"), SecondaryWeapon->IsActorBeingDestroyed());
	TestNull(TEXT("Pooled equipped weapon loses its owner"), PrimaryWeapon->GetOwner());
	TestTrue(TEXT("Pooled equipped weapon is hidden"), PrimaryWeapon->IsHidden());

	// 重复清理幂等：不重复归还，也不产生错误状态。
	Inventory->ClearInventory();
	TestTrue(TEXT("Repeated death cleanup keeps both weapons pooled exactly once"),
		Runtime->IsPooled(PrimaryWeapon) && Runtime->IsPooled(SecondaryWeapon));
	TestEqual(TEXT("Repeated death cleanup keeps Inventory empty"), Inventory->GetWeaponCount(), 0);

	DestroyPoolIntegrationTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
