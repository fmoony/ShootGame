// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ShooterAbilitySystemComponent.h"
#include "ShooterGameplayAbility_Equip.h"
#include "ShooterGameplayAbility_Fire.h"
#include "ShooterGameplayAbility_Reload.h"
#include "ShooterPlayerState.h"
#include "ShooterWeapon.h"

namespace ShooterAbilityReloadEquipAutomationTests
{
	bool TestServerOnlyShell(
		FAutomationTestBase& Test,
		const TCHAR* AbilityName,
		const UShooterGameplayAbility* AbilityDefaults)
	{
		if (!Test.TestNotNull(
			FString::Printf(TEXT("%s has defaults"), AbilityName),
			AbilityDefaults))
		{
			return false;
		}

		Test.TestEqual(
			FString::Printf(TEXT("%s uses InstancedPerActor"), AbilityName),
			static_cast<int32>(AbilityDefaults->GetInstancingPolicy()),
			static_cast<int32>(EGameplayAbilityInstancingPolicy::InstancedPerActor));
		Test.TestEqual(
			FString::Printf(TEXT("%s uses ServerOnly net execution"), AbilityName),
			static_cast<int32>(AbilityDefaults->GetNetExecutionPolicy()),
			static_cast<int32>(EGameplayAbilityNetExecutionPolicy::ServerOnly));
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityReloadEquipGrantPlayerTest,
	"ShootGame.Ability.ReloadEquip.Grant.Player",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityReloadEquipGrantPlayerTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityReloadEquipAutomationTests;

	const AShooterPlayerState* PlayerStateDefaults = GetDefault<AShooterPlayerState>();
	if (!TestNotNull(TEXT("ShooterPlayerState has defaults"), PlayerStateDefaults))
	{
		return false;
	}

	TestTrue(
		TEXT("PlayerState default ReloadAbilityClass is GA_Reload"),
		PlayerStateDefaults->GetReloadAbilityClass() == UShooterGameplayAbility_Reload::StaticClass());
	TestTrue(
		TEXT("PlayerState default EquipAbilityClass is GA_Equip"),
		PlayerStateDefaults->GetEquipAbilityClass() == UShooterGameplayAbility_Equip::StaticClass());

	const UShooterAbilitySystemComponent* AbilitySystemComponent =
		Cast<UShooterAbilitySystemComponent>(PlayerStateDefaults->GetAbilitySystemComponent());
	if (!TestNotNull(TEXT("PlayerState CDO has a Shooter ASC"), AbilitySystemComponent))
	{
		return false;
	}

	// 授予发生在服务器 PostInitializeComponents / BeginPlay 生命周期内，
	// CDO 本身不能携带任何 Spec；实际“有且只有一个 Spec”由网络测试协调器验证。
	TestEqual(
		TEXT("PlayerState CDO has no Reload Spec before lifecycle grant"),
		PlayerStateDefaults->GetReloadAbilitySpecCount(),
		0);
	TestEqual(
		TEXT("PlayerState CDO has no Equip Spec before lifecycle grant"),
		PlayerStateDefaults->GetEquipAbilitySpecCount(),
		0);
	TestEqual(
		TEXT("ASC on CDO counts zero Reload Specs"),
		AbilitySystemComponent->GetAbilitySpecCountForClass(
			PlayerStateDefaults->GetReloadAbilityClass()),
		0);
	TestEqual(
		TEXT("ASC on CDO counts zero Equip Specs"),
		AbilitySystemComponent->GetAbilitySpecCountForClass(
			PlayerStateDefaults->GetEquipAbilityClass()),
		0);

	const UShooterGameplayAbility_Reload* ReloadDefaults =
		GetDefault<UShooterGameplayAbility_Reload>();
	const UShooterGameplayAbility_Equip* EquipDefaults =
		GetDefault<UShooterGameplayAbility_Equip>();
	if (!TestServerOnlyShell(*this, TEXT("GA_Reload"), ReloadDefaults) ||
		!TestServerOnlyShell(*this, TEXT("GA_Equip"), EquipDefaults))
	{
		return false;
	}

	TestTrue(TEXT("GA_Reload AbilityTags contains Input.Reload"), ReloadDefaults->HasInputReloadTag());
	TestTrue(TEXT("GA_Reload blocked by State.Dead"), ReloadDefaults->IsBlockedByStateDead());
	TestTrue(TEXT("GA_Reload blocked by State.Reloading"), ReloadDefaults->IsBlockedByStateReloading());
	TestTrue(TEXT("GA_Reload blocked by State.Equipping"), ReloadDefaults->IsBlockedByStateEquipping());
	TestTrue(TEXT("GA_Reload owns State.Reloading while active"), ReloadDefaults->OwnsStateReloadingWhileActive());
	TestFalse(TEXT("GA_Reload does not retrigger an already active instance"), ReloadDefaults->CanRetriggerInstancedAbility());

	TestTrue(TEXT("GA_Equip AbilityTags contains Input.Equip.Next"), EquipDefaults->HasInputEquipNextTag());
	TestTrue(TEXT("GA_Equip blocked by State.Dead"), EquipDefaults->IsBlockedByStateDead());
	TestTrue(TEXT("GA_Equip blocked by State.Equipping"), EquipDefaults->IsBlockedByStateEquipping());
	TestTrue(TEXT("GA_Equip owns State.Equipping while active"), EquipDefaults->OwnsStateEquippingWhileActive());
	TestFalse(TEXT("GA_Equip does not retrigger an already active instance"), EquipDefaults->CanRetriggerInstancedAbility());

	// GameplayTag 互斥合同：Fire 在换弹与装备事务期间也必须拒绝激活。
	const UShooterGameplayAbility_Fire* FireDefaults = GetDefault<UShooterGameplayAbility_Fire>();
	TestTrue(TEXT("GA_Fire blocked by State.Reloading"), FireDefaults->IsBlockedByStateReloading());
	TestTrue(TEXT("GA_Fire blocked by State.Equipping"), FireDefaults->IsBlockedByStateEquipping());

	// WeaponActor 必须提供不依赖动画的服务器权威事务时长。
	const AShooterWeapon* WeaponDefaults = GetDefault<AShooterWeapon>();
	if (TestNotNull(TEXT("AShooterWeapon has defaults"), WeaponDefaults))
	{
		TestTrue(TEXT("Weapon ReloadDuration is positive"), WeaponDefaults->GetReloadDuration() > 0.0f);
		TestTrue(TEXT("Weapon EquipDuration is positive"), WeaponDefaults->GetEquipDuration() > 0.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityReloadEquipGrantRespawnNoDuplicateTest,
	"ShootGame.Ability.ReloadEquip.Grant.RespawnNoDuplicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityReloadEquipGrantRespawnNoDuplicateTest::RunTest(const FString& Parameters)
{
	// 真实重生不重复授予由 ShooterNetworkTestCoordinator 在 Dedicated / Listen 中验证。
	// 本测试守住可静态检查的授予边界：授予入口只在服务器生命周期调用，CDO 不预置 Spec。
	const AShooterPlayerState* PlayerStateDefaults = GetDefault<AShooterPlayerState>();
	if (!TestNotNull(TEXT("PlayerState has defaults"), PlayerStateDefaults))
	{
		return false;
	}

	TestEqual(
		TEXT("PlayerState CDO carries no granted Reload Spec"),
		PlayerStateDefaults->GetReloadAbilitySpecCount(),
		0);
	TestEqual(
		TEXT("PlayerState CDO carries no granted Equip Spec"),
		PlayerStateDefaults->GetEquipAbilitySpecCount(),
		0);

	// 客户端不可远程调用授予入口；它们只是服务器生命周期内的普通 C++ 方法。
	TestNull(
		TEXT("GrantReloadAbility is not a remote-callable UFUNCTION"),
		AShooterPlayerState::StaticClass()->FindFunctionByName(TEXT("GrantReloadAbility")));
	TestNull(
		TEXT("GrantEquipAbility is not a remote-callable UFUNCTION"),
		AShooterPlayerState::StaticClass()->FindFunctionByName(TEXT("GrantEquipAbility")));

	// 运行时重复 Grant 与重生 Handle 不变由 ShooterNetworkTestCoordinator 在
	// Dedicated / Listen 会话中验证；EditorContext 不在 CDO 上执行会污染 CDO 的授予。
	return true;
}

#endif
