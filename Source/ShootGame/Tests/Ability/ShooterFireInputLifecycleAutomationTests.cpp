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
#include "Tests/Weapon/ShooterWeaponTestTableTypes.h"
#include "Weapons/ShooterWeapon.h"

/**
 * Fire 输入意图在「多份 Fire Spec + 切枪」下的 Press / Held / Release 生命周期证据。
 *
 * 全部用例都走生产输入链：能力入口 Character.DoStartFiring / DoStopFiring → ASC 的采集回调
 * → ProcessAbilityInput → Spec 解析 → 真实 GA_Fire 激活。测试世界是 Standalone，
 * 因此"一次 Activation = 一发权威 Shot"可以直接用武器上的权威 Shot 计数观察。
 *
 * 本轮要证明的语义：
 * - 采集先于 Spec 解析：没有可响应的 Spec 时，物理输入事实仍然被记录；
 * - 按住真值属于输入层（Held Input Tag），不属于"哪一份 Spec 曾经收到过 Press"；
 * - Held intent 可以迁移到切枪后的全自动武器，Press edge 不会迁移成新的半自动 Shot；
 * - 物理 Release 全局终止 Fire 意图，并且不给任何 Fire Spec 留下 InputPressed 残留。
 */
namespace ShooterFireInputLifecycleAutomationTests
{
	UWorld* CreateFireInputTestWorld()
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

	void DestroyFireInputTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	/** 本机拥有者视图 + 可装备武器的输入夹具。 */
	struct FFireInputFixture
	{
		AShooterWeaponPresentationTestCharacter* Character = nullptr;
		UShooterAbilitySystemComponent* AbilitySystemComponent = nullptr;
		UShooterInventoryComponent* Inventory = nullptr;
		UShooterEquipmentComponent* Equipment = nullptr;
	};

