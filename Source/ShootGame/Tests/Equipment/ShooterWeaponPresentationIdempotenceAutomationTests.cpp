// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/ShooterTestWorld.h"
#include "AbilitySystem/Abilities/ShooterGameplayAbility_Fire.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "UObject/UnrealType.h"
#include "Tests/Weapon/ShooterWeaponTestTableTypes.h"
#include "Weapons/ShooterWeapon.h"
#include "Tests/Equipment/ShooterWeaponPresentationTestTypes.h"

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

		ShooterTestWorld::Destroy(World);
	}

	AShooterWeaponPresentationTestCharacter* SpawnIdempotenceTestCharacter(FAutomationTestBase& Test, UWorld* World)
	{
		AShooterWeaponPresentationTestCharacter* Character = World->SpawnActor<AShooterWeaponPresentationTestCharacter>(
				FVector::ZeroVector, FRotator::ZeroRotator);
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

	AShooterWeapon* GrantIdempotenceTestWeapon(FAutomationTestBase& Test,
		AShooterWeaponPresentationTestCharacter* Character, TSubclassOf<AShooterWeapon> WeaponClass)
	{
		UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
		if (!Test.TestNotNull(TEXT("Idempotence test character owns Inventory"), Inventory))
		{
			return nullptr;
		}

		// S3 授予链：运行时池 Acquire + Inventory AddWeapon。
		EShooterInventoryAddResult AddResult = EShooterInventoryAddResult::NotAuthoritative;
		AShooterWeapon* Weapon = GrantTestWeapon(Character->GetWorld(), Inventory, WeaponClass,
			/*MagazineSize*/ 10,
			/*InitialReserveAmmo*/ -1,
			&AddResult);
		if (!Test.TestEqual(TEXT("Idempotence test weapon is granted"), static_cast<int32>(AddResult),
			static_cast<int32>(EShooterInventoryAddResult::Added)))
		{
			return nullptr;
		}

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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponPresentationIdempotenceTest,
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

	UShooterWeaponPresentationEventTestListener* PresentationListener = NewObject<UShooterWeaponPresentationEventTestListener>();
	UShooterBulletCountEventTestListener* HudListener = NewObject<UShooterBulletCountEventTestListener>();
	if (!TestNotNull(TEXT("Presentation listener created"), PresentationListener) ||
		!TestNotNull(TEXT("HUD listener created"), HudListener))
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}
	Character->OnWeaponPresentationChanged.AddDynamic(PresentationListener,
		&UShooterWeaponPresentationEventTestListener::HandleWeaponPresentationChanged);
	Character->OnBulletCountUpdated.AddDynamic(HudListener,
		&UShooterBulletCountEventTestListener::HandleBulletCountUpdated);

	AShooterWeapon* PrimaryWeapon = GrantIdempotenceTestWeapon(*this, Character,
		AShooterWeaponPresentationTestWeaponPrimary::StaticClass());
	if (!PrimaryWeapon)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}

	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	TestTrue(TEXT("Primary weapon is equipped"), Equipment->EquipWeapon(PrimaryWeapon));
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponPresentationRepairAttachTest,
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

	UShooterWeaponPresentationEventTestListener* PresentationListener = NewObject<UShooterWeaponPresentationEventTestListener>();
	UShooterBulletCountEventTestListener* HudListener = NewObject<UShooterBulletCountEventTestListener>();
	if (!PresentationListener || !HudListener)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}
	Character->OnWeaponPresentationChanged.AddDynamic(PresentationListener,
		&UShooterWeaponPresentationEventTestListener::HandleWeaponPresentationChanged);
	Character->OnBulletCountUpdated.AddDynamic(HudListener,
		&UShooterBulletCountEventTestListener::HandleBulletCountUpdated);

	AShooterWeapon* PrimaryWeapon = GrantIdempotenceTestWeapon(*this, Character,
		AShooterWeaponPresentationTestWeaponPrimary::StaticClass());
	if (!PrimaryWeapon)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}

	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	TestTrue(TEXT("Primary weapon is equipped"), Equipment->EquipWeapon(PrimaryWeapon));
	TestEqual(TEXT("Equip publishes one presentation completion"), PresentationListener->EventCount, 1);
	TestEqual(TEXT("Equip updates HUD once"), HudListener->EventCount, 1);

	// 破坏第三人称附着：相同武器但后置条件不再成立。
	PrimaryWeapon->GetThirdPersonMesh()->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	TestTrue(TEXT("Broken attach makes presentation incomplete"),
		PrimaryWeapon->GetThirdPersonMesh()->GetAttachParent() != Character->GetMesh());

	TestTrue(TEXT("Ensure repairs broken attach"), Character->EnsureWeaponPresentation(PrimaryWeapon));
	TestTrue(TEXT("Ensure restores third-person attach socket"),
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponPresentationAnimClassRepairAndPrivacyTest,
	"ShootGame.Equipment.Presentation.AnimClassRepairAndPrivacy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponPresentationAnimClassRepairAndPrivacyTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponPresentationIdempotenceAutomationTests;

	// 私有表现缓存没有任何反射属性、公开 Getter 或第二套公开状态。
	TestNull(TEXT("LastAppliedPresentationWeapon has no replicated/public property"),
		FindFProperty<FProperty>(AShooterCharacter::StaticClass(), TEXT("LastAppliedPresentationWeapon")));
	TestNull(TEXT("No PresentedWeapon second state is exposed"),
		FindFProperty<FProperty>(AShooterCharacter::StaticClass(), TEXT("PresentedWeapon")));
	TestNull(TEXT("No LastAppliedPresentationWeapon getter is exposed"),
		AShooterCharacter::StaticClass()->FindFunctionByName(TEXT("GetLastAppliedPresentationWeapon")));
	TestNull(TEXT("No PresentedWeapon getter is exposed"),
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

	UShooterWeaponPresentationEventTestListener* PresentationListener = NewObject<UShooterWeaponPresentationEventTestListener>();
	UShooterBulletCountEventTestListener* HudListener = NewObject<UShooterBulletCountEventTestListener>();
	if (!PresentationListener || !HudListener)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}
	Character->OnWeaponPresentationChanged.AddDynamic(PresentationListener,
		&UShooterWeaponPresentationEventTestListener::HandleWeaponPresentationChanged);
	Character->OnBulletCountUpdated.AddDynamic(HudListener,
		&UShooterBulletCountEventTestListener::HandleBulletCountUpdated);

	AShooterWeapon* PrimaryWeapon = GrantIdempotenceTestWeapon(*this, Character,
		AShooterWeaponPresentationTestWeaponPrimary::StaticClass());
	if (!PrimaryWeapon)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}

	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	TestTrue(TEXT("Primary weapon is equipped"), Equipment->EquipWeapon(PrimaryWeapon));
	TestEqual(TEXT("Equip publishes one presentation completion"), PresentationListener->EventCount, 1);
	TestEqual(TEXT("Equip updates HUD once"), HudListener->EventCount, 1);

	// 破坏 TP AnimClass：只补 AnimClass，不重复 HUD / Activate。
	Character->GetMesh()->SetAnimInstanceClass(nullptr);
	TestTrue(TEXT("Ensure repairs broken AnimClass"), Character->EnsureWeaponPresentation(PrimaryWeapon));
	TestTrue(TEXT("TP AnimClass restored to stable character class"),
		Character->GetMesh()->GetAnimClass() == UShooterThirdPersonAnimInstance::StaticClass());
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
	TestTrue(TEXT("Unequip keeps previous FP AnimClass in this plan"),
		Character->GetFirstPersonMesh()->GetAnimClass() == PreviousFPAnimClass);
	TestTrue(TEXT("Unequip keeps previous TP AnimClass in this plan"),
		Character->GetMesh()->GetAnimClass() == PreviousTPAnimClass);

	TestTrue(TEXT("Repeated empty Ensure succeeds"), Character->EnsureWeaponPresentation(nullptr));
	TestEqual(TEXT("Repeated empty Ensure does not repeat presentation event"), PresentationListener->EventCount, 3);

	DestroyIdempotenceTestWorld(World);
	return true;
}

