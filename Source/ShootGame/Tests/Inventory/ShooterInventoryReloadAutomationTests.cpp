// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "ShooterInventoryReserveTestTypes.h"
#include "ShooterWeapon.h"

/**
 * S2 起换弹原子事务收敛在 WeaponActor（ReloadFromReserve）。
 * 本文件改为在裸权威测试世界中驱动 Actor 事务，覆盖：
 * 转移量、容量截断、零备弹拒绝、满弹匣拒绝与多武器隔离。
 */
namespace ShooterInventoryReloadAutomationTests
{
	UWorld* CreateReloadTestWorld(FAutomationTestBase& Test)
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!Test.TestNotNull(TEXT("Reload test world created"), World) || !GEngine)
		{
			return nullptr;
		}

		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		return World;
	}

	void DestroyReloadTestWorld(UWorld* World)
	{
		if (World && GEngine)
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	}

	AShooterInventoryReloadTestWeapon* SpawnReloadTestWeapon(
		FAutomationTestBase& Test,
		UWorld* World,
		int32 MagazineSize,
		int32 MagazineAmmo,
		int32 ReserveAmmo)
	{
		AShooterInventoryReloadTestWeapon* Weapon = World
			? World->SpawnActor<AShooterInventoryReloadTestWeapon>(
				FVector::ZeroVector, FRotator::ZeroRotator)
			: nullptr;
		if (!Test.TestNotNull(TEXT("Reload test weapon spawned"), Weapon))
		{
			return nullptr;
		}

		Weapon->SetMagazineSizeForTest(MagazineSize);
		Weapon->SetAmmoForTest(MagazineAmmo, ReserveAmmo);
		return Weapon;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryReloadTransferTest,
	"ShootGame.Inventory.Reload.Transfer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryReloadTransferTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryReloadAutomationTests;

	UWorld* World = CreateReloadTestWorld(*this);
	if (!World)
	{
		return false;
	}

	// 弹匣 5 / 备弹 20 / 容量 30：备弹不足剩余容量，全部转移。
	AShooterInventoryReloadTestWeapon* Weapon = SpawnReloadTestWeapon(*this, World, 30, 5, 20);
	if (Weapon)
	{
		int32 TransferredAmmo = INDEX_NONE;
		TestTrue(TEXT("Reload transaction commits"), Weapon->ReloadFromReserve(TransferredAmmo));
		TestEqual(TEXT("Transfer equals available reserve"), TransferredAmmo, 20);
		TestEqual(TEXT("Magazine becomes 5 + 20"), Weapon->GetBulletCount(), 25);
		TestEqual(TEXT("Reserve becomes 20 - 20"), Weapon->GetReserveAmmo(), 0);
	}

	DestroyReloadTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryReloadClampCapacityTest,
	"ShootGame.Inventory.Reload.ClampCapacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryReloadClampCapacityTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryReloadAutomationTests;

	UWorld* World = CreateReloadTestWorld(*this);
	if (!World)
	{
		return false;
	}

	// 弹匣 4 / 备弹 20 / 容量 12：转移量被剩余容量截断为 8。
	AShooterInventoryReloadTestWeapon* Weapon = SpawnReloadTestWeapon(*this, World, 12, 4, 20);
	if (Weapon)
	{
		int32 TransferredAmmo = INDEX_NONE;
		TestTrue(TEXT("Reload transaction commits"), Weapon->ReloadFromReserve(TransferredAmmo));
		TestEqual(TEXT("Transfer is clamped by remaining capacity"), TransferredAmmo, 8);
		TestEqual(TEXT("Magazine is filled exactly to capacity"), Weapon->GetBulletCount(), 12);
		TestEqual(TEXT("Reserve keeps the unneeded ammo"), Weapon->GetReserveAmmo(), 12);
	}

	DestroyReloadTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryReloadNoReserveTest,
	"ShootGame.Inventory.Reload.NoReserve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryReloadNoReserveTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryReloadAutomationTests;

	UWorld* World = CreateReloadTestWorld(*this);
	if (!World)
	{
		return false;
	}

	AShooterInventoryReloadTestWeapon* Weapon = SpawnReloadTestWeapon(*this, World, 30, 5, 0);
	if (Weapon)
	{
		int32 TransferredAmmo = INDEX_NONE;
		TestFalse(TEXT("Reload with no reserve is rejected"), Weapon->ReloadFromReserve(TransferredAmmo));
		TestEqual(TEXT("Transfer reports zero"), TransferredAmmo, 0);
		TestEqual(TEXT("Magazine is unchanged"), Weapon->GetBulletCount(), 5);
		TestEqual(TEXT("Reserve is unchanged"), Weapon->GetReserveAmmo(), 0);
	}

	DestroyReloadTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryReloadFullMagazineTest,
	"ShootGame.Inventory.Reload.FullMagazine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryReloadFullMagazineTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryReloadAutomationTests;

	UWorld* World = CreateReloadTestWorld(*this);
	if (!World)
	{
		return false;
	}

	AShooterInventoryReloadTestWeapon* Weapon = SpawnReloadTestWeapon(*this, World, 30, 30, 20);
	if (Weapon)
	{
		int32 TransferredAmmo = INDEX_NONE;
		TestFalse(TEXT("Full magazine rejects reload"), Weapon->ReloadFromReserve(TransferredAmmo));
		TestEqual(TEXT("Transfer reports zero"), TransferredAmmo, 0);
		TestEqual(TEXT("Magazine is unchanged"), Weapon->GetBulletCount(), 30);
		TestEqual(TEXT("Reserve is unchanged"), Weapon->GetReserveAmmo(), 20);
	}

	DestroyReloadTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryReloadInstanceIsolationTest,
	"ShootGame.Inventory.Reload.InstanceIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryReloadInstanceIsolationTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryReloadAutomationTests;

	UWorld* World = CreateReloadTestWorld(*this);
	if (!World)
	{
		return false;
	}

	// 两把同状态武器：只对第一把提交换弹，第二把的弹药必须保持隔离。
	AShooterInventoryReloadTestWeapon* First = SpawnReloadTestWeapon(*this, World, 30, 5, 20);
	AShooterInventoryReloadTestWeapon* Second = SpawnReloadTestWeapon(*this, World, 30, 5, 20);
	if (First && Second)
	{
		int32 TransferredAmmo = INDEX_NONE;
		TestTrue(TEXT("Only the first weapon reloads"), First->ReloadFromReserve(TransferredAmmo));
		TestEqual(TEXT("First weapon receives the transfer"), TransferredAmmo, 20);
		TestEqual(TEXT("First magazine is modified"), First->GetBulletCount(), 25);
		TestEqual(TEXT("First reserve is modified"), First->GetReserveAmmo(), 0);
		TestEqual(TEXT("Second magazine is isolated"), Second->GetBulletCount(), 5);
		TestEqual(TEXT("Second reserve is isolated"), Second->GetReserveAmmo(), 20);

		// 满弹匣后重复提交拒绝：换弹事务没有第二次转移。
		int32 RepeatTransferAmmo = INDEX_NONE;
		TestFalse(
			TEXT("Repeat reload on the now-full magazine is rejected"),
			First->ReloadFromReserve(RepeatTransferAmmo));
		TestEqual(TEXT("Repeat transfer reports zero"), RepeatTransferAmmo, 0);
	}

	DestroyReloadTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
