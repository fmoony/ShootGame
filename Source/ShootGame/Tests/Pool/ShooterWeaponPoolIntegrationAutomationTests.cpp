// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "Pool/ShooterActorPoolSubsystem.h"
#include "ShooterActorPoolTestTypes.h"
#include "ShooterWeaponPresentationTestTypes.h"
#include "Weapons/ShooterWeapon.h"

/**
 * B3 定向验证：Inventory 通过对象池 Acquire / Release WeaponActor。
 *
 * 覆盖边界：
 * - 授予 = 池 Acquire，移除 / 回滚 / 死亡清理 = 池 Release（不再 Destroy）；
 * - 同一池化 Actor 复用后重新绑定 Owner / RowName / InstanceId，不继承旧状态；
 * - Acquire 失败必须回滚已写入的 WeaponInstance 并释放 Slot；
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

	AShooterWeaponPresentationTestCharacter* SpawnPoolIntegrationCharacter(
		FAutomationTestBase& Test,
		UWorld* World)
	{
		AShooterWeaponPresentationTestCharacter* Character =
			World->SpawnActor<AShooterWeaponPresentationTestCharacter>(
				FVector::ZeroVector,
				FRotator::ZeroRotator);
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

/** 授予走池 Acquire；移除走池 Release；重新授予复用同一 Actor 且不继承旧绑定与旧行配置。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponPoolGrantReuseTest,
	"ShootGame.Pool.WeaponGrant.AcquireBindsAndReuses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponPoolGrantReuseTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponPoolIntegrationAutomationTests;

	UWorld* World = CreatePoolIntegrationTestWorld();
	if (!TestNotNull(TEXT("Pool integration world created"), World))
	{
		return false;
	}

	UShooterActorPoolSubsystem* Pool = World->GetSubsystem<UShooterActorPoolSubsystem>();
	if (!TestNotNull(TEXT("Pool subsystem exists"), Pool))
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
	if (!TestNotNull(TEXT("Character owns Inventory"), Inventory) ||
		!TestNotNull(TEXT("Character owns Equipment"), Equipment))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	UDataTable* Table = GetOrCreateTestWeaponTable(Inventory);
	if (!TestNotNull(TEXT("Test weapon table injected"), Table))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	// 第一次授予：10 发弹匣 + 显式 30 发备弹。
	const FName FirstRowName = AddTestWeaponRow(
		Table,
		MakeTestWeaponRow(AShooterInventoryOrderTestWeapon::StaticClass(), /*MagazineSize*/ 10, /*InitialReserveAmmo*/ 30));
	FGuid FirstInstanceId;
	TestEqual(
		TEXT("First WeaponRow grant succeeds"),
		static_cast<int32>(Inventory->TryAddWeaponRow(FirstRowName, FirstInstanceId)),
		static_cast<int32>(EShooterInventoryAddResult::Added));

	AShooterInventoryOrderTestWeapon* FirstWeapon = Cast<AShooterInventoryOrderTestWeapon>(
		Inventory->FindWeaponActor(FirstInstanceId));
	if (!TestNotNull(TEXT("Granted WeaponActor exists"), FirstWeapon))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	// 授予后的池状态：Actor 由池管理（在使用中），不是池内待复用对象。
	TestTrue(TEXT("Granted WeaponActor is acquired from the pool"), Pool->IsManaged(FirstWeapon));
	TestFalse(TEXT("Granted WeaponActor is not sitting in the pool"), Pool->IsPooled(FirstWeapon));
	TestEqual(
		TEXT("Granting does not return anything to the pool"),
		Pool->GetPooledCount(AShooterInventoryOrderTestWeapon::StaticClass()),
		0);
	TestEqual(TEXT("Granted WeaponActor is bound to the instance"), FirstWeapon->GetBoundInstanceId(), FirstInstanceId);
	TestEqual(TEXT("Granted WeaponActor carries the weapon row name"), FirstWeapon->GetWeaponRowName(), FirstRowName);
	TestEqual(TEXT("Granted WeaponActor applies the row magazine size"), FirstWeapon->GetMagazineSize(), 10);
	TestTrue(TEXT("Granted WeaponActor is owned by the character"), FirstWeapon->GetOwner() == Character);
	TestEqual(
		TEXT("Granted WeaponActor waits at Holstered"),
		static_cast<int32>(FirstWeapon->GetLifecycleState()),
		static_cast<int32>(EShooterWeaponLifecycleState::Holstered));

	// 装备后移除：Equipment 先清空当前装备，再把 Actor 归还池。
	TestTrue(TEXT("Granted weapon is equipped"), Equipment->EquipWeapon(FirstInstanceId));
	TestTrue(TEXT("Equipped weapon is visible"), !FirstWeapon->IsHidden());

	TestTrue(TEXT("Granted instance is removed"), Inventory->RemoveWeaponInstance(FirstInstanceId));
	TestTrue(TEXT("Removed WeaponActor is returned to the pool"), Pool->IsPooled(FirstWeapon));
	TestFalse(TEXT("Removed WeaponActor is no longer in use"), Pool->IsManaged(FirstWeapon));
	TestFalse(TEXT("Removed WeaponActor is not destroyed while pooled"), FirstWeapon->IsActorBeingDestroyed());
	TestNull(TEXT("Removed WeaponActor has no owner"), FirstWeapon->GetOwner());
	TestFalse(TEXT("Removed WeaponActor has no instance binding"), FirstWeapon->GetBoundInstanceId().IsValid());
	TestTrue(TEXT("Removed WeaponActor clears the weapon row name"), FirstWeapon->GetWeaponRowName().IsNone());
	TestTrue(TEXT("Removed WeaponActor is hidden"), FirstWeapon->IsHidden());
	TestEqual(
		TEXT("Removed WeaponActor returns to InPool"),
		static_cast<int32>(FirstWeapon->GetLifecycleState()),
		static_cast<int32>(EShooterWeaponLifecycleState::InPool));
	TestEqual(
		TEXT("Removed WeaponActor waits in its class pool"),
		Pool->GetPooledCount(AShooterInventoryOrderTestWeapon::StaticClass()),
		1);
	TestNull(TEXT("Removed instance leaves no Inventory actor mapping"), Inventory->FindWeaponActor(FirstInstanceId));

	// 第二次授予同一 WeaponActorClass 的另一行：必须复用同一实例，并重新绑定新行与新 Instance。
	const FName SecondRowName = AddTestWeaponRow(
		Table,
		MakeTestWeaponRow(AShooterInventoryOrderTestWeapon::StaticClass(), /*MagazineSize*/ 4, /*InitialReserveAmmo*/ 8));
	FGuid SecondInstanceId;
	TestEqual(
		TEXT("Second WeaponRow grant succeeds"),
		static_cast<int32>(Inventory->TryAddWeaponRow(SecondRowName, SecondInstanceId)),
		static_cast<int32>(EShooterInventoryAddResult::Added));

	AShooterInventoryOrderTestWeapon* SecondWeapon = Cast<AShooterInventoryOrderTestWeapon>(
		Inventory->FindWeaponActor(SecondInstanceId));
	if (!TestNotNull(TEXT("Re-granted WeaponActor exists"), SecondWeapon))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	TestTrue(TEXT("Re-grant reuses the pooled WeaponActor"), SecondWeapon == FirstWeapon);
	TestTrue(TEXT("Reused WeaponActor is out of the pool again"), !Pool->IsPooled(SecondWeapon));
	TestTrue(TEXT("Reused WeaponActor is managed again"), Pool->IsManaged(SecondWeapon));
	TestTrue(TEXT("Reused WeaponActor receives a new InstanceId"), SecondInstanceId != FirstInstanceId);
	TestEqual(TEXT("Reused WeaponActor rebinds the new instance"), SecondWeapon->GetBoundInstanceId(), SecondInstanceId);
	TestEqual(TEXT("Reused WeaponActor rebinds the new weapon row"), SecondWeapon->GetWeaponRowName(), SecondRowName);
	TestTrue(TEXT("Reused WeaponActor rebinds the owner"), SecondWeapon->GetOwner() == Character);
	TestEqual(TEXT("Reused WeaponActor applies the new row magazine size"), SecondWeapon->GetMagazineSize(), 4);
	TestEqual(TEXT("Reused WeaponActor does not inherit the old ammo mirror"), SecondWeapon->GetBulletCount(), 4);
	TestEqual(TEXT("Reused instance starts from the new row ammo"), Inventory->GetMagazineAmmo(SecondInstanceId), 4);
	TestEqual(TEXT("Reused instance starts from the new row reserve"), Inventory->GetReserveAmmo(SecondInstanceId), 8);
	TestEqual(
		TEXT("Reused WeaponActor waits at Holstered"),
		static_cast<int32>(SecondWeapon->GetLifecycleState()),
		static_cast<int32>(EShooterWeaponLifecycleState::Holstered));

	// 复用后的武器必须能正常装备与开火前置判定。
	TestTrue(TEXT("Reused weapon can be equipped"), Equipment->EquipWeapon(SecondInstanceId));
	TestTrue(TEXT("Reused equipped weapon is visible"), !SecondWeapon->IsHidden());
	TestTrue(TEXT("Reused equipped weapon can consume ammo"), SecondWeapon->CanConsumeAmmo());

	DestroyPoolIntegrationTestWorld(World);
	return true;
}

