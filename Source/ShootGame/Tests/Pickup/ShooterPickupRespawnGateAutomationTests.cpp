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
#include "Weapons/ShooterPickup.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/ShooterWeaponRuntimeSubsystem.h"
#include "../Equipment/ShooterWeaponPresentationTestTypes.h"
#include "../Weapon/ShooterWeaponTestTableTypes.h"

namespace ShooterPickupRespawnGateAutomationTests
{
	UWorld* CreateRespawnGateTestWorld()
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

	void DestroyRespawnGateTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	AShooterWeaponPresentationTestCharacter* SpawnRespawnGateTestCharacter(
		FAutomationTestBase& Test,
		UWorld* World)
	{
		AShooterWeaponPresentationTestCharacter* Character =
			World->SpawnActor<AShooterWeaponPresentationTestCharacter>(
				FVector::ZeroVector,
				FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("Respawn gate test character spawned"), Character))
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
}

/**
 * Pickup 重生逻辑门定向前置验证（武器与 Inventory 正式架构实施计划第 5 节）：
 * 玩家 A 拾取成功 → Pickup 隐藏且闸门关闭 → 隐藏期间连续 Overlap 不重复授予 →
 * Respawn 恢复闸门与可见性 → 玩家 B 再次拾取成功 → A、B 各有独立 WeaponInstance，
 * 且第二次拾取后 Pickup 重新进入隐藏状态。
 * 该测试运行在无网络驱动的服务器权威 World 中，与 Dedicated Server 的授予端一致。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterPickupRespawnGateCrossRespawnTest,
	"ShootGame.Pickup.RespawnGate.CrossRespawnGrant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterPickupRespawnGateCrossRespawnTest::RunTest(const FString& Parameters)
{
	using namespace ShooterPickupRespawnGateAutomationTests;

	UWorld* World = CreateRespawnGateTestWorld();
	if (!TestNotNull(TEXT("Respawn gate test world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* PlayerA = SpawnRespawnGateTestCharacter(*this, World);
	AShooterWeaponPresentationTestCharacter* PlayerB = SpawnRespawnGateTestCharacter(*this, World);
	if (!PlayerA || !PlayerB)
	{
		DestroyRespawnGateTestWorld(World);
		return false;
	}

	AShooterWeaponPresentationTestPickup* Pickup = World->SpawnActor<AShooterWeaponPresentationTestPickup>(
		FVector(0.0f, 0.0f, 100.0f),
		FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Respawn gate test pickup spawned"), Pickup))
	{
		DestroyRespawnGateTestWorld(World);
		return false;
	}
	UShooterInventoryComponent* InventoryA = PlayerA->GetInventoryComponent();
	UShooterInventoryComponent* InventoryB = PlayerB->GetInventoryComponent();
	if (!TestNotNull(TEXT("Player A owns inventory"), InventoryA) ||
		!TestNotNull(TEXT("Player B owns inventory"), InventoryB))
	{
		DestroyRespawnGateTestWorld(World);
		return false;
	}

	// Pickup 只持有 WeaponId（S3 由 WeaponType.RowName 承载）；
	// 运行时快照是 World 级的，A、B 天然共享同一行定义。
	const FName PickupWeaponRowName = AddTestWeaponRow(
		GetOrInjectRuntimeTestTable(World),
		MakeTestWeaponRow(AShooterInventoryOrderTestWeapon::StaticClass()));
	World->GetSubsystem<UShooterWeaponRuntimeSubsystem>()->InitializeWeaponRuntimeForTest();
	Pickup->SetWeaponRowNameForTest(PickupWeaponRowName);

	// --- 玩家 A 首次拾取：授予成功并立即装备，Pickup 进入隐藏态。 ---
	Pickup->TriggerOverlapForTest(PlayerA);
	TestEqual(TEXT("Player A granted one weapon"), InventoryA->GetWeaponCount(), 1);
	AShooterWeapon* WeaponA = PlayerA->GetEquipmentComponent()
		? PlayerA->GetEquipmentComponent()->GetCurrentWeaponActor()
		: nullptr;
	TestNotNull(TEXT("Player A equipped the granted weapon"), WeaponA);
	TestTrue(TEXT("Pickup hides after first grant"), Pickup->IsHidden());
	TestFalse(TEXT("Pickup gate closes after first grant"), Pickup->IsPickupAvailableForTest());
	TestFalse(TEXT("Pickup collision disabled after first grant"), Pickup->GetActorEnableCollision());

	// --- 隐藏期间连续 Overlap（A、B 先后）不得重复授予或消费 Pickup。 ---
	Pickup->TriggerOverlapForTest(PlayerA);
	Pickup->TriggerOverlapForTest(PlayerB);
	TestEqual(TEXT("Hidden pickup does not re-grant to A"), InventoryA->GetWeaponCount(), 1);
	TestEqual(TEXT("Hidden pickup does not grant to B"), InventoryB->GetWeaponCount(), 0);

	// --- 重生：闸门恢复、可见性恢复（FinishRespawn 恢复碰撞由蓝图动画链调用）。 ---
	Pickup->TriggerRespawnForTest();
	TestTrue(TEXT("Respawn restores the pickup gate"), Pickup->IsPickupAvailableForTest());
	TestFalse(TEXT("Respawn restores visibility"), Pickup->IsHidden());

	// --- 玩家 B 第二次拾取：授予成功，A、B 持有独立 WeaponInstance。 ---
	Pickup->TriggerOverlapForTest(PlayerB);
	TestEqual(TEXT("Player B granted one weapon"), InventoryB->GetWeaponCount(), 1);
	TestEqual(TEXT("Player A keeps exactly one weapon"), InventoryA->GetWeaponCount(), 1);
	AShooterWeapon* WeaponB = PlayerB->GetEquipmentComponent()
		? PlayerB->GetEquipmentComponent()->GetCurrentWeaponActor()
		: nullptr;
	TestNotNull(TEXT("Player B equipped the granted weapon"), WeaponB);
	TestTrue(TEXT("A and B hold distinct WeaponActors"), WeaponA != WeaponB);

	// --- 第二次拾取后 Pickup 重新进入隐藏/闸门关闭状态。 ---
	TestTrue(TEXT("Pickup hides again after second grant"), Pickup->IsHidden());
	TestFalse(TEXT("Pickup gate closes again after second grant"), Pickup->IsPickupAvailableForTest());

	DestroyRespawnGateTestWorld(World);
	return true;
}

/**
 * SlotFull 拒绝不消费 Pickup：背包已满的玩家拾取失败后 Pickup 保持可见与可拾取，
 * 后续空背包玩家可以立即成功拾取同一 Pickup。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterPickupRespawnGateSlotFullRetryTest,
	"ShootGame.Pickup.RespawnGate.SlotFullKeepsAvailable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterPickupRespawnGateSlotFullRetryTest::RunTest(const FString& Parameters)
{
	using namespace ShooterPickupRespawnGateAutomationTests;

	UWorld* World = CreateRespawnGateTestWorld();
	if (!TestNotNull(TEXT("Slot full test world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* FullPlayer = SpawnRespawnGateTestCharacter(*this, World);
	AShooterWeaponPresentationTestCharacter* EmptyPlayer = SpawnRespawnGateTestCharacter(*this, World);
	if (!FullPlayer || !EmptyPlayer)
	{
		DestroyRespawnGateTestWorld(World);
		return false;
	}

	AShooterWeaponPresentationTestPickup* Pickup = World->SpawnActor<AShooterWeaponPresentationTestPickup>(
		FVector(0.0f, 0.0f, 100.0f),
		FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Slot full test pickup spawned"), Pickup))
	{
		DestroyRespawnGateTestWorld(World);
		return false;
	}
	UShooterInventoryComponent* FullInventory = FullPlayer->GetInventoryComponent();
	UShooterInventoryComponent* EmptyInventory = EmptyPlayer->GetInventoryComponent();
	if (!TestNotNull(TEXT("Full player owns inventory"), FullInventory) ||
		!TestNotNull(TEXT("Empty player owns inventory"), EmptyInventory))
	{
		DestroyRespawnGateTestWorld(World);
		return false;
	}

	// Pickup 只持有 WeaponId（S3 由 WeaponType.RowName 承载）；两个玩家共享同一 World 级运行时快照。
	const FName PickupWeaponRowName = AddTestWeaponRow(
		GetOrInjectRuntimeTestTable(World),
		MakeTestWeaponRow(AShooterWeaponPresentationTestWeaponPrimary::StaticClass()));
	World->GetSubsystem<UShooterWeaponRuntimeSubsystem>()->InitializeWeaponRuntimeForTest();
	Pickup->SetWeaponRowNameForTest(PickupWeaponRowName);

	// 背包填满：三个不同 WeaponId 占满默认 3 个 Slot。
	EShooterInventoryAddResult FillResult = EShooterInventoryAddResult::NotAuthoritative;
	GrantTestWeapon(World, FullInventory, AShooterInventoryOrderTestWeapon::StaticClass(), 10, -1, &FillResult);
	GrantTestWeapon(World, FullInventory, AShooterWeaponPresentationTestWeaponPrimary::StaticClass(), 10, -1, &FillResult);
	GrantTestWeapon(World, FullInventory, AShooterWeaponPresentationTestWeaponSecondary::StaticClass(), 10, -1, &FillResult);
	TestEqual(TEXT("Full player fills all three slots"), FullInventory->GetWeaponCount(), 3);

	// 满背包玩家拾取：SlotFull 被明确拒绝，Pickup 不隐藏、闸门保持开启。
	Pickup->TriggerOverlapForTest(FullPlayer);
	TestEqual(TEXT("SlotFull keeps full inventory unchanged"), FullInventory->GetWeaponCount(), 3);
	TestFalse(TEXT("SlotFull does not hide the pickup"), Pickup->IsHidden());
	TestTrue(TEXT("SlotFull keeps the pickup gate open"), Pickup->IsPickupAvailableForTest());

	// 空背包玩家随后拾取同一 Pickup 必须立即成功。
	Pickup->TriggerOverlapForTest(EmptyPlayer);
	TestEqual(TEXT("Empty player grants after SlotFull rejection"), EmptyInventory->GetWeaponCount(), 1);
	TestTrue(TEXT("Pickup hides after the retry grant"), Pickup->IsHidden());

	DestroyRespawnGateTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
