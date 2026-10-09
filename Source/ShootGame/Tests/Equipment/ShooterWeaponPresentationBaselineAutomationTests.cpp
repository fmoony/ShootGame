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
#include "Tests/Weapon/ShooterWeaponTestTableTypes.h"
#include "Weapons/ShooterWeapon.h"
#include "Tests/Equipment/ShooterWeaponPresentationTestTypes.h"

namespace ShooterWeaponPresentationBaselineAutomationTests
{
	UWorld* CreatePresentationTestWorld()
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

	void DestroyPresentationTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
}

/**
 * 订阅审计的反射面：装备组件的武器变化委托仍是 BlueprintAssignable 动态委托。
 * 武器行不再承载任何 AnimClass（第一/第三人称动画类都固定在角色上），
 * 因此本测试只保留与动画类无关的订阅面契约。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterEquipmentSubscriptionSurfaceTest,
	"ShootGame.Equipment.Presentation.EquipmentSubscriptionSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterEquipmentSubscriptionSurfaceTest::RunTest(const FString& Parameters)
{
	// 订阅审计的反射面：OnEquippedWeaponChanged 仍是 BlueprintAssignable 动态委托。
	const FProperty* EquippedChangedProperty = FindFProperty<FProperty>(UShooterEquipmentComponent::StaticClass(),
		TEXT("OnEquippedWeaponChanged"));
	if (TestNotNull(TEXT("Equipment exposes OnEquippedWeaponChanged"), EquippedChangedProperty))
	{
		TestTrue(TEXT("OnEquippedWeaponChanged is BlueprintAssignable"),
			EquippedChangedProperty->HasAnyPropertyFlags(CPF_BlueprintAssignable));
	}

	return true;
}

/**
 * E0 当前表现链路基线：
 * 首次装备、切枪、清空后的附着、显隐与 AnimClass 后置条件保持不变。
 * 该测试在 E2/E3 重构后必须原样通过，是表现收敛重构的行为护栏。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponPresentationBaselineTest,
	"ShootGame.Equipment.Presentation.BaselineChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponPresentationBaselineTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponPresentationBaselineAutomationTests;

	UWorld* World = CreatePresentationTestWorld();
	if (!TestNotNull(TEXT("Presentation test world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = World->SpawnActor<AShooterWeaponPresentationTestCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Presentation test character spawned"), Character))
	{
		DestroyPresentationTestWorld(World);
		return false;
	}

	// 组件与 Actor 的 BeginPlay 依赖 World 已开始播放；否则 WeaponOwner / 组件订阅不会初始化。
	// 测试 World 没有 GameMode，UWorld::BeginPlay 不会走 GameState 通知链，这里直接驱动 WorldSettings。
	if (AWorldSettings* WorldSettings = World->GetWorldSettings())
	{
		WorldSettings->NotifyBeginPlay();
	}

	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	if (!TestNotNull(TEXT("Presentation test character owns Inventory"), Inventory) ||
		!TestNotNull(TEXT("Presentation test character owns Equipment"), Equipment))
	{
		DestroyPresentationTestWorld(World);
		return false;
	}

	EShooterInventoryAddResult PrimaryAddResult = EShooterInventoryAddResult::NotAuthoritative;
	AShooterWeapon* PrimaryWeapon = GrantTestWeapon(World, Inventory,
		AShooterWeaponPresentationTestWeaponPrimary::StaticClass(),
		/*MagazineSize*/ 10,
		/*InitialReserveAmmo*/ -1,
		&PrimaryAddResult);
	TestEqual(TEXT("Primary weapon is granted"), static_cast<int32>(PrimaryAddResult),
		static_cast<int32>(EShooterInventoryAddResult::Added));

	// 无网络驱动的测试 World 不会自动走 PostNetInit 的 BeginPlay 补偿；
	// 这里显式补齐，使 WeaponOwner 初始化与生产服务器路径一致。
	AShooterWeapon* SpawnedPrimaryWeapon = PrimaryWeapon;
	if (SpawnedPrimaryWeapon && !SpawnedPrimaryWeapon->HasActorBegunPlay())
	{
		SpawnedPrimaryWeapon->DispatchBeginPlay();
	}

	TestTrue(TEXT("Primary weapon is equipped"), Equipment->EquipWeapon(PrimaryWeapon));

	PrimaryWeapon = Equipment->GetCurrentWeaponActor();
	if (!TestNotNull(TEXT("Primary weapon becomes current"), PrimaryWeapon))
	{
		DestroyPresentationTestWorld(World);
		return false;
	}

	TestFalse(TEXT("Primary weapon is visible after equip"), PrimaryWeapon->IsHidden());
	TestTrue(TEXT("Test character class implements IShooterWeaponHolder"),
		Character->GetClass()->ImplementsInterface(UShooterWeaponHolder::StaticClass()));
	TestTrue(TEXT("Test character can cast to IShooterWeaponHolder"), Cast<IShooterWeaponHolder>(Character) != nullptr);
	TestTrue(TEXT("Primary weapon resolves its WeaponOwner"),
		Cast<AShooterWeaponPresentationTestWeaponPrimary>(PrimaryWeapon) &&
		Cast<AShooterWeaponPresentationTestWeaponPrimary>(PrimaryWeapon)->HasWeaponOwnerForTest());
	TestTrue(TEXT("Primary FP mesh is attached to first-person mesh socket"),
		PrimaryWeapon->GetFirstPersonMesh()->GetAttachParent() == Character->GetFirstPersonMesh() &&
		PrimaryWeapon->GetFirstPersonMesh()->GetAttachSocketName() == FName(TEXT("HandGrip_R")));
	TestTrue(TEXT("Primary TP mesh is attached to third-person mesh socket"),
		PrimaryWeapon->GetThirdPersonMesh()->GetAttachParent() == Character->GetMesh() &&
		PrimaryWeapon->GetThirdPersonMesh()->GetAttachSocketName() == FName(TEXT("HandGrip_R")));
	TestTrue(TEXT("Primary FP AnimClass is the stable character class"),
		Character->GetFirstPersonMesh()->GetAnimClass() == UShooterFirstPersonAnimInstance::StaticClass());
	TestTrue(TEXT("Primary TP AnimClass is the stable character class"), Character->GetMesh()->GetAnimClass() ==
			UShooterThirdPersonAnimInstance::StaticClass());

	// 切枪：旧武器隐藏，新武器可见；两侧动画类都固定在角色上，不随武器切换。
	EShooterInventoryAddResult SecondaryAddResult = EShooterInventoryAddResult::NotAuthoritative;
	AShooterWeapon* SecondaryWeapon = GrantTestWeapon(World, Inventory,
		AShooterWeaponPresentationTestWeaponSecondary::StaticClass(),
		/*MagazineSize*/ 10,
		/*InitialReserveAmmo*/ -1,
		&SecondaryAddResult);
	TestEqual(TEXT("Secondary weapon is granted"), static_cast<int32>(SecondaryAddResult),
		static_cast<int32>(EShooterInventoryAddResult::Added));

	AShooterWeapon* SpawnedSecondaryWeapon = SecondaryWeapon;
	if (SpawnedSecondaryWeapon && !SpawnedSecondaryWeapon->HasActorBegunPlay())
	{
		SpawnedSecondaryWeapon->DispatchBeginPlay();
	}

	TestTrue(TEXT("Secondary weapon is equipped"), Equipment->EquipWeapon(SecondaryWeapon));

	SecondaryWeapon = Equipment->GetCurrentWeaponActor();
	if (!TestNotNull(TEXT("Secondary weapon becomes current"), SecondaryWeapon))
	{
		DestroyPresentationTestWorld(World);
		return false;
	}

	TestTrue(TEXT("Previous weapon is hidden after switch"), PrimaryWeapon->IsHidden());
	TestFalse(TEXT("New weapon is visible after switch"), SecondaryWeapon->IsHidden());
	TestTrue(TEXT("FP AnimClass stays the stable character class"),
		Character->GetFirstPersonMesh()->GetAnimClass() == UShooterFirstPersonAnimInstance::StaticClass());
	TestTrue(TEXT("TP AnimClass remains the stable character class"),
		Character->GetMesh()->GetAnimClass() == UShooterThirdPersonAnimInstance::StaticClass());

	const UClass* SecondaryFPAnimClass = Character->GetFirstPersonMesh()->GetAnimClass();
	const UClass* SecondaryTPAnimClass = Character->GetMesh()->GetAnimClass();

	// 清空：武器隐藏、逻辑装备为空；当前行为保留上一 AnimClass（本计划明确保留）。
	Equipment->ClearEquippedWeapon();
	TestNull(TEXT("Clear empties CurrentWeaponActor"), Equipment->GetCurrentWeaponActor());
	TestTrue(TEXT("Cleared weapon is hidden"), SecondaryWeapon->IsHidden());
	TestTrue(TEXT("Clear keeps previous FP AnimClass in this plan baseline"),
		Character->GetFirstPersonMesh()->GetAnimClass() == SecondaryFPAnimClass);
	TestTrue(TEXT("Clear keeps previous TP AnimClass in this plan baseline"),
		Character->GetMesh()->GetAnimClass() == SecondaryTPAnimClass);

	DestroyPresentationTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