/** Acquire 失败必须回滚 WeaponInstance，并保持 Slot 与池状态不变。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponPoolAcquireFailureRollbackTest,
	"ShootGame.Pool.WeaponGrant.AcquireFailureRollsBackInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponPoolAcquireFailureRollbackTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponPoolIntegrationAutomationTests;

	UWorld* World = CreatePoolIntegrationTestWorld();
	if (!TestNotNull(TEXT("Acquire failure world created"), World))
	{
		return false;
	}

	UShooterActorPoolSubsystem* Pool = World->GetSubsystem<UShooterActorPoolSubsystem>();
	if (!TestNotNull(TEXT("Pool subsystem exists"), Pool))
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
	if (!TestNotNull(TEXT("Character owns Inventory"), Inventory))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	UDataTable* Table = GetOrCreateTestWeaponTable(Inventory);
	if (!TestNotNull(TEXT("Test weapon table injected"), Table))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	// 行本身合法（具体 AShooterWeapon 子类 + 合法弹匣），但类是 abstract：
	// 池的 Acquire 会明确拒绝，授予必须回滚刚刚写入的 WeaponInstance。
	const FName AbstractRowName = AddTestWeaponRow(
		Table,
		MakeTestWeaponRow(AShooterWeapon::StaticClass(), /*MagazineSize*/ 10, /*InitialReserveAmmo*/ -1));

	FGuid FailedInstanceId;
	TestEqual(
		TEXT("Abstract WeaponActorClass row fails at pool acquire"),
		static_cast<int32>(Inventory->TryAddWeaponRow(AbstractRowName, FailedInstanceId)),
		static_cast<int32>(EShooterInventoryAddResult::AcquireFailed));
	TestFalse(TEXT("Failed grant returns no instance id"), FailedInstanceId.IsValid());
	TestEqual(TEXT("Failed grant rolls back the Inventory entry"), Inventory->GetWeaponCount(), 0);
	TestNull(TEXT("Failed grant leaves no instance resolvable by row"), Inventory->FindWeaponInstanceByRowName(AbstractRowName));
	TestEqual(TEXT("Failed grant releases the slot"), Inventory->FindFreeSlotIndex(), 0);
	TestEqual(
		TEXT("Failed grant leaves the pool untouched"),
		Pool->GetPooledCount(AShooterWeapon::StaticClass()),
		0);

	// 回滚后同一 Inventoy 仍可正常授予另一把武器，证明回滚没有污染状态。
	const FName ValidRowName = AddTestWeaponRow(
		Table,
		MakeTestWeaponRow(AShooterInventoryOrderTestWeapon::StaticClass(), /*MagazineSize*/ 10, /*InitialReserveAmmo*/ -1));
	FGuid ValidInstanceId;
	TestEqual(
		TEXT("Valid grant still succeeds after the rollback"),
		static_cast<int32>(Inventory->TryAddWeaponRow(ValidRowName, ValidInstanceId)),
		static_cast<int32>(EShooterInventoryAddResult::Added));
	TestEqual(TEXT("Valid grant fills slot 0"), Inventory->GetWeaponCount(), 1);
	TestNotNull(TEXT("Valid grant binds a WeaponActor"), Inventory->FindWeaponActor(ValidInstanceId));

	DestroyPoolIntegrationTestWorld(World);
	return true;
}

