// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ShooterInventoryTypes.h"

namespace ShooterInventoryReloadAutomationTests
{
	FShooterWeaponInstanceData MakeWeaponInstanceData(
		const FGuid& InstanceId,
		int32 SlotIndex,
		int32 MagazineAmmo,
		int32 ReserveAmmo)
	{
		FShooterWeaponInstanceData InstanceData;
		InstanceData.InstanceId = InstanceId;
		InstanceData.DefinitionId = FPrimaryAssetId(
			FPrimaryAssetType(TEXT("ShooterTest")),
			FName(TEXT("Weapon")));
		InstanceData.MagazineAmmo = MagazineAmmo;
		InstanceData.ReserveAmmo = ReserveAmmo;
		InstanceData.SlotIndex = SlotIndex;
		return InstanceData;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryReloadTransferTest,
	"ShootGame.Inventory.Reload.Transfer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryReloadTransferTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryReloadAutomationTests;

	FShooterWeaponInventoryList Inventory;
	const FGuid InstanceId = FGuid::NewGuid();
	TestTrue(
		TEXT("Weapon instance is added"),
		Inventory.AddItem(MakeWeaponInstanceData(InstanceId, 0, 5, 20)));

	int32 TransferredAmmo = INDEX_NONE;
	const bool bReloaded = Inventory.ReloadMagazine(InstanceId, 30, TransferredAmmo);
	TestTrue(TEXT("Reload transaction commits"), bReloaded);
	TestEqual(TEXT("Transfer equals available reserve"), TransferredAmmo, 20);
	TestEqual(
		TEXT("Magazine becomes 5 + 20"),
		Inventory.FindItem(InstanceId)->InstanceData.MagazineAmmo,
		25);
	TestEqual(
		TEXT("Reserve becomes 20 - 20"),
		Inventory.FindItem(InstanceId)->InstanceData.ReserveAmmo,
		0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryReloadClampCapacityTest,
	"ShootGame.Inventory.Reload.ClampCapacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryReloadClampCapacityTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryReloadAutomationTests;

	FShooterWeaponInventoryList Inventory;
	const FGuid InstanceId = FGuid::NewGuid();
	TestTrue(
		TEXT("Weapon instance is added"),
		Inventory.AddItem(MakeWeaponInstanceData(InstanceId, 0, 4, 20)));

	int32 TransferredAmmo = INDEX_NONE;
	TestTrue(TEXT("Reload transaction commits"), Inventory.ReloadMagazine(InstanceId, 12, TransferredAmmo));
	TestEqual(TEXT("Transfer is clamped by remaining capacity"), TransferredAmmo, 8);
	TestEqual(
		TEXT("Magazine is filled exactly to capacity"),
		Inventory.FindItem(InstanceId)->InstanceData.MagazineAmmo,
		12);
	TestEqual(
		TEXT("Reserve keeps the unneeded ammo"),
		Inventory.FindItem(InstanceId)->InstanceData.ReserveAmmo,
		12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryReloadNoReserveTest,
	"ShootGame.Inventory.Reload.NoReserve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryReloadNoReserveTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryReloadAutomationTests;

	FShooterWeaponInventoryList Inventory;
	const FGuid InstanceId = FGuid::NewGuid();
	TestTrue(
		TEXT("Weapon instance is added"),
		Inventory.AddItem(MakeWeaponInstanceData(InstanceId, 0, 5, 0)));

	int32 TransferredAmmo = INDEX_NONE;
	TestFalse(TEXT("Reload with no reserve is rejected"), Inventory.ReloadMagazine(InstanceId, 30, TransferredAmmo));
	TestEqual(TEXT("Transfer reports zero"), TransferredAmmo, 0);
	TestEqual(
		TEXT("Magazine is unchanged"),
		Inventory.FindItem(InstanceId)->InstanceData.MagazineAmmo,
		5);
	TestEqual(
		TEXT("Reserve is unchanged"),
		Inventory.FindItem(InstanceId)->InstanceData.ReserveAmmo,
		0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryReloadFullMagazineTest,
	"ShootGame.Inventory.Reload.FullMagazine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryReloadFullMagazineTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryReloadAutomationTests;

	FShooterWeaponInventoryList Inventory;
	const FGuid InstanceId = FGuid::NewGuid();
	TestTrue(
		TEXT("Weapon instance is added"),
		Inventory.AddItem(MakeWeaponInstanceData(InstanceId, 0, 30, 20)));

	int32 TransferredAmmo = INDEX_NONE;
	TestFalse(TEXT("Full magazine rejects reload"), Inventory.ReloadMagazine(InstanceId, 30, TransferredAmmo));
	TestEqual(TEXT("Transfer reports zero"), TransferredAmmo, 0);
	TestEqual(
		TEXT("Magazine is unchanged"),
		Inventory.FindItem(InstanceId)->InstanceData.MagazineAmmo,
		30);
	TestEqual(
		TEXT("Reserve is unchanged"),
		Inventory.FindItem(InstanceId)->InstanceData.ReserveAmmo,
		20);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryReloadInstanceIsolationTest,
	"ShootGame.Inventory.Reload.InstanceIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryReloadInstanceIsolationTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryReloadAutomationTests;

	FShooterWeaponInventoryList Inventory;
	const FGuid FirstId = FGuid::NewGuid();
	const FGuid SecondId = FGuid::NewGuid();
	TestTrue(
		TEXT("First weapon instance is added"),
		Inventory.AddItem(MakeWeaponInstanceData(FirstId, 0, 5, 20)));
	TestTrue(
		TEXT("Second weapon instance is added"),
		Inventory.AddItem(MakeWeaponInstanceData(SecondId, 1, 5, 20)));

	int32 TransferredAmmo = INDEX_NONE;
	TestTrue(TEXT("Only first instance reloads"), Inventory.ReloadMagazine(FirstId, 30, TransferredAmmo));
	TestEqual(TEXT("First instance receives the transfer"), TransferredAmmo, 20);
	TestEqual(
		TEXT("First magazine is modified"),
		Inventory.FindItem(FirstId)->InstanceData.MagazineAmmo,
		25);
	TestEqual(
		TEXT("First reserve is modified"),
		Inventory.FindItem(FirstId)->InstanceData.ReserveAmmo,
		0);
	TestEqual(
		TEXT("Second magazine is isolated"),
		Inventory.FindItem(SecondId)->InstanceData.MagazineAmmo,
		5);
	TestEqual(
		TEXT("Second reserve is isolated"),
		Inventory.FindItem(SecondId)->InstanceData.ReserveAmmo,
		20);

	int32 InvalidTransferAmmo = INDEX_NONE;
	TestFalse(
		TEXT("Invalid InstanceId is rejected"),
		Inventory.ReloadMagazine(FGuid::NewGuid(), 30, InvalidTransferAmmo));
	TestEqual(TEXT("Invalid transfer reports zero"), InvalidTransferAmmo, 0);
	return true;
}

#endif
