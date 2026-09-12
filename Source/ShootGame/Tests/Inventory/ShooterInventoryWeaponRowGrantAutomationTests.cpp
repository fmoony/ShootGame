// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/ShooterWeaponConfigRow.h"
#include "Weapons/ShooterWeaponRuntimeSubsystem.h"
#include "../Equipment/ShooterWeaponPresentationTestTypes.h"
#include "../Weapon/ShooterWeaponTestTableTypes.h"

/**
 * S3 授予链测试：WeaponId 行 → 运行时快照 → Acquire → Inventory.AddWeapon。
 * 行名值即 WeaponId；实例数据、绑定与查表全部不再存在。
 */
namespace ShooterInventoryWeaponRowGrantAutomationTests
{
	UWorld* CreateWeaponRowGrantTestWorld()
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

	void DestroyWeaponRowGrantTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	AShooterWeaponPresentationTestCharacter* SpawnWeaponRowGrantTestCharacter(FAutomationTestBase& Test, UWorld* World)
	{
		AShooterWeaponPresentationTestCharacter* Character = World->SpawnActor<AShooterWeaponPresentationTestCharacter>(
				FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("Weapon grant test character spawned"), Character))
		{
			return nullptr;
		}

		if (AWorldSettings* WorldSettings = World->GetWorldSettings())
		{
			WorldSettings->NotifyBeginPlay();
		}
		return Character;
	}
}

/** 授予主路径：WeaponActor 完全由所选行的运行时快照驱动，弹药与容量来自 Actor。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInventoryWeaponGrantTest, "ShootGame.Inventory.WeaponGrant.Initialize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryWeaponGrantTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryWeaponRowGrantAutomationTests;

	UWorld* World = CreateWeaponRowGrantTestWorld();
	if (!TestNotNull(TEXT("Weapon grant world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnWeaponRowGrantTestCharacter(*this, World);
	if (!Character)
	{
		DestroyWeaponRowGrantTestWorld(World);
		return false;
	}

	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	if (!TestNotNull(TEXT("Character owns inventory"), Inventory))
	{
		DestroyWeaponRowGrantTestWorld(World);
		return false;
	}

	EShooterInventoryAddResult Result = EShooterInventoryAddResult::NotAuthoritative;
	AShooterWeapon* Weapon = GrantTestWeapon(World, Inventory, AShooterInventoryOrderTestWeapon::StaticClass(),
		/*MagazineSize*/ 12,
		/*InitialReserveAmmo*/ 36,
		&Result);
	TestEqual(TEXT("Weapon grant succeeds"), static_cast<int32>(Result),
		static_cast<int32>(EShooterInventoryAddResult::Added));
	if (!TestNotNull(TEXT("Granted weapon actor exists"), Weapon))
	{
		DestroyWeaponRowGrantTestWorld(World);
		return false;
	}

	TestFalse(TEXT("Granted WeaponId is valid"), Weapon->GetWeaponId().IsNone());
	TestTrue(TEXT("Weapon actor class is the row WeaponActorClass"),
		Weapon->GetClass() == AShooterInventoryOrderTestWeapon::StaticClass());
	// 行配置在创建时应用到 WeaponActor：容量与初始弹药都来自快照。
	TestEqual(TEXT("Weapon actor magazine capacity comes from the row"), Weapon->GetMagazineSize(), 12);
	TestEqual(TEXT("Magazine initializes from the row"), Weapon->GetBulletCount(), 12);
	TestEqual(TEXT("Reserve initializes from the row"), Weapon->GetReserveAmmo(), 36);
	TestTrue(TEXT("Weapon actor starts hidden"), Weapon->IsHidden());
	TestEqual(TEXT("Granted weapon waits at Holstered"), static_cast<int32>(Weapon->GetLifecycleState()),
		static_cast<int32>(EShooterWeaponLifecycleState::Holstered));

	// Entry 数据只包含 Actor 与 Slot。
	TestEqual(TEXT("Inventory holds one entry"), Inventory->GetWeaponCount(), 1);
	if (Inventory->GetWeaponCount() == 1)
	{
		const FShooterInventoryWeaponEntry& Entry = Inventory->GetWeaponEntries()[0];
		TestEqual(TEXT("Entry references the granted actor"), Entry.Weapon.Get(), Weapon);
		TestEqual(TEXT("Slot auto-selected"), Entry.SlotIndex, 0);
	}

	DestroyWeaponRowGrantTestWorld(World);
	return true;
}

