// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/CoreNet.h"
#include "UObject/UnrealType.h"
#include "ShooterCharacter.h"
#include "ShooterInventoryComponent.h"
#include "ShooterInventoryTypes.h"

namespace ShooterInventoryAutomationTests
{
	FShooterWeaponInstanceData MakeWeaponInstanceData(const FGuid& InstanceId, int32 SlotIndex)
	{
		FShooterWeaponInstanceData InstanceData;
		InstanceData.InstanceId = InstanceId;
		InstanceData.DefinitionId = FPrimaryAssetId(
			FPrimaryAssetType(TEXT("ShooterTest")),
			FName(TEXT("Weapon")));
		InstanceData.MagazineAmmo = 24;
		InstanceData.ReserveAmmo = 90;
		InstanceData.SlotIndex = SlotIndex;
		return InstanceData;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryAddWeaponTest,
	"ShootGame.Inventory.AddWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryAddWeaponTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryAutomationTests;

	FShooterWeaponInventoryList Inventory;
	const FGuid FirstId = FGuid::NewGuid();
	const FGuid SecondId = FGuid::NewGuid();

	TestTrue(TEXT("First weapon is added"), Inventory.AddItem(MakeWeaponInstanceData(FirstId, 0)));
	TestEqual(TEXT("Inventory count becomes 1"), Inventory.Items.Num(), 1);
	TestNotNull(TEXT("Added weapon can be found"), Inventory.FindItem(FirstId));

	TestTrue(TEXT("Second weapon with another slot is added"), Inventory.AddItem(MakeWeaponInstanceData(SecondId, 1)));
	TestEqual(TEXT("Inventory count becomes 2"), Inventory.Items.Num(), 2);
	TestNotNull(TEXT("Second weapon can be found"), Inventory.FindItem(SecondId));

	TestTrue(TEXT("First weapon can be removed"), Inventory.RemoveItem(FirstId));
	TestEqual(TEXT("Inventory count returns to 1"), Inventory.Items.Num(), 1);
	TestNull(TEXT("Removed weapon is no longer found"), Inventory.FindItem(FirstId));

	Inventory.ClearItems();
	TestEqual(TEXT("Clear empties inventory"), Inventory.Items.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryUniqueInstanceIdTest,
	"ShootGame.Inventory.UniqueInstanceId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryUniqueInstanceIdTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryAutomationTests;

	FShooterWeaponInventoryList Inventory;
	const FGuid FirstId = FGuid::NewGuid();
	const FGuid SecondId = FGuid::NewGuid();

	TestTrue(TEXT("First InstanceId is valid"), FirstId.IsValid());
	TestTrue(TEXT("Second InstanceId is valid"), SecondId.IsValid());
	TestFalse(TEXT("Two generated InstanceIds differ"), FirstId == SecondId);

	TestTrue(TEXT("First instance is added"), Inventory.AddItem(MakeWeaponInstanceData(FirstId, 0)));
	TestTrue(TEXT("Second instance is added"), Inventory.AddItem(MakeWeaponInstanceData(SecondId, 1)));
	TestFalse(TEXT("Duplicate InstanceId is rejected"), Inventory.AddItem(MakeWeaponInstanceData(FirstId, 2)));
	TestEqual(TEXT("Duplicate did not change count"), Inventory.Items.Num(), 2);

	const FShooterWeaponInstanceEntry* FirstEntry = Inventory.FindItem(FirstId);
	const FShooterWeaponInstanceEntry* SecondEntry = Inventory.FindItem(SecondId);
	TestNotNull(TEXT("First entry exists"), FirstEntry);
	TestNotNull(TEXT("Second entry exists"), SecondEntry);
	if (FirstEntry && SecondEntry)
	{
		TestFalse(TEXT("Entries keep distinct InstanceIds"), FirstEntry->InstanceData.InstanceId == SecondEntry->InstanceData.InstanceId);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventorySlotUniquenessTest,
	"ShootGame.Inventory.SlotUniqueness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventorySlotUniquenessTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryAutomationTests;

	FShooterWeaponInventoryList Inventory;
	const FGuid FirstId = FGuid::NewGuid();
	const FGuid SecondId = FGuid::NewGuid();
	const FGuid ThirdId = FGuid::NewGuid();

	TestTrue(TEXT("Slot 0 is accepted"), Inventory.AddItem(MakeWeaponInstanceData(FirstId, 0)));
	TestFalse(TEXT("Duplicate Slot 0 is rejected"), Inventory.AddItem(MakeWeaponInstanceData(SecondId, 0)));
	TestTrue(TEXT("Slot 1 is accepted"), Inventory.AddItem(MakeWeaponInstanceData(SecondId, 1)));
	TestFalse(TEXT("Duplicate Slot 1 is rejected"), Inventory.AddItem(MakeWeaponInstanceData(ThirdId, 1)));
	TestEqual(TEXT("Inventory keeps two entries"), Inventory.Items.Num(), 2);

	FShooterWeaponInstanceData InvalidSlotData = MakeWeaponInstanceData(ThirdId, INDEX_NONE);
	TestFalse(TEXT("Invalid slot is rejected"), Inventory.AddItem(InvalidSlotData));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryOwnerReplicationTest,
	"ShootGame.Inventory.OwnerReplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryOwnerReplicationTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryAutomationTests;

	const AShooterCharacter* CharacterDefaults = GetDefault<AShooterCharacter>();
	if (!TestNotNull(TEXT("ShooterCharacter has defaults"), CharacterDefaults))
	{
		return false;
	}

	UShooterInventoryComponent* InventoryComponent = CharacterDefaults->GetInventoryComponent();
	if (!TestNotNull(TEXT("ShooterCharacter creates InventoryComponent"), InventoryComponent))
	{
		return false;
	}

	TestTrue(TEXT("InventoryComponent is replicated"), InventoryComponent->GetIsReplicated());

	const FStructProperty* InventoryListProperty = FindFProperty<FStructProperty>(
		UShooterInventoryComponent::StaticClass(),
		TEXT("ReplicatedInventory"));
	if (!TestNotNull(TEXT("InventoryComponent exposes ReplicatedInventory"), InventoryListProperty))
	{
		return false;
	}

	TestTrue(
		TEXT("ReplicatedInventory is FShooterWeaponInventoryList"),
		InventoryListProperty->Struct == FShooterWeaponInventoryList::StaticStruct());

	TestTrue(
		TEXT("Inventory FastArray list enables NetDeltaSerialize"),
		TStructOpsTypeTraits<FShooterWeaponInventoryList>::WithNetDeltaSerializer);
	TestTrue(
		TEXT("Inventory FastArray item enables NetSerialize"),
		TStructOpsTypeTraits<FShooterWeaponInstanceEntry>::WithNetSerializer);

	const FProperty* ActiveIdProperty = FindFProperty<FProperty>(
		UShooterInventoryComponent::StaticClass(),
		TEXT("ActiveWeaponInstanceId"));
	if (!TestNotNull(TEXT("InventoryComponent exposes ActiveWeaponInstanceId"), ActiveIdProperty))
	{
		return false;
	}
	TestTrue(TEXT("ActiveWeaponInstanceId is replicated"), ActiveIdProperty->HasAnyPropertyFlags(CPF_Net));
	TestEqual(
		TEXT("ActiveWeaponInstanceId uses OnRep_ActiveWeaponInstanceId"),
		ActiveIdProperty->RepNotifyFunc,
		FName(TEXT("OnRep_ActiveWeaponInstanceId")));

	// 完整 FastArray 与 Active ID 的 COND_OwnerOnly 登记属于网络运行时行为，
	// 由 ShooterNetworkTestCoordinator 在 Listen / Dedicated 会话中验证。
	// 这里只验证复制属性与 FastArray 序列化契约均已正确注册。
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryRemoteHiddenTest,
	"ShootGame.Inventory.RemoteHidden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryRemoteHiddenTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryAutomationTests;

	// COND_OwnerOnly 的实际“远端不收到完整列表”由网络测试协调器在
	// Listen / Dedicated 会话中验证；这里覆盖 FastArray Entry 的完整序列化闭环，
	// 确保不依赖 UPROPERTY 反射的 DefinitionId 也能正确往返。
	const FShooterWeaponInstanceData Source = MakeWeaponInstanceData(FGuid::NewGuid(), 0);

	FShooterWeaponInstanceEntry SourceEntry;
	SourceEntry.InstanceData = Source;

	TArray<uint8> Buffer;
	FMemoryWriter Writer(Buffer, true);
	bool bWriteSuccess = false;
	SourceEntry.NetSerialize(Writer, nullptr, bWriteSuccess);
	TestTrue(TEXT("Inventory entry serializes"), bWriteSuccess && !Writer.IsError());

	FShooterWeaponInstanceEntry ReadEntry;
	FMemoryReader Reader(Buffer, true);
	bool bReadSuccess = false;
	ReadEntry.NetSerialize(Reader, nullptr, bReadSuccess);
	TestTrue(TEXT("Inventory entry deserializes"), bReadSuccess && !Reader.IsError());

	TestTrue(TEXT("InstanceId survives roundtrip"), ReadEntry.InstanceData.InstanceId == Source.InstanceId);
	TestTrue(TEXT("DefinitionId survives roundtrip"), ReadEntry.InstanceData.DefinitionId == Source.DefinitionId);
	TestEqual(TEXT("MagazineAmmo survives roundtrip"), ReadEntry.InstanceData.MagazineAmmo, Source.MagazineAmmo);
	TestEqual(TEXT("ReserveAmmo survives roundtrip"), ReadEntry.InstanceData.ReserveAmmo, Source.ReserveAmmo);
	TestEqual(TEXT("SlotIndex survives roundtrip"), ReadEntry.InstanceData.SlotIndex, Source.SlotIndex);
	return true;
}

#endif