/** 拥有者销毁（断线 / teardown）把池化 WeaponActor 归还池，而不是留下 PendingKill 引用。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponPoolOwnerDestroyedReleaseTest,
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

	UShooterActorPoolSubsystem* Pool = World->GetSubsystem<UShooterActorPoolSubsystem>();
	if (!TestNotNull(TEXT("Pool subsystem exists"), Pool))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	AShooterPoolTestActorA* OwnerActor = World->SpawnActor<AShooterPoolTestActorA>(
		FVector::ZeroVector,
		FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Pool owner actor spawned"), OwnerActor))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	FActorSpawnParameters AcquireParams;
	AcquireParams.Owner = OwnerActor;
	AShooterInventoryOrderTestWeapon* Weapon = Cast<AShooterInventoryOrderTestWeapon>(Pool->Acquire(
		AShooterInventoryOrderTestWeapon::StaticClass(),
		FTransform::Identity,
		AcquireParams));
	if (!TestNotNull(TEXT("Weapon acquired for owner destroy test"), Weapon))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	TestTrue(TEXT("Weapon is managed before owner destruction"), Pool->IsManaged(Weapon));
	TestTrue(TEXT("Weapon owner destroy handler is bound"), OwnerActor->OnDestroyed.IsBound());

	// 直接驱动拥有者销毁回调：Editor 自动化测试世界不投递 Actor 销毁通知
	// （实测 EndPlay 与 OnDestroyed 都不到达），通知投递本身由网络阶段 DisconnectCleanup 覆盖，
	// 这里验证「通知到达后的归还边界」。
	Weapon->SimulateOwnerDestroyedForTest();

	TestTrue(TEXT("Owner destruction returns the weapon to the pool"), Pool->IsPooled(Weapon));
	TestFalse(TEXT("Owner destruction is no longer managed"), Pool->IsManaged(Weapon));
	TestFalse(TEXT("Owner destruction does not destroy the pooled weapon"), Weapon->IsActorBeingDestroyed());
	TestNull(TEXT("Pooled weapon loses its owner"), Weapon->GetOwner());
	TestEqual(
		TEXT("Pooled weapon returns to InPool"),
		static_cast<int32>(Weapon->GetLifecycleState()),
		static_cast<int32>(EShooterWeaponLifecycleState::InPool));

	// 非池出生（NPC / 旧测试直接 Spawn）无法归还，必须保留原有销毁语义。
	AShooterInventoryOrderTestWeapon* DirectlySpawned =
		World->SpawnActor<AShooterInventoryOrderTestWeapon>(
			FVector::ZeroVector,
			FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Directly spawned weapon exists"), DirectlySpawned))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	TestFalse(TEXT("Directly spawned weapon is not pool managed"), Pool->IsManaged(DirectlySpawned));
	DirectlySpawned->SimulateOwnerDestroyedForTest();
	TestTrue(TEXT("Non-pooled weapon falls back to destroy"), DirectlySpawned->IsActorBeingDestroyed());

	DestroyPoolIntegrationTestWorld(World);
	return true;
}

/** 死亡 / EndPlay 清理：全部 WeaponActor 归还池，顺序为清 Entries → 清装备 → 归还。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponPoolDeathCleanupReleaseTest,
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

	UShooterActorPoolSubsystem* Pool = World->GetSubsystem<UShooterActorPoolSubsystem>();
	if (!TestNotNull(TEXT("Pool subsystem exists"), Pool))
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
	if (!TestNotNull(TEXT("Character owns Inventory"), Inventory) ||
		!TestNotNull(TEXT("Character owns Equipment"), Equipment))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	UDataTable* Table = GetOrCreateTestWeaponTable(Inventory);
	if (!TestNotNull(TEXT("Test weapon table injected"), Table))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	const FName PrimaryRowName = AddTestWeaponRow(
		Table,
		MakeTestWeaponRow(AShooterInventoryOrderTestWeapon::StaticClass()));
	const FName SecondaryRowName = AddTestWeaponRow(
		Table,
		MakeTestWeaponRow(AShooterWeaponPresentationTestWeaponPrimary::StaticClass()));

	FGuid PrimaryInstanceId;
	FGuid SecondaryInstanceId;
	TestEqual(
		TEXT("Primary weapon granted"),
		static_cast<int32>(Inventory->TryAddWeaponRow(PrimaryRowName, PrimaryInstanceId)),
		static_cast<int32>(EShooterInventoryAddResult::Added));
	TestEqual(
		TEXT("Secondary weapon granted"),
		static_cast<int32>(Inventory->TryAddWeaponRow(SecondaryRowName, SecondaryInstanceId)),
		static_cast<int32>(EShooterInventoryAddResult::Added));

	AShooterWeapon* PrimaryWeapon = Inventory->FindWeaponActor(PrimaryInstanceId);
	AShooterWeapon* SecondaryWeapon = Inventory->FindWeaponActor(SecondaryInstanceId);
	if (!TestNotNull(TEXT("Primary WeaponActor exists"), PrimaryWeapon) ||
		!TestNotNull(TEXT("Secondary WeaponActor exists"), SecondaryWeapon))
	{
		DestroyPoolIntegrationTestWorld(World);
		return false;
	}

	TestTrue(TEXT("Primary weapon is equipped before death"), Equipment->EquipWeapon(PrimaryInstanceId));

	// ClearInventory 是 Character::Die 与 Character::EndPlay（断线 / teardown）共用的清理入口；
	// Editor 自动化测试世界不投递 EndPlay，因此这里直接驱动该入口，
	// 死亡链路的通知投递由网络阶段的死亡 / DisconnectCleanup 场景覆盖。
	Inventory->ClearInventory();

	TestEqual(TEXT("Death cleanup empties Inventory entries"), Inventory->GetWeaponCount(), 0);
	TestNull(TEXT("Death cleanup clears Equipment CurrentWeaponActor"), Equipment->GetCurrentWeaponActor());
	TestFalse(
		TEXT("Death cleanup clears Equipment ActiveWeaponInstanceId"),
		Equipment->GetActiveWeaponInstanceId().IsValid());

	TestTrue(TEXT("Death cleanup pools the equipped weapon"), Pool->IsPooled(PrimaryWeapon));
	TestTrue(TEXT("Death cleanup pools the holstered weapon"), Pool->IsPooled(SecondaryWeapon));
	TestFalse(TEXT("Death cleanup does not destroy the equipped weapon"), PrimaryWeapon->IsActorBeingDestroyed());
	TestFalse(TEXT("Death cleanup does not destroy the holstered weapon"), SecondaryWeapon->IsActorBeingDestroyed());
	TestNull(TEXT("Pooled equipped weapon loses its owner"), PrimaryWeapon->GetOwner());
	TestFalse(TEXT("Pooled equipped weapon loses its instance binding"), PrimaryWeapon->GetBoundInstanceId().IsValid());
	TestTrue(TEXT("Pooled equipped weapon is hidden"), PrimaryWeapon->IsHidden());
	TestEqual(
		TEXT("Equipped weapon class pool holds one entry"),
		Pool->GetPooledCount(AShooterInventoryOrderTestWeapon::StaticClass()),
		1);
	TestEqual(
		TEXT("Holstered weapon class pool holds one entry"),
		Pool->GetPooledCount(AShooterWeaponPresentationTestWeaponPrimary::StaticClass()),
		1);

	// 重复清理幂等：不重复归还，也不产生错误状态。
	Inventory->ClearInventory();
	TestEqual(
		TEXT("Repeated death cleanup keeps the pool unchanged"),
		Pool->GetPooledCount(AShooterInventoryOrderTestWeapon::StaticClass()),
		1);
	TestEqual(TEXT("Repeated death cleanup keeps Inventory empty"), Inventory->GetWeaponCount(), 0);

	DestroyPoolIntegrationTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
