// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "UObject/UnrealType.h"
#include "Weapons/ShooterWeapon.h"
#include "ShooterWeaponPresentationTestTypes.h"

namespace ShooterEquipmentLogicalEventAutomationTests
{
	UWorld* CreateEquipmentEventTestWorld()
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

	void DestroyEquipmentEventTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	AShooterWeaponPresentationTestCharacter* SpawnEquipmentEventTestCharacter(
		FAutomationTestBase& Test,
		UWorld* World)
	{
		AShooterWeaponPresentationTestCharacter* Character =
			World->SpawnActor<AShooterWeaponPresentationTestCharacter>(
				FVector::ZeroVector,
				FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("Equipment event test character spawned"), Character))
		{
			return nullptr;
		}

		if (AWorldSettings* WorldSettings = World->GetWorldSettings())
		{
			WorldSettings->NotifyBeginPlay();
		}
		return Character;
	}

	AShooterWeapon* GrantEquipmentEventTestWeapon(
		FAutomationTestBase& Test,
		AShooterWeaponPresentationTestCharacter* Character,
		TSubclassOf<AShooterWeapon> WeaponClass,
		FGuid& OutInstanceId)
	{
		UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
		if (!Test.TestNotNull(TEXT("Equipment event test character owns Inventory"), Inventory))
		{
			return nullptr;
		}

		const EShooterInventoryAddResult AddResult = Inventory->TryAddWeapon(WeaponClass, OutInstanceId);
		if (!Test.TestEqual(
			TEXT("Equipment event test weapon is granted"),
			static_cast<int32>(AddResult),
			static_cast<int32>(EShooterInventoryAddResult::Added)))
		{
			return nullptr;
		}

		AShooterWeapon* Weapon = Inventory->FindWeaponActor(OutInstanceId);
		if (!Test.TestNotNull(TEXT("Granted equipment event weapon actor exists"), Weapon))
		{
			return nullptr;
		}

		if (!Weapon->HasActorBegunPlay())
		{
			Weapon->DispatchBeginPlay();
		}
		return Weapon;
	}
}

