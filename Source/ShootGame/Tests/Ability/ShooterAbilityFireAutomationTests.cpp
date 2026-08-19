// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "ShooterAbilitySystemComponent.h"
#include "ShooterCharacter.h"
#include "ShooterGameplayAbility.h"
#include "ShooterGameplayAbility_Fire.h"
#include "ShooterGameplayTags.h"
#include "ShooterNPC.h"
#include "ShooterPlayerState.h"

namespace ShooterAbilityAutomationTests
{
	bool TestFireAbilityConfiguration(
		FAutomationTestBase& Test,
		const UShooterGameplayAbility_Fire* AbilityDefaults)
	{
		if (!Test.TestNotNull(TEXT("GA_Fire has defaults"), AbilityDefaults))
		{
			return false;
		}

		Test.TestEqual(
			TEXT("GA_Fire uses InstancedPerActor"),
			static_cast<int32>(AbilityDefaults->GetInstancingPolicy()),
			static_cast<int32>(EGameplayAbilityInstancingPolicy::InstancedPerActor));
		Test.TestEqual(
			TEXT("GA_Fire uses ServerOnly net execution"),
			static_cast<int32>(AbilityDefaults->GetNetExecutionPolicy()),
			static_cast<int32>(EGameplayAbilityNetExecutionPolicy::ServerOnly));
		Test.TestTrue(
			TEXT("GA_Fire AbilityTags contains Input.Fire"),
			AbilityDefaults->HasInputFireTag());
		Test.TestTrue(
			TEXT("GA_Fire blocked by State.Dead"),
			AbilityDefaults->IsBlockedByStateDead());
		Test.TestTrue(
			TEXT("GA_Fire owns State.Firing while active"),
			AbilityDefaults->OwnsStateFiringWhileActive());
		return true;
	}

