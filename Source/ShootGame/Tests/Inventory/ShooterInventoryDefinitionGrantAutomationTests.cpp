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
#include "Weapons/Definitions/ShooterWeaponDefinition.h"
#include "Weapons/ShooterWeapon.h"
#include "../Equipment/ShooterWeaponPresentationTestTypes.h"

namespace ShooterInventoryDefinitionGrantAutomationTests
{
	UWorld* CreateDefinitionGrantTestWorld()
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

	void DestroyDefinitionGrantTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	AShooterWeaponPresentationTestCharacter* SpawnDefinitionGrantTestCharacter(
		FAutomationTestBase& Test,
		UWorld* World)
	{
		AShooterWeaponPresentationTestCharacter* Character =
			World->SpawnActor<AShooterWeaponPresentationTestCharacter>(
				FVector::ZeroVector,
				FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("Definition grant test character spawned"), Character))
		{
			return nullptr;
		}

		if (AWorldSettings* WorldSettings = World->GetWorldSettings())
		{
			WorldSettings->NotifyBeginPlay();
		}
		return Character;
	}

	/** 内存 Definition：不进入 AssetManager，用于授予数据流测试。 */
	UShooterWeaponDefinition* MakeTransientDefinition(
		FAutomationTestBase& Test,
		const FName& Name,
		TSubclassOf<AShooterWeapon> WeaponActorClass,
		int32 MagazineSize,
		int32 InitialReserveAmmo)
	{
		UShooterWeaponDefinition* Definition = NewObject<UShooterWeaponDefinition>(
			GetTransientPackage(),
			Name);
		Definition->WeaponActorClass = WeaponActorClass;
		Definition->AmmoConfig.MagazineSize = MagazineSize;
		Definition->AmmoConfig.InitialReserveAmmo = InitialReserveAmmo;
		return Definition;
	}
}

