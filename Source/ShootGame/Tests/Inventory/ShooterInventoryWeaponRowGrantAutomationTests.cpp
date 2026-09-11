// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "Inventory/ShooterInventoryTypes.h"
#include "UObject/Package.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/ShooterWeaponConfigRow.h"
#include "../Equipment/ShooterWeaponPresentationTestTypes.h"

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

	AShooterWeaponPresentationTestCharacter* SpawnWeaponRowGrantTestCharacter(
		FAutomationTestBase& Test,
		UWorld* World)
	{
		AShooterWeaponPresentationTestCharacter* Character =
			World->SpawnActor<AShooterWeaponPresentationTestCharacter>(
				FVector::ZeroVector,
				FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("Weapon row grant test character spawned"), Character))
		{
			return nullptr;
		}

		if (AWorldSettings* WorldSettings = World->GetWorldSettings())
		{
			WorldSettings->NotifyBeginPlay();
		}
		return Character;
	}

	/** 向测试表追加一行并授予，返回行名；用于需要复用同一行名的拒绝路径测试。 */
	FName AddAndGrantWeaponRow(
		UShooterInventoryComponent* Inventory,
		const FShooterWeaponConfigRow& Row,
		FGuid& OutInstanceId,
		EShooterInventoryAddResult& OutResult)
	{
		const FName RowName = AddTestWeaponRow(GetOrCreateTestWeaponTable(Inventory), Row);
		OutResult = Inventory->TryAddWeaponRow(RowName, OutInstanceId);
		return RowName;
	}
}

/** 武器模板行授予主路径：实例数据与 WeaponActor 完全由所选行驱动。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryWeaponRowGrantTest,
	"ShootGame.Inventory.WeaponRowGrant.Initialize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryWeaponRowGrantTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryWeaponRowGrantAutomationTests;

	UWorld* World = CreateWeaponRowGrantTestWorld();
	if (!TestNotNull(TEXT("Weapon row grant world created"), World))
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

	FGuid GrantedId;
	EShooterInventoryAddResult Result = EShooterInventoryAddResult::NotAuthoritative;
	const FName RowName = GrantTestWeaponRow(
		Inventory,
		AShooterInventoryOrderTestWeapon::StaticClass(),
		GrantedId,
		/*MagazineSize*/ 12,
		/*InitialReserveAmmo*/ 36,
		&Result);
	TestEqual(
		TEXT("Weapon row grant succeeds"),
		static_cast<int32>(Result),
		static_cast<int32>(EShooterInventoryAddResult::Added));
	TestTrue(TEXT("Granted instance id is valid"), GrantedId.IsValid());
	TestFalse(TEXT("Granted row name is valid"), RowName.IsNone());

	const FShooterWeaponInstanceData* Instance = Inventory->FindWeaponInstance(GrantedId);
	if (TestNotNull(TEXT("Granted instance exists"), Instance))
	{
		TestEqual(TEXT("Instance row name comes from the granted row"), Instance->WeaponRowName, RowName);
		TestEqual(TEXT("Magazine initializes from the row"), Instance->MagazineAmmo, 12);
		TestEqual(TEXT("Reserve initializes from the row"), Instance->ReserveAmmo, 36);
		TestEqual(TEXT("Slot auto-selected"), Instance->SlotIndex, 0);
	}

	AShooterWeapon* Weapon = Inventory->FindWeaponActor(GrantedId);
	if (TestNotNull(TEXT("Weapon actor spawned from the row actor class"), Weapon))
	{
		TestTrue(
			TEXT("Weapon actor class is the row WeaponActorClass"),
			Weapon->GetClass() == AShooterInventoryOrderTestWeapon::StaticClass());
		TestEqual(TEXT("Weapon actor bound to instance"), Weapon->GetBoundInstanceId(), GrantedId);
		TestEqual(TEXT("Weapon actor carries the granted row name"), Weapon->GetWeaponRowName(), RowName);
		// 行配置在绑定时应用到 WeaponActor：弹匣容量镜像必须等于行值。
		TestEqual(TEXT("Weapon actor magazine capacity mirrors the row"), Weapon->GetMagazineSize(), 12);
		TestTrue(TEXT("Weapon actor starts hidden"), Weapon->IsHidden());
	}

	DestroyWeaponRowGrantTestWorld(World);
	return true;
}

