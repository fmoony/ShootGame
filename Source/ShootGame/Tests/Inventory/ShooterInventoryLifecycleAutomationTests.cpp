// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "Pool/ShooterActorPoolSubsystem.h"
#include "UObject/UnrealType.h"
#include "Weapons/ShooterWeapon.h"
#include "ShooterWeaponPresentationTestTypes.h"
#include "../Weapon/ShooterWeaponTestTableTypes.h"

namespace ShooterInventoryLifecycleAutomationTests
{
	UWorld* CreateLifecycleTestWorld()
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

	void DestroyLifecycleTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	AShooterWeaponPresentationTestCharacter* SpawnLifecycleTestCharacter(
		FAutomationTestBase& Test,
		UWorld* World)
	{
		AShooterWeaponPresentationTestCharacter* Character =
			World->SpawnActor<AShooterWeaponPresentationTestCharacter>(
				FVector::ZeroVector,
				FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("Lifecycle test character spawned"), Character))
		{
			return nullptr;
		}

		// 测试 World 没有 GameMode；直接驱动 WorldSettings 让组件订阅与 Actor BeginPlay 生效。
		if (AWorldSettings* WorldSettings = World->GetWorldSettings())
		{
			WorldSettings->NotifyBeginPlay();
		}
		return Character;
	}

	AShooterWeapon* GrantWeaponForLifecycleTest(
		FAutomationTestBase& Test,
		AShooterWeaponPresentationTestCharacter* Character,
		TSubclassOf<AShooterWeapon> WeaponClass,
		FGuid& OutInstanceId)
	{
		UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
		if (!Test.TestNotNull(TEXT("Lifecycle test character owns Inventory"), Inventory))
		{
			return nullptr;
		}

		// 单表武器配置纠偏：授予入口只接受武器模板行名（DT_WeaponData 行），
		// 行由测试助手写入注入本 Inventory 的瞬态测试表；弹药参数保持旧 Definition 的默认值。
		EShooterInventoryAddResult AddResult = EShooterInventoryAddResult::NotAuthoritative;
		GrantTestWeaponRow(
			Inventory,
			WeaponClass,
			OutInstanceId,
			/*MagazineSize*/ 10,
			/*InitialReserveAmmo*/ -1,
			&AddResult);
		if (!Test.TestEqual(
			TEXT("Lifecycle test weapon is granted"),
			static_cast<int32>(AddResult),
			static_cast<int32>(EShooterInventoryAddResult::Added)))
		{
			return nullptr;
		}

		AShooterWeapon* Weapon = Inventory->FindWeaponActor(OutInstanceId);
		if (!Test.TestNotNull(TEXT("Granted lifecycle weapon actor exists"), Weapon))
		{
			return nullptr;
		}

		// 无网络驱动的测试 World 会推迟复制 Actor 的 BeginPlay；显式补齐，与生产服务器一致。
		if (!Weapon->HasActorBegunPlay())
		{
			Weapon->DispatchBeginPlay();
		}
		return Weapon;
	}
}

