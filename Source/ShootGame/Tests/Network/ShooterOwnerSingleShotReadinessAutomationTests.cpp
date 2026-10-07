// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/Abilities/ShooterGameplayAbility_Fire.h"
#include "AbilitySystem/ShooterAbilitySystemComponent.h"
#include "AbilitySystem/ShooterGameplayTags.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState/ShooterPlayerState.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "Tests/Equipment/ShooterWeaponPresentationTestTypes.h"
#include "Tests/Network/ShooterNetworkTestCoordinator.h"
#include "Tests/Weapon/ShooterWeaponTestTableTypes.h"
#include "Weapons/ShooterWeapon.h"

/**
 * 普通 Dedicated fixture 的 OwnerSingleShot 就绪判定证据。
 *
 * 背景（2026-10-07 取证，PlayerId=257 的全零报告）：失败不是服务器丢请求，而是客户端在
 * 「武器 Actor 已复制、这把武器的 GA_Fire Spec 还没复制」的窗口里按下了同帧 Press + Release：
 * 没有可解析的 Spec，Press edge 随帧末采集失效，既不激活也不重放，2 秒后按超时提交全零报告。
 *
 * 因此正向用例必须先证明「当前武器已拥有可解析且上下文匹配的 Fire Spec」才允许按下。
 * 本文件只验证这个就绪判定与它的输入后果，不改变任何生产输入语义：
 * 不新增 Press buffer、不改 PressedInputTags 生命周期、不伪造 Press、不重试 TryActivate。
 *
 * Spec 授予时机由测试直接控制（生产授予 / 撤销入口），不依赖真实复制延迟。
 */
namespace ShooterOwnerSingleShotReadinessAutomationTests
{
	UWorld* CreateReadinessTestWorld()
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

	void DestroyReadinessTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	/** 本机拥有者视图 + 可控制 Fire Spec 授予时机的夹具。 */
	struct FReadinessFixture
	{
		AShooterWeaponPresentationTestCharacter* Character = nullptr;
		AShooterPlayerState* PlayerState = nullptr;
		UShooterAbilitySystemComponent* AbilitySystemComponent = nullptr;
		UShooterInventoryComponent* Inventory = nullptr;
		UShooterEquipmentComponent* Equipment = nullptr;
	};