/**
 * 收尾回归：已应用 Weapon 在 Equipment 清空前先失效时，Character 仍发布一次空表现事件。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponPresentationStaleWeaponClearTest,
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
	UShooterWeaponPresentationEventTestListener* Listener = NewObject<UShooterWeaponPresentationEventTestListener>();
	if (!Character || !TestNotNull(TEXT("Stale weapon listener created"), Listener))
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}
	Character->OnWeaponPresentationChanged.AddDynamic(Listener,
		&UShooterWeaponPresentationEventTestListener::HandleWeaponPresentationChanged);

	AShooterWeapon* Weapon = GrantIdempotenceTestWeapon(*this, Character, AShooterWeaponPresentationTestWeaponPrimary::StaticClass());
	if (!Weapon)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}

	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	TestTrue(TEXT("Stale test weapon is equipped"), Equipment->EquipWeapon(Weapon));
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

/** 表现补齐不得激活使用状态、取消真实提交产生的通知或刷新射速间隔。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterPresentationPreservesAuthorityTest,
	"ShootGame.Equipment.Presentation.PreservesAuthorityUsage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterPresentationPreservesAuthorityTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponPresentationIdempotenceAutomationTests;
	UWorld* World = CreateIdempotenceTestWorld();
	if (!TestNotNull(TEXT("World exists"), World))
	{
		return false;
	}
	auto* Character = SpawnIdempotenceTestCharacter(*this, World);
	if (!Character)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}
	auto* Weapon = Cast<AShooterWeaponLifecycleTestWeapon>(GrantIdempotenceTestWeapon(*this, Character,
		AShooterWeaponLifecycleTestWeapon::StaticClass()));
	if (!Weapon)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}
	auto* Equipment = Character->GetEquipmentComponent();
	auto* Current = FindFProperty<FObjectProperty>(UShooterEquipmentComponent::StaticClass(), TEXT("CurrentWeaponActor"));
	if (!TestNotNull(TEXT("Logical property exists"), Current))
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}

	// 制造只有逻辑目标的窗口，确认表现补齐本身不代替服务器激活。
	Current->SetObjectPropertyValue_InContainer(Equipment, Weapon);
	TestTrue(TEXT("Presentation can converge"), Character->EnsureWeaponPresentation(Weapon));
	TestTrue(TEXT("Ensure leaves usage Holstered"),
		Weapon->GetLifecycleState() == EShooterWeaponLifecycleState::Holstered);
	Equipment->ClearEquippedWeapon();
	TestTrue(TEXT("Authority equips weapon"), Equipment->EquipWeapon(Weapon));
	TestTrue(TEXT("Actual authority commit succeeds"), Weapon->CommitSingleShot(0));
	TestTrue(TEXT("Commit scheduled a notification"), Weapon->IsRefireTimerActiveForTest());
	TestFalse(TEXT("Commit starts authority cooldown"), Weapon->CanCommitAuthorityShot());
	const float ShotTime = Weapon->GetTimeOfLastShotForAutomationTest();
	const uint32 Generation = Weapon->GetAmmoPredictionGeneration();
	const int32 Ammo = Weapon->GetBulletCount();
	auto* Ability = NewObject<UShooterGameplayAbility_Fire>();
	Weapon->BindPredictionAbility(Ability);
	Ability->RegisterShotRecordForTest(501, Weapon, false, false);
	TestEqual(TEXT("A verdict is outstanding"), Ability->GetUnresolvedShotRecordCountForTest(), 1);

	Weapon->GetThirdPersonMesh()->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		TestTrue(TEXT("Ready repairs and repeats safely"), Character->EnsureWeaponPresentation(Weapon));
	}
	TestTrue(TEXT("Repair preserves real notification"), Weapon->IsRefireTimerActiveForTest());
	TestEqual(TEXT("Repair preserves shot clock"), Weapon->GetTimeOfLastShotForAutomationTest(), ShotTime);
	TestEqual(TEXT("Repair preserves prediction lease"), Weapon->GetAmmoPredictionGeneration(), Generation);
	TestEqual(TEXT("Repair does not commit ammo"), Weapon->GetBulletCount(), Ammo);

	Equipment->ClearEquippedWeapon();
	TestFalse(TEXT("Authority clear cancels notification"), Weapon->IsRefireTimerActiveForTest());
	TestEqual(TEXT("Holster retains the outstanding verdict"), Ability->GetUnresolvedShotRecordCountForTest(), 1);
	TestTrue(TEXT("Authority clear holsters weapon"),
		Weapon->GetLifecycleState() == EShooterWeaponLifecycleState::Holstered);
	TestTrue(TEXT("Same lease can be equipped again"), Equipment->EquipWeapon(Weapon));
	TestFalse(TEXT("Switch cannot bypass cooldown"), Weapon->CanCommitAuthorityShot());
	TestEqual(TEXT("Switch retains pending prediction context"), Weapon->GetAmmoPredictionGeneration(), Generation);
	Ability->HandleAuthorityShotVerdictForTest(501, true);
	TestTrue(TEXT("Late verdict still resolves the original record"), Ability->IsShotRecordResolvedForTest(501));
	TestEqual(TEXT("Late verdict does not request owner replay"),
		Ability->GetOwnerConfirmedReplayRequestCountForTest(), 0);
	DestroyIdempotenceTestWorld(World);
	return true;
}

/** 逻辑旧武器未完成过表现时，服务器清空仍必须取消使用和通知。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterClearUnpresentedUsageTest,
	"ShootGame.Equipment.Presentation.ClearUnpresentedUsage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterClearUnpresentedUsageTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponPresentationIdempotenceAutomationTests;
	UWorld* World = CreateIdempotenceTestWorld();
	if (!TestNotNull(TEXT("World exists"), World))
	{
		return false;
	}
	auto* Character = SpawnIdempotenceTestCharacter(*this, World);
	if (!Character)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}
	auto* Weapon = Cast<AShooterWeaponLifecycleTestWeapon>(GrantIdempotenceTestWeapon(*this, Character,
		AShooterWeaponLifecycleTestWeapon::StaticClass()));
	if (!Weapon)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}
	auto* Current = FindFProperty<FObjectProperty>(UShooterEquipmentComponent::StaticClass(), TEXT("CurrentWeaponActor"));
	if (!TestNotNull(TEXT("Logical property exists"), Current))
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}
	Current->SetObjectPropertyValue_InContainer(Character->GetEquipmentComponent(), Weapon);
	Weapon->ActivateWeapon();
	TestTrue(TEXT("Authority commit succeeds before view completion"), Weapon->CommitSingleShot(0));
	TestTrue(TEXT("Notification exists before clear"), Weapon->IsRefireTimerActiveForTest());
	Character->GetEquipmentComponent()->ClearEquippedWeapon();
	TestFalse(TEXT("Logical clear cancels unpresented weapon notification"), Weapon->IsRefireTimerActiveForTest());
	TestTrue(TEXT("Logical clear holsters unpresented weapon"),
		Weapon->GetLifecycleState() == EShooterWeaponLifecycleState::Holstered);
	TestTrue(TEXT("Logical clear hides unpresented weapon"), Weapon->IsHidden());
	DestroyIdempotenceTestWorld(World);
	return true;
}

/** 手动投递原生复制回调：覆盖两种 Owner / Current 顺序、旧 Ready 和死亡先到。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterPresentationArrivalOrderTest,
	"ShootGame.Equipment.Presentation.ArrivalOrderAndDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterPresentationArrivalOrderTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponPresentationIdempotenceAutomationTests;
	for (int32 OwnerFirst = 0; OwnerFirst < 2; ++OwnerFirst)
	{
		UWorld* World = CreateIdempotenceTestWorld();
		if (!TestNotNull(TEXT("World exists"), World))
		{
			return false;
		}
		auto* Character = SpawnIdempotenceTestCharacter(*this, World);
		if (!Character)
		{
			DestroyIdempotenceTestWorld(World);
			return false;
		}
		auto* Weapon = Cast<AShooterWeaponLifecycleTestWeapon>(GrantIdempotenceTestWeapon(*this, Character,
			AShooterWeaponLifecycleTestWeapon::StaticClass()));
		auto* Other = GrantIdempotenceTestWeapon(*this, Character, AShooterWeaponPresentationTestWeaponSecondary::StaticClass());
		if (!Weapon || !Other)
		{
			DestroyIdempotenceTestWorld(World);
			return false;
		}
		auto* Equipment = Character->GetEquipmentComponent();
		auto* Current = FindFProperty<FObjectProperty>(UShooterEquipmentComponent::StaticClass(), TEXT("CurrentWeaponActor"));
		auto* OnRep = Equipment->FindFunction(TEXT("OnRep_CurrentWeaponActor"));
		if (!TestNotNull(TEXT("Current property exists"), Current) || !TestNotNull(TEXT("OnRep exists"), OnRep))
		{
			DestroyIdempotenceTestWorld(World);
			return false;
		}
		auto* Listener = NewObject<UShooterWeaponPresentationEventTestListener>();
		auto* Hud = NewObject<UShooterBulletCountEventTestListener>();
		Character->OnWeaponPresentationChanged.AddDynamic(Listener,
			&UShooterWeaponPresentationEventTestListener::HandleWeaponPresentationChanged);
		Character->OnBulletCountUpdated.AddDynamic(Hud, &UShooterBulletCountEventTestListener::HandleBulletCountUpdated);
		Character->SetRole(ROLE_SimulatedProxy);
		Weapon->SetRole(ROLE_SimulatedProxy);
		Other->SetRole(ROLE_SimulatedProxy);
		Weapon->SimulateOwnerReplicationForTest(nullptr);
		if (OwnerFirst)
		{
			Weapon->SimulateOwnerReplicationForTest(Character);
		}
		AShooterWeapon* Previous = nullptr;
		Current->SetObjectPropertyValue_InContainer(Equipment, Weapon);
		Equipment->ProcessEvent(OnRep, &Previous);
		if (!OwnerFirst)
		{
			TestEqual(TEXT("Missing owner does not complete"), Listener->EventCount, 0);
			Weapon->SimulateOwnerReplicationForTest(Character);
		}
		Weapon->SimulateWeaponIdReplicationForTest();
		TestFalse(TEXT("Either arrival order shows current weapon"), Weapon->IsHidden());
		TestEqual(TEXT("Arrival publishes one completion"), Listener->EventCount, 1);
		TestEqual(TEXT("Arrival publishes one HUD update"), Hud->EventCount, 1);
		TestTrue(TEXT("Client presentation leaves usage enum untouched"),
			Weapon->GetLifecycleState() == EShooterWeaponLifecycleState::Holstered);
		Previous = Weapon;
		Current->SetObjectPropertyValue_InContainer(Equipment, Other);
		Equipment->ProcessEvent(OnRep, &Previous);
		Equipment->HandleWeaponActorReady(Weapon);
		TestTrue(TEXT("Old Ready cannot reveal old weapon"), Weapon->IsHidden());
		TestFalse(TEXT("New logical weapon remains visible"), Other->IsHidden());
		TestEqual(TEXT("Old Ready produces no extra completion"), Listener->EventCount, 2);

		Character->SimulateDeathReplicationForTest();
		TestTrue(TEXT("Death hides current before logical clear arrives"), Other->IsHidden());
		TestTrue(TEXT("Death keeps replicated logical field intact"), Equipment->GetCurrentWeaponActor() == Other);
		const int32 DeathEvents = Listener->EventCount;
		const int32 DeathHud = Hud->EventCount;
		Equipment->HandleWeaponActorReady(Other);
		TestTrue(TEXT("Ready after death cannot reveal weapon"), Other->IsHidden());
		TestEqual(TEXT("Ready after death does not repeat completion"), Listener->EventCount, DeathEvents);
		TestEqual(TEXT("Ready after death does not repeat HUD"), Hud->EventCount, DeathHud);
		Previous = Other;
		Current->SetObjectPropertyValue_InContainer(Equipment, nullptr);
		Equipment->ProcessEvent(OnRep, &Previous);
		TestEqual(TEXT("Late logical clear does not repeat view clear"), Listener->EventCount, DeathEvents);
		Character->SetRole(ROLE_Authority);
		Weapon->SetRole(ROLE_Authority);
		Other->SetRole(ROLE_Authority);
		DestroyIdempotenceTestWorld(World);
	}
	return true;
}

/** 装备逻辑事件消费者立即清空时，外层提交不能重新激活已被撤销的目标。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterEquipReentrantClearTest, "ShootGame.Equipment.Event.ReentrantClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterEquipReentrantClearTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponPresentationIdempotenceAutomationTests;
	UWorld* World = CreateIdempotenceTestWorld();
	if (!TestNotNull(TEXT("World exists"), World))
	{
		return false;
	}
	auto* Character = SpawnIdempotenceTestCharacter(*this, World);
	if (!Character)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}
	auto* Weapon = GrantIdempotenceTestWeapon(*this, Character, AShooterWeaponPresentationTestWeaponPrimary::StaticClass());
	if (!Weapon)
	{
		DestroyIdempotenceTestWorld(World);
		return false;
	}
	auto* Equipment = Character->GetEquipmentComponent();
	auto* Listener = NewObject<UShooterEquipmentEventTestListener>();
	Listener->ClearOnNextChange = Equipment;
	Equipment->OnEquippedWeaponChanged.AddDynamic(Listener, &UShooterEquipmentEventTestListener::HandleEquippedWeaponChanged);
	TestFalse(TEXT("Revoked equip does not report an active result"), Equipment->EquipWeapon(Weapon));
	TestNull(TEXT("Reentrant clear remains current"), Equipment->GetCurrentWeaponActor());
	TestTrue(TEXT("Revoked weapon remains holstered"),
		Weapon->GetLifecycleState() == EShooterWeaponLifecycleState::Holstered);
	TestTrue(TEXT("Revoked weapon remains hidden"), Weapon->IsHidden());
	TestEqual(TEXT("Equip then clear publish their two logical transitions"), Listener->EventCount, 2);
	DestroyIdempotenceTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
