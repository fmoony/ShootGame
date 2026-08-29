// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "UObject/UnrealType.h"
#include "Weapons/ShooterWeapon.h"
#include "ShooterWeaponPresentationTestTypes.h"

namespace ShooterWeaponPresentationIdempotenceAutomationTests
{
	UWorld* CreateIdempotenceTestWorld()
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

	void DestroyIdempotenceTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	AShooterWeaponPresentationTestCharacter* SpawnIdempotenceTestCharacter(
		FAutomationTestBase& Test,
		UWorld* World)
	{
		AShooterWeaponPresentationTestCharacter* Character =
			World->SpawnActor<AShooterWeaponPresentationTestCharacter>(
				FVector::ZeroVector,
				FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("Idempotence test character spawned"), Character))
		{
			return nullptr;
		}

		if (AWorldSettings* WorldSettings = World->GetWorldSettings())
		{
			WorldSettings->NotifyBeginPlay();
		}
		return Character;
	}

	AShooterWeapon* GrantIdempotenceTestWeapon(
		FAutomationTestBase& Test,
		AShooterWeaponPresentationTestCharacter* Character,
		TSubclassOf<AShooterWeapon> WeaponClass,
		FGuid& OutInstanceId)
	{
		UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
		if (!Test.TestNotNull(TEXT("Idempotence test character owns Inventory"), Inventory))
		{
			return nullptr;
		}

		const EShooterInventoryAddResult AddResult = Inventory->TryAddWeapon(WeaponClass, OutInstanceId);
		if (!Test.TestEqual(
			TEXT("Idempotence test weapon is granted"),
			static_cast<int32>(AddResult),
			static_cast<int32>(EShooterInventoryAddResult::Added)))
		{
			return nullptr;
		}

		AShooterWeapon* Weapon = Inventory->FindWeaponActor(OutInstanceId);
		if (!Test.TestNotNull(TEXT("Granted idempotence weapon actor exists"), Weapon))
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
 * E3 验证：重复 Ensure 不重复 Activate / HUD / 表现事件；
 * 错误输入自动收敛到 Equipment.CurrentWeaponActor。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponPresentationIdempotenceTest,
	"ShootGame.Equipment.Presentation.IdempotentEnsure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponPresentationIdempotenceTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponPresentationIdempotenceAutomationTests;

	UWorld* World = CreateIdempotenceTestWorld();
	if (!TestNotNull(TEXT("Idempotence test world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnIdempotenceTestCharacter(*this, World);
	if (!Character)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}

	UShooterWeaponPresentationEventTestListener* PresentationListener =
		NewObject<UShooterWeaponPresentationEventTestListener>();
	UShooterBulletCountEventTestListener* HudListener = NewObject<UShooterBulletCountEventTestListener>();
	if (!TestNotNull(TEXT("Presentation listener created"), PresentationListener) ||
		!TestNotNull(TEXT("HUD listener created"), HudListener))
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}
	Character->OnWeaponPresentationChanged.AddDynamic(
		PresentationListener,
		&UShooterWeaponPresentationEventTestListener::HandleWeaponPresentationChanged);
	Character->OnBulletCountUpdated.AddDynamic(
		HudListener,
		&UShooterBulletCountEventTestListener::HandleBulletCountUpdated);

	FGuid PrimaryId;
	AShooterWeapon* PrimaryWeapon = GrantIdempotenceTestWeapon(
		*this,
		Character,
		AShooterWeaponPresentationTestWeaponPrimary::StaticClass(),
		PrimaryId);
	if (!PrimaryWeapon)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}

	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	TestTrue(TEXT("Primary weapon is equipped"), Equipment->EquipWeapon(PrimaryId));
	TestEqual(TEXT("First equip publishes one presentation completion"), PresentationListener->EventCount, 1);
	TestNull(TEXT("First presentation Previous is null"), PresentationListener->LastPreviousWeapon.Get());
	TestTrue(TEXT("First presentation Current is primary"), PresentationListener->LastCurrentWeapon == PrimaryWeapon);
	TestEqual(TEXT("First equip updates HUD exactly once"), HudListener->EventCount, 1);

	// 完整后置条件 + 相同对象：任何重复调用都直接成功且零副作用。
	TestTrue(TEXT("Repeated Ensure succeeds"), Character->EnsureWeaponPresentation(PrimaryWeapon));
	TestEqual(TEXT("Repeated Ensure does not repeat presentation event"), PresentationListener->EventCount, 1);
	TestEqual(TEXT("Repeated Ensure does not repeat HUD"), HudListener->EventCount, 1);

	// 传入值不等于 Equipment 当前值时以 Equipment 为准。
	TestTrue(TEXT("Ensure with wrong expected converges to logical weapon"), Character->EnsureWeaponPresentation(nullptr));
	TestEqual(TEXT("Wrong expected does not repeat presentation event"), PresentationListener->EventCount, 1);
	TestEqual(TEXT("Wrong expected does not repeat HUD"), HudListener->EventCount, 1);

	DestroyIdempotenceTestWorld(World);
	return true;
}

