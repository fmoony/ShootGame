// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimInstance.h"
#include "Characters/Aim/ShooterAimPresentationComponent.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Characters/ShooterCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "ShooterArchitectureTestTypes.h"
#include "UObject/UnrealType.h"
#include "Weapons/ShooterWeapon.h"

namespace ShooterArchitectureBaselineAutomationTests
{
	UWorld* CreateArchitectureTestWorld()
	{
		// CreateWorld 总会创建并初始化 PersistentLevel / WorldSettings；
	// 测试只补一个 WorldContext，不能再次调用 InitializeNewWorld。
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World || !GEngine)
	{
		return World;
	}

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	return World;
	}

	void DestroyArchitectureTestWorld(UWorld* World)
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
 * R0/R1 行为冻结：用真实 UWorld 驱动角色武器授予路径。
 * R1 后玩家武器只有 Inventory.TryAddWeapon 一条创建路径，且授予后仍立即装备。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterArchitectureWeaponGrantSurfaceTest,
	"ShootGame.Architecture.Baseline.WeaponGrantSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterArchitectureWeaponGrantSurfaceTest::RunTest(const FString& Parameters)
{
	using namespace ShooterArchitectureBaselineAutomationTests;

	UWorld* World = CreateArchitectureTestWorld();
	if (!TestNotNull(TEXT("Architecture test world created"), World))
	{
		return false;
	}

	AShooterArchitectureTestCharacter* Character = World->SpawnActor<AShooterArchitectureTestCharacter>(
		FVector::ZeroVector,
		FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Architecture test character spawned"), Character))
	{
		DestroyArchitectureTestWorld(World);
		return false;
	}

	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	if (!TestNotNull(TEXT("Architecture test character owns InventoryComponent"), Inventory))
	{
		DestroyArchitectureTestWorld(World);
		return false;
	}

	// R1 完成条件：AddWeaponClass 必须进入 Inventory.TryAddWeapon 唯一创建路径，
	// 并继续走 HandleWeaponAddedToInventory 的“授予后立即装备”入口。
	Character->AddWeaponClass(AShooterArchitectureTestWeapon::StaticClass());

	AShooterWeapon* CurrentWeapon = Character->GetCurrentWeaponActor();
	TestNotNull(TEXT("AddWeaponClass still equips a weapon after R1"), CurrentWeapon);
	if (CurrentWeapon)
	{
		TestTrue(TEXT("Granted weapon is owned by the character"), CurrentWeapon->GetOwner() == Character);
		TestTrue(TEXT("Granted weapon has an Inventory bound identity"), CurrentWeapon->GetBoundInstanceId().IsValid());
		TestTrue(
			TEXT("CurrentWeaponActor matches the Inventory Active weapon actor"),
			CurrentWeapon == Inventory->GetActiveWeaponActor());
	}

	TestEqual(TEXT("AddWeaponClass creates exactly one Inventory entry"), Inventory->GetWeaponCount(), 1);
	TestTrue(TEXT("Inventory ActiveWeaponInstanceId becomes valid"), Inventory->GetActiveWeaponInstanceId().IsValid());

	// 重复授予同一类型不能产生第二把武器：R1 由 TryAddWeapon 的 DuplicateDefinition 拒绝。
	Character->AddWeaponClass(AShooterArchitectureTestWeapon::StaticClass());
	AShooterWeapon* CurrentWeaponAfterSecondGrant = Character->GetCurrentWeaponActor();
	TestTrue(
		TEXT("Duplicate weapon class grant keeps the same CurrentWeapon"),
		CurrentWeaponAfterSecondGrant == CurrentWeapon);
	TestEqual(TEXT("Duplicate grant keeps exactly one Inventory entry"), Inventory->GetWeaponCount(), 1);

	DestroyArchitectureTestWorld(World);
	return true;
}

/**
 * R0 资产表面快照：五个 AnimBP 必须可加载，且记录当前父类分布。
 * R6.2 资产迁移提交会在此处把第一人称父类改为 UShooterFirstPersonAnimInstance、
 * 第三人称 Pistol 父类改为 UShooterThirdPersonAnimInstance。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterArchitectureAnimBPSurfaceTest,
	"ShootGame.Architecture.Baseline.AnimBPSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterArchitectureAnimBPSurfaceTest::RunTest(const FString& Parameters)
{
	struct FAnimBPSnapshot
	{
		const TCHAR* Path;
		const TCHAR* Name;
		bool bExpectedThirdPersonAnimInstance;
	};

	const FAnimBPSnapshot Snapshots[] = {
		{TEXT("/Game/Shooter/Animation/FirstPerson/ABP_FP_Weapon.ABP_FP_Weapon_C"), TEXT("ABP_FP_Weapon"), false},
		{TEXT("/Game/Shooter/Animation/FirstPerson/ABP_FP_Pistol.ABP_FP_Pistol_C"), TEXT("ABP_FP_Pistol"), false},
		{TEXT("/Game/Shooter/Animation/FirstPerson/ABP_FP_Copy.ABP_FP_Copy_C"), TEXT("ABP_FP_Copy"), false},
		{TEXT("/Game/Shooter/Animation/ThirdPerson/ABP_TP_Rifle.ABP_TP_Rifle_C"), TEXT("ABP_TP_Rifle"), true},
		{TEXT("/Game/Shooter/Animation/ThirdPerson/ABP_TP_Pistol.ABP_TP_Pistol_C"), TEXT("ABP_TP_Pistol"), false},
	};

	for (const FAnimBPSnapshot& Snapshot : Snapshots)
	{
		const UClass* AnimClass = LoadClass<UAnimInstance>(nullptr, Snapshot.Path);
		if (!TestNotNull(
			FString::Printf(TEXT("AnimBP can be loaded: %s"), Snapshot.Name),
			AnimClass))
		{
			continue;
		}

		TestTrue(
			FString::Printf(TEXT("%s derives from UAnimInstance"), Snapshot.Name),
			AnimClass->IsChildOf(UAnimInstance::StaticClass()));
		TestEqual(
			FString::Printf(TEXT("%s current ShooterThirdPersonAnimInstance parent surface matches snapshot"), Snapshot.Name),
			AnimClass->IsChildOf(UShooterThirdPersonAnimInstance::StaticClass()),
			Snapshot.bExpectedThirdPersonAnimInstance);
	}

	return true;
}

/**
 * R0/R2/R4 所有权表快照：
 * Aim 由 AimPresentationComponent 承接；CurrentWeaponActor 与 ActiveWeaponInstanceId
 * 在 R4 统一迁入 EquipmentComponent；Inventory 只保留拥有关系与只读转发。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterArchitectureOwnershipSurfaceTest,
	"ShootGame.Architecture.Baseline.OwnershipSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterArchitectureOwnershipSurfaceTest::RunTest(const FString& Parameters)
{
	const AShooterCharacter* CharacterDefaults =
		AShooterCharacter::StaticClass()->GetDefaultObject<AShooterCharacter>();
	if (!TestNotNull(TEXT("Character has defaults"), CharacterDefaults))
	{
		return false;
	}

	const UShooterAimPresentationComponent* AimPresentationComponent =
		CharacterDefaults->GetAimPresentationComponent();
	if (!TestNotNull(TEXT("Character creates AimPresentationComponent"), AimPresentationComponent))
	{
		return false;
	}
	TestTrue(TEXT("AimPresentationComponent is replicated"), AimPresentationComponent->GetIsReplicated());

	const UShooterEquipmentComponent* EquipmentComponent =
		CharacterDefaults->GetEquipmentComponent();
	if (!TestNotNull(TEXT("Character creates EquipmentComponent"), EquipmentComponent))
	{
		return false;
	}
	TestTrue(TEXT("EquipmentComponent is replicated after R4"), EquipmentComponent->GetIsReplicated());

	// R4：Character 不再拥有 CurrentWeapon 复制属性。
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

	const FProperty* EquipmentActiveIdProperty = FindFProperty<FProperty>(
		UShooterEquipmentComponent::StaticClass(),
		TEXT("ActiveWeaponInstanceId"));
	if (!TestNotNull(TEXT("Equipment exposes ActiveWeaponInstanceId"), EquipmentActiveIdProperty))
	{
		return false;
	}
	TestTrue(TEXT("Equipment ActiveWeaponInstanceId is replicated"), EquipmentActiveIdProperty->HasAnyPropertyFlags(CPF_Net));
	TestEqual(
		TEXT("Equipment ActiveWeaponInstanceId uses OnRep_ActiveWeaponInstanceId"),
		EquipmentActiveIdProperty->RepNotifyFunc,
		FName(TEXT("OnRep_ActiveWeaponInstanceId")));

	// R4：Inventory 不再持有 Active 复制字段，只有 R4 迁移期只读转发入口。
	TestNull(
		TEXT("Inventory no longer owns ActiveWeaponInstanceId property"),
		FindFProperty<FProperty>(UShooterInventoryComponent::StaticClass(), TEXT("ActiveWeaponInstanceId")));

	// R2：Character 不再持有 Aim 网络属性和 Server RPC。
	TestNull(
		TEXT("Character no longer owns PresentationAimTarget property"),
		FindFProperty<FProperty>(AShooterCharacter::StaticClass(), TEXT("PresentationAimTarget")));
	TestNull(
		TEXT("Character no longer owns presentation aim Server RPC"),
		AShooterCharacter::StaticClass()->FindFunctionByName(TEXT("ServerUpdatePresentationAimTarget")));

	const FProperty* ComponentPresentationAimTargetProperty = FindFProperty<FProperty>(
		UShooterAimPresentationComponent::StaticClass(),
		TEXT("PresentationAimTarget"));
	if (!TestNotNull(TEXT("AimPresentationComponent exposes PresentationAimTarget"), ComponentPresentationAimTargetProperty))
	{
		return false;
	}
	TestTrue(
		TEXT("AimPresentationComponent PresentationAimTarget is replicated"),
		ComponentPresentationAimTargetProperty->HasAnyPropertyFlags(CPF_Net));
	TestEqual(
		TEXT("AimPresentationComponent PresentationAimTarget uses OnRep_PresentationAimTarget"),
		ComponentPresentationAimTargetProperty->RepNotifyFunc,
		FName(TEXT("OnRep_PresentationAimTarget")));

	const UFunction* AimServerRpc = UShooterAimPresentationComponent::StaticClass()->FindFunctionByName(
		TEXT("ServerUpdatePresentationAimTarget"));
	TestNotNull(TEXT("AimPresentationComponent owns the presentation aim Server RPC"), AimServerRpc);
	TestTrue(
		TEXT("AimPresentationComponent presentation aim RPC is a server RPC"),
		AimServerRpc && AimServerRpc->HasAnyFunctionFlags(FUNC_NetServer));

	return true;
}


#endif // WITH_DEV_AUTOMATION_TESTS
