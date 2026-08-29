// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimInstance.h"
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

	struct FExpectedWeaponAnimClasses
	{
		const TCHAR* WeaponClassPath;
		const TCHAR* WeaponName;
		const TCHAR* ExpectedFirstPersonAnimClassPath;
		const TCHAR* ExpectedThirdPersonAnimClassPath;
	};
}

/**
 * E0 资产订阅审计结论的机器可验证部分：
 * Rifle / Pistol 的 FP / TP AnimClass 配置与 AnimBP 父类分布冻结为基线快照。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponPresentationAnimClassMappingTest,
	"ShootGame.Equipment.Presentation.AnimClassMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponPresentationAnimClassMappingTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponPresentationBaselineAutomationTests;

	const FExpectedWeaponAnimClasses Expected[] = {
		{
			TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C"),
			TEXT("Rifle"),
			TEXT("/Game/Shooter/Animation/FirstPerson/ABP_FP_Weapon.ABP_FP_Weapon_C"),
			TEXT("/Game/Shooter/Animation/ThirdPerson/ABP_TP_Rifle.ABP_TP_Rifle_C"),
		},
		{
			TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Pistol.BP_ShooterWeapon_Pistol_C"),
			TEXT("Pistol"),
			TEXT("/Game/Shooter/Animation/FirstPerson/ABP_FP_Pistol.ABP_FP_Pistol_C"),
			TEXT("/Game/Shooter/Animation/ThirdPerson/ABP_TP_Pistol.ABP_TP_Pistol_C"),
		},
	};

	for (const FExpectedWeaponAnimClasses& Snapshot : Expected)
	{
		const UClass* WeaponClass = LoadClass<AShooterWeapon>(nullptr, Snapshot.WeaponClassPath);
		if (!TestNotNull(
			FString::Printf(TEXT("%s weapon class loads"), Snapshot.WeaponName),
			WeaponClass))
		{
			continue;
		}

		const AShooterWeapon* WeaponDefaults = WeaponClass->GetDefaultObject<AShooterWeapon>();
		if (!TestNotNull(
			FString::Printf(TEXT("%s weapon has defaults"), Snapshot.WeaponName),
			WeaponDefaults))
		{
			continue;
		}

		const UClass* ExpectedFirstPersonClass = LoadClass<UAnimInstance>(
			nullptr,
			Snapshot.ExpectedFirstPersonAnimClassPath);
		const UClass* ExpectedThirdPersonClass = LoadClass<UAnimInstance>(
			nullptr,
			Snapshot.ExpectedThirdPersonAnimClassPath);
		if (!TestNotNull(
			FString::Printf(TEXT("%s expected FP AnimClass loads"), Snapshot.WeaponName),
			ExpectedFirstPersonClass) ||
			!TestNotNull(
				FString::Printf(TEXT("%s expected TP AnimClass loads"), Snapshot.WeaponName),
				ExpectedThirdPersonClass))
		{
			continue;
		}

		TestTrue(
			FString::Printf(TEXT("%s FP AnimClass matches baseline"), Snapshot.WeaponName),
			WeaponDefaults->GetFirstPersonAnimInstanceClass() == ExpectedFirstPersonClass);
		TestTrue(
			FString::Printf(TEXT("%s TP AnimClass matches baseline"), Snapshot.WeaponName),
			WeaponDefaults->GetThirdPersonAnimInstanceClass() == ExpectedThirdPersonClass);
	}

	// 订阅审计的反射面：OnEquippedWeaponChanged 仍是 BlueprintAssignable 动态委托。
	const FProperty* EquippedChangedProperty = FindFProperty<FProperty>(
		UShooterEquipmentComponent::StaticClass(),
		TEXT("OnEquippedWeaponChanged"));
	if (TestNotNull(TEXT("Equipment exposes OnEquippedWeaponChanged"), EquippedChangedProperty))
	{
		TestTrue(
			TEXT("OnEquippedWeaponChanged is BlueprintAssignable"),
			EquippedChangedProperty->HasAnyPropertyFlags(CPF_BlueprintAssignable));
	}

	return true;
}

/**
 * E0 当前表现链路基线：
 * 首次装备、切枪、清空后的附着、显隐与 AnimClass 后置条件保持不变。
 * 该测试在 E2/E3 重构后必须原样通过，是表现收敛重构的行为护栏。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponPresentationBaselineTest,
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
		FVector::ZeroVector,
		FRotator::ZeroRotator);
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

	FGuid PrimaryId;
	FGuid SecondaryId;
	const EShooterInventoryAddResult PrimaryAddResult = Inventory->TryAddWeapon(
		AShooterWeaponPresentationTestWeaponPrimary::StaticClass(),
		PrimaryId);
	TestEqual(
		TEXT("Primary weapon is granted"),
		static_cast<int32>(PrimaryAddResult),
		static_cast<int32>(EShooterInventoryAddResult::Added));

	// 无网络驱动的测试 World 不会自动走 PostNetInit 的 BeginPlay 补偿；
	// 这里显式补齐，使 WeaponOwner 初始化与生产服务器路径一致。
	AShooterWeapon* SpawnedPrimaryWeapon = Inventory->FindWeaponActor(PrimaryId);
	if (SpawnedPrimaryWeapon && !SpawnedPrimaryWeapon->HasActorBegunPlay())
	{
		SpawnedPrimaryWeapon->DispatchBeginPlay();
	}

	TestTrue(TEXT("Primary weapon is equipped"), Equipment->EquipWeapon(PrimaryId));

	AShooterWeapon* PrimaryWeapon = Equipment->GetCurrentWeaponActor();
	if (!TestNotNull(TEXT("Primary weapon becomes current"), PrimaryWeapon))
	{
		DestroyPresentationTestWorld(World);
		return false;
	}

	TestFalse(TEXT("Primary weapon is visible after equip"), PrimaryWeapon->IsHidden());
	TestTrue(
		TEXT("Test character class implements IShooterWeaponHolder"),
		Character->GetClass()->ImplementsInterface(UShooterWeaponHolder::StaticClass()));
	TestTrue(
		TEXT("Test character can cast to IShooterWeaponHolder"),
		Cast<IShooterWeaponHolder>(Character) != nullptr);
	TestTrue(
		TEXT("Primary weapon resolves its WeaponOwner"),
		Cast<AShooterWeaponPresentationTestWeaponPrimary>(PrimaryWeapon) &&
		Cast<AShooterWeaponPresentationTestWeaponPrimary>(PrimaryWeapon)->HasWeaponOwnerForTest());
	TestTrue(
		TEXT("Primary FP mesh is attached to first-person mesh socket"),
		PrimaryWeapon->GetFirstPersonMesh()->GetAttachParent() == Character->GetFirstPersonMesh() &&
		PrimaryWeapon->GetFirstPersonMesh()->GetAttachSocketName() == FName(TEXT("HandGrip_R")));
	TestTrue(
		TEXT("Primary TP mesh is attached to third-person mesh socket"),
		PrimaryWeapon->GetThirdPersonMesh()->GetAttachParent() == Character->GetMesh() &&
		PrimaryWeapon->GetThirdPersonMesh()->GetAttachSocketName() == FName(TEXT("HandGrip_R")));
	TestTrue(
		TEXT("Primary FP AnimClass is applied"),
		Character->GetFirstPersonMesh()->GetAnimClass() ==
			PrimaryWeapon->GetFirstPersonAnimInstanceClass().Get());
	TestTrue(
		TEXT("Primary TP AnimClass is applied"),
		Character->GetMesh()->GetAnimClass() ==
			PrimaryWeapon->GetThirdPersonAnimInstanceClass().Get());

	// 切枪：旧武器隐藏，新武器可见，AnimClass 同步切换。
	const EShooterInventoryAddResult SecondaryAddResult = Inventory->TryAddWeapon(
		AShooterWeaponPresentationTestWeaponSecondary::StaticClass(),
		SecondaryId);
	TestEqual(
		TEXT("Secondary weapon is granted"),
		static_cast<int32>(SecondaryAddResult),
		static_cast<int32>(EShooterInventoryAddResult::Added));

	AShooterWeapon* SpawnedSecondaryWeapon = Inventory->FindWeaponActor(SecondaryId);
	if (SpawnedSecondaryWeapon && !SpawnedSecondaryWeapon->HasActorBegunPlay())
	{
		SpawnedSecondaryWeapon->DispatchBeginPlay();
	}

	TestTrue(TEXT("Secondary weapon is equipped"), Equipment->EquipWeapon(SecondaryId));

	AShooterWeapon* SecondaryWeapon = Equipment->GetCurrentWeaponActor();
	if (!TestNotNull(TEXT("Secondary weapon becomes current"), SecondaryWeapon))
	{
		DestroyPresentationTestWorld(World);
		return false;
	}

	TestTrue(TEXT("Previous weapon is hidden after switch"), PrimaryWeapon->IsHidden());
	TestFalse(TEXT("New weapon is visible after switch"), SecondaryWeapon->IsHidden());
	TestTrue(
		TEXT("FP AnimClass switches to secondary weapon config"),
		Character->GetFirstPersonMesh()->GetAnimClass() ==
			SecondaryWeapon->GetFirstPersonAnimInstanceClass().Get());
	TestTrue(
		TEXT("TP AnimClass switches to secondary weapon config"),
		Character->GetMesh()->GetAnimClass() ==
			SecondaryWeapon->GetThirdPersonAnimInstanceClass().Get());

	const UClass* SecondaryFPAnimClass = Character->GetFirstPersonMesh()->GetAnimClass();
	const UClass* SecondaryTPAnimClass = Character->GetMesh()->GetAnimClass();

	// 清空：武器隐藏、逻辑装备为空；当前行为保留上一 AnimClass（本计划明确保留）。
	Equipment->ClearEquippedWeapon();
	TestNull(TEXT("Clear empties CurrentWeaponActor"), Equipment->GetCurrentWeaponActor());
	TestTrue(TEXT("Cleared weapon is hidden"), SecondaryWeapon->IsHidden());
	TestTrue(
		TEXT("Clear keeps previous FP AnimClass in this plan baseline"),
		Character->GetFirstPersonMesh()->GetAnimClass() == SecondaryFPAnimClass);
	TestTrue(
		TEXT("Clear keeps previous TP AnimClass in this plan baseline"),
		Character->GetMesh()->GetAnimClass() == SecondaryTPAnimClass);

	DestroyPresentationTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
