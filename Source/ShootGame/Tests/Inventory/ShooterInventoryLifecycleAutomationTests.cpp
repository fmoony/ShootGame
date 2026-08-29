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
#include "UObject/UnrealType.h"
#include "Weapons/ShooterWeapon.h"
#include "ShooterWeaponPresentationTestTypes.h"

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

		const EShooterInventoryAddResult AddResult = Inventory->TryAddWeapon(WeaponClass, OutInstanceId);
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

	AShooterWeaponPresentationTestPickup* Pickup = World->SpawnActor<AShooterWeaponPresentationTestPickup>(
		FVector(0.0f, 0.0f, 100.0f),
		FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Test pickup spawned"), Pickup))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}
	Pickup->SetWeaponClassForTest(AShooterInventoryOrderTestWeapon::StaticClass());

	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	TestEqual(TEXT("Inventory is empty before pickup"), Inventory->GetWeaponCount(), 0);

	Pickup->TriggerOverlapForTest(Character);

	TestEqual(TEXT("Equip failure rolls back the granted instance"), Inventory->GetWeaponCount(), 0);
	TestNull(TEXT("Rollback clears the current weapon"), Character->GetCurrentWeaponActor());
	TestFalse(TEXT("Pickup stays visible and available after rollback"), Pickup->IsHidden());

	// 新授予的 WeaponActor 已随回滚销毁，不残留任何已绑定 Actor。
	int32 RemainingWeaponActors = 0;
	for (TActorIterator<AShooterInventoryOrderTestWeapon> It(World); It; ++It)
	{
		++RemainingWeaponActors;
	}
	TestEqual(TEXT("Rollback leaves no granted WeaponActor in the world"), RemainingWeaponActors, 0);

	DestroyLifecycleTestWorld(World);
	return true;
}

/**
 * E1 验证：移除当前装备时，Equipment Deactivate / Clear 必须先于 WeaponActor Destroy。
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
	TestFalse(TEXT("Current weapon has not been deactivated yet"), CurrentWeapon->bDeactivatedForTest);

	TestTrue(TEXT("Current weapon instance is removed"), Inventory->RemoveWeaponInstance(CurrentId));

	// 广播先于 Destroy 的关键证据：DeactivateWeapon 的隐藏后置条件在 Destroy 前达成。
	TestTrue(TEXT("Current weapon was hidden by deactivate before destroy"), CurrentWeapon->IsHidden());
	TestTrue(TEXT("Current weapon was destroyed after deactivate"), CurrentWeapon->IsActorBeingDestroyed());
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
 * E1 验证：ClearInventory 先清逻辑 Entries 并广播，Equipment 清理完当前装备后再 Destroy 全部 WeaponActor；
 * 重复 Clear 幂等。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryClearWeaponOrderTest,
	"ShootGame.Inventory.Clear.WeaponDestroyOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryClearWeaponOrderTest::RunTest(const FString& Parameters)
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

	TestTrue(TEXT("Clear hid the current weapon by deactivate before destroy"), CurrentWeapon->IsHidden());
	TestTrue(TEXT("Clear destroyed the current weapon after deactivate"), CurrentWeapon->IsActorBeingDestroyed());
	TestNull(TEXT("Clear empties Equipment CurrentWeaponActor"), Equipment->GetCurrentWeaponActor());
	TestFalse(TEXT("Clear empties Equipment ActiveWeaponInstanceId"), Equipment->GetActiveWeaponInstanceId().IsValid());
	TestEqual(TEXT("Clear empties Inventory entries"), Inventory->GetWeaponCount(), 0);

	// 重复 Clear 幂等：不再销毁对象，也不产生错误状态。
	Inventory->ClearInventory();
	TestNull(TEXT("Repeated clear keeps Equipment empty"), Equipment->GetCurrentWeaponActor());
	TestEqual(TEXT("Repeated clear keeps Inventory empty"), Inventory->GetWeaponCount(), 0);

	DestroyLifecycleTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
