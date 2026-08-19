// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Abilities/GameplayAbility.h"
#include "ShooterAbilitySystemComponent.h"
#include "ShooterGameplayAbility_Fire.h"
#include "ShooterPlayerState.h"
#include "ShooterWeapon.h"
#include "UObject/UnrealType.h"

namespace ShooterAbilityFireBehaviorAutomationTests
{
	bool TestServerOnlyContract(FAutomationTestBase& Test)
	{
		const UShooterGameplayAbility_Fire* FireDefaults =
			GetDefault<UShooterGameplayAbility_Fire>();
		if (!Test.TestNotNull(TEXT("GA_Fire has defaults"), FireDefaults))
		{
			return false;
		}

		Test.TestEqual(
			TEXT("GA_Fire executes only on server"),
			static_cast<int32>(FireDefaults->GetNetExecutionPolicy()),
			static_cast<int32>(EGameplayAbilityNetExecutionPolicy::ServerOnly));
		Test.TestEqual(
			TEXT("GA_Fire is InstancedPerActor"),
			static_cast<int32>(FireDefaults->GetInstancingPolicy()),
			static_cast<int32>(EGameplayAbilityInstancingPolicy::InstancedPerActor));
		Test.TestFalse(
			TEXT("GA_Fire does not retrigger an already active instance"),
			FireDefaults->CanRetriggerInstancedAbility());
		Test.TestTrue(
			TEXT("GA_Fire is bound to Input.Fire"),
			FireDefaults->HasInputFireTag());
		return true;
	}

	bool TestWeaponExecutionBoundary(FAutomationTestBase& Test)
	{
		// 弹丸生成仍是 WeaponActor 的内部实现；Fire / FireProjectile 不能成为
		// 客户端可远程调用的 UFUNCTION，否则会绕过 GA_Fire 唯一入口。
		Test.TestNull(
			TEXT("Weapon Fire is not a remote-callable UFUNCTION"),
			AShooterWeapon::StaticClass()->FindFunctionByName(TEXT("Fire")));
		Test.TestNull(
			TEXT("Weapon FireProjectile is not a remote-callable UFUNCTION"),
			AShooterWeapon::StaticClass()->FindFunctionByName(TEXT("FireProjectile")));
		Test.TestNull(
			TEXT("Weapon StartFiring is not a remote-callable UFUNCTION"),
			AShooterWeapon::StaticClass()->FindFunctionByName(TEXT("StartFiring")));
		Test.TestNull(
			TEXT("Weapon StopFiring is not a remote-callable UFUNCTION"),
			AShooterWeapon::StaticClass()->FindFunctionByName(TEXT("StopFiring")));

		const AShooterWeapon* WeaponDefaults = GetDefault<AShooterWeapon>();
		if (!Test.TestNotNull(TEXT("AShooterWeapon has defaults"), WeaponDefaults))
		{
			return false;
		}

		Test.TestTrue(TEXT("WeaponActor replicates"), WeaponDefaults->GetIsReplicated());
		return true;
	}

	bool TestAmmoAuthorityContract(FAutomationTestBase& Test)
	{
		const UShooterGameplayAbility_Fire* FireDefaults =
			GetDefault<UShooterGameplayAbility_Fire>();
		if (!Test.TestNotNull(TEXT("GA_Fire has defaults"), FireDefaults))
		{
			return false;
		}

		// Ability 不得新增第二份 Ammo 权威：没有 Cost GE，弹药只由 WeaponActor
		// 调用 Inventory.ConsumeMagazineAmmo 修改。真实扣减由网络测试验证。
		Test.TestNull(
			TEXT("GA_Fire has no cost GameplayEffect"),
			FireDefaults->GetCostGameplayEffect());

		const AShooterPlayerState* PlayerStateDefaults = GetDefault<AShooterPlayerState>();
		if (Test.TestNotNull(TEXT("PlayerState has defaults"), PlayerStateDefaults))
		{
			Test.TestEqual(
				TEXT("PlayerState ASC counts zero active Fire abilities on CDO"),
				Cast<UShooterAbilitySystemComponent>(
					PlayerStateDefaults->GetAbilitySystemComponent())
						? Cast<UShooterAbilitySystemComponent>(
							PlayerStateDefaults->GetAbilitySystemComponent())
							->GetActiveAbilityCountForClass(
								PlayerStateDefaults->GetFireAbilityClass())
						: INDEX_NONE,
				0);
		}
		return true;
	}

	bool TestFullAutoScenarioConfiguration(FAutomationTestBase& Test)
	{
		const UClass* RifleClass = LoadClass<AShooterWeapon>(
			nullptr,
			TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C"));
		if (!Test.TestNotNull(TEXT("Rifle class can be loaded"), RifleClass))
		{
			return false;
		}

		const AShooterWeapon* RifleDefaults = RifleClass->GetDefaultObject<AShooterWeapon>();
		if (!Test.TestNotNull(TEXT("Rifle has defaults"), RifleDefaults))
		{
			return false;
		}

		const FBoolProperty* FullAutoProperty =
			FindFProperty<FBoolProperty>(RifleClass, TEXT("bFullAuto"));
		const FFloatProperty* RefireRateProperty =
			FindFProperty<FFloatProperty>(RifleClass, TEXT("RefireRate"));
		if (!Test.TestNotNull(TEXT("Rifle exposes bFullAuto"), FullAutoProperty) ||
			!Test.TestNotNull(TEXT("Rifle exposes RefireRate"), RefireRateProperty))
		{
			return false;
		}

		Test.TestTrue(
			TEXT("Rifle is full-auto for the release scenario"),
			FullAutoProperty->GetPropertyValue_InContainer(RifleDefaults));
		Test.TestTrue(
			TEXT("Rifle refire rate is positive"),
			RefireRateProperty->GetPropertyValue_InContainer(RifleDefaults) > 0.0f);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityFireServerOnlyTest,
	"ShootGame.Ability.Fire.ServerOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityFireServerOnlyTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFireBehaviorAutomationTests;
	return TestServerOnlyContract(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityFireSingleActivationTest,
	"ShootGame.Ability.Fire.SingleActivation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityFireSingleActivationTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFireBehaviorAutomationTests;

	// 静态契约：InstancedPerActor + bRetriggerInstancedAbility=false。
	// 真实“一次按下只激活一个 GA_Fire / 只生成一颗弹丸”由网络测试协调器验证。
	return TestServerOnlyContract(*this) && TestWeaponExecutionBoundary(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityFireSingleProjectileTest,
	"ShootGame.Ability.Fire.SingleProjectile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityFireSingleProjectileTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFireBehaviorAutomationTests;

	// 静态契约：弹丸实现留在 WeaponActor，且无法通过 UFUNCTION 绕过 GA 入口。
	// 真实 Projectile 数量断言由网络测试协调器的 ProjectileSpawnCount 完成。
	return TestServerOnlyContract(*this) && TestWeaponExecutionBoundary(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityFireAmmoConsumeTest,
	"ShootGame.Ability.Fire.AmmoConsume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityFireAmmoConsumeTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFireBehaviorAutomationTests;
	return TestServerOnlyContract(*this) && TestAmmoAuthorityContract(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityFireFullAutoReleaseTest,
	"ShootGame.Ability.Fire.FullAutoRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityFireFullAutoReleaseTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFireBehaviorAutomationTests;

	// 静态契约：网络测试所用步枪确实是全自动且 RefireRate 有效；
	// 保持期间单活动 Ability 与释放后无残留由网络测试协调器验证。
	return TestServerOnlyContract(*this) && TestFullAutoScenarioConfiguration(*this);
}

#endif
