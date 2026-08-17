// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "ShooterNPC.h"
#include "ShooterPlayerState.h"

namespace ShooterGASAutomationTests
{
	bool TestAbilitySystemHost(
		FAutomationTestBase& Test,
		const TCHAR* ClassName,
		UClass* Class,
		EGameplayEffectReplicationMode ExpectedReplicationMode)
	{
		if (!Test.TestNotNull(
			FString::Printf(TEXT("%s class exists"), ClassName),
			Class))
		{
			return false;
		}

		if (!Test.TestTrue(
			FString::Printf(TEXT("%s implements IAbilitySystemInterface"), ClassName),
			Class->ImplementsInterface(UAbilitySystemInterface::StaticClass())))
		{
			return false;
		}

		const AActor* Defaults = Class->GetDefaultObject<AActor>();
		if (!Test.TestNotNull(
			FString::Printf(TEXT("%s has defaults"), ClassName),
			Defaults))
		{
			return false;
		}

		const IAbilitySystemInterface* AbilitySystemInterface =
			Cast<IAbilitySystemInterface>(Defaults);
		UAbilitySystemComponent* AbilitySystemComponent = AbilitySystemInterface
			? AbilitySystemInterface->GetAbilitySystemComponent()
			: nullptr;
		if (!Test.TestNotNull(
			FString::Printf(TEXT("%s default ASC exists"), ClassName),
			AbilitySystemComponent))
		{
			return false;
		}

		Test.TestTrue(
			FString::Printf(TEXT("%s default ASC replicates"), ClassName),
			AbilitySystemComponent->GetIsReplicated());
		Test.TestEqual(
			FString::Printf(TEXT("%s default ASC replication mode"), ClassName),
			static_cast<int32>(AbilitySystemComponent->ReplicationMode),
			static_cast<int32>(ExpectedReplicationMode));

		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterGasConfigurationTest,
	"ShootGame.GAS.Configuration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterGasConfigurationTest::RunTest(const FString& Parameters)
{
	using namespace ShooterGASAutomationTests;

	bool bSucceeded = true;
	bSucceeded &= TestAbilitySystemHost(
		*this,
		TEXT("ShooterPlayerState"),
		AShooterPlayerState::StaticClass(),
		EGameplayEffectReplicationMode::Mixed);
	bSucceeded &= TestAbilitySystemHost(
		*this,
		TEXT("ShooterNPC"),
		AShooterNPC::StaticClass(),
		EGameplayEffectReplicationMode::Minimal);

	return bSucceeded;
}

#endif