/**
 * E1 验证：Pickup Add 成功但首次 Equip 失败时，本次新增 Instance 与 WeaponActor 完整回滚，
 * 且 Pickup 保持未消费状态（不隐藏），不会同时留下 Inventory 武器和可拾取 Pickup。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryPickupEquipFailureRollbackTest,
	"ShootGame.Inventory.Pickup.EquipFailureRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryPickupEquipFailureRollbackTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryLifecycleAutomationTests;

	UWorld* World = CreateLifecycleTestWorld();
	if (!TestNotNull(TEXT("Lifecycle test world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnLifecycleTestCharacter(*this, World);
	if (!Character)
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	// 模拟 Equipment 缺失：Pickup 的 Add 已成功，Equip 前置直接失败。
	FObjectProperty* EquipmentProperty = FindFProperty<FObjectProperty>(
		AShooterCharacter::StaticClass(),
		TEXT("EquipmentComponent"));
	if (!TestNotNull(TEXT("Character exposes EquipmentComponent for test injection"), EquipmentProperty))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}
	EquipmentProperty->SetObjectPropertyValue_InContainer(Character, nullptr);
	TestNull(TEXT("Equipment is unavailable for the rollback scenario"), Character->GetEquipmentComponent());

	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	if (!TestNotNull(TEXT("Rollback test character owns Inventory"), Inventory))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	AShooterWeaponPresentationTestPickup* Pickup = World->SpawnActor<AShooterWeaponPresentationTestPickup>(
		FVector(0.0f, 0.0f, 100.0f),
		FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Test pickup spawned"), Pickup))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	// 单表武器配置纠偏：Pickup 只选择武器模板行名（WeaponType.RowName），行本身必须先存在。
	// 这里显式往注入 Inventory 的测试表追加回滚场景需要的行，再交给 Pickup 选择。
	UDataTable* PickupRowTable = GetOrCreateTestWeaponTable(Inventory);
	if (!TestNotNull(TEXT("Rollback test weapon table injected"), PickupRowTable))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	const FName PickupRowName = AddTestWeaponRow(
		PickupRowTable,
		MakeTestWeaponRow(
			AShooterInventoryOrderTestWeapon::StaticClass(),
			/*MagazineSize*/ 10,
			/*InitialReserveAmmo*/ -1));
	TestFalse(TEXT("Rollback test weapon row name is valid"), PickupRowName.IsNone());
	Pickup->SetWeaponRowNameForTest(PickupRowName);

	TestEqual(TEXT("Inventory is empty before pickup"), Inventory->GetWeaponCount(), 0);

	Pickup->TriggerOverlapForTest(Character);

	TestEqual(TEXT("Equip failure rolls back the granted instance"), Inventory->GetWeaponCount(), 0);
	TestNull(TEXT("Rollback clears the current weapon"), Character->GetCurrentWeaponActor());
	TestFalse(TEXT("Pickup stays visible and available after rollback"), Pickup->IsHidden());

	// B3：回滚把本次 Acquire 的 WeaponActor 归还对象池，而不是销毁；
	// 世界实体仍存在但已脱离 Inventory、无 Owner、隐藏，可被后续授予复用。
	UShooterActorPoolSubsystem* Pool = World->GetSubsystem<UShooterActorPoolSubsystem>();
	if (!TestNotNull(TEXT("Rollback test world owns an actor pool"), Pool))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	int32 RemainingBoundWeaponActors = 0;
	int32 WorldWeaponActorCount = 0;
	for (TActorIterator<AShooterInventoryOrderTestWeapon> It(World); It; ++It)
	{
		++WorldWeaponActorCount;
		TestTrue(TEXT("Rolled back WeaponActor is pooled"), Pool->IsPooled(*It));
		TestFalse(TEXT("Rolled back WeaponActor is no longer in use"), Pool->IsManaged(*It));
		TestTrue(TEXT("Rolled back WeaponActor is hidden"), It->IsHidden());
		TestTrue(TEXT("Rolled back WeaponActor has no owner"), It->GetOwner() == nullptr);
		TestFalse(TEXT("Rolled back WeaponActor has no instance binding"), It->GetBoundInstanceId().IsValid());

		if (Inventory->FindWeaponActor(It->GetBoundInstanceId()) != nullptr)
		{
			++RemainingBoundWeaponActors;
		}
	}
	TestEqual(TEXT("Rollback leaves exactly one pooled WeaponActor"), WorldWeaponActorCount, 1);
	TestEqual(TEXT("Rollback leaves no WeaponActor bound to the Inventory"), RemainingBoundWeaponActors, 0);
	TestEqual(
		TEXT("Rollback returns the WeaponActor to its class pool"),
		Pool->GetPooledCount(AShooterInventoryOrderTestWeapon::StaticClass()),
		1);

	DestroyLifecycleTestWorld(World);
	return true;
}

