// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimInstance.h"
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
 * R0 行为冻结：用真实 UWorld 驱动角色武器授予路径。
 * 基线阶段锁定“AddWeaponClass 仍可绕开 Inventory 直接生成并装备武器”的现状；
 * R1 会在同一测试中把预期改为 Inventory 唯一创建路径，其余外部行为保持不变。
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

	// R0 现状：AddWeaponClass 直接生成 WeaponActor 并写 Character.CurrentWeapon，
	// 但不会向 Inventory 登记实例或当前 Active 身份。R1 将修改这些内部预期。
	Character->AddWeaponClass(AShooterArchitectureTestWeapon::StaticClass());

	AShooterWeapon* CurrentWeapon = Character->GetCurrentWeaponActor();
	TestNotNull(TEXT("AddWeaponClass still equips a weapon on baseline"), CurrentWeapon);
	if (CurrentWeapon)
	{
		TestTrue(TEXT("Baseline granted weapon is owned by the character"), CurrentWeapon->GetOwner() == Character);
		TestFalse(TEXT("Baseline granted weapon has no Inventory bound identity"), CurrentWeapon->GetBoundInstanceId().IsValid());
	}

	TestEqual(TEXT("Baseline AddWeaponClass does not create Inventory entries"), Inventory->GetWeaponCount(), 0);
	TestFalse(TEXT("Baseline Inventory ActiveWeaponInstanceId stays invalid"), Inventory->GetActiveWeaponInstanceId().IsValid());

	// 重复授予同一类型不能产生第二把武器：该行为在 R1 前后都必须保持。
	Character->AddWeaponClass(AShooterArchitectureTestWeapon::StaticClass());
	AShooterWeapon* CurrentWeaponAfterSecondGrant = Character->GetCurrentWeaponActor();
	TestTrue(
		TEXT("Duplicate weapon class grant keeps the same CurrentWeapon"),
		CurrentWeaponAfterSecondGrant == CurrentWeapon);
	TestEqual(TEXT("Baseline duplicate grant keeps Inventory empty"), Inventory->GetWeaponCount(), 0);

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
 * R0 所有权表快照：当前 Aim 网络属性与 RPC 位于 Character，
 * CurrentWeapon 与 Inventory.ActiveWeaponInstanceId 是并存的复制字段。
 * R2 / R4 会在相应阶段更新这里的唯一权威预期。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterArchitectureOwnershipSurfaceTest,
	"ShootGame.Architecture.Baseline.OwnershipSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterArchitectureOwnershipSurfaceTest::RunTest(const FString& Parameters)
{
	const FProperty* CurrentWeaponProperty = FindFProperty<FProperty>(
		AShooterCharacter::StaticClass(),
		TEXT("CurrentWeapon"));
	if (!TestNotNull(TEXT("Character exposes CurrentWeapon"), CurrentWeaponProperty))
	{
		return false;
	}
	TestTrue(TEXT("Character CurrentWeapon is replicated"), CurrentWeaponProperty->HasAnyPropertyFlags(CPF_Net));
	TestEqual(
		TEXT("Character CurrentWeapon uses OnRep_CurrentWeapon"),
		CurrentWeaponProperty->RepNotifyFunc,
		FName(TEXT("OnRep_CurrentWeapon")));

	const FProperty* PresentationAimTargetProperty = FindFProperty<FProperty>(
		AShooterCharacter::StaticClass(),
		TEXT("PresentationAimTarget"));
	if (!TestNotNull(TEXT("Character exposes PresentationAimTarget"), PresentationAimTargetProperty))
	{
		return false;
	}
	TestTrue(TEXT("Character PresentationAimTarget is replicated"), PresentationAimTargetProperty->HasAnyPropertyFlags(CPF_Net));
	TestEqual(
		TEXT("Character PresentationAimTarget uses OnRep_PresentationAimTarget"),
		PresentationAimTargetProperty->RepNotifyFunc,
		FName(TEXT("OnRep_PresentationAimTarget")));

	const UFunction* AimServerRpc = AShooterCharacter::StaticClass()->FindFunctionByName(
		TEXT("ServerUpdatePresentationAimTarget"));
	TestNotNull(TEXT("Character owns the presentation aim Server RPC on baseline"), AimServerRpc);
	TestTrue(
		TEXT("Character presentation aim RPC is a server RPC"),
		AimServerRpc && AimServerRpc->HasAnyFunctionFlags(FUNC_NetServer));

	const FProperty* ActiveWeaponInstanceIdProperty = FindFProperty<FProperty>(
		UShooterInventoryComponent::StaticClass(),
		TEXT("ActiveWeaponInstanceId"));
	if (!TestNotNull(TEXT("Inventory exposes ActiveWeaponInstanceId"), ActiveWeaponInstanceIdProperty))
	{
		return false;
	}
	TestTrue(TEXT("Inventory ActiveWeaponInstanceId is replicated"), ActiveWeaponInstanceIdProperty->HasAnyPropertyFlags(CPF_Net));
	TestEqual(
		TEXT("Inventory ActiveWeaponInstanceId uses OnRep_ActiveWeaponInstanceId"),
		ActiveWeaponInstanceIdProperty->RepNotifyFunc,
		FName(TEXT("OnRep_ActiveWeaponInstanceId")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