	/**
	 * 建立本机拥有者视图。
	 * ASC 的 ActorInfo 从 OwnerActor 的所有者链解析 PlayerController，
	 * 因此 PlayerState 必须由本机 PlayerController 拥有，才能进入输入解释路径。
	 */
	FFireInputFixture CreateFireInputFixture(FAutomationTestBase& Test, UWorld* World)
	{
		FFireInputFixture Fixture;
		if (!World)
		{
			return Fixture;
		}

		APlayerController* PlayerController = World->SpawnActor<APlayerController>(FVector::ZeroVector,
			FRotator::ZeroRotator);
		AShooterWeaponPresentationTestCharacter* Character =
			World->SpawnActor<AShooterWeaponPresentationTestCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("fire input PlayerController spawned"), PlayerController) ||
			!Test.TestNotNull(TEXT("fire input character spawned"), Character))
		{
			return Fixture;
		}

		FActorSpawnParameters PlayerStateSpawnParameters;
		PlayerStateSpawnParameters.Owner = PlayerController;
		PlayerStateSpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AShooterPlayerState* PlayerState = World->SpawnActor<AShooterPlayerState>(
			AShooterPlayerState::StaticClass(), PlayerStateSpawnParameters);
		if (!Test.TestNotNull(TEXT("fire input PlayerState spawned"), PlayerState))
		{
			return Fixture;
		}

		PlayerController->DispatchBeginPlay();
		Character->DispatchBeginPlay();
		PlayerController->Possess(Character);
		PlayerController->PlayerState = PlayerState;
		Character->SetPlayerState(PlayerState);
		PlayerState->InitializeAbilityActorInfo(Character);

		UShooterAbilitySystemComponent* AbilitySystemComponent = Cast<UShooterAbilitySystemComponent>(
			Character->GetAbilitySystemComponent());
		if (!Test.TestNotNull(TEXT("fire input ASC resolved"), AbilitySystemComponent) ||
			!Test.TestTrue(TEXT("fire input ASC is a locally controlled player view"),
				AbilitySystemComponent->AbilityActorInfo.IsValid() &&
				AbilitySystemComponent->AbilityActorInfo->IsLocallyControlledPlayer()))
		{
			return Fixture;
		}

		Fixture.Character = Character;
		Fixture.AbilitySystemComponent = AbilitySystemComponent;
		Fixture.Inventory = Character->GetInventoryComponent();
		Fixture.Equipment = Character->GetEquipmentComponent();
		Test.TestNotNull(TEXT("fire input inventory resolved"), Fixture.Inventory);
		Test.TestNotNull(TEXT("fire input equipment resolved"), Fixture.Equipment);
		return Fixture;
	}

	/**
	 * 授予并装备一把真实武器。
	 * Inventory.AddWeapon 同时为该武器授予它自己的一份 GA_Fire Spec（SourceObject = 该武器）。
	 */
	AShooterWeaponLifecycleTestWeapon* GrantAndEquipWeapon(FAutomationTestBase& Test, UWorld* World,
		const FFireInputFixture& Fixture, bool bFullAuto)
	{
		if (!World || !Fixture.Inventory || !Fixture.Equipment)
		{
			return nullptr;
		}

		AShooterWeapon* Weapon = GrantTestWeapon(World, Fixture.Inventory, AShooterWeaponLifecycleTestWeapon::StaticClass());
		if (!Test.TestNotNull(TEXT("fire input weapon granted"), Weapon))
		{
			return nullptr;
		}

		AShooterWeaponLifecycleTestWeapon* TestWeapon = Cast<AShooterWeaponLifecycleTestWeapon>(Weapon);
		if (!Test.TestNotNull(TEXT("fire input weapon is the lifecycle test type"), TestWeapon))
		{
			return nullptr;
		}

		// 连发语义由测试显式设定：不修改任何正式武器配置。
		TestWeapon->SetFullAutoForTest(bFullAuto);

		if (!Test.TestTrue(TEXT("fire input weapon equipped"), Fixture.Equipment->EquipWeapon(TestWeapon)))
		{
			return nullptr;
		}

		return TestWeapon;
	}

	/** 一次物理按下：与 Character.DoStartFiring 完全相同的入口。 */
	void PressFire(const FFireInputFixture& Fixture)
	{
		Fixture.AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	}

	/** 一次物理松开：与 Character.DoStopFiring 完全相同的入口。 */
	void ReleaseFire(const FFireInputFixture& Fixture)
	{
		Fixture.AbilitySystemComponent->AbilityInputTagReleased(ShooterGameplayTags::Input_Fire);
	}

	/** 本机拥有者每帧一次的输入解释时点。 */
	void ProcessInput(const FFireInputFixture& Fixture)
	{
		Fixture.AbilitySystemComponent->ProcessAbilityInputForTest();
	}

	/** 仍然认为"按着"的 Fire Spec 数量：> 0 表示留下了 stale InputPressed 残留。 */
	int32 CountFireSpecsHoldingInput(const UShooterAbilitySystemComponent* AbilitySystemComponent)
	{
		int32 Count = 0;
		for (const FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
		{
			if (Spec.InputPressed && Spec.Ability && Spec.Ability->IsA<UShooterGameplayAbility_Fire>())
			{
				++Count;
			}
		}

		return Count;
	}

	/**
	 * 重新打开"这一发之后的节拍窗口"。
	 * 本轮验证的是输入意图而不是射速，因此每次期望新的一发之前只复位节拍，不伪造任何输入。
	 */
	void ReopenCadence(AShooterWeapon* Weapon)
	{
		Weapon->SetLocalFireCooldownRemainingForAutomationTest(0.0f);
		Weapon->SetAuthorityRefireRemainingForAutomationTest(0.0f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFireInputSwitchSemiToFullAutoTest,
	"ShootGame.Ability.FireInput.SwitchSemiToFullAutoKeepsHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFireInputSwitchSemiToFullAutoTest::RunTest(const FString& Parameters)
{
	using namespace ShooterFireInputLifecycleAutomationTests;

	// 组合 A：按住半自动 A 的 Fire → 切到全自动 B → 不松键。
	// 期望：A 只产生原 Press 对应的那一枪；B 只凭 Held intent 就开始产生独立 Activation。
	UWorld* World = CreateFireInputTestWorld();
	if (!TestNotNull(TEXT("fire input test world created"), World))
	{
		return false;
	}

	const FFireInputFixture Fixture = CreateFireInputFixture(*this, World);
	AShooterWeaponLifecycleTestWeapon* SemiWeapon = GrantAndEquipWeapon(*this, World, Fixture, /*bFullAuto*/ false);
	AShooterWeaponLifecycleTestWeapon* FullAutoWeapon = GrantAndEquipWeapon(*this, World, Fixture, /*bFullAuto*/ true);
	if (!Fixture.Character || !SemiWeapon || !FullAutoWeapon)
	{
		DestroyFireInputTestWorld(World);
		return false;
	}

	// 先切回半自动：本次切换才是待验证的 A → B 方向。
	TestTrue(TEXT("semi-auto weapon becomes current before the switch"), Fixture.Equipment->EquipWeapon(SemiWeapon));

	PressFire(Fixture);
	ProcessInput(Fixture);

	TestEqual(TEXT("the press edge fires the semi-auto weapon exactly once"),
		SemiWeapon->GetAuthorityShotCountForAutomationTest(), 1);
	TestTrue(TEXT("the held intent is recorded while the key stays down"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Fire));

	// 按住不松直接切枪：新武器从未收到过 Press，只有 Held intent 迁移过来。
	TestTrue(TEXT("the full-auto weapon becomes current while the key is still down"),
		Fixture.Equipment->EquipWeapon(FullAutoWeapon));

	ProcessInput(Fixture);
	TestEqual(TEXT("the full-auto weapon starts from the migrated held intent"),
		FullAutoWeapon->GetAuthorityShotCountForAutomationTest(), 1);

	// 第二发只重开节拍，不产生任何新的 Press。
	ReopenCadence(FullAutoWeapon);
	ProcessInput(Fixture);
	TestEqual(TEXT("the full-auto weapon keeps producing independent activations while held"),
		FullAutoWeapon->GetAuthorityShotCountForAutomationTest(), 2);
	TestEqual(TEXT("the old weapon never fires again after the switch"),
		SemiWeapon->GetAuthorityShotCountForAutomationTest(), 1);

	// 松开即终止：Held 语义不允许在松手之后继续产生新的一发。
	ReleaseFire(Fixture);
	ProcessInput(Fixture);
	ReopenCadence(FullAutoWeapon);
	ProcessInput(Fixture);
	TestEqual(TEXT("a released held intent stops the full-auto weapon"),
		FullAutoWeapon->GetAuthorityShotCountForAutomationTest(), 2);

	DestroyFireInputTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFireInputSwitchFullAutoToFullAutoTest,
	"ShootGame.Ability.FireInput.SwitchFullAutoToFullAutoKeepsHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFireInputSwitchFullAutoToFullAutoTest::RunTest(const FString& Parameters)
{
	using namespace ShooterFireInputLifecycleAutomationTests;

	// 组合 B：一直按住，切到另一把全自动。
	// 期望：旧武器停止；新武器继续产生独立 Activation；不存在"必须重新按一次"的死状态。
	UWorld* World = CreateFireInputTestWorld();
	if (!TestNotNull(TEXT("fire input test world created"), World))
	{
		return false;
	}

	const FFireInputFixture Fixture = CreateFireInputFixture(*this, World);
	AShooterWeaponLifecycleTestWeapon* FirstWeapon = GrantAndEquipWeapon(*this, World, Fixture, /*bFullAuto*/ true);
	AShooterWeaponLifecycleTestWeapon* SecondWeapon = GrantAndEquipWeapon(*this, World, Fixture, /*bFullAuto*/ true);
	if (!Fixture.Character || !FirstWeapon || !SecondWeapon)
	{
		DestroyFireInputTestWorld(World);
		return false;
	}

	TestTrue(TEXT("first full-auto weapon becomes current before the switch"),
		Fixture.Equipment->EquipWeapon(FirstWeapon));

	PressFire(Fixture);
	ProcessInput(Fixture);
	TestEqual(TEXT("the press edge fires the first full-auto weapon"),
		FirstWeapon->GetAuthorityShotCountForAutomationTest(), 1);

	TestTrue(TEXT("the second full-auto weapon becomes current while the key is still down"),
		Fixture.Equipment->EquipWeapon(SecondWeapon));

	ProcessInput(Fixture);
	TestEqual(TEXT("the second full-auto weapon continues without a new press"),
		SecondWeapon->GetAuthorityShotCountForAutomationTest(), 1);

	ReopenCadence(SecondWeapon);
	ProcessInput(Fixture);
	TestEqual(TEXT("the second full-auto weapon keeps producing independent activations"),
		SecondWeapon->GetAuthorityShotCountForAutomationTest(), 2);
	TestEqual(TEXT("the first weapon stops producing shots after the switch"),
		FirstWeapon->GetAuthorityShotCountForAutomationTest(), 1);

	DestroyFireInputTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFireInputSameFrameClickTest,
	"ShootGame.Ability.FireInput.SameFrameClickFormsOneActionBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFireInputSameFrameClickTest::RunTest(const FString& Parameters)
{
	using namespace ShooterFireInputLifecycleAutomationTests;

	// 按下与松开落在同一帧的"点击"：全自动武器同样必须形成恰好一次动作边界。
	// 这是真实输入形态之一（网络夹具的一次点击就是这种形态），
	// 因此输入意图的判定不能只看"解释时点是否仍按住"。
	UWorld* World = CreateFireInputTestWorld();
	if (!TestNotNull(TEXT("fire input test world created"), World))
	{
		return false;
	}

	const FFireInputFixture Fixture = CreateFireInputFixture(*this, World);
	AShooterWeaponLifecycleTestWeapon* FullAutoWeapon = GrantAndEquipWeapon(*this, World, Fixture, /*bFullAuto*/ true);
	if (!Fixture.Character || !FullAutoWeapon)
	{
		DestroyFireInputTestWorld(World);
		return false;
	}

	// 同一帧内按下 + 松开：帧末采集里 Held 已经为空，但那一次 Press edge 仍然是真实的输入事实。
	PressFire(Fixture);
	ReleaseFire(Fixture);
	TestFalse(TEXT("the release already cleared the held collection before interpretation"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Fire));
	TestTrue(TEXT("the press edge of this frame is still observable"),
		Fixture.AbilitySystemComponent->IsInputTagPressedThisFrame(ShooterGameplayTags::Input_Fire));

	ProcessInput(Fixture);
	TestEqual(TEXT("a same-frame click forms exactly one full-auto action boundary"),
		FullAutoWeapon->GetAuthorityShotCountForAutomationTest(), 1);

	// 之后既没有按住也没有按下沿：不得再开一枪。
	ReopenCadence(FullAutoWeapon);
	ProcessInput(Fixture);
	ProcessInput(Fixture);
	TestEqual(TEXT("a same-frame click never turns into a held burst"),
		FullAutoWeapon->GetAuthorityShotCountForAutomationTest(), 1);

	// 真正的按住仍然照常连续响应。
	const int32 ShotsBeforeHold = FullAutoWeapon->GetAuthorityShotCountForAutomationTest();
	PressFire(Fixture);
	ProcessInput(Fixture);
	TestEqual(TEXT("a real hold still fires"), FullAutoWeapon->GetAuthorityShotCountForAutomationTest(), ShotsBeforeHold + 1);

	ReopenCadence(FullAutoWeapon);
	ProcessInput(Fixture);
	TestEqual(TEXT("a real hold keeps firing on later frames"),
		FullAutoWeapon->GetAuthorityShotCountForAutomationTest(), ShotsBeforeHold + 2);

	ReleaseFire(Fixture);
	ProcessInput(Fixture);
	ReopenCadence(FullAutoWeapon);
	ProcessInput(Fixture);
	TestEqual(TEXT("the release stops the held burst"),
		FullAutoWeapon->GetAuthorityShotCountForAutomationTest(), ShotsBeforeHold + 2);

	DestroyFireInputTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFireInputSwitchFullAutoToSemiTest,
	"ShootGame.Ability.FireInput.SwitchFullAutoToSemiDoesNotFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFireInputSwitchFullAutoToSemiTest::RunTest(const FString& Parameters)
{
	using namespace ShooterFireInputLifecycleAutomationTests;

	// 组合 C：按住全自动切到半自动。
	// 期望：新半自动不自动开枪；Release 后重新 Press 恰好一枪。
	UWorld* World = CreateFireInputTestWorld();
	if (!TestNotNull(TEXT("fire input test world created"), World))
	{
		return false;
	}

	const FFireInputFixture Fixture = CreateFireInputFixture(*this, World);
	AShooterWeaponLifecycleTestWeapon* FullAutoWeapon = GrantAndEquipWeapon(*this, World, Fixture, /*bFullAuto*/ true);
	AShooterWeaponLifecycleTestWeapon* SemiWeapon = GrantAndEquipWeapon(*this, World, Fixture, /*bFullAuto*/ false);
	if (!Fixture.Character || !FullAutoWeapon || !SemiWeapon)
	{
		DestroyFireInputTestWorld(World);
		return false;
	}

	TestTrue(TEXT("full-auto weapon becomes current before the switch"),
		Fixture.Equipment->EquipWeapon(FullAutoWeapon));

	PressFire(Fixture);
	ProcessInput(Fixture);
	TestEqual(TEXT("the press edge fires the full-auto weapon"),
		FullAutoWeapon->GetAuthorityShotCountForAutomationTest(), 1);

	TestTrue(TEXT("the semi-auto weapon becomes current while the key is still down"),
		Fixture.Equipment->EquipWeapon(SemiWeapon));

	// Held 只能启动 WhileInputActive；半自动必须等一个新的 Press edge。
	ReopenCadence(SemiWeapon);
	ProcessInput(Fixture);
	ProcessInput(Fixture);
	ProcessInput(Fixture);
	TestEqual(TEXT("a held intent never fires a semi-auto weapon"),
		SemiWeapon->GetAuthorityShotCountForAutomationTest(), 0);

	ReleaseFire(Fixture);
	ProcessInput(Fixture);
	ReopenCadence(SemiWeapon);
	ProcessInput(Fixture);
	TestEqual(TEXT("releasing alone does not fire a semi-auto weapon"),
		SemiWeapon->GetAuthorityShotCountForAutomationTest(), 0);

	PressFire(Fixture);
	ProcessInput(Fixture);
	TestEqual(TEXT("a new press edge fires the semi-auto weapon exactly once"),
		SemiWeapon->GetAuthorityShotCountForAutomationTest(), 1);

	ReopenCadence(SemiWeapon);
	ProcessInput(Fixture);
	ProcessInput(Fixture);
	TestEqual(TEXT("the press edge is not replayed on later frames"),
		SemiWeapon->GetAuthorityShotCountForAutomationTest(), 1);
	TestEqual(TEXT("the old full-auto weapon never fires after the switch"),
		FullAutoWeapon->GetAuthorityShotCountForAutomationTest(), 1);

	DestroyFireInputTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFireInputSwitchSemiToSemiTest,
	"ShootGame.Ability.FireInput.SwitchSemiToSemiDoesNotFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFireInputSwitchSemiToSemiTest::RunTest(const FString& Parameters)
{
	using namespace ShooterFireInputLifecycleAutomationTests;

	// 组合 D：按住半自动切到另一把半自动。
	// 期望：新半自动不因为旧的 Held 开枪；Release → Press 恰好一枪。
	UWorld* World = CreateFireInputTestWorld();
	if (!TestNotNull(TEXT("fire input test world created"), World))
	{
		return false;
	}

	const FFireInputFixture Fixture = CreateFireInputFixture(*this, World);
	AShooterWeaponLifecycleTestWeapon* FirstWeapon = GrantAndEquipWeapon(*this, World, Fixture, /*bFullAuto*/ false);
	AShooterWeaponLifecycleTestWeapon* SecondWeapon = GrantAndEquipWeapon(*this, World, Fixture, /*bFullAuto*/ false);
	if (!Fixture.Character || !FirstWeapon || !SecondWeapon)
	{
		DestroyFireInputTestWorld(World);
		return false;
	}

	TestTrue(TEXT("first semi-auto weapon becomes current before the switch"),
		Fixture.Equipment->EquipWeapon(FirstWeapon));

	PressFire(Fixture);
	ProcessInput(Fixture);
	TestEqual(TEXT("the press edge fires the first semi-auto weapon"),
		FirstWeapon->GetAuthorityShotCountForAutomationTest(), 1);

	TestTrue(TEXT("the second semi-auto weapon becomes current while the key is still down"),
		Fixture.Equipment->EquipWeapon(SecondWeapon));

	ReopenCadence(SecondWeapon);
	ProcessInput(Fixture);
	ProcessInput(Fixture);
	TestEqual(TEXT("a held intent never fires the second semi-auto weapon"),
		SecondWeapon->GetAuthorityShotCountForAutomationTest(), 0);

	ReleaseFire(Fixture);
	ProcessInput(Fixture);
	PressFire(Fixture);
	ProcessInput(Fixture);
	TestEqual(TEXT("release and press fires the second semi-auto weapon exactly once"),
		SecondWeapon->GetAuthorityShotCountForAutomationTest(), 1);
	TestEqual(TEXT("the first semi-auto weapon never fires after the switch"),
		FirstWeapon->GetAuthorityShotCountForAutomationTest(), 1);

	DestroyFireInputTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFireInputReleaseClearsCrossWeaponMirrorsTest,
	"ShootGame.Ability.FireInput.ReleaseClearsHeldIntentAcrossWeapons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFireInputReleaseClearsCrossWeaponMirrorsTest::RunTest(const FString& Parameters)
{
	using namespace ShooterFireInputLifecycleAutomationTests;

	// Press 全自动 A → 按住切到半自动 B → Release。
	// 期望：Held 采集被清空；**旧 A 的 Spec 也不留 InputPressed 残留**；
	// 切回 A 时在没有新 Press 的情况下不得自动开枪；新 Press 按正常策略工作。
	UWorld* World = CreateFireInputTestWorld();
	if (!TestNotNull(TEXT("fire input test world created"), World))
	{
		return false;
	}

	const FFireInputFixture Fixture = CreateFireInputFixture(*this, World);
	AShooterWeaponLifecycleTestWeapon* FullAutoWeapon = GrantAndEquipWeapon(*this, World, Fixture, /*bFullAuto*/ true);
	AShooterWeaponLifecycleTestWeapon* SemiWeapon = GrantAndEquipWeapon(*this, World, Fixture, /*bFullAuto*/ false);
	if (!Fixture.Character || !FullAutoWeapon || !SemiWeapon)
	{
		DestroyFireInputTestWorld(World);
		return false;
	}

	TestTrue(TEXT("full-auto weapon becomes current before the switch"),
		Fixture.Equipment->EquipWeapon(FullAutoWeapon));

	PressFire(Fixture);
	ProcessInput(Fixture);
	TestEqual(TEXT("the press edge fires the full-auto weapon"),
		FullAutoWeapon->GetAuthorityShotCountForAutomationTest(), 1);
	TestEqual(TEXT("the pressed spec carries the engine input mirror"),
		CountFireSpecsHoldingInput(Fixture.AbilitySystemComponent), 1);

	TestTrue(TEXT("the semi-auto weapon becomes current while the key is still down"),
		Fixture.Equipment->EquipWeapon(SemiWeapon));

	// 物理松开：与当前解析到哪一份 Spec 无关，必须全局终止 Fire 意图。
	ReleaseFire(Fixture);
	ProcessInput(Fixture);

	TestFalse(TEXT("the release clears the held intent"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Fire));
	TestEqual(TEXT("the held collection is empty after the release"),
		Fixture.AbilitySystemComponent->GetHeldInputTagCountForTest(), 0);
	TestEqual(TEXT("no fire spec keeps a stale input mirror after the release"),
		CountFireSpecsHoldingInput(Fixture.AbilitySystemComponent), 0);
	TestEqual(TEXT("the semi-auto weapon did not fire on release"),
		SemiWeapon->GetAuthorityShotCountForAutomationTest(), 0);

	// 切回旧武器：没有新 Press，就没有任何输入意图，不得自动开枪。
	TestTrue(TEXT("the full-auto weapon becomes current again"), Fixture.Equipment->EquipWeapon(FullAutoWeapon));
	ReopenCadence(FullAutoWeapon);
	ProcessInput(Fixture);
	ProcessInput(Fixture);
	TestEqual(TEXT("a weapon switched back to never fires without a new press"),
		FullAutoWeapon->GetAuthorityShotCountForAutomationTest(), 1);

	// 新的 Press edge 仍然按正常策略工作。
	PressFire(Fixture);
	ProcessInput(Fixture);
	TestEqual(TEXT("a new press edge fires the weapon again"),
		FullAutoWeapon->GetAuthorityShotCountForAutomationTest(), 2);
	ProcessInput(Fixture);
	TestEqual(TEXT("the second shot needs its own press edge or cadence"),
		FullAutoWeapon->GetAuthorityShotCountForAutomationTest(), 2);

	ReleaseFire(Fixture);
	DestroyFireInputTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFireInputPressWithoutSpecIsNotLostTest,
	"ShootGame.Ability.FireInput.PressWithoutMatchingSpecIsNotLost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFireInputPressWithoutSpecIsNotLostTest::RunTest(const FString& Parameters)
{
	using namespace ShooterFireInputLifecycleAutomationTests;

	// 没有可响应 Spec 时的采集语义：Press 仍然记录 Held；Release 仍然清除 Held；
	// 之后 Spec 才可解析时，全自动可以只凭残留的 Held intent 开始响应。
	UWorld* World = CreateFireInputTestWorld();
	if (!TestNotNull(TEXT("fire input test world created"), World))
	{
		return false;
	}

	const FFireInputFixture Fixture = CreateFireInputFixture(*this, World);
	if (!Fixture.Character)
	{
		DestroyFireInputTestWorld(World);
		return false;
	}

	// 前置：此刻确实没有任何 Fire Spec 可以响应这个输入。
	TestNull(TEXT("no fire spec can respond before any weapon is granted"),
		Fixture.AbilitySystemComponent->FindAbilitySpecFromInputTag(ShooterGameplayTags::Input_Fire));

	PressFire(Fixture);
	TestTrue(TEXT("a press without a matching spec is still recorded as held"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Fire));

	ProcessInput(Fixture);
	TestTrue(TEXT("the held intent survives the input pass that had nothing to activate"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Fire));

	// 没有 Spec 也必须有明确的松开边界：否则 Held 会永久残留。
	ReleaseFire(Fixture);
	TestFalse(TEXT("a release without a matching spec still clears the held intent"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Fire));
	ProcessInput(Fixture);

	// 再次按住，这一次 Spec 稍后才出现。
	PressFire(Fixture);
	ProcessInput(Fixture);
	TestTrue(TEXT("the held intent is kept while the spec is still missing"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Fire));

	AShooterWeaponLifecycleTestWeapon* FullAutoWeapon = GrantAndEquipWeapon(*this, World, Fixture, /*bFullAuto*/ true);
	if (!FullAutoWeapon)
	{
		DestroyFireInputTestWorld(World);
		return false;
	}

	ProcessInput(Fixture);
	TestEqual(TEXT("a full-auto weapon appearing later starts from the surviving held intent"),
		FullAutoWeapon->GetAuthorityShotCountForAutomationTest(), 1);

	ReleaseFire(Fixture);
	ProcessInput(Fixture);
	TestFalse(TEXT("the release still terminates the intent for the late spec"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Fire));
	TestEqual(TEXT("no fire spec keeps a stale input mirror after the release"),
		CountFireSpecsHoldingInput(Fixture.AbilitySystemComponent), 0);

	DestroyFireInputTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFireInputNoSpecDoesNotReplaySemiAutoPressTest,
	"ShootGame.Ability.FireInput.NoSpecDoesNotReplaySemiAutoPress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFireInputNoSpecDoesNotReplaySemiAutoPressTest::RunTest(const FString& Parameters)
{
	using namespace ShooterFireInputLifecycleAutomationTests;

	// Press edge 是瞬时事实：按下时没有合法上下文，就不允许几百毫秒后突然补一枪。
	// 半自动 Spec 稍后才可解析时，旧 Press 不得被翻译成它的一次 Shot。
	UWorld* World = CreateFireInputTestWorld();
	if (!TestNotNull(TEXT("fire input test world created"), World))
	{
		return false;
	}

	const FFireInputFixture Fixture = CreateFireInputFixture(*this, World);
	if (!Fixture.Character)
	{
		DestroyFireInputTestWorld(World);
		return false;
	}

	TestNull(TEXT("no fire spec can respond before any weapon is granted"),
		Fixture.AbilitySystemComponent->FindAbilitySpecFromInputTag(ShooterGameplayTags::Input_Fire));

	PressFire(Fixture);
	ProcessInput(Fixture);
	TestTrue(TEXT("the held intent is recorded even without a spec"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Fire));

	// Spec 稍后出现：Held 仍然成立，但半自动只认 Press edge。
	AShooterWeaponLifecycleTestWeapon* SemiWeapon = GrantAndEquipWeapon(*this, World, Fixture, /*bFullAuto*/ false);
	if (!SemiWeapon)
	{
		DestroyFireInputTestWorld(World);
		return false;
	}

	ProcessInput(Fixture);
	ProcessInput(Fixture);
	TestEqual(TEXT("a semi-auto weapon never replays a press that had no context"),
		SemiWeapon->GetAuthorityShotCountForAutomationTest(), 0);
	TestEqual(TEXT("the stale press edge is not kept as a pending intent"),
		Fixture.AbilitySystemComponent->GetBufferedInputCountForTest(), 0);

	// 释放后重新按下：这才是一次有合法上下文的 Press edge。
	ReleaseFire(Fixture);
	ProcessInput(Fixture);
	PressFire(Fixture);
	ProcessInput(Fixture);
	TestEqual(TEXT("a press with a valid context fires the semi-auto weapon exactly once"),
		SemiWeapon->GetAuthorityShotCountForAutomationTest(), 1);

	ReleaseFire(Fixture);
	ProcessInput(Fixture);
	TestEqual(TEXT("no fire spec keeps a stale input mirror after the release"),
		CountFireSpecsHoldingInput(Fixture.AbilitySystemComponent), 0);

	DestroyFireInputTestWorld(World);
	return true;
}

#endif