/** Definition 授予主路径：实例数据与 WeaponActor 完全由 Definition 驱动。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryDefinitionGrantTest,
	"ShootGame.Inventory.DefinitionGrant.Initialize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryDefinitionGrantTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryDefinitionGrantAutomationTests;

	UWorld* World = CreateDefinitionGrantTestWorld();
	if (!TestNotNull(TEXT("Definition grant world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnDefinitionGrantTestCharacter(*this, World);
	if (!Character)
	{
		DestroyDefinitionGrantTestWorld(World);
		return false;
	}

	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	if (!TestNotNull(TEXT("Character owns inventory"), Inventory))
	{
		DestroyDefinitionGrantTestWorld(World);
		return false;
	}

	UShooterWeaponDefinition* Definition = MakeTransientDefinition(
		*this,
		TEXT("WD_DefGrantOne"),
		AShooterInventoryOrderTestWeapon::StaticClass(),
		12,
		36);
	if (!TestNotNull(TEXT("Transient definition created"), Definition))
	{
		DestroyDefinitionGrantTestWorld(World);
		return false;
	}

	FGuid GrantedId;
	const EShooterInventoryAddResult Result = Inventory->TryAddWeaponDefinition(Definition, GrantedId);
	TestEqual(
		TEXT("Definition grant succeeds"),
		static_cast<int32>(Result),
		static_cast<int32>(EShooterInventoryAddResult::Added));
	TestTrue(TEXT("Granted instance id is valid"), GrantedId.IsValid());

	const FShooterWeaponInstanceData* Instance = Inventory->FindWeaponInstance(GrantedId);
	if (TestNotNull(TEXT("Granted instance exists"), Instance))
	{
		TestEqual(
			TEXT("Instance DefinitionId comes from the definition"),
			Instance->DefinitionId,
			Definition->GetDefinitionId());
		TestEqual(TEXT("Magazine initializes from definition"), Instance->MagazineAmmo, 12);
		TestEqual(TEXT("Reserve initializes from definition"), Instance->ReserveAmmo, 36);
		TestEqual(TEXT("Slot auto-selected"), Instance->SlotIndex, 0);
	}

	AShooterWeapon* Weapon = Inventory->FindWeaponActor(GrantedId);
	if (TestNotNull(TEXT("Weapon actor spawned from definition actor class"), Weapon))
	{
		TestTrue(
			TEXT("Weapon actor class is the definition WeaponActorClass"),
			Weapon->GetClass() == Definition->WeaponActorClass);
		TestEqual(TEXT("Weapon actor bound to instance"), Weapon->GetBoundInstanceId(), GrantedId);
		TestTrue(TEXT("Weapon actor starts hidden"), Weapon->IsHidden());
	}

	DestroyDefinitionGrantTestWorld(World);
	return true;
}

/** Definition 授予失败路径：空指针、非法配置、重复定义、Slot 满。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryDefinitionGrantRejectionTest,
	"ShootGame.Inventory.DefinitionGrant.Rejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryDefinitionGrantRejectionTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryDefinitionGrantAutomationTests;

	UWorld* World = CreateDefinitionGrantTestWorld();
	if (!TestNotNull(TEXT("Definition rejection world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnDefinitionGrantTestCharacter(*this, World);
	if (!Character)
	{
		DestroyDefinitionGrantTestWorld(World);
		return false;
	}

	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	if (!TestNotNull(TEXT("Character owns inventory"), Inventory))
	{
		DestroyDefinitionGrantTestWorld(World);
		return false;
	}

	FGuid OutId;

	TestEqual(
		TEXT("Null definition is rejected"),
		static_cast<int32>(Inventory->TryAddWeaponDefinition(nullptr, OutId)),
		static_cast<int32>(EShooterInventoryAddResult::InvalidDefinition));

	UShooterWeaponDefinition* InvalidDefinition = MakeTransientDefinition(
		*this,
		TEXT("WD_DefGrantInvalid"),
		AShooterInventoryOrderTestWeapon::StaticClass(),
		0,
		-1);
	TestEqual(
		TEXT("Illegal magazine config is rejected"),
		static_cast<int32>(Inventory->TryAddWeaponDefinition(InvalidDefinition, OutId)),
		static_cast<int32>(EShooterInventoryAddResult::InvalidDefinition));
	TestEqual(TEXT("Rejected grant leaves inventory empty"), Inventory->GetWeaponCount(), 0);

	// 占满 3 个 Slot 后第 4 个不同 Definition 被 SlotFull 拒绝。
	const FName Names[3] = {
		TEXT("WD_DefGrantSlotOne"),
		TEXT("WD_DefGrantSlotTwo"),
		TEXT("WD_DefGrantSlotThree")};
	int32 GrantedCount = 0;
	for (const FName& Name : Names)
	{
		UShooterWeaponDefinition* Definition = MakeTransientDefinition(
			*this,
			Name,
			AShooterInventoryOrderTestWeapon::StaticClass(),
			10,
			10);
		GrantedCount += Inventory->TryAddWeaponDefinition(Definition, OutId)
			== EShooterInventoryAddResult::Added;
	}
	TestEqual(TEXT("Three distinct definitions fill all slots"), GrantedCount, 3);

	UShooterWeaponDefinition* FourthDefinition = MakeTransientDefinition(
		*this,
		TEXT("WD_DefGrantSlotFour"),
		AShooterInventoryOrderTestWeapon::StaticClass(),
		10,
		10);
	TestEqual(
		TEXT("Fourth definition is rejected by SlotFull"),
		static_cast<int32>(Inventory->TryAddWeaponDefinition(FourthDefinition, OutId)),
		static_cast<int32>(EShooterInventoryAddResult::SlotFull));
	TestEqual(TEXT("SlotFull leaves inventory at three"), Inventory->GetWeaponCount(), 3);

	// 重复 Definition：与首个槽位同 ID 的 Definition 被拒绝。
	UShooterWeaponDefinition* DuplicateDefinition = MakeTransientDefinition(
		*this,
		Names[0],
		AShooterInventoryOrderTestWeapon::StaticClass(),
		10,
		10);
	TestEqual(
		TEXT("Duplicate definition is rejected"),
		static_cast<int32>(Inventory->TryAddWeaponDefinition(DuplicateDefinition, OutId)),
		static_cast<int32>(EShooterInventoryAddResult::DuplicateDefinition));

	DestroyDefinitionGrantTestWorld(World);
	return true;
}

/**
 * 换弹容量来自 Definition：WD_TestAuto（弹匣 7 / 备弹 21）与 Rifle CDO 配置刻意不同，
 * 消耗两发后换弹只补到 7，证明容量边界由 Definition 而非 WeaponActor CDO 决定。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryDefinitionReloadCapacityTest,
	"ShootGame.Inventory.DefinitionGrant.ReloadCapacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryDefinitionReloadCapacityTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryDefinitionGrantAutomationTests;

	const FPrimaryAssetId TestAutoId(
		UShooterWeaponDefinition::GetWeaponDefinitionAssetType(),
		FName(TEXT("WD_TestAuto")));
	UShooterWeaponDefinition* TestAuto =
		UShooterWeaponDefinition::ResolveDefinitionSync(TestAutoId);
	if (!TestNotNull(
		TEXT("WD_TestAuto resolves (run ShootGame.Tools.WeaponDefinition.CreateTestAsset first)"),
		TestAuto))
	{
		return false;
	}

	UWorld* World = CreateDefinitionGrantTestWorld();
	if (!TestNotNull(TEXT("Reload capacity world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnDefinitionGrantTestCharacter(*this, World);
	if (!Character)
	{
		DestroyDefinitionGrantTestWorld(World);
		return false;
	}

	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	if (!TestNotNull(TEXT("Character owns inventory"), Inventory))
	{
		DestroyDefinitionGrantTestWorld(World);
		return false;
	}

	FGuid GrantedId;
	const EShooterInventoryAddResult Result = Inventory->TryAddWeaponDefinition(TestAuto, GrantedId);
	if (!TestEqual(
		TEXT("WD_TestAuto grant succeeds"),
		static_cast<int32>(Result),
		static_cast<int32>(EShooterInventoryAddResult::Added)))
	{
		DestroyDefinitionGrantTestWorld(World);
		return false;
	}

	// 装备并消耗两发：7 -> 5。
	TestTrue(TEXT("Granted weapon equips"), Character->GetEquipmentComponent()->EquipWeapon(GrantedId));
	TestTrue(TEXT("Two rounds consumed"), Inventory->ConsumeMagazineAmmo(GrantedId, 2));
	TestEqual(TEXT("Magazine drops to five"), Inventory->GetMagazineAmmo(GrantedId), 5);

	int32 Transferred = 0;
	TestTrue(TEXT("Reload transaction commits"), Inventory->ReloadMagazine(GrantedId, Transferred));
	TestEqual(TEXT("Reload transfers exactly to definition capacity"), Transferred, 2);
	TestEqual(TEXT("Magazine refills to definition capacity"), Inventory->GetMagazineAmmo(GrantedId), 7);
	TestEqual(TEXT("Reserve decreases by transfer"), Inventory->GetReserveAmmo(GrantedId), 19);

	DestroyDefinitionGrantTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
