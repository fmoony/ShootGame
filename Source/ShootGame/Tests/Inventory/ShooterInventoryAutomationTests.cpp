// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/CoreNet.h"
#include "UObject/UnrealType.h"
#include "ShooterCharacter.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "ShooterInventoryComponent.h"
#include "ShooterInventoryTypes.h"
#include "ShooterWeapon.h"

namespace ShooterInventoryAutomationTests
{
	FShooterWeaponInstanceData MakeWeaponInstanceData(const FGuid& InstanceId, int32 SlotIndex)
	{
		FShooterWeaponInstanceData InstanceData;
		InstanceData.InstanceId = InstanceId;
		// 单表武器配置纠偏：武器类型身份由 DefinitionId 改为武器模板行名（DT_WeaponData 行），
		// IsValid() 也要求行名非空，这里按 InstanceId 派生唯一行名。
		InstanceData.WeaponRowName = FName(*FString::Printf(
			TEXT("TestWeapon_%s"),
			*InstanceId.ToString()));
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
	// 单表武器配置纠偏：Entry 只含 UHT 反射字段（FGuid / FName / int32），
	// 因此恢复 UE 默认 Struct Delta 序列化：不再有手写 NetSerialize，也不再需要
	// TStructOpsTypeTraits<FShooterWeaponInstanceEntry> 特化。
	TestFalse(
		TEXT("Inventory FastArray item no longer enables a manual NetSerialize"),
		TStructOpsTypeTraits<FShooterWeaponInstanceEntry>::WithNetSerializer);

	// R4：Active 身份迁入 Equipment；Inventory 不再持有复制字段。
	TestNull(
		TEXT("Inventory no longer owns ActiveWeaponInstanceId property"),
		FindFProperty<FProperty>(UShooterInventoryComponent::StaticClass(), TEXT("ActiveWeaponInstanceId")));

	const FProperty* EquipmentCurrentWeaponProperty = FindFProperty<FProperty>(
		UShooterEquipmentComponent::StaticClass(),
		TEXT("CurrentWeaponActor"));
	if (!TestNotNull(TEXT("Equipment exposes CurrentWeaponActor"), EquipmentCurrentWeaponProperty))
	{
		return false;
	}
	TestTrue(TEXT("Equipment CurrentWeaponActor is replicated"), EquipmentCurrentWeaponProperty->HasAnyPropertyFlags(CPF_Net));
	TestEqual(
		TEXT("Equipment CurrentWeaponActor uses OnRep_CurrentWeaponActor"),
		EquipmentCurrentWeaponProperty->RepNotifyFunc,
		FName(TEXT("OnRep_CurrentWeaponActor")));

	// 完整 FastArray 的 COND_OwnerOnly 登记属于网络运行时行为，
	// 由 ShooterNetworkTestCoordinator 在 Listen / Dedicated 会话中验证。
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
	// Listen / Dedicated 会话中验证；这里覆盖 FastArray Entry 的序列化闭环。
	//
	// 单表武器配置纠偏：FShooterWeaponInstanceEntry 不再手写 NetSerialize，负载由 UE
	// 默认的反射 Struct Delta 序列化承担（FShooterWeaponInventoryList 仍启用
	// WithNetDeltaSerializer）。InstanceData 只含 FGuid / FName / int32 等 UHT 反射字段，
	// 因此这里用 UScriptStruct::SerializeItem 走同一条默认反射序列化路径。
	FShooterWeaponInventoryList SourceInventory;
	const FShooterWeaponInstanceData Source = MakeWeaponInstanceData(FGuid::NewGuid(), 0);
	TestTrue(TEXT("Source entry is added through AddItem"), SourceInventory.AddItem(Source));
	TestEqual(TEXT("Source inventory holds one entry"), SourceInventory.Items.Num(), 1);
	if (SourceInventory.Items.Num() != 1)
	{
		return false;
	}

	FShooterWeaponInstanceEntry* SourceEntry = &SourceInventory.Items[0];

	TArray<uint8> Buffer;
	FMemoryWriter Writer(Buffer, true);
	FShooterWeaponInstanceEntry::StaticStruct()->SerializeItem(Writer, SourceEntry, nullptr);
	TestFalse(TEXT("Inventory entry serializes without archive error"), Writer.IsError());
	TestTrue(TEXT("Inventory entry writes a non-empty payload"), Buffer.Num() > 0);

	FShooterWeaponInstanceEntry ReadEntry;
	FMemoryReader Reader(Buffer, true);
	FShooterWeaponInstanceEntry::StaticStruct()->SerializeItem(Reader, &ReadEntry, nullptr);
	TestFalse(TEXT("Inventory entry deserializes without archive error"), Reader.IsError());

	TestTrue(TEXT("InstanceId survives roundtrip"), ReadEntry.InstanceData.InstanceId == Source.InstanceId);
	TestTrue(TEXT("WeaponRowName survives roundtrip"), ReadEntry.InstanceData.WeaponRowName == Source.WeaponRowName);
	TestEqual(TEXT("MagazineAmmo survives roundtrip"), ReadEntry.InstanceData.MagazineAmmo, Source.MagazineAmmo);
	TestEqual(TEXT("ReserveAmmo survives roundtrip"), ReadEntry.InstanceData.ReserveAmmo, Source.ReserveAmmo);
	TestEqual(TEXT("SlotIndex survives roundtrip"), ReadEntry.InstanceData.SlotIndex, Source.SlotIndex);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryWeaponActorBindingTest,
	"ShootGame.Inventory.WeaponActorBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryWeaponActorBindingTest::RunTest(const FString& Parameters)
{
	const FProperty* BoundInstanceIdProperty = FindFProperty<FProperty>(
		AShooterWeapon::StaticClass(),
		TEXT("BoundInstanceId"));
	if (!TestNotNull(TEXT("AShooterWeapon exposes BoundInstanceId"), BoundInstanceIdProperty))
	{
		return false;
	}

	TestTrue(TEXT("BoundInstanceId is replicated"), BoundInstanceIdProperty->HasAnyPropertyFlags(CPF_Net));
	TestEqual(
		TEXT("BoundInstanceId uses OnRep_BoundInstanceId"),
		BoundInstanceIdProperty->RepNotifyFunc,
		FName(TEXT("OnRep_BoundInstanceId")));

	const AShooterWeapon* WeaponDefaults = GetDefault<AShooterWeapon>();
	if (!TestNotNull(TEXT("AShooterWeapon has defaults"), WeaponDefaults))
	{
		return false;
	}

	TestFalse(TEXT("WeaponActor starts unbound"), WeaponDefaults->GetBoundInstanceId().IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryPickupGrantContractTest,
	"ShootGame.Inventory.Pickup.ServerAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryPickupGrantContractTest::RunTest(const FString& Parameters)
{
	const UShooterInventoryComponent* InventoryDefaults =
		GetDefault<UShooterInventoryComponent>();
	if (!TestNotNull(TEXT("InventoryComponent has defaults"), InventoryDefaults))
	{
		return false;
	}

	// 服务器权威授予 + SlotFull Reject 的网络行为由 ShooterNetworkTestCoordinator 在
	// Dedicated / Listen 中验证；这里检查本地数据契约的配置面。
	TestTrue(TEXT("Inventory has a positive Slot limit"), InventoryDefaults->GetMaxWeaponSlots() > 0);

	const FProperty* MaxWeaponSlotsProperty = FindFProperty<FProperty>(
		UShooterInventoryComponent::StaticClass(),
		TEXT("MaxWeaponSlots"));
	TestNotNull(TEXT("Inventory exposes MaxWeaponSlots"), MaxWeaponSlotsProperty);
	TestTrue(
		TEXT("MaxWeaponSlots is a replicated-data-free config"),
		MaxWeaponSlotsProperty && !MaxWeaponSlotsProperty->HasAnyPropertyFlags(CPF_Net));

	const UFunction* TryAddWeaponFunction =
		UShooterInventoryComponent::StaticClass()->FindFunctionByName(TEXT("TryAddWeaponRow"));
	TestNull(
		TEXT("TryAddWeaponRow is not exposed as a client-callable UFUNCTION"),
		TryAddWeaponFunction);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventorySwitchContractTest,
	"ShootGame.Inventory.Switch.Valid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventorySwitchContractTest::RunTest(const FString& Parameters)
{
	// R4：CurrentWeaponActor 是 Equipment 的复制字段，Character 只有转发 Getter。
	TestNull(
		TEXT("Character no longer owns CurrentWeapon property"),
		FindFProperty<FProperty>(AShooterCharacter::StaticClass(), TEXT("CurrentWeapon")));

	const FProperty* EquipmentCurrentWeaponProperty = FindFProperty<FProperty>(
		UShooterEquipmentComponent::StaticClass(),
		TEXT("CurrentWeaponActor"));
	if (!TestNotNull(TEXT("Equipment exposes CurrentWeaponActor"), EquipmentCurrentWeaponProperty))
	{
		return false;
	}
	TestTrue(TEXT("Equipment CurrentWeaponActor is replicated"), EquipmentCurrentWeaponProperty->HasAnyPropertyFlags(CPF_Net));
	TestEqual(
		TEXT("Equipment CurrentWeaponActor uses OnRep_CurrentWeaponActor"),
		EquipmentCurrentWeaponProperty->RepNotifyFunc,
		FName(TEXT("OnRep_CurrentWeaponActor")));

	const AShooterCharacter* CharacterDefaults = GetDefault<AShooterCharacter>();
	TestNotNull(TEXT("Character has defaults"), CharacterDefaults);
	TestEqual(
		TEXT("GetCurrentWeaponActor mirrors GetCurrentWeapon"),
		CharacterDefaults ? CharacterDefaults->GetCurrentWeaponActor() : nullptr,
		CharacterDefaults ? CharacterDefaults->GetCurrentWeapon() : nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryAmmoConsumeTest,
	"ShootGame.Inventory.AmmoConsume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryAmmoConsumeTest::RunTest(const FString& Parameters)
{
	FShooterWeaponInventoryList Inventory;
	FShooterWeaponInstanceData InstanceData;
	InstanceData.InstanceId = FGuid::NewGuid();
	// 单表武器配置纠偏：武器类型身份改为武器模板行名，IsValid() 要求行名非空。
	InstanceData.WeaponRowName = FName(TEXT("TestWeapon_AmmoConsume"));
	InstanceData.MagazineAmmo = 2;
	InstanceData.ReserveAmmo = 10;
	InstanceData.SlotIndex = 0;

	TestTrue(TEXT("Weapon instance is added"), Inventory.AddItem(InstanceData));
	TestTrue(TEXT("First round can be consumed"), Inventory.ConsumeMagazineAmmo(InstanceData.InstanceId));
	TestEqual(TEXT("Magazine decreases to 1"), Inventory.FindItem(InstanceData.InstanceId)->InstanceData.MagazineAmmo, 1);
	TestTrue(TEXT("Second round can be consumed"), Inventory.ConsumeMagazineAmmo(InstanceData.InstanceId));
	TestFalse(TEXT("Empty magazine cannot be consumed"), Inventory.ConsumeMagazineAmmo(InstanceData.InstanceId));
	TestEqual(TEXT("Magazine remains 0"), Inventory.FindItem(InstanceData.InstanceId)->InstanceData.MagazineAmmo, 0);
	TestFalse(TEXT("Invalid InstanceId is rejected"), Inventory.ConsumeMagazineAmmo(FGuid::NewGuid()));
	TestEqual(TEXT("ReserveAmmo is untouched by magazine consumption"), Inventory.FindItem(InstanceData.InstanceId)->InstanceData.ReserveAmmo, 10);
	return true;
}

#endif