/** 授予失败路径：非法 Actor、Slot 满、重复 WeaponId。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInventoryWeaponGrantRejectionTest, "ShootGame.Inventory.WeaponGrant.Rejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryWeaponGrantRejectionTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryWeaponRowGrantAutomationTests;

	UWorld* World = CreateWeaponRowGrantTestWorld();
	if (!TestNotNull(TEXT("Weapon rejection world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnWeaponRowGrantTestCharacter(*this, World);
	if (!Character)
	{
		DestroyWeaponRowGrantTestWorld(World);
		return false;
	}

	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	UShooterWeaponRuntimeSubsystem* Runtime = World->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
	if (!TestNotNull(TEXT("Character owns inventory"), Inventory) || !TestNotNull(TEXT("World owns weapon runtime"), Runtime))
	{
		DestroyWeaponRowGrantTestWorld(World);
		return false;
	}

	// 未知 WeaponId：Runtime 不提供该桶，Pickup 在 Acquire 前即拒绝、不消费。
	TestNull(TEXT("Unknown WeaponId acquires nothing"),
		Runtime->AcquireWeapon(TEXT("MissingWeaponId"), Character, nullptr));

	// 占满 3 个 Slot 后第 4 把被 SlotFull 拒绝。
	AShooterWeapon* Granted[3] = {nullptr, nullptr, nullptr};
	for (int32 SlotIndex = 0; SlotIndex < 3; ++SlotIndex)
	{
		EShooterInventoryAddResult Result = EShooterInventoryAddResult::NotAuthoritative;
		Granted[SlotIndex] = GrantTestWeapon(World, Inventory, AShooterInventoryOrderTestWeapon::StaticClass(), 10, 10, &Result);
		TestEqual(FString::Printf(TEXT("Slot %d grant succeeds"), SlotIndex).GetCharArray().GetData(),
			static_cast<int32>(Result), static_cast<int32>(EShooterInventoryAddResult::Added));
	}
	TestEqual(TEXT("Three distinct weapons fill all slots"), Inventory->GetWeaponCount(), 3);

	// 第 4 把：Acquire 成功但 AddWeapon 被 SlotFull 拒绝，调用方负责归还。
	UDataTable* Table = GetOrInjectRuntimeTestTable(World);
	const FName FourthRow = AddTestWeaponRow(Table,
		MakeTestWeaponRow(AShooterInventoryOrderTestWeapon::StaticClass(), 10, 10));
	Runtime->InitializeWeaponRuntimeForTest();
	AShooterWeapon* Fourth = Runtime->AcquireWeapon(FourthRow, Character, nullptr);
	if (TestNotNull(TEXT("Fourth weapon acquires from the runtime"), Fourth))
	{
		TestEqual(TEXT("Fourth weapon is rejected by SlotFull"), static_cast<int32>(Inventory->AddWeapon(Fourth)),
			static_cast<int32>(EShooterInventoryAddResult::SlotFull));
		TestTrue(TEXT("SlotFull caller returns the weapon to the pool"), Runtime->ReleaseWeapon(Fourth));
	}
	TestEqual(TEXT("SlotFull leaves inventory at three"), Inventory->GetWeaponCount(), 3);

	// 重复武器类型：与首个槽位相同 WeaponId 的新实体被拒绝。
	AShooterWeapon* Duplicate = Runtime->AcquireWeapon(Granted[0]->GetWeaponId(), Character, nullptr);
	if (TestNotNull(TEXT("Duplicate weapon acquires from the runtime"), Duplicate))
	{
		TestEqual(TEXT("Duplicate WeaponId is rejected"), static_cast<int32>(Inventory->AddWeapon(Duplicate)),
			static_cast<int32>(EShooterInventoryAddResult::DuplicateWeapon));
		TestTrue(TEXT("Duplicate caller returns the weapon to the pool"), Runtime->ReleaseWeapon(Duplicate));
	}

	// 非法 Actor：无 WeaponId 身份、非本角色持有或池内状态都不进入背包。
	AShooterWeapon* Unbound = World->SpawnActor<AShooterInventoryOrderTestWeapon>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (TestNotNull(TEXT("Unbound weapon spawns"), Unbound))
	{
		TestEqual(TEXT("Weapon without WeaponId identity is rejected"),
			static_cast<int32>(Inventory->AddWeapon(Unbound)),
			static_cast<int32>(EShooterInventoryAddResult::InvalidWeapon));
	}

	DestroyWeaponRowGrantTestWorld(World);
	return true;
}

/**
 * 换弹容量来自运行时快照：测试行刻意使用弹匣 7 / 备弹 21，
 * 与 WeaponActor 默认配置（10）不同；消耗两发后换弹只补到 7，
 * 证明容量边界由快照而非 WeaponActor 默认值决定。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInventoryWeaponGrantReloadCapacityTest,
	"ShootGame.Inventory.WeaponGrant.ReloadCapacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryWeaponGrantReloadCapacityTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryWeaponRowGrantAutomationTests;

	UWorld* World = CreateWeaponRowGrantTestWorld();
	if (!TestNotNull(TEXT("Reload capacity world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnWeaponRowGrantTestCharacter(*this, World);
	if (!Character)
	{
		DestroyWeaponRowGrantTestWorld(World);
		return false;
	}

	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	if (!TestNotNull(TEXT("Character owns inventory"), Inventory))
	{
		DestroyWeaponRowGrantTestWorld(World);
		return false;
	}

	// 行使用与 WeaponActor 默认值不同的弹匣容量，确保断言能区分两个来源。
	TestNotEqual(TEXT("Row magazine size differs from the weapon actor default"), 7,
		AShooterInventoryOrderTestWeapon::StaticClass() ->GetDefaultObject<AShooterWeapon>() ->GetMagazineSize());

	EShooterInventoryAddResult Result = EShooterInventoryAddResult::NotAuthoritative;
	AShooterWeapon* Weapon = GrantTestWeapon(World, Inventory, AShooterInventoryOrderTestWeapon::StaticClass(),
		/*MagazineSize*/ 7,
		/*InitialReserveAmmo*/ 21,
		&Result);
	if (!TestEqual(TEXT("Reload capacity grant succeeds"), static_cast<int32>(Result),
		static_cast<int32>(EShooterInventoryAddResult::Added)) || !TestNotNull(TEXT("Granted weapon exists"), Weapon))
	{
		DestroyWeaponRowGrantTestWorld(World);
		return false;
	}

	// 装备并消耗两发：7 -> 5；换弹事务在 WeaponActor 上提交。
	TestTrue(TEXT("Granted weapon equips"), Character->GetEquipmentComponent()->EquipWeapon(Weapon));
	TestTrue(TEXT("Two rounds consumed"), Weapon->ConsumeAmmo(2));
	TestEqual(TEXT("Magazine drops to five"), Weapon->GetBulletCount(), 5);

	int32 Transferred = 0;
	TestTrue(TEXT("Reload transaction commits"), Weapon->ReloadFromReserve(Transferred));
	TestEqual(TEXT("Reload transfers exactly to the row capacity"), Transferred, 2);
	TestEqual(TEXT("Magazine refills to the row capacity"), Weapon->GetBulletCount(), 7);
	TestEqual(TEXT("Reserve decreases by transfer"), Weapon->GetReserveAmmo(), 19);

	DestroyWeaponRowGrantTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