	FReadinessFixture CreateReadinessFixture(FAutomationTestBase& Test, UWorld* World)
	{
		FReadinessFixture Fixture;
		if (!World)
		{
			return Fixture;
		}

		APlayerController* PlayerController = World->SpawnActor<APlayerController>(FVector::ZeroVector,
			FRotator::ZeroRotator);
		AShooterWeaponPresentationTestCharacter* Character =
			World->SpawnActor<AShooterWeaponPresentationTestCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("readiness PlayerController spawned"), PlayerController) ||
			!Test.TestNotNull(TEXT("readiness character spawned"), Character))
		{
			return Fixture;
		}

		FActorSpawnParameters PlayerStateSpawnParameters;
		PlayerStateSpawnParameters.Owner = PlayerController;
		PlayerStateSpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AShooterPlayerState* PlayerState = World->SpawnActor<AShooterPlayerState>(AShooterPlayerState::StaticClass(),
			PlayerStateSpawnParameters);
		if (!Test.TestNotNull(TEXT("readiness PlayerState spawned"), PlayerState))
		{
			return Fixture;
		}

		PlayerController->DispatchBeginPlay();
		Character->DispatchBeginPlay();
		PlayerController->Possess(Character);
		PlayerController->PlayerState = PlayerState;
		Character->SetPlayerState(PlayerState);
		PlayerState->InitializeAbilityActorInfo(Character);

		UShooterAbilitySystemComponent* AbilitySystemComponent =
			Cast<UShooterAbilitySystemComponent>(Character->GetAbilitySystemComponent());
		if (!Test.TestNotNull(TEXT("readiness ASC resolved"), AbilitySystemComponent) ||
			!Test.TestTrue(TEXT("readiness ASC is a locally controlled player view"),
				AbilitySystemComponent->AbilityActorInfo.IsValid() &&
					AbilitySystemComponent->AbilityActorInfo->IsLocallyControlledPlayer()))
		{
			return Fixture;
		}

		Fixture.Character = Character;
		Fixture.PlayerState = PlayerState;
		Fixture.AbilitySystemComponent = AbilitySystemComponent;
		Fixture.Inventory = Character->GetInventoryComponent();
		Fixture.Equipment = Character->GetEquipmentComponent();
		Test.TestNotNull(TEXT("readiness inventory resolved"), Fixture.Inventory);
		Test.TestNotNull(TEXT("readiness equipment resolved"), Fixture.Equipment);
		return Fixture;
	}

	/**
	 * 授予并装备一把真实武器。
	 * Inventory.AddWeapon 会同时授予这把武器自己的 GA_Fire Spec（SourceObject = 该武器），
	 * 因此调用方可以随后用生产撤销入口 RemoveFireAbilityForWeapon 造出「武器在、Spec 不在」的窗口，
	 * 也可以在稍后的某一拍用 GrantFireAbilityForWeapon 让 Spec "晚到"。
	 */
	AShooterWeaponLifecycleTestWeapon* GrantAndEquipReadinessWeapon(FAutomationTestBase& Test, UWorld* World,
		const FReadinessFixture& Fixture)
	{
		if (!World || !Fixture.Inventory || !Fixture.Equipment)
		{
			return nullptr;
		}

		AShooterWeapon* Weapon = GrantTestWeapon(World, Fixture.Inventory, AShooterWeaponLifecycleTestWeapon::StaticClass());
		AShooterWeaponLifecycleTestWeapon* TestWeapon = Cast<AShooterWeaponLifecycleTestWeapon>(Weapon);
		if (!Test.TestNotNull(TEXT("readiness weapon granted"), TestWeapon))
		{
			return nullptr;
		}

		// 普通 Dedicated 的 Rifle 是全自动：就绪判定与连发语义无关，这里保持与夹具一致的形态。
		TestWeapon->SetFullAutoForTest(true);
		if (!Test.TestTrue(TEXT("readiness weapon equipped"), Fixture.Equipment->EquipWeapon(TestWeapon)))
		{
			return nullptr;
		}

		return TestWeapon;
	}

	/** 一次物理按下 + 松开：与 Character.DoStartFiring / DoStopFiring 完全相同的入口。 */
	void PressAndReleaseFire(const FReadinessFixture& Fixture)
	{
		Fixture.AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
		Fixture.AbilitySystemComponent->AbilityInputTagReleased(ShooterGameplayTags::Input_Fire);
	}

	/** 本机拥有者每帧一次的输入解释时点。 */
	void ProcessInput(const FReadinessFixture& Fixture)
	{
		Fixture.AbilitySystemComponent->ProcessAbilityInputForTest();
	}

	/** 重新打开"这一发之后的节拍窗口"；只复位节拍，不伪造任何输入。 */
	void ReopenCadence(AShooterWeapon* Weapon)
	{
		Weapon->SetLocalFireCooldownRemainingForAutomationTest(0.0f);
		Weapon->SetAuthorityRefireRemainingForAutomationTest(0.0f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterOwnerSingleShotReadinessWaitsForSpecTest,
	"ShootGame.Network.OwnerSingleShotReadiness.WaitsForPerWeaponFireSpec",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterOwnerSingleShotReadinessWaitsForSpecTest::RunTest(const FString& Parameters)
{
	using namespace ShooterOwnerSingleShotReadinessAutomationTests;

	// fixture readiness 测试：证明"武器在、Spec 不在"时正向用例不会按下，
	// 并且 Spec 出现后恰好发起一次原始 single-shot attempt（不重试、不重放、不延长窗口）。
	UWorld* World = CreateReadinessTestWorld();
	if (!TestNotNull(TEXT("readiness test world created"), World))
	{
		return false;
	}

	const FReadinessFixture Fixture = CreateReadinessFixture(*this, World);
	AShooterWeaponLifecycleTestWeapon* Weapon = GrantAndEquipReadinessWeapon(*this, World, Fixture);
	if (!Fixture.Character || !Fixture.PlayerState || !Weapon)
	{
		DestroyReadinessTestWorld(World);
		return false;
	}

	// 基线：AddWeapon 已为这把武器授予它自己的一份 Fire Spec，就绪判定此刻必须为真（判据不是恒假的）。
	const FGameplayAbilitySpec* GrantedSpec =
		AShooterNetworkTestCoordinator::ResolveOwnerFireSpecForSingleShot(Fixture.PlayerState, Weapon);
	if (!TestNotNull(TEXT("the granted weapon resolves its own Fire Spec"), GrantedSpec))
	{
		DestroyReadinessTestWorld(World);
		return false;
	}
	TestTrue(TEXT("the resolved Spec's SourceObject is the current weapon"), GrantedSpec->SourceObject.Get() == Weapon);
	TestTrue(TEXT("the equipped weapon is the current weapon"), Fixture.Equipment->GetCurrentWeaponActor() == Weapon);

	// ---- 阶段 A：取证窗口复现（武器仍是当前武器，这把武器的 Fire Spec 已被撤销）----
	Fixture.PlayerState->RemoveFireAbilityForWeapon(Weapon);
	TestTrue(TEXT("the weapon stays current while its Fire Spec is gone"),
		Fixture.Equipment->GetCurrentWeaponActor() == Weapon);
	TestNull(TEXT("readiness is false once the current weapon has no Fire Spec"),
		AShooterNetworkTestCoordinator::ResolveOwnerFireSpecForSingleShot(Fixture.PlayerState, Weapon));

	PressAndReleaseFire(Fixture);
	TestTrue(TEXT("the press edge of this frame is still observable before interpretation"),
		Fixture.AbilitySystemComponent->IsInputTagPressedThisFrame(ShooterGameplayTags::Input_Fire));
	ProcessInput(Fixture);
	TestEqual(TEXT("a press without a resolvable Spec never reaches the authority"),
		Weapon->GetAuthorityShotCountForAutomationTest(), 0);

	Fixture.PlayerState->GrantFireAbilityForWeapon(Weapon);
	ReopenCadence(Weapon);
	ProcessInput(Fixture);
	ProcessInput(Fixture);
	TestEqual(TEXT("the lost press is not replayed after the Spec arrives"),
		Weapon->GetAuthorityShotCountForAutomationTest(), 0);

	// ---- 阶段 B：就绪驱动的正向流程（未就绪的每一拍都不按，第一拍就绪时恰好按一次）----
	Fixture.PlayerState->RemoveFireAbilityForWeapon(Weapon);
	const int32 SpecArrivesAtPoll = 2;
	int32 NotReadyPolls = 0;
	int32 ReadyPressCount = 0;
	for (int32 Poll = 0; Poll < 6; ++Poll)
	{
		if (Poll == SpecArrivesAtPoll)
		{
			// Spec "晚到"：授予时机由测试显式控制，不依赖真实复制延迟。
			Fixture.PlayerState->GrantFireAbilityForWeapon(Weapon);
		}

		if (!AShooterNetworkTestCoordinator::ResolveOwnerFireSpecForSingleShot(Fixture.PlayerState, Weapon))
		{
			++NotReadyPolls;
			continue;
		}

		if (ReadyPressCount == 0)
		{
			ReopenCadence(Weapon);
			PressAndReleaseFire(Fixture);
			ProcessInput(Fixture);
			++ReadyPressCount;
		}
	}

	TestEqual(TEXT("every poll without a resolvable Fire Spec refuses to press"), NotReadyPolls, SpecArrivesAtPoll);
	TestEqual(TEXT("the ready poll issues exactly one original single-shot attempt"), ReadyPressCount, 1);
	TestEqual(TEXT("exactly one shot reaches the authority"), Weapon->GetAuthorityShotCountForAutomationTest(), 1);

	DestroyReadinessTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterOwnerSingleShotReadinessPerWeaponSpecTest,
	"ShootGame.Network.OwnerSingleShotReadiness.SpecIdentityTracksCurrentWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterOwnerSingleShotReadinessPerWeaponSpecTest::RunTest(const FString& Parameters)
{
	using namespace ShooterOwnerSingleShotReadinessAutomationTests;

	// per-Weapon Spec 架构下，"有任意一份 Fire Spec"不够：就绪必须跟着当前武器的那一份。
	UWorld* World = CreateReadinessTestWorld();
	if (!TestNotNull(TEXT("per-weapon readiness test world created"), World))
	{
		return false;
	}

	const FReadinessFixture Fixture = CreateReadinessFixture(*this, World);
	AShooterWeaponLifecycleTestWeapon* FirstWeapon = GrantAndEquipReadinessWeapon(*this, World, Fixture);
	if (!Fixture.PlayerState || !FirstWeapon)
	{
		DestroyReadinessTestWorld(World);
		return false;
	}

	AShooterWeapon* SecondWeaponActor = GrantTestWeapon(World, Fixture.Inventory, AShooterWeaponLifecycleTestWeapon::StaticClass());
	AShooterWeaponLifecycleTestWeapon* SecondWeapon = Cast<AShooterWeaponLifecycleTestWeapon>(SecondWeaponActor);
	if (!TestNotNull(TEXT("second readiness weapon granted"), SecondWeapon))
	{
		DestroyReadinessTestWorld(World);
		return false;
	}

	SecondWeapon->SetFullAutoForTest(true);
	TestTrue(TEXT("second readiness weapon equipped"), Fixture.Equipment->EquipWeapon(SecondWeapon));
	TestTrue(TEXT("the second weapon is current"), Fixture.Equipment->GetCurrentWeaponActor() == SecondWeapon);

	const FGameplayAbilitySpec* FirstSpec =
		AShooterNetworkTestCoordinator::ResolveOwnerFireSpecForSingleShot(Fixture.PlayerState, FirstWeapon);
	const FGameplayAbilitySpec* SecondSpec =
		AShooterNetworkTestCoordinator::ResolveOwnerFireSpecForSingleShot(Fixture.PlayerState, SecondWeapon);
	if (TestNotNull(TEXT("the first weapon resolves its own Spec"), FirstSpec) &&
		TestNotNull(TEXT("the second weapon resolves its own Spec"), SecondSpec))
	{
		TestTrue(TEXT("two weapons hold two distinct Fire Specs"), FirstSpec->Handle != SecondSpec->Handle);
		TestTrue(TEXT("the first Spec's SourceObject is the first weapon"), FirstSpec->SourceObject.Get() == FirstWeapon);
		TestTrue(TEXT("the second Spec's SourceObject is the second weapon"),
			SecondSpec->SourceObject.Get() == SecondWeapon);
	}

	// 撤销第一把武器的 Spec 不得影响第二把；切回第一把时，就绪必须按"当前武器"重新判定。
	Fixture.PlayerState->RemoveFireAbilityForWeapon(FirstWeapon);
	TestNull(TEXT("the first weapon loses readiness after its Spec is revoked"),
		AShooterNetworkTestCoordinator::ResolveOwnerFireSpecForSingleShot(Fixture.PlayerState, FirstWeapon));
	TestNotNull(TEXT("the second weapon keeps its own readiness"),
		AShooterNetworkTestCoordinator::ResolveOwnerFireSpecForSingleShot(Fixture.PlayerState, SecondWeapon));

	TestTrue(TEXT("switching back to the first weapon succeeds"), Fixture.Equipment->EquipWeapon(FirstWeapon));
	TestTrue(TEXT("the first weapon is current again"), Fixture.Equipment->GetCurrentWeaponActor() == FirstWeapon);
	TestNull(TEXT("readiness follows the current weapon, not any weapon"),
		AShooterNetworkTestCoordinator::ResolveOwnerFireSpecForSingleShot(Fixture.PlayerState,
			Fixture.Equipment->GetCurrentWeaponActor()));

	// 边界：没有当前武器 / 没有 PlayerState 时同样不得就绪。
	TestNull(TEXT("no PlayerState is never ready"),
		AShooterNetworkTestCoordinator::ResolveOwnerFireSpecForSingleShot(nullptr, FirstWeapon));
	TestNull(TEXT("no current weapon is never ready"),
		AShooterNetworkTestCoordinator::ResolveOwnerFireSpecForSingleShot(Fixture.PlayerState, nullptr));

	DestroyReadinessTestWorld(World);
	return true;
}

#endif
