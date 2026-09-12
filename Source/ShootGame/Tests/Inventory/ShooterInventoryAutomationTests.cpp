// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/CoreNet.h"
#include "UObject/UnrealType.h"
#include "ShooterCharacter.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "ShooterInventoryComponent.h"
#include "ShooterInventoryTypes.h"
#include "ShooterInventoryReserveTestTypes.h"
#include "ShooterWeapon.h"
#include "Tests/Pool/ShooterWeaponRuntimeTestTypes.h"

/**
 * S3 Inventory 契约测试：最小 Actor Entry（WeaponActor + SlotIndex）。
 * FastArray 数据契约使用真实 WeaponActor 实例（WeaponRuntime 测试武器）驱动。
 */
namespace ShooterInventoryAutomationTests
{
	UWorld* CreateInventoryContractWorld(FAutomationTestBase& Test)
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!Test.TestNotNull(TEXT("Inventory contract world created"), World) || !GEngine)
		{
			return nullptr;
		}

		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		return World;
	}

	void DestroyInventoryContractWorld(UWorld* World)
	{
		if (World && GEngine)
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	}

	/** 生成一把具备唯一 WeaponId 的权威测试武器（直接 Spawn，不入池）。 */
	AShooterRuntimePoolTestWeapon* SpawnInventoryContractWeapon(FAutomationTestBase& Test, UWorld* World, FName WeaponId)
	{
		AShooterRuntimePoolTestWeapon* Weapon = World
			? World->SpawnActor<AShooterRuntimePoolTestWeapon>(FVector::ZeroVector, FRotator::ZeroRotator)
			: nullptr;
		if (!Test.TestNotNull(TEXT("Inventory contract weapon spawned"), Weapon))
		{
			return nullptr;
		}

		Weapon->InitializeWeaponIdentity(WeaponId);
		Weapon->DispatchBeginPlay();
		return Weapon;
	}
}

