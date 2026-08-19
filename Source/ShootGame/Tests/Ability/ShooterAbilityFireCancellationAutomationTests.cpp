// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AbilitySystemInterface.h"
#include "ShooterCharacter.h"
#include "ShooterGameplayAbility_Fire.h"
#include "ShooterGameplayTags.h"
#include "ShooterNPC.h"
#include "ShooterWeapon.h"
#include "ShooterWeaponHolder.h"
#include "UObject/UnrealType.h"

namespace ShooterAbilityFireCancellationAutomationTests
{
	bool TestCommonCancellationContracts(FAutomationTestBase& Test)
	{
		const UShooterGameplayAbility_Fire* FireDefaults =
			GetDefault<UShooterGameplayAbility_Fire>();
		if (!Test.TestNotNull(TEXT("GA_Fire has defaults"), FireDefaults))
		{
			return false;
		}

		Test.TestTrue(
			TEXT("State.Dead blocks GA_Fire activation"),
			FireDefaults->IsBlockedByStateDead());
		Test.TestTrue(
			TEXT("Input.Fire tag is valid"),
			ShooterGameplayTags::Input_Fire.GetTag().IsValid());
		Test.TestTrue(
			TEXT("State.Dead tag is valid"),
			ShooterGameplayTags::State_Dead.GetTag().IsValid());
		Test.TestTrue(
			TEXT("State.Firing tag is valid"),
			ShooterGameplayTags::State_Firing.GetTag().IsValid());

		// NPC 的意图入口是普通 C++ 方法，不是可被客户端远程调用的 UFUNCTION。
		Test.TestNull(
			TEXT("NPC StartShooting is not a remote-callable UFUNCTION"),
			AShooterNPC::StaticClass()->FindFunctionByName(TEXT("StartShooting")));
		Test.TestNull(
			TEXT("NPC StopShooting is not a remote-callable UFUNCTION"),
			AShooterNPC::StaticClass()->FindFunctionByName(TEXT("StopShooting")));
		return true;
	}

	bool TestWeaponHolderReadOnlyInterface(FAutomationTestBase& Test)
	{
		Test.TestTrue(
			TEXT("ShooterCharacter implements IShooterWeaponHolder"),
			AShooterCharacter::StaticClass()->ImplementsInterface(
				UShooterWeaponHolder::StaticClass()));
		Test.TestTrue(
			TEXT("ShooterNPC implements IShooterWeaponHolder"),
			AShooterNPC::StaticClass()->ImplementsInterface(
				UShooterWeaponHolder::StaticClass()));

		const AShooterCharacter* CharacterDefaults = GetDefault<AShooterCharacter>();
		if (Test.TestNotNull(TEXT("Character has defaults"), CharacterDefaults))
		{
			Test.TestNull(
				TEXT("Character CDO has no current weapon"),
				CharacterDefaults->GetCurrentWeapon());
		}

		const AShooterNPC* NpcDefaults = GetDefault<AShooterNPC>();
		if (Test.TestNotNull(TEXT("NPC has defaults"), NpcDefaults))
		{
			Test.TestNull(
				TEXT("NPC CDO has no current weapon"),
				NpcDefaults->GetCurrentWeapon());
			Test.TestFalse(TEXT("NPC CDO is not dead"), NpcDefaults->IsDead());
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityFireRejectDeadTest,
	"ShootGame.Ability.Fire.Reject.Dead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityFireRejectDeadTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFireCancellationAutomationTests;
	return TestCommonCancellationContracts(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityFireRejectNoWeaponTest,
	"ShootGame.Ability.Fire.Reject.NoWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityFireRejectNoWeaponTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFireCancellationAutomationTests;
	return TestCommonCancellationContracts(*this) &&
		TestWeaponHolderReadOnlyInterface(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityFireRejectNoAmmoTest,
	"ShootGame.Ability.Fire.Reject.NoAmmo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityFireRejectNoAmmoTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFireCancellationAutomationTests;
	return TestCommonCancellationContracts(*this) &&
		TestWeaponHolderReadOnlyInterface(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityFireCancelSwitchWeaponTest,
	"ShootGame.Ability.Fire.Cancel.SwitchWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityFireCancelSwitchWeaponTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFireCancellationAutomationTests;
	return TestCommonCancellationContracts(*this) &&
		TestWeaponHolderReadOnlyInterface(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityFireCancelDeathTest,
	"ShootGame.Ability.Fire.Cancel.Death",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityFireCancelDeathTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFireCancellationAutomationTests;
	return TestCommonCancellationContracts(*this) &&
		TestWeaponHolderReadOnlyInterface(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityFireCancelDisconnectTest,
	"ShootGame.Ability.Fire.Cancel.Disconnect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityFireCancelDisconnectTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFireCancellationAutomationTests;
	return TestCommonCancellationContracts(*this) &&
		TestWeaponHolderReadOnlyInterface(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityFireNPCTest,
	"ShootGame.Ability.Fire.NPC",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityFireNPCTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFireCancellationAutomationTests;
	return TestCommonCancellationContracts(*this) &&
		TestWeaponHolderReadOnlyInterface(*this);
}

#endif