/**
 * E2 验证：OnEquippedWeaponChanged 只在 CurrentWeaponActor 真实转移时广播，
 * 重复装备与重复 Clear 不产生第二次逻辑变化。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterEquipmentLogicalEventSemanticsTest,
	"ShootGame.Equipment.Event.LogicalChangeSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterEquipmentLogicalEventSemanticsTest::RunTest(const FString& Parameters)
{
	using namespace ShooterEquipmentLogicalEventAutomationTests;

	UWorld* World = CreateEquipmentEventTestWorld();
	if (!TestNotNull(TEXT("Equipment event test world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnEquipmentEventTestCharacter(*this, World);
	if (!Character)
	{
		DestroyEquipmentEventTestWorld(World);
		return false;
	}

	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	UShooterEquipmentEventTestListener* Listener = NewObject<UShooterEquipmentEventTestListener>();
	if (!TestNotNull(TEXT("Equipment listener created"), Listener))
	{
		DestroyEquipmentEventTestWorld(World);
		return false;
	}
	Equipment->OnEquippedWeaponChanged.AddDynamic(
		Listener,
		&UShooterEquipmentEventTestListener::HandleEquippedWeaponChanged);

	FGuid PrimaryId;
	AShooterWeapon* PrimaryWeapon = GrantEquipmentEventTestWeapon(
		*this,
		Character,
		AShooterWeaponPresentationTestWeaponPrimary::StaticClass(),
		PrimaryId);
	if (!PrimaryWeapon)
	{
		DestroyEquipmentEventTestWorld(World);
		return false;
	}

	TestTrue(TEXT("Primary weapon is equipped"), Equipment->EquipWeapon(PrimaryId));
	TestEqual(TEXT("First equip publishes exactly one logical change"), Listener->EventCount, 1);
	TestNull(TEXT("First equip PreviousWeapon is null"), Listener->LastPreviousWeapon.Get());
	TestTrue(TEXT("First equip CurrentWeapon is primary"), Listener->LastCurrentWeapon == PrimaryWeapon);

	// 同一武器重复提交：不是真实转移，不发布第二次逻辑变化。
	TestTrue(TEXT("Re-equipping the same weapon succeeds"), Equipment->EquipWeapon(PrimaryId));
	TestEqual(TEXT("Re-equip does not publish a second logical change"), Listener->EventCount, 1);

	FGuid SecondaryId;
	AShooterWeapon* SecondaryWeapon = GrantEquipmentEventTestWeapon(
		*this,
		Character,
		AShooterWeaponPresentationTestWeaponSecondary::StaticClass(),
		SecondaryId);
	if (!SecondaryWeapon)
	{
		DestroyEquipmentEventTestWorld(World);
		return false;
	}

	TestTrue(TEXT("Secondary weapon is equipped"), Equipment->EquipWeapon(SecondaryId));
	TestEqual(TEXT("Switch publishes exactly one logical change"), Listener->EventCount, 2);
	TestTrue(TEXT("Switch PreviousWeapon is primary"), Listener->LastPreviousWeapon == PrimaryWeapon);
	TestTrue(TEXT("Switch CurrentWeapon is secondary"), Listener->LastCurrentWeapon == SecondaryWeapon);

	Equipment->ClearEquippedWeapon();
	TestEqual(TEXT("Unequip publishes exactly one logical change"), Listener->EventCount, 3);
	TestTrue(TEXT("Unequip PreviousWeapon is secondary"), Listener->LastPreviousWeapon == SecondaryWeapon);
	TestNull(TEXT("Unequip CurrentWeapon is null"), Listener->LastCurrentWeapon.Get());

	Equipment->ClearEquippedWeapon();
	TestEqual(TEXT("Repeated unequip does not publish a second logical change"), Listener->EventCount, 3);
	TestEqual(TEXT("Inventory still holds both owned weapons"), Inventory->GetWeaponCount(), 2);

	DestroyEquipmentEventTestWorld(World);
	return true;
}

/**
 * E2 验证：ActiveWeaponInstanceId OnRep 不再触发表现应用或逻辑事件；
 * 当前 WeaponActor 保持隐藏，直到 CurrentWeaponActor OnRep / Ready 补偿。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterEquipmentActiveInstanceIdOnRepTest,
	"ShootGame.Equipment.OnRep.ActiveInstanceIdNoApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterEquipmentActiveInstanceIdOnRepTest::RunTest(const FString& Parameters)
{
	using namespace ShooterEquipmentLogicalEventAutomationTests;

	UWorld* World = CreateEquipmentEventTestWorld();
	if (!TestNotNull(TEXT("Equipment OnRep test world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnEquipmentEventTestCharacter(*this, World);
	if (!Character)
	{
		DestroyEquipmentEventTestWorld(World);
		return false;
	}

	FGuid InstanceId;
	AShooterWeapon* Weapon = GrantEquipmentEventTestWeapon(
		*this,
		Character,
		AShooterWeaponPresentationTestWeaponPrimary::StaticClass(),
		InstanceId);
	if (!Weapon)
	{
		DestroyEquipmentEventTestWorld(World);
		return false;
	}

	UShooterEquipmentTestHarnessComponent* Harness =
		NewObject<UShooterEquipmentTestHarnessComponent>(Character);
	UShooterEquipmentEventTestListener* Listener = NewObject<UShooterEquipmentEventTestListener>();
	if (!TestNotNull(TEXT("Equipment harness created"), Harness) ||
		!TestNotNull(TEXT("Equipment OnRep listener created"), Listener))
	{
		DestroyEquipmentEventTestWorld(World);
		return false;
	}
	Harness->OnEquippedWeaponChanged.AddDynamic(
		Listener,
		&UShooterEquipmentEventTestListener::HandleEquippedWeaponChanged);

	TestTrue(TEXT("Granted weapon starts hidden"), Weapon->IsHidden());
	Harness->SetCurrentWeaponActorForTest(Weapon);
	Harness->SetActiveWeaponInstanceIdForTest(InstanceId);
	Harness->CallOnRepActiveWeaponInstanceIdForTest();

	TestTrue(TEXT("ActiveInstanceId OnRep keeps weapon hidden"), Weapon->IsHidden());
	TestEqual(TEXT("ActiveInstanceId OnRep publishes no logical event"), Listener->EventCount, 0);

	DestroyEquipmentEventTestWorld(World);
	return true;
}

/**
 * E2 验证：CurrentWeaponActor OnRep 按 Previous/Current 语义发布逻辑事件，
 * 相同 Previous/Current 不发布；Apply 仍能补做表现。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterEquipmentCurrentWeaponOnRepSemanticsTest,
	"ShootGame.Equipment.OnRep.CurrentWeaponSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterEquipmentCurrentWeaponOnRepSemanticsTest::RunTest(const FString& Parameters)
{
	using namespace ShooterEquipmentLogicalEventAutomationTests;

	UWorld* World = CreateEquipmentEventTestWorld();
	if (!TestNotNull(TEXT("Equipment CurrentWeapon OnRep test world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnEquipmentEventTestCharacter(*this, World);
	if (!Character)
	{
		DestroyEquipmentEventTestWorld(World);
		return false;
	}

	FGuid InstanceId;
	AShooterWeapon* Weapon = GrantEquipmentEventTestWeapon(
		*this,
		Character,
		AShooterWeaponPresentationTestWeaponPrimary::StaticClass(),
		InstanceId);
	if (!Weapon)
	{
		DestroyEquipmentEventTestWorld(World);
		return false;
	}

	// E3 起 OnRep 会进入 Character::EnsureWeaponPresentation，该入口只认角色真实 EquipmentComponent；
	// 这里直接驱动角色自带 Equipment 的 OnRep（UFUNCTION 受保护，测试用 ProcessEvent 调用）。
	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	UShooterEquipmentEventTestListener* Listener = NewObject<UShooterEquipmentEventTestListener>();
	if (!TestNotNull(TEXT("Character Equipment created"), Equipment) ||
		!TestNotNull(TEXT("Equipment CurrentWeapon listener created"), Listener))
	{
		DestroyEquipmentEventTestWorld(World);
		return false;
	}
	Equipment->OnEquippedWeaponChanged.AddDynamic(
		Listener,
		&UShooterEquipmentEventTestListener::HandleEquippedWeaponChanged);

	UFunction* OnRepCurrentWeaponFunction = UShooterEquipmentComponent::StaticClass()->FindFunctionByName(
		TEXT("OnRep_CurrentWeaponActor"));
	FObjectProperty* CurrentWeaponProperty = FindFProperty<FObjectProperty>(
		UShooterEquipmentComponent::StaticClass(),
		TEXT("CurrentWeaponActor"));
	if (!TestNotNull(TEXT("OnRep_CurrentWeaponActor function exists"), OnRepCurrentWeaponFunction) ||
		!TestNotNull(TEXT("CurrentWeaponActor property exists"), CurrentWeaponProperty))
	{
		DestroyEquipmentEventTestWorld(World);
		return false;
	}

	AShooterWeapon* PreviousWeapon = nullptr;
	CurrentWeaponProperty->SetObjectPropertyValue_InContainer(Equipment, Weapon);
	Equipment->ProcessEvent(OnRepCurrentWeaponFunction, &PreviousWeapon);
	TestEqual(TEXT("CurrentWeapon null->weapon publishes once"), Listener->EventCount, 1);
	TestNull(TEXT("OnRep equip PreviousWeapon is null"), Listener->LastPreviousWeapon.Get());
	TestTrue(TEXT("OnRep equip CurrentWeapon is weapon"), Listener->LastCurrentWeapon == Weapon);
	TestFalse(TEXT("OnRep equip applies visible presentation"), Weapon->IsHidden());

	// 相同 Previous/Current 不是真实转移：不发布第二次逻辑事件。
	PreviousWeapon = Weapon;
	Equipment->ProcessEvent(OnRepCurrentWeaponFunction, &PreviousWeapon);
	TestEqual(TEXT("Same Previous/Current does not publish again"), Listener->EventCount, 1);

	// weapon -> null：Unequip 事件一次，旧武器表现被清空隐藏。
	PreviousWeapon = Weapon;
	CurrentWeaponProperty->SetObjectPropertyValue_InContainer(Equipment, nullptr);
	Equipment->ProcessEvent(OnRepCurrentWeaponFunction, &PreviousWeapon);
	TestEqual(TEXT("CurrentWeapon weapon->null publishes once"), Listener->EventCount, 2);
	TestTrue(TEXT("OnRep unequip PreviousWeapon is weapon"), Listener->LastPreviousWeapon == Weapon);
	TestNull(TEXT("OnRep unequip CurrentWeapon is null"), Listener->LastCurrentWeapon.Get());
	TestTrue(TEXT("OnRep unequip hides the previous weapon"), Weapon->IsHidden());

	DestroyEquipmentEventTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