	bool TestAbilityHost(
		FAutomationTestBase& Test,
		const TCHAR* HostName,
		const UObject* HostDefaults,
		const IAbilitySystemInterface* AbilitySystemInterface,
		const UObject* ExpectedFireAbilityClass)
	{
		if (!Test.TestNotNull(
			FString::Printf(TEXT("%s has defaults"), HostName),
			HostDefaults))
		{
			return false;
		}

		if (!Test.TestNotNull(
			FString::Printf(TEXT("%s implements IAbilitySystemInterface"), HostName),
			AbilitySystemInterface))
		{
			return false;
		}

		const UAbilitySystemComponent* AbilitySystemComponent =
			AbilitySystemInterface ? AbilitySystemInterface->GetAbilitySystemComponent() : nullptr;
		if (!Test.TestNotNull(
			FString::Printf(TEXT("%s has an ASC"), HostName),
			AbilitySystemComponent))
		{
			return false;
		}

		Test.TestTrue(
			FString::Printf(TEXT("%s ASC uses ShooterAbilitySystemComponent"), HostName),
			AbilitySystemComponent && AbilitySystemComponent->IsA<UShooterAbilitySystemComponent>());
		Test.TestTrue(
			FString::Printf(TEXT("%s default FireAbilityClass is GA_Fire"), HostName),
			ExpectedFireAbilityClass == UShooterGameplayAbility_Fire::StaticClass());
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityFireGrantPlayerTest,
	"ShootGame.Ability.Fire.Grant.Player",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityFireGrantPlayerTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityAutomationTests;

	const AShooterPlayerState* PlayerStateDefaults = GetDefault<AShooterPlayerState>();
	const IAbilitySystemInterface* AbilitySystemInterface =
		Cast<IAbilitySystemInterface>(PlayerStateDefaults);
	const bool bHostOk = TestAbilityHost(
		*this,
		TEXT("ShooterPlayerState"),
		PlayerStateDefaults,
		AbilitySystemInterface,
		PlayerStateDefaults ? PlayerStateDefaults->GetFireAbilityClass() : nullptr);
	const bool bAbilityOk = TestFireAbilityConfiguration(
		*this,
		GetDefault<UShooterGameplayAbility_Fire>());

	// 授予发生在服务器 PostInitializeComponents / BeginPlay 生命周期内，
	// CDO 本身不能携带任何 Fire Spec；实际“有且只有一个 Spec”由网络测试协调器验证。
	this->TestEqual(
		TEXT("PlayerState CDO has no Fire Spec before lifecycle grant"),
		PlayerStateDefaults ? PlayerStateDefaults->GetFireAbilitySpecCount() : INDEX_NONE,
		0);
	return bHostOk && bAbilityOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityFireGrantNPCTest,
	"ShootGame.Ability.Fire.Grant.NPC",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityFireGrantNPCTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityAutomationTests;

	const AShooterNPC* NpcDefaults = GetDefault<AShooterNPC>();
	const IAbilitySystemInterface* AbilitySystemInterface =
		Cast<IAbilitySystemInterface>(NpcDefaults);
	const bool bHostOk = TestAbilityHost(
		*this,
		TEXT("ShooterNPC"),
		NpcDefaults,
		AbilitySystemInterface,
		NpcDefaults ? NpcDefaults->GetFireAbilityClass() : nullptr);
	const bool bAbilityOk = TestFireAbilityConfiguration(
		*this,
		GetDefault<UShooterGameplayAbility_Fire>());

	this->TestEqual(
		TEXT("NPC CDO has no Fire Spec before lifecycle grant"),
		NpcDefaults ? NpcDefaults->GetFireAbilitySpecCount() : INDEX_NONE,
		0);
	return bHostOk && bAbilityOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityFireGrantRespawnNoDuplicateTest,
	"ShootGame.Ability.Fire.Grant.RespawnNoDuplicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityFireGrantRespawnNoDuplicateTest::RunTest(const FString& Parameters)
{
	// 真实重生不重复授予由 ShooterNetworkTestCoordinator 在 Dedicated / Listen 中验证。
	// 本测试守住可静态检查的授予边界：
	// Fire Ability 类只挂在宿主配置上，授予入口属于服务器生命周期函数，CDO 不预置 Spec。
	const AShooterPlayerState* PlayerStateDefaults = GetDefault<AShooterPlayerState>();
	const AShooterNPC* NpcDefaults = GetDefault<AShooterNPC>();
	if (!this->TestNotNull(TEXT("PlayerState has defaults"), PlayerStateDefaults) ||
		!this->TestNotNull(TEXT("NPC has defaults"), NpcDefaults))
	{
		return false;
	}

	this->TestEqual(
		TEXT("PlayerState CDO carries no granted Fire Spec"),
		PlayerStateDefaults->GetFireAbilitySpecCount(),
		0);
	this->TestEqual(
		TEXT("NPC CDO carries no granted Fire Spec"),
		NpcDefaults->GetFireAbilitySpecCount(),
		0);

	const UShooterAbilitySystemComponent* AbilitySystemComponent =
		PlayerStateDefaults->GetAbilitySystemComponent()
			? Cast<UShooterAbilitySystemComponent>(PlayerStateDefaults->GetAbilitySystemComponent())
			: nullptr;
	if (this->TestNotNull(TEXT("PlayerState CDO ASC exists"), AbilitySystemComponent))
	{
		this->TestEqual(
			TEXT("ASC on CDO counts zero Fire Specs for configured class"),
			AbilitySystemComponent->GetAbilitySpecCountForClass(
				PlayerStateDefaults->GetFireAbilityClass()),
			0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityFireActorInfoTest,
	"ShootGame.Ability.Fire.ActorInfo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityFireActorInfoTest::RunTest(const FString& Parameters)
{
	const UShooterGameplayAbility* AbilityDefaults = GetDefault<UShooterGameplayAbility>();
	if (!this->TestNotNull(TEXT("ShooterGameplayAbility has defaults"), AbilityDefaults))
	{
		return false;
	}

	// ActorInfo 未初始化时，基类安全 Avatar 入口必须返回空且不崩溃；
	// 真实 Owner/Avatar 绑定由网络测试协调器在 Dedicated / Listen 中验证。
	this->TestNull(
		TEXT("Safe avatar getter returns null without ActorInfo"),
		AbilityDefaults->GetShooterAvatarActor());
	this->TestFalse(
		TEXT("Authority check is false without ActorInfo"),
		AbilityDefaults->IsAvatarAuthoritative());

	const AShooterCharacter* CharacterDefaults = GetDefault<AShooterCharacter>();
	if (this->TestNotNull(TEXT("ShooterCharacter has defaults"), CharacterDefaults))
	{
		this->TestNull(
			TEXT("Character CDO has no ASC without PlayerState"),
			CharacterDefaults->GetAbilitySystemComponent());
	}
	return true;
}

#endif