/** 武器模板行授予失败路径：空行名、缺失行、非法配置、Slot 满与重复行。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryWeaponRowGrantRejectionTest,
	"ShootGame.Inventory.WeaponRowGrant.Rejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryWeaponRowGrantRejectionTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryWeaponRowGrantAutomationTests;

	UWorld* World = CreateWeaponRowGrantTestWorld();
	if (!TestNotNull(TEXT("Weapon row rejection world created"), World))
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

	FGuid OutId;
	EShooterInventoryAddResult Result = EShooterInventoryAddResult::Added;

	// 空行名与缺失行都在解析入口 fail closed。
	TestEqual(
		TEXT("Empty row name is rejected"),
		static_cast<int32>(Inventory->TryAddWeaponRow(NAME_None, OutId)),
		static_cast<int32>(EShooterInventoryAddResult::InvalidWeaponRow));

	UDataTable* TestTable = GetOrCreateTestWeaponTable(Inventory);
	if (!TestNotNull(TEXT("Test weapon table injected"), TestTable))
	{
		DestroyWeaponRowGrantTestWorld(World);
		return false;
	}

	TestEqual(
		TEXT("Missing row is rejected"),
		static_cast<int32>(Inventory->TryAddWeaponRow(FName(TEXT("MissingWeaponRow")), OutId)),
		static_cast<int32>(EShooterInventoryAddResult::InvalidWeaponRow));

	// 非法弹匣容量：行存在但配置非法，授予必须 fail closed。
	AddAndGrantWeaponRow(
		Inventory,
		MakeTestWeaponRow(AShooterInventoryOrderTestWeapon::StaticClass(), /*MagazineSize*/ 0),
		OutId,
		Result);
	TestEqual(
		TEXT("Illegal magazine config is rejected"),
		static_cast<int32>(Result),
		static_cast<int32>(EShooterInventoryAddResult::InvalidWeaponRow));
	TestEqual(TEXT("Rejected grant leaves inventory empty"), Inventory->GetWeaponCount(), 0);

	// 占满 3 个 Slot 后第 4 个不同行被 SlotFull 拒绝。
	const TCHAR* SlotRowNames[3] = {
		TEXT("SlotRowOne"),
		TEXT("SlotRowTwo"),
		TEXT("SlotRowThree")};
	int32 GrantedCount = 0;
	for (int32 SlotIndex = 0; SlotIndex < 3; ++SlotIndex)
	{
		TestTable->AddRow(
			FName(SlotRowNames[SlotIndex]),
			MakeTestWeaponRow(AShooterInventoryOrderTestWeapon::StaticClass(), 10, 10));
		GrantedCount += Inventory->TryAddWeaponRow(FName(SlotRowNames[SlotIndex]), OutId)
			== EShooterInventoryAddResult::Added;
	}
	TestEqual(TEXT("Three distinct rows fill all slots"), GrantedCount, 3);

	TestTable->AddRow(
		FName(TEXT("SlotRowFour")),
		MakeTestWeaponRow(AShooterInventoryOrderTestWeapon::StaticClass(), 10, 10));
	TestEqual(
		TEXT("Fourth row is rejected by SlotFull"),
		static_cast<int32>(Inventory->TryAddWeaponRow(FName(TEXT("SlotRowFour")), OutId)),
		static_cast<int32>(EShooterInventoryAddResult::SlotFull));
	TestEqual(TEXT("SlotFull leaves inventory at three"), Inventory->GetWeaponCount(), 3);

	// 重复武器类型：与首个槽位同名的行被拒绝。
	TestEqual(
		TEXT("Duplicate weapon row is rejected"),
		static_cast<int32>(Inventory->TryAddWeaponRow(FName(SlotRowNames[0]), OutId)),
		static_cast<int32>(EShooterInventoryAddResult::DuplicateWeaponRow));

	DestroyWeaponRowGrantTestWorld(World);
	return true;
}

/**
 * 换弹容量来自武器模板行：测试行刻意使用弹匣 7 / 备弹 21，
 * 与 WeaponActor 默认配置（10）不同；消耗两发后换弹只补到 7，
 * 证明容量边界由行而非 WeaponActor 默认值决定。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryWeaponRowReloadCapacityTest,
	"ShootGame.Inventory.WeaponRowGrant.ReloadCapacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryWeaponRowReloadCapacityTest::RunTest(const FString& Parameters)
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
	TestNotEqual(
		TEXT("Row magazine size differs from the weapon actor default"),
		7,
		AShooterInventoryOrderTestWeapon::StaticClass()
			->GetDefaultObject<AShooterWeapon>()
			->GetMagazineSize());

	FGuid GrantedId;
	EShooterInventoryAddResult Result = EShooterInventoryAddResult::NotAuthoritative;
	GrantTestWeaponRow(
		Inventory,
		AShooterInventoryOrderTestWeapon::StaticClass(),
		GrantedId,
		/*MagazineSize*/ 7,
		/*InitialReserveAmmo*/ 21,
		&Result);
	if (!TestEqual(
		TEXT("Reload capacity row grant succeeds"),
		static_cast<int32>(Result),
		static_cast<int32>(EShooterInventoryAddResult::Added)))
	{
		DestroyWeaponRowGrantTestWorld(World);
		return false;
	}

	// 装备并消耗两发：7 -> 5。
	TestTrue(TEXT("Granted weapon equips"), Character->GetEquipmentComponent()->EquipWeapon(GrantedId));
	TestTrue(TEXT("Two rounds consumed"), Inventory->ConsumeMagazineAmmo(GrantedId, 2));
	TestEqual(TEXT("Magazine drops to five"), Inventory->GetMagazineAmmo(GrantedId), 5);

	int32 Transferred = 0;
	TestTrue(TEXT("Reload transaction commits"), Inventory->ReloadMagazine(GrantedId, Transferred));
	TestEqual(TEXT("Reload transfers exactly to the row capacity"), Transferred, 2);
	TestEqual(TEXT("Magazine refills to the row capacity"), Inventory->GetMagazineAmmo(GrantedId), 7);
	TestEqual(TEXT("Reserve decreases by transfer"), Inventory->GetReserveAmmo(GrantedId), 19);

	DestroyWeaponRowGrantTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