/** FastArray 数据契约：Actor + Slot 的 Add / Remove / Clear 与查找。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInventoryAddWeaponTest, "ShootGame.Inventory.AddWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryAddWeaponTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryAutomationTests;

	UWorld* World = CreateInventoryContractWorld(*this);
	if (!World)
	{
		return false;
	}

	FShooterWeaponInventoryList Inventory;
	AShooterRuntimePoolTestWeapon* First = SpawnInventoryContractWeapon(*this, World, TEXT("TestWeapon_First"));
	AShooterRuntimePoolTestWeapon* Second = SpawnInventoryContractWeapon(*this, World, TEXT("TestWeapon_Second"));
	if (!First || !Second)
	{
		DestroyInventoryContractWorld(World);
		return false;
	}

	TestTrue(TEXT("First weapon is added"), Inventory.AddItem(First, 0));
	TestEqual(TEXT("Inventory count becomes 1"), Inventory.Items.Num(), 1);
	TestNotNull(TEXT("Added weapon can be found"), Inventory.FindItem(First));
	TestNotNull(TEXT("Added weapon can be found by WeaponId"), Inventory.FindItemByWeaponId(TEXT("TestWeapon_First")));

	TestTrue(TEXT("Second weapon with another slot is added"), Inventory.AddItem(Second, 1));
	TestEqual(TEXT("Inventory count becomes 2"), Inventory.Items.Num(), 2);
	TestNotNull(TEXT("Second weapon can be found"), Inventory.FindItem(Second));

	TestTrue(TEXT("First weapon can be removed"), Inventory.RemoveItem(First));
	TestEqual(TEXT("Inventory count returns to 1"), Inventory.Items.Num(), 1);
	TestNull(TEXT("Removed weapon is no longer found"), Inventory.FindItem(First));

	Inventory.ClearItems();
	TestEqual(TEXT("Clear empties inventory"), Inventory.Items.Num(), 0);
	DestroyInventoryContractWorld(World);
	return true;
}

/** 同一 Actor 重复入库拒绝；WeaponId 查找语义与空身份拒绝。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInventoryUniqueWeaponTest, "ShootGame.Inventory.UniqueWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryUniqueWeaponTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryAutomationTests;

	UWorld* World = CreateInventoryContractWorld(*this);
	if (!World)
	{
		return false;
	}

	FShooterWeaponInventoryList Inventory;
	AShooterRuntimePoolTestWeapon* First = SpawnInventoryContractWeapon(*this, World, TEXT("TestWeapon_Dup"));
	AShooterRuntimePoolTestWeapon* Second = SpawnInventoryContractWeapon(*this, World, TEXT("TestWeapon_Other"));
	AShooterRuntimePoolTestWeapon* SameId = SpawnInventoryContractWeapon(*this, World, TEXT("TestWeapon_Dup"));
	if (!First || !Second || !SameId)
	{
		DestroyInventoryContractWorld(World);
		return false;
	}

	// FastArray 层只保证 Actor 与 Slot 唯一；WeaponId 判重是 InventoryComponent 的组件层策略
	//（由 WeaponGrant.Rejection 测试覆盖）。
	TestTrue(TEXT("First weapon is added"), Inventory.AddItem(First, 0));
	TestTrue(TEXT("Second weapon with another WeaponId is added"), Inventory.AddItem(Second, 1));
	TestFalse(TEXT("Same actor cannot be added twice"), Inventory.AddItem(First, 2));
	TestNotNull(TEXT("FindItemByWeaponId resolves the first holder of the WeaponId"),
		Inventory.FindItemByWeaponId(TEXT("TestWeapon_Dup")));
	TestEqual(TEXT("Container keeps two entries"), Inventory.Items.Num(), 2);
	TestNull(TEXT("None WeaponId finds nothing"), Inventory.FindItemByWeaponId(NAME_None));
	DestroyInventoryContractWorld(World);
	return true;
}

/** Slot 唯一性：重复 Slot 拒绝，非法 Slot 拒绝。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInventorySlotUniquenessTest, "ShootGame.Inventory.SlotUniqueness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventorySlotUniquenessTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryAutomationTests;

	UWorld* World = CreateInventoryContractWorld(*this);
	if (!World)
	{
		return false;
	}

	FShooterWeaponInventoryList Inventory;
	AShooterRuntimePoolTestWeapon* First = SpawnInventoryContractWeapon(*this, World, TEXT("TestWeapon_SlotA"));
	AShooterRuntimePoolTestWeapon* Second = SpawnInventoryContractWeapon(*this, World, TEXT("TestWeapon_SlotB"));
	AShooterRuntimePoolTestWeapon* Third = SpawnInventoryContractWeapon(*this, World, TEXT("TestWeapon_SlotC"));
	if (!First || !Second || !Third)
	{
		DestroyInventoryContractWorld(World);
		return false;
	}

	TestTrue(TEXT("Slot 0 is accepted"), Inventory.AddItem(First, 0));
	TestFalse(TEXT("Duplicate Slot 0 is rejected"), Inventory.AddItem(Second, 0));
	TestTrue(TEXT("Slot 1 is accepted"), Inventory.AddItem(Second, 1));
	TestFalse(TEXT("Duplicate Slot 1 is rejected"), Inventory.AddItem(Third, 1));
	TestEqual(TEXT("Inventory keeps two entries"), Inventory.Items.Num(), 2);
	TestFalse(TEXT("Invalid slot is rejected"), Inventory.AddItem(Third, INDEX_NONE));
	DestroyInventoryContractWorld(World);
	return true;
}

/** 复制契约：OwnerOnly FastArray 结构、反射字段与 WeaponId 身份；无任何实例身份残留。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInventoryOwnerReplicationTest, "ShootGame.Inventory.OwnerReplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryOwnerReplicationTest::RunTest(const FString& Parameters)
{
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
		UShooterInventoryComponent::StaticClass(), TEXT("ReplicatedInventory"));
	if (!TestNotNull(TEXT("InventoryComponent exposes ReplicatedInventory"), InventoryListProperty))
	{
		return false;
	}

	TestTrue(TEXT("ReplicatedInventory is FShooterWeaponInventoryList"),
		InventoryListProperty->Struct == FShooterWeaponInventoryList::StaticStruct());

	TestTrue(TEXT("Inventory FastArray list enables NetDeltaSerialize"),
		TStructOpsTypeTraits<FShooterWeaponInventoryList>::WithNetDeltaSerializer);
	// S3：Entry 只含 WeaponActor 引用与 SlotIndex，不再有手写 NetSerialize 或实例身份。
	TestFalse(TEXT("Inventory FastArray item no longer enables a manual NetSerialize"),
		TStructOpsTypeTraits<FShooterInventoryWeaponEntry>::WithNetSerializer);

	// 实例身份属性已从所有复制面删除。
	TestNull(TEXT("Inventory no longer owns ActiveWeaponInstanceId property"),
		FindFProperty<FProperty>(UShooterInventoryComponent::StaticClass(), TEXT("ActiveWeaponInstanceId")));
	TestNull(TEXT("WeaponActor no longer owns BoundInstanceId property"),
		FindFProperty<FProperty>(AShooterWeapon::StaticClass(), TEXT("BoundInstanceId")));
	TestNull(TEXT("Equipment no longer owns ActiveWeaponInstanceId property"),
		FindFProperty<FProperty>(UShooterEquipmentComponent::StaticClass(), TEXT("ActiveWeaponInstanceId")));

	const FProperty* EquipmentCurrentWeaponProperty = FindFProperty<FProperty>(
		UShooterEquipmentComponent::StaticClass(), TEXT("CurrentWeaponActor"));
	if (!TestNotNull(TEXT("Equipment exposes CurrentWeaponActor"), EquipmentCurrentWeaponProperty))
	{
		return false;
	}
	TestTrue(TEXT("Equipment CurrentWeaponActor is replicated"), EquipmentCurrentWeaponProperty->HasAnyPropertyFlags(CPF_Net));
	TestEqual(TEXT("Equipment CurrentWeaponActor uses OnRep_CurrentWeaponActor"),
		EquipmentCurrentWeaponProperty->RepNotifyFunc, FName(TEXT("OnRep_CurrentWeaponActor")));

	const FProperty* WeaponIdProperty = FindFProperty<FProperty>(AShooterWeapon::StaticClass(), TEXT("WeaponId"));
	if (!TestNotNull(TEXT("WeaponActor exposes WeaponId"), WeaponIdProperty))
	{
		return false;
	}
	TestTrue(TEXT("WeaponId is replicated to everyone"), WeaponIdProperty->HasAnyPropertyFlags(CPF_Net));

	// 完整 FastArray 的 COND_OwnerOnly 登记属于网络运行时行为，
	// 由 ShooterNetworkTestCoordinator 在 Listen / Dedicated 会话中验证。
	return true;
}

/** Entry 复制面：Actor 引用 + Slot 由 FastArray 默认反射序列化承载，无手写 NetSerialize。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInventoryRemoteHiddenTest, "ShootGame.Inventory.RemoteHidden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryRemoteHiddenTest::RunTest(const FString& Parameters)
{
	UScriptStruct* EntryStruct = FShooterInventoryWeaponEntry::StaticStruct();
	if (!TestNotNull(TEXT("Inventory entry struct exists"), EntryStruct))
	{
		return false;
	}

	// Entry 只包含 WeaponActor 引用与 SlotIndex 两个反射字段。
	const FProperty* WeaponProperty = EntryStruct->FindPropertyByName(TEXT("Weapon"));
	const FProperty* SlotProperty = EntryStruct->FindPropertyByName(TEXT("SlotIndex"));
	if (!TestNotNull(TEXT("Entry exposes Weapon actor reference"), WeaponProperty) ||
		!TestNotNull(TEXT("Entry exposes SlotIndex"), SlotProperty))
	{
		return false;
	}
	TestTrue(TEXT("Weapon field is an object property"), WeaponProperty && WeaponProperty->IsA<FObjectProperty>());
	TestTrue(TEXT("SlotIndex field is an int property"), SlotProperty && SlotProperty->IsA<FIntProperty>());

	// Actor 引用由 FastArray 网络序列化（NetGUID）解析，真实复制到达顺序
	// 由 ShooterNetworkTestCoordinator 在 Listen / Dedicated 会话中验证。
	return true;
}

#endif