/**
 * E3 验证：相同 Weapon 但附着被破坏时修复；不重复 Activate / HUD；
 * 从“未完成”转“完成”只发布一次表现事件。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponPresentationRepairAttachTest,
	"ShootGame.Equipment.Presentation.RepairAttachWithoutReactivate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponPresentationRepairAttachTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponPresentationIdempotenceAutomationTests;

	UWorld* World = CreateIdempotenceTestWorld();
	if (!TestNotNull(TEXT("Repair test world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnIdempotenceTestCharacter(*this, World);
	if (!Character)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}

	UShooterWeaponPresentationEventTestListener* PresentationListener =
		NewObject<UShooterWeaponPresentationEventTestListener>();
	UShooterBulletCountEventTestListener* HudListener = NewObject<UShooterBulletCountEventTestListener>();
	if (!PresentationListener || !HudListener)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}
	Character->OnWeaponPresentationChanged.AddDynamic(
		PresentationListener,
		&UShooterWeaponPresentationEventTestListener::HandleWeaponPresentationChanged);
	Character->OnBulletCountUpdated.AddDynamic(
		HudListener,
		&UShooterBulletCountEventTestListener::HandleBulletCountUpdated);

	FGuid PrimaryId;
	AShooterWeapon* PrimaryWeapon = GrantIdempotenceTestWeapon(
		*this,
		Character,
		AShooterWeaponPresentationTestWeaponPrimary::StaticClass(),
		PrimaryId);
	if (!PrimaryWeapon)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}

	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	TestTrue(TEXT("Primary weapon is equipped"), Equipment->EquipWeapon(PrimaryId));
	TestEqual(TEXT("Equip publishes one presentation completion"), PresentationListener->EventCount, 1);
	TestEqual(TEXT("Equip updates HUD once"), HudListener->EventCount, 1);

	// 破坏第三人称附着：相同武器但后置条件不再成立。
	PrimaryWeapon->GetThirdPersonMesh()->DetachFromComponent(
		FDetachmentTransformRules::KeepWorldTransform);
	TestTrue(
		TEXT("Broken attach makes presentation incomplete"),
		PrimaryWeapon->GetThirdPersonMesh()->GetAttachParent() != Character->GetMesh());

	TestTrue(TEXT("Ensure repairs broken attach"), Character->EnsureWeaponPresentation(PrimaryWeapon));
	TestTrue(
		TEXT("Ensure restores third-person attach socket"),
		PrimaryWeapon->GetThirdPersonMesh()->GetAttachParent() == Character->GetMesh() &&
		PrimaryWeapon->GetThirdPersonMesh()->GetAttachSocketName() == FName(TEXT("HandGrip_R")));
	TestEqual(TEXT("Repair publishes one incomplete->complete event"), PresentationListener->EventCount, 2);
	TestEqual(TEXT("Repair does not reactivate or update HUD"), HudListener->EventCount, 1);

	TestTrue(TEXT("Repeated Ensure after repair is side-effect free"), Character->EnsureWeaponPresentation(PrimaryWeapon));
	TestEqual(TEXT("Repeated Ensure after repair keeps event count"), PresentationListener->EventCount, 2);
	TestEqual(TEXT("Repeated Ensure after repair keeps HUD count"), HudListener->EventCount, 1);

	DestroyIdempotenceTestWorld(World);
	return true;
}

/**
 * E3 验证：AnimClass 被破坏时只补 AnimClass，不重复 HUD；清空后保留上一 AnimClass；
 * LastAppliedPresentationWeapon 无公开属性 / Getter / Gameplay 读取方。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponPresentationAnimClassRepairAndPrivacyTest,
	"ShootGame.Equipment.Presentation.AnimClassRepairAndPrivacy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponPresentationAnimClassRepairAndPrivacyTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponPresentationIdempotenceAutomationTests;

	// 私有表现缓存没有任何反射属性、公开 Getter 或第二套公开状态。
	TestNull(
		TEXT("LastAppliedPresentationWeapon has no replicated/public property"),
		FindFProperty<FProperty>(AShooterCharacter::StaticClass(), TEXT("LastAppliedPresentationWeapon")));
	TestNull(
		TEXT("No PresentedWeapon second state is exposed"),
		FindFProperty<FProperty>(AShooterCharacter::StaticClass(), TEXT("PresentedWeapon")));
	TestNull(
		TEXT("No LastAppliedPresentationWeapon getter is exposed"),
		AShooterCharacter::StaticClass()->FindFunctionByName(TEXT("GetLastAppliedPresentationWeapon")));
	TestNull(
		TEXT("No PresentedWeapon getter is exposed"),
		AShooterCharacter::StaticClass()->FindFunctionByName(TEXT("GetPresentedWeapon")));

	UWorld* World = CreateIdempotenceTestWorld();
	if (!TestNotNull(TEXT("AnimClass repair test world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnIdempotenceTestCharacter(*this, World);
	if (!Character)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}

	UShooterWeaponPresentationEventTestListener* PresentationListener =
		NewObject<UShooterWeaponPresentationEventTestListener>();
	UShooterBulletCountEventTestListener* HudListener = NewObject<UShooterBulletCountEventTestListener>();
	if (!PresentationListener || !HudListener)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}
	Character->OnWeaponPresentationChanged.AddDynamic(
		PresentationListener,
		&UShooterWeaponPresentationEventTestListener::HandleWeaponPresentationChanged);
	Character->OnBulletCountUpdated.AddDynamic(
		HudListener,
		&UShooterBulletCountEventTestListener::HandleBulletCountUpdated);

	FGuid PrimaryId;
	AShooterWeapon* PrimaryWeapon = GrantIdempotenceTestWeapon(
		*this,
		Character,
		AShooterWeaponPresentationTestWeaponPrimary::StaticClass(),
		PrimaryId);
	if (!PrimaryWeapon)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}

	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	TestTrue(TEXT("Primary weapon is equipped"), Equipment->EquipWeapon(PrimaryId));
	TestEqual(TEXT("Equip publishes one presentation completion"), PresentationListener->EventCount, 1);
	TestEqual(TEXT("Equip updates HUD once"), HudListener->EventCount, 1);

	// 破坏 TP AnimClass：只补 AnimClass，不重复 HUD / Activate。
	Character->GetMesh()->SetAnimInstanceClass(nullptr);
	TestTrue(TEXT("Ensure repairs broken AnimClass"), Character->EnsureWeaponPresentation(PrimaryWeapon));
	TestTrue(
		TEXT("TP AnimClass restored to weapon config"),
		Character->GetMesh()->GetAnimClass() == PrimaryWeapon->GetThirdPersonAnimInstanceClass().Get());
	TestEqual(TEXT("AnimClass repair publishes incomplete->complete event"), PresentationListener->EventCount, 2);
	TestEqual(TEXT("AnimClass repair does not repeat HUD"), HudListener->EventCount, 1);

	const UClass* PreviousFPAnimClass = Character->GetFirstPersonMesh()->GetAnimClass();
	const UClass* PreviousTPAnimClass = Character->GetMesh()->GetAnimClass();

	// Unequip：本地表现缓存清空、发布 (Previous, nullptr)，AnimClass 保留。
	Equipment->ClearEquippedWeapon();
	TestEqual(TEXT("Unequip publishes one presentation event"), PresentationListener->EventCount, 3);
	TestTrue(TEXT("Unequip Previous is primary"), PresentationListener->LastPreviousWeapon == PrimaryWeapon);
	TestNull(TEXT("Unequip Current is null"), PresentationListener->LastCurrentWeapon.Get());
	TestTrue(TEXT("Unequip hides the previous weapon"), PrimaryWeapon->IsHidden());
	TestTrue(
		TEXT("Unequip keeps previous FP AnimClass in this plan"),
		Character->GetFirstPersonMesh()->GetAnimClass() == PreviousFPAnimClass);
	TestTrue(
		TEXT("Unequip keeps previous TP AnimClass in this plan"),
		Character->GetMesh()->GetAnimClass() == PreviousTPAnimClass);

	TestTrue(TEXT("Repeated empty Ensure succeeds"), Character->EnsureWeaponPresentation(nullptr));
	TestEqual(TEXT("Repeated empty Ensure does not repeat presentation event"), PresentationListener->EventCount, 3);

	DestroyIdempotenceTestWorld(World);
	return true;
}

/**
 * 收尾回归：已应用 Weapon 在 Equipment 清空前先失效时，Character 仍发布一次空表现事件。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponPresentationStaleWeaponClearTest,
	"ShootGame.Equipment.Presentation.StaleWeaponClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponPresentationStaleWeaponClearTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponPresentationIdempotenceAutomationTests;

	UWorld* World = CreateIdempotenceTestWorld();
	if (!TestNotNull(TEXT("Stale weapon test world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnIdempotenceTestCharacter(*this, World);
	UShooterWeaponPresentationEventTestListener* Listener =
		NewObject<UShooterWeaponPresentationEventTestListener>();
	if (!Character || !TestNotNull(TEXT("Stale weapon listener created"), Listener))
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}
	Character->OnWeaponPresentationChanged.AddDynamic(
		Listener,
		&UShooterWeaponPresentationEventTestListener::HandleWeaponPresentationChanged);

	FGuid InstanceId;
	AShooterWeapon* Weapon = GrantIdempotenceTestWeapon(
		*this,
		Character,
		AShooterWeaponPresentationTestWeaponPrimary::StaticClass(),
		InstanceId);
	if (!Weapon)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}

	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	TestTrue(TEXT("Stale test weapon is equipped"), Equipment->EquipWeapon(InstanceId));
	TestEqual(TEXT("Equip publishes one presentation event"), Listener->EventCount, 1);

	TestTrue(TEXT("Weapon is destroyed before Equipment clear"), Weapon->Destroy());
	TestFalse(TEXT("Destroyed weapon is no longer valid"), IsValid(Weapon));
	Equipment->ClearEquippedWeapon();

	TestEqual(TEXT("Stale previous weapon still publishes one clear event"), Listener->EventCount, 2);
	TestNull(TEXT("Stale clear cannot expose a destroyed Previous weapon"), Listener->LastPreviousWeapon.Get());
	TestNull(TEXT("Stale clear Current weapon is null"), Listener->LastCurrentWeapon.Get());

	DestroyIdempotenceTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