/**
 * E1 验证：移除当前装备时，Equipment Deactivate / Clear 必须先于 WeaponActor 归还对象池（B3）。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryRemoveCurrentWeaponOrderTest,
	"ShootGame.Inventory.Remove.CurrentWeaponOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryRemoveCurrentWeaponOrderTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryLifecycleAutomationTests;

	UWorld* World = CreateLifecycleTestWorld();
	if (!TestNotNull(TEXT("Lifecycle test world created"), World))
	{
		return false;
	}

	UShooterActorPoolSubsystem* Pool = World->GetSubsystem<UShooterActorPoolSubsystem>();
	if (!TestNotNull(TEXT("Lifecycle test world owns an actor pool"), Pool))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnLifecycleTestCharacter(*this, World);
	if (!Character)
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	FGuid CurrentId;
	AShooterInventoryOrderTestWeapon* CurrentWeapon = Cast<AShooterInventoryOrderTestWeapon>(
		GrantWeaponForLifecycleTest(
			*this,
			Character,
			AShooterInventoryOrderTestWeapon::StaticClass(),
			CurrentId));
	if (!CurrentWeapon)
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	TestTrue(TEXT("Current weapon is equipped"), Equipment->EquipWeapon(CurrentId));
	TestTrue(TEXT("Current weapon is visible before remove"), !CurrentWeapon->IsHidden());
	TestEqual(TEXT("Current weapon has not been deactivated yet"), Character->WeaponDeactivatedCount, 0);

	TestTrue(TEXT("Current weapon instance is removed"), Inventory->RemoveWeaponInstance(CurrentId));

	// 顺序证据：移除广播先驱动 Equipment 收敛并 Deactivate（经 IShooterWeaponHolder 回调可观测），
	// 之后 WeaponActor 才归还池；归还发生在解绑与隐藏之后，池内对象不再属于 Inventory。
	TestEqual(TEXT("Removal broadcast deactivated the current weapon exactly once"), Character->WeaponDeactivatedCount, 1);
	TestTrue(TEXT("Current weapon was hidden by deactivate before release"), CurrentWeapon->IsHidden());
	TestFalse(TEXT("Removed WeaponActor is not destroyed while pooled"), CurrentWeapon->IsActorBeingDestroyed());
	TestTrue(TEXT("Removed WeaponActor is pooled"), Pool->IsPooled(CurrentWeapon));
	TestFalse(TEXT("Removed WeaponActor is no longer in use"), Pool->IsManaged(CurrentWeapon));
	TestNull(TEXT("Removed WeaponActor has no owner"), CurrentWeapon->GetOwner());
	TestFalse(TEXT("Removed WeaponActor has no instance binding"), CurrentWeapon->GetBoundInstanceId().IsValid());
	TestNull(TEXT("Remove unbinds the WeaponActor from Inventory"), Inventory->FindWeaponActor(CurrentId));
	TestNull(TEXT("Remove clears Equipment CurrentWeaponActor"), Equipment->GetCurrentWeaponActor());
	TestFalse(TEXT("Remove clears Equipment ActiveWeaponInstanceId"), Equipment->GetActiveWeaponInstanceId().IsValid());
	TestEqual(TEXT("Remove empties Inventory entries"), Inventory->GetWeaponCount(), 0);

	// 重复 Remove 幂等，不崩溃也不重复广播错误状态。
	TestFalse(TEXT("Repeated remove of the same instance is rejected"), Inventory->RemoveWeaponInstance(CurrentId));

	DestroyLifecycleTestWorld(World);
	return true;
}

/**
 * E1 验证：删除非当前武器不影响当前装备与表现。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryRemoveNonCurrentWeaponTest,
	"ShootGame.Inventory.Remove.NonCurrentKeepsCurrent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryRemoveNonCurrentWeaponTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryLifecycleAutomationTests;

	UWorld* World = CreateLifecycleTestWorld();
	if (!TestNotNull(TEXT("Lifecycle test world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnLifecycleTestCharacter(*this, World);
	if (!Character)
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	FGuid PrimaryId;
	AShooterWeapon* PrimaryWeapon = GrantWeaponForLifecycleTest(
		*this,
		Character,
		AShooterWeaponPresentationTestWeaponPrimary::StaticClass(),
		PrimaryId);
	FGuid SecondaryId;
	AShooterWeapon* SecondaryWeapon = GrantWeaponForLifecycleTest(
		*this,
		Character,
		AShooterWeaponPresentationTestWeaponSecondary::StaticClass(),
		SecondaryId);
	if (!PrimaryWeapon || !SecondaryWeapon)
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	TestTrue(TEXT("Primary weapon is equipped"), Equipment->EquipWeapon(PrimaryId));
	TestTrue(TEXT("Primary weapon is visible"), !PrimaryWeapon->IsHidden());

	TestTrue(TEXT("Non-current secondary weapon is removed"), Inventory->RemoveWeaponInstance(SecondaryId));

	TestTrue(TEXT("Removing non-current keeps CurrentWeaponActor"), Equipment->GetCurrentWeaponActor() == PrimaryWeapon);
	TestTrue(TEXT("Removing non-current keeps the current weapon visible"), !PrimaryWeapon->IsHidden());
	TestEqual(TEXT("Removing non-current leaves one Inventory entry"), Inventory->GetWeaponCount(), 1);

	DestroyLifecycleTestWorld(World);
	return true;
}

/**
 * E1 验证：ClearInventory 先清逻辑 Entries 并广播，Equipment 清理完当前装备后再把全部
 * WeaponActor 归还对象池（B3）；重复 Clear 幂等。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryClearWeaponOrderTest,
	"ShootGame.Inventory.Clear.WeaponReleaseOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryClearWeaponOrderTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryLifecycleAutomationTests;

	UWorld* World = CreateLifecycleTestWorld();
	if (!TestNotNull(TEXT("Lifecycle test world created"), World))
	{
		return false;
	}

	UShooterActorPoolSubsystem* Pool = World->GetSubsystem<UShooterActorPoolSubsystem>();
	if (!TestNotNull(TEXT("Clear test world owns an actor pool"), Pool))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnLifecycleTestCharacter(*this, World);
	if (!Character)
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	FGuid CurrentId;
	AShooterInventoryOrderTestWeapon* CurrentWeapon = Cast<AShooterInventoryOrderTestWeapon>(
		GrantWeaponForLifecycleTest(
			*this,
			Character,
			AShooterInventoryOrderTestWeapon::StaticClass(),
			CurrentId));
	if (!CurrentWeapon)
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	TestTrue(TEXT("Current weapon is equipped"), Equipment->EquipWeapon(CurrentId));

	Inventory->ClearInventory();

	// 顺序证据：Clear 先清逻辑 Entries 并广播，Equipment 收敛后 Deactivate 一次，最后才归还池。
	TestEqual(TEXT("Clear broadcast deactivated the current weapon exactly once"), Character->WeaponDeactivatedCount, 1);
	TestTrue(TEXT("Clear hid the current weapon by deactivate before release"), CurrentWeapon->IsHidden());
	TestFalse(TEXT("Cleared WeaponActor is not destroyed while pooled"), CurrentWeapon->IsActorBeingDestroyed());
	TestTrue(TEXT("Cleared WeaponActor is pooled"), Pool->IsPooled(CurrentWeapon));
	TestFalse(TEXT("Cleared WeaponActor is no longer in use"), Pool->IsManaged(CurrentWeapon));
	TestNull(TEXT("Cleared WeaponActor has no owner"), CurrentWeapon->GetOwner());
	TestFalse(TEXT("Cleared WeaponActor has no instance binding"), CurrentWeapon->GetBoundInstanceId().IsValid());
	TestEqual(
		TEXT("Clear returns the WeaponActor to its class pool"),
		Pool->GetPooledCount(AShooterInventoryOrderTestWeapon::StaticClass()),
		1);
	TestNull(TEXT("Clear empties Equipment CurrentWeaponActor"), Equipment->GetCurrentWeaponActor());
	TestFalse(TEXT("Clear empties Equipment ActiveWeaponInstanceId"), Equipment->GetActiveWeaponInstanceId().IsValid());
	TestEqual(TEXT("Clear empties Inventory entries"), Inventory->GetWeaponCount(), 0);

	// 重复 Clear 幂等：不重复归还，也不产生错误状态。
	Inventory->ClearInventory();
	TestNull(TEXT("Repeated clear keeps Equipment empty"), Equipment->GetCurrentWeaponActor());
	TestEqual(TEXT("Repeated clear keeps Inventory empty"), Inventory->GetWeaponCount(), 0);
	TestEqual(
		TEXT("Repeated clear does not pool the same WeaponActor twice"),
		Pool->GetPooledCount(AShooterInventoryOrderTestWeapon::StaticClass()),
		1);

	DestroyLifecycleTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
