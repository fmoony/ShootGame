// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/ShooterAbilitySystemComponent.h"
#include "AbilitySystem/ShooterGameplayTags.h"
#include "Characters/ShooterCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState/ShooterPlayerState.h"
#include "Tests/Ability/ShooterInputBufferTestTypes.h"
#include "Tests/Equipment/ShooterWeaponPresentationTestTypes.h"
#include "UObject/Package.h"

/**
 * 输入缓冲的运行时证据。
 *
 * 这里使用生产 ASC 的真实输入路径（AbilityInputTagPressed / 采集集合 / 失败分类 / ProcessAbilityInput），
 * 只把被驱动对象换成测试 Ability，避免把 Weapon / Ammo / Inventory 规则搬进测试。
 *
 * 采集与解释已分离：输入回调只记录 Press / Release / Held，解释发生在 ProcessAbilityInput；
 * 因此每个用例都在"按下之后、断言之前"显式推进一次处理时点。
 *
 * 两种输入策略各有一组用例：
 * - OnInputTriggered（Semi）：pending intent 由短期 Buffered Press Edge 表达；
 * - WhileInputActive（FullAuto）：pending intent 只由 Held 集合与 Spec.InputPressed 表达，Release 即终止。
 */
namespace ShooterAbilityInputBufferAutomationTests
{
	UWorld* CreateInputBufferTestWorld()
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

	void DestroyInputBufferTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	/**
	 * 建立「本机拥有者视图 + ShooterPlayerState ASC」最小上下文。
	 * 只有同时满足 IsPlayerControlled 与本地控制器，ASC 才会进入输入处理路径。
	 */
	UShooterAbilitySystemComponent* CreateLocalInputContext(FAutomationTestBase& Test, UWorld* World, AShooterPlayerState*& OutPlayerState)
	{
		OutPlayerState = nullptr;
		if (!World)
		{
			return nullptr;
		}

		APlayerController* PlayerController = World->SpawnActor<APlayerController>(FVector::ZeroVector,
			FRotator::ZeroRotator);
		AShooterWeaponPresentationTestCharacter* Character =
			World->SpawnActor<AShooterWeaponPresentationTestCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("input buffer PlayerController spawned"), PlayerController) ||
			!Test.TestNotNull(TEXT("input buffer character spawned"), Character))
		{
			return nullptr;
		}

		// ASC 的 FGameplayAbilityActorInfo 从 OwnerActor 的所有者链解析 PlayerController，
		// 因此 PlayerState 必须由 PlayerController 拥有，才能构成本机拥有者视图。
		FActorSpawnParameters PlayerStateSpawnParameters;
		PlayerStateSpawnParameters.Owner = PlayerController;
		PlayerStateSpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AShooterPlayerState* PlayerState = World->SpawnActor<AShooterPlayerState>(
			AShooterPlayerState::StaticClass(), PlayerStateSpawnParameters);
		if (!Test.TestNotNull(TEXT("input buffer PlayerState spawned"), PlayerState))
		{
			return nullptr;
		}

		PlayerController->DispatchBeginPlay();
		Character->DispatchBeginPlay();
		PlayerController->Possess(Character);
		PlayerController->PlayerState = PlayerState;
		Character->SetPlayerState(PlayerState);
		PlayerState->InitializeAbilityActorInfo(Character);

		UShooterAbilitySystemComponent* AbilitySystemComponent = Cast<UShooterAbilitySystemComponent>(
			Character->GetAbilitySystemComponent());
		if (!Test.TestNotNull(TEXT("input buffer ASC resolved"), AbilitySystemComponent))
		{
			return nullptr;
		}

		Test.TestTrue(TEXT("input buffer ASC has a locally controlled player view"),
			AbilitySystemComponent->AbilityActorInfo.IsValid() &&
			AbilitySystemComponent->AbilityActorInfo->IsLocallyControlledPlayer());
		OutPlayerState = PlayerState;
		return AbilitySystemComponent;
	}

	/** 授予测试 Ability 并返回它的实例；失败返回 nullptr。 */
	UShooterInputBufferTestAbility* GrantInputBufferTestAbility(FAutomationTestBase& Test,
		UShooterAbilitySystemComponent* AbilitySystemComponent, TSubclassOf<UGameplayAbility> AbilityClass)
	{
		if (!AbilitySystemComponent || !AbilityClass)
		{
			return nullptr;
		}

		const FGameplayAbilitySpecHandle Handle = AbilitySystemComponent->GiveAbility(
			FGameplayAbilitySpec(AbilityClass, /*AbilityLevel*/ 1, INDEX_NONE, nullptr));
		const FGameplayAbilitySpec* Spec = AbilitySystemComponent->FindAbilitySpecFromHandle(Handle);
		if (!Test.TestNotNull(TEXT("input buffer test ability spec granted"), Spec))
		{
			return nullptr;
		}

		UShooterInputBufferTestAbility* AbilityInstance =
			Cast<UShooterInputBufferTestAbility>(Spec->GetPrimaryInstance());
		if (!Test.TestNotNull(TEXT("input buffer test ability instance created"), AbilityInstance))
		{
			return nullptr;
		}

		return AbilityInstance;
	}

	/** 取指定测试 Ability 的当前激活次数；实例缺失时返回 INDEX_NONE。 */
	int32 GetActivationCount(const UShooterAbilitySystemComponent* AbilitySystemComponent,
		TSubclassOf<UGameplayAbility> AbilityClass)
	{
		if (!AbilitySystemComponent || !AbilityClass)
		{
			return INDEX_NONE;
		}

		const FGameplayAbilitySpec* Spec = AbilitySystemComponent->FindAbilitySpecFromClass(AbilityClass);
		const UShooterInputBufferTestAbility* AbilityInstance = Spec
			? Cast<UShooterInputBufferTestAbility>(Spec->GetPrimaryInstance())
			: nullptr;
		return AbilityInstance ? AbilityInstance->ActivationCountForTest : INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInputBufferTransientBlockKeepsPressTest,
	"ShootGame.Ability.InputBuffer.TransientBlockKeepsPress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInputBufferTransientBlockKeepsPressTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityInputBufferAutomationTests;

	UWorld* World = CreateInputBufferTestWorld();
	if (!TestNotNull(TEXT("input buffer test world created"), World))
	{
		return false;
	}

	AShooterPlayerState* PlayerState = nullptr;
	UShooterAbilitySystemComponent* AbilitySystemComponent = CreateLocalInputContext(*this, World, PlayerState);
	UShooterInputBufferTestAbility* Ability = GrantInputBufferTestAbility(*this,
		AbilitySystemComponent, UShooterInputBufferTestAbility::StaticClass());
	if (!AbilitySystemComponent || !Ability)
	{
		DestroyInputBufferTestWorld(World);
		return false;
	}

	// 半自动式场景：按下时被短暂动作阻塞 → 松开 → 阻塞解除后恰好消费一次。
	// 按下只采集；失败分类与窗口登记发生在解释时点。
	AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	TestEqual(TEXT("a press is collected as held"), AbilitySystemComponent->GetHeldInputTagCountForTest(), 1);
	TestEqual(TEXT("collection alone does not interpret input"), Ability->ActivationCountForTest, 0);
	TestEqual(TEXT("collection alone does not create a pending entry"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 0);

	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("transient blocked press is buffered"), AbilitySystemComponent->GetBufferedInputCountForTest(), 1);
	TestEqual(TEXT("blocked press does not activate"), Ability->ActivationCountForTest, 0);

	// 松开后窗口仍然保留；本次解释的失败尝试不删除也不续期。
	AbilitySystemComponent->AbilityInputTagReleased(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("release keeps the press edge until it expires"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 1);
	TestEqual(TEXT("release clears the held collection"), AbilitySystemComponent->GetHeldInputTagCountForTest(), 0);

	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->ProcessAbilityInputForTest();

	TestEqual(TEXT("buffered press activates exactly once after the block ends"), Ability->ActivationCountForTest, 1);
	TestEqual(TEXT("consumed press leaves no pending entry"), AbilitySystemComponent->GetBufferedInputCountForTest(), 0);
	TestEqual(TEXT("a press already released is released again after activation"),
		AbilitySystemComponent->GetActiveAbilityCountForClass(UShooterInputBufferTestAbility::StaticClass()), 0);

	DestroyInputBufferTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInputBufferHeldPressStaysActiveTest,
	"ShootGame.Ability.InputBuffer.HeldPressStaysActive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInputBufferHeldPressStaysActiveTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityInputBufferAutomationTests;

	UWorld* World = CreateInputBufferTestWorld();
	if (!TestNotNull(TEXT("input buffer test world created"), World))
	{
		return false;
	}

	AShooterPlayerState* PlayerState = nullptr;
	UShooterAbilitySystemComponent* AbilitySystemComponent = CreateLocalInputContext(*this, World, PlayerState);
	UShooterInputBufferTestAbility* Ability = GrantInputBufferTestAbility(*this,
		AbilitySystemComponent, UShooterInputBufferTestAbility::StaticClass());
	if (!AbilitySystemComponent || !Ability)
	{
		DestroyInputBufferTestWorld(World);
		return false;
	}

	AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("blocked held press is buffered"), AbilitySystemComponent->GetBufferedInputCountForTest(), 1);

	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->ProcessAbilityInputForTest();

	TestEqual(TEXT("held buffered press activates once"), Ability->ActivationCountForTest, 1);
	TestEqual(TEXT("a still held press is not released by the buffer"),
		AbilitySystemComponent->GetActiveAbilityCountForClass(UShooterInputBufferTestAbility::StaticClass()), 1);

	// 真实松开仍然走标准释放路径。
	AbilitySystemComponent->AbilityInputTagReleased(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("real release ends the predicted instance"),
		AbilitySystemComponent->GetActiveAbilityCountForClass(UShooterInputBufferTestAbility::StaticClass()), 0);

	DestroyInputBufferTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInputBufferHardFailureIsNotBufferedTest,
	"ShootGame.Ability.InputBuffer.HardFailureIsNotBuffered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInputBufferHardFailureIsNotBufferedTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityInputBufferAutomationTests;

	UWorld* World = CreateInputBufferTestWorld();
	if (!TestNotNull(TEXT("input buffer test world created"), World))
	{
		return false;
	}

	AShooterPlayerState* PlayerState = nullptr;
	UShooterAbilitySystemComponent* AbilitySystemComponent = CreateLocalInputContext(*this, World, PlayerState);
	UShooterInputBufferTestAbility* Ability = GrantInputBufferTestAbility(*this,
		AbilitySystemComponent, UShooterInputBufferHardFailTestAbility::StaticClass());
	if (!AbilitySystemComponent || !Ability)
	{
		DestroyInputBufferTestWorld(World);
		return false;
	}

	// 硬失败（本地节拍未 Ready / 武器无效这一类的代表）不产生 Tag 阻塞，因此绝不被缓冲。
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("hard failure is not buffered"), AbilitySystemComponent->GetBufferedInputCountForTest(), 0);

	AbilitySystemComponent->AbilityInputTagReleased(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("hard failed press never activates later"), Ability->ActivationCountForTest, 0);

	DestroyInputBufferTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInputBufferSelfBlockedPressIsDroppedTest,
	"ShootGame.Ability.InputBuffer.SelfBlockedPressIsDropped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInputBufferSelfBlockedPressIsDroppedTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityInputBufferAutomationTests;

	UWorld* World = CreateInputBufferTestWorld();
	if (!TestNotNull(TEXT("input buffer test world created"), World))
	{
		return false;
	}

	AShooterPlayerState* PlayerState = nullptr;
	UShooterAbilitySystemComponent* AbilitySystemComponent = CreateLocalInputContext(*this, World, PlayerState);
	UShooterInputBufferTestAbility* Ability = GrantInputBufferTestAbility(*this,
		AbilitySystemComponent, UShooterInputBufferSelfBlockedTestAbility::StaticClass());
	if (!AbilitySystemComponent || !Ability)
	{
		DestroyInputBufferTestWorld(World);
		return false;
	}

	// 阻塞标签同时是该 Ability 自己的活动标签：重复按下没有意义，不得保留。
	AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("self blocked press is dropped"), AbilitySystemComponent->GetBufferedInputCountForTest(), 0);

	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("dropped press never activates later"), Ability->ActivationCountForTest, 0);

	DestroyInputBufferTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInputBufferExpiredPressIsNotConsumedTest,
	"ShootGame.Ability.InputBuffer.ExpiredPressIsNotConsumed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInputBufferExpiredPressIsNotConsumedTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityInputBufferAutomationTests;

	UWorld* World = CreateInputBufferTestWorld();
	if (!TestNotNull(TEXT("input buffer test world created"), World))
	{
		return false;
	}

	AShooterPlayerState* PlayerState = nullptr;
	UShooterAbilitySystemComponent* AbilitySystemComponent = CreateLocalInputContext(*this, World, PlayerState);
	UShooterInputBufferTestAbility* Ability = GrantInputBufferTestAbility(*this,
		AbilitySystemComponent, UShooterInputBufferTestAbility::StaticClass());
	if (!AbilitySystemComponent || !Ability)
	{
		DestroyInputBufferTestWorld(World);
		return false;
	}

	AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("blocked press leaves a pending entry"), AbilitySystemComponent->GetBufferedInputCountForTest(), 1);

	AbilitySystemComponent->AbilityInputTagReleased(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("failed retry keeps the pending entry"), AbilitySystemComponent->GetBufferedInputCountForTest(), 1);

	// 窗口按绝对时间过期，且重试不得续期。
	AbilitySystemComponent->ExpireBufferedInputsForTest();
	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->ProcessAbilityInputForTest();

	TestEqual(TEXT("expired press is removed"), AbilitySystemComponent->GetBufferedInputCountForTest(), 0);
	TestEqual(TEXT("expired press never activates later"), Ability->ActivationCountForTest, 0);

	DestroyInputBufferTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInputBufferContextChangeDropsPressTest,
	"ShootGame.Ability.InputBuffer.ContextChangeDropsPress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInputBufferContextChangeDropsPressTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityInputBufferAutomationTests;

	UWorld* World = CreateInputBufferTestWorld();
	if (!TestNotNull(TEXT("input buffer test world created"), World))
	{
		return false;
	}

	AShooterPlayerState* PlayerState = nullptr;
	UShooterAbilitySystemComponent* AbilitySystemComponent = CreateLocalInputContext(*this, World, PlayerState);
	UShooterInputBufferTestAbility* Ability = GrantInputBufferTestAbility(*this,
		AbilitySystemComponent, UShooterInputBufferTestAbility::StaticClass());
	if (!AbilitySystemComponent || !Ability)
	{
		DestroyInputBufferTestWorld(World);
		return false;
	}

	// 上下文用两个已存在的对象表示（PlayerState 与 ASC 本身）；
	// 这里不构造新的 UObject：UObject 在 5.6 被标记为 abstract，NewObject 会触发 ensure。
	Ability->SetInputContextForTest(PlayerState);

	AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("context carrying press is buffered"), AbilitySystemComponent->GetBufferedInputCountForTest(), 1);

	// 上下文变化（换枪提交 / 武器归还池）：旧武器的按下沿不得在新上下文上消费。
	Ability->SetInputContextForTest(AbilitySystemComponent);
	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->ProcessAbilityInputForTest();

	TestEqual(TEXT("context change drops the press"), AbilitySystemComponent->GetBufferedInputCountForTest(), 0);
	TestEqual(TEXT("context changed press never activates"), Ability->ActivationCountForTest, 0);

	DestroyInputBufferTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInputBufferWhileInputActiveRetriesWhileHeldTest,
	"ShootGame.Ability.InputBuffer.WhileInputActiveRetriesWhileHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInputBufferWhileInputActiveRetriesWhileHeldTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityInputBufferAutomationTests;

	UWorld* World = CreateInputBufferTestWorld();
	if (!TestNotNull(TEXT("input buffer test world created"), World))
	{
		return false;
	}

	AShooterPlayerState* PlayerState = nullptr;
	UShooterAbilitySystemComponent* AbilitySystemComponent = CreateLocalInputContext(*this, World, PlayerState);
	UShooterInputBufferTestAbility* Ability = GrantInputBufferTestAbility(*this,
		AbilitySystemComponent, UShooterInputBufferSustainedTestAbility::StaticClass());
	if (!AbilitySystemComponent || !Ability)
	{
		DestroyInputBufferTestWorld(World);
		return false;
	}

	// 1. WhileInputActive：按下沿不登记短期按下沿；被短暂阻塞时当场尝试一次但不成立。
	//    首帧由 Press edge 分支发起尝试，Held 分支不在同一帧重复发起。
	AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("a while-input-active press never registers a buffered entry"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 0);
	TestEqual(TEXT("a blocked while-input-active press does not activate"), Ability->ActivationCountForTest, 0);

	// 2. 阻塞解除后由解释时点的 Held 分支激活；已激活时同一时点不重复请求。
	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("held intent starts once after the block ends"), Ability->ActivationCountForTest, 1);

	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("an active ability is never re-activated by the held pass"), Ability->ActivationCountForTest, 1);

	// 3. 动作被取消但玩家仍按住：下一个解释时点重试一次（无 timer、无计数）。
	AbilitySystemComponent->CancelAbilitiesByTag(ShooterGameplayTags::Input_Fire);
	TestEqual(TEXT("cancel ends the running instance"),
		AbilitySystemComponent->GetActiveAbilityCountForClass(UShooterInputBufferSustainedTestAbility::StaticClass()), 0);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("held input is retried after a cancel"), Ability->ActivationCountForTest, 2);
	TestEqual(TEXT("a held retry never creates a pending buffered entry"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 0);

	// 4. Release 终止持续意图：活动实例走标准释放路径，之后不再重试。
	AbilitySystemComponent->AbilityInputTagReleased(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("a released held intent is never retried"), Ability->ActivationCountForTest, 2);
	TestEqual(TEXT("release ends the active instance"),
		AbilitySystemComponent->GetActiveAbilityCountForClass(UShooterInputBufferSustainedTestAbility::StaticClass()), 0);

	DestroyInputBufferTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInputBufferOnInputTriggeredDoesNotRetryWhileHeldTest,
	"ShootGame.Ability.InputBuffer.OnInputTriggeredDoesNotRetryWhileHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInputBufferOnInputTriggeredDoesNotRetryWhileHeldTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityInputBufferAutomationTests;

	UWorld* World = CreateInputBufferTestWorld();
	if (!TestNotNull(TEXT("input buffer test world created"), World))
	{
		return false;
	}

	AShooterPlayerState* PlayerState = nullptr;
	UShooterAbilitySystemComponent* AbilitySystemComponent = CreateLocalInputContext(*this, World, PlayerState);
	UShooterInputBufferTestAbility* Ability = GrantInputBufferTestAbility(*this,
		AbilitySystemComponent, UShooterInputBufferTestAbility::StaticClass());
	if (!AbilitySystemComponent || !Ability)
	{
		DestroyInputBufferTestWorld(World);
		return false;
	}

	// 单发语义（OnInputTriggered）：按下沿在解释时点尝试；被短暂阻塞时保留一条短期按下沿。
	AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("a single press edge is buffered while transiently blocked"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 1);
	TestEqual(TEXT("a blocked press edge does not activate"), Ability->ActivationCountForTest, 0);

	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("the buffered press edge is consumed exactly once"), Ability->ActivationCountForTest, 1);

	// 仍按住但动作被取消：OnInputTriggered 不因 Held 补枪（这是 Semi 的核心语义）。
	AbilitySystemComponent->CancelAbilitiesByTag(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("held input without WhileInputActive is never retried"), Ability->ActivationCountForTest, 1);

	DestroyInputBufferTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInputBufferWhileInputActiveReleasedPressDoesNotRetryTest,
	"ShootGame.Ability.InputBuffer.WhileInputActiveReleasedPressDoesNotRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInputBufferWhileInputActiveReleasedPressDoesNotRetryTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityInputBufferAutomationTests;

	UWorld* World = CreateInputBufferTestWorld();
	if (!TestNotNull(TEXT("input buffer test world created"), World))
	{
		DestroyInputBufferTestWorld(World);
		return false;
	}

	AShooterPlayerState* PlayerState = nullptr;
	UShooterAbilitySystemComponent* AbilitySystemComponent = CreateLocalInputContext(*this, World, PlayerState);
	UShooterInputBufferTestAbility* Ability = GrantInputBufferTestAbility(*this,
		AbilitySystemComponent, UShooterInputBufferSustainedTestAbility::StaticClass());
	if (!AbilitySystemComponent || !Ability)
	{
		DestroyInputBufferTestWorld(World);
		return false;
	}

	// WhileInputActive 的持续意图只由 Held 集合表达：Release 即终止，也不留下按下沿。
	// 阻塞保持到解释时点，因此这次按下沿尝试在阻塞中失败，且不登记任何窗口。
	AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->AbilityInputTagReleased(ShooterGameplayTags::Input_Fire);
	TestEqual(TEXT("release clears the held collection before interpretation"),
		AbilitySystemComponent->GetHeldInputTagCountForTest(), 0);

	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("a released while-input-active press does not start"), Ability->ActivationCountForTest, 0);
	TestEqual(TEXT("a released while-input-active press leaves no pending entry"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 0);

	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("a released while-input-active press is never retried"), Ability->ActivationCountForTest, 0);

	DestroyInputBufferTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInputBufferSameFramePressReleaseFiresOnceTest,
	"ShootGame.Ability.InputBuffer.SameFramePressReleaseFiresOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInputBufferSameFramePressReleaseFiresOnceTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityInputBufferAutomationTests;

	UWorld* World = CreateInputBufferTestWorld();
	if (!TestNotNull(TEXT("input buffer test world created"), World))
	{
		DestroyInputBufferTestWorld(World);
		return false;
	}

	AShooterPlayerState* PlayerState = nullptr;
	UShooterAbilitySystemComponent* AbilitySystemComponent = CreateLocalInputContext(*this, World, PlayerState);
	UShooterInputBufferTestAbility* Ability = GrantInputBufferTestAbility(*this,
		AbilitySystemComponent, UShooterInputBufferSustainedTestAbility::StaticClass());
	if (!AbilitySystemComponent || !Ability)
	{
		DestroyInputBufferTestWorld(World);
		return false;
	}

	// 同帧「按下 + 松开」必须形成一次动作边界：按下沿在同一次解释里先激活，松开随后走标准释放。
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->AbilityInputTagReleased(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessAbilityInputForTest();

	TestEqual(TEXT("a same frame tap activates exactly once"), Ability->ActivationCountForTest, 1);
	TestEqual(TEXT("a same frame tap is released in the same pass"),
		AbilitySystemComponent->GetActiveAbilityCountForClass(UShooterInputBufferSustainedTestAbility::StaticClass()), 0);
	TestEqual(TEXT("a same frame tap leaves no pending entry"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 0);

	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("a same frame tap is never retried after release"), Ability->ActivationCountForTest, 1);

	DestroyInputBufferTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInputBufferNewPressEdgeSupersedesBufferedPressTest,
	"ShootGame.Ability.InputBuffer.NewPressEdgeSupersedesBufferedPress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInputBufferNewPressEdgeSupersedesBufferedPressTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityInputBufferAutomationTests;

	UWorld* World = CreateInputBufferTestWorld();
	if (!TestNotNull(TEXT("input buffer test world created"), World))
	{
		DestroyInputBufferTestWorld(World);
		return false;
	}

	AShooterPlayerState* PlayerState = nullptr;
	UShooterAbilitySystemComponent* AbilitySystemComponent = CreateLocalInputContext(*this, World, PlayerState);
	UShooterInputBufferTestAbility* Ability = GrantInputBufferTestAbility(*this,
		AbilitySystemComponent, UShooterInputBufferTestAbility::StaticClass());
	if (!AbilitySystemComponent || !Ability)
	{
		DestroyInputBufferTestWorld(World);
		return false;
	}

	// 被短暂阻塞的第一次按下留下窗口，随后松开（窗口保留到过期，不被释放清除）。
	AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("the first blocked press leaves a pending entry"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 1);

	AbilitySystemComponent->AbilityInputTagReleased(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("the pending entry survives the release"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 1);

	// 阻塞解除后的新按下沿立即废弃旧窗口：新边自己形成动作边界，旧边不再补出第二枪。
	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessAbilityInputForTest();

	TestEqual(TEXT("the new press edge activates once"), Ability->ActivationCountForTest, 1);
	TestEqual(TEXT("the new press edge supersedes the old pending entry"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 0);

	AbilitySystemComponent->AbilityInputTagReleased(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	AbilitySystemComponent->ProcessAbilityInputForTest();

	TestEqual(TEXT("a superseded window never fires a second shot"), Ability->ActivationCountForTest, 1);
	TestEqual(TEXT("no pending entry survives the supersede"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 0);

	DestroyInputBufferTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInputBufferLifecycleClearsIntentTest,
	"ShootGame.Ability.InputBuffer.LifecycleClearsIntent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInputBufferLifecycleClearsIntentTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityInputBufferAutomationTests;

	UWorld* World = CreateInputBufferTestWorld();
	if (!TestNotNull(TEXT("input buffer test world created"), World))
	{
		return false;
	}

	AShooterPlayerState* PlayerState = nullptr;
	UShooterAbilitySystemComponent* AbilitySystemComponent = CreateLocalInputContext(*this, World, PlayerState);
	UShooterInputBufferTestAbility* Ability = GrantInputBufferTestAbility(*this,
		AbilitySystemComponent, UShooterInputBufferTestAbility::StaticClass());
	if (!AbilitySystemComponent || !Ability)
	{
		DestroyInputBufferTestWorld(World);
		return false;
	}

	// 死亡：State.Dead 不是可缓冲的阻塞，因此死亡状态下的按下不进入短期按下沿；
	// 旧意图的清理由宿主在死亡边界显式调用（ShooterCharacter::ApplyDeathState 走同一入口）。
	AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	TestEqual(TEXT("press is collected as held before death"),
		AbilitySystemComponent->GetHeldInputTagCountForTest(), 1);

	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("reloading press is buffered before death"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 1);

	AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Dead);
	AbilitySystemComponent->InvalidateInputIntents();
	TestEqual(TEXT("the death boundary invalidates pending input intent"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 0);
	TestEqual(TEXT("the death boundary clears the held collection"),
		AbilitySystemComponent->GetHeldInputTagCountForTest(), 0);

	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("dead press is not buffered"), AbilitySystemComponent->GetBufferedInputCountForTest(), 0);
	TestEqual(TEXT("dead press never activates"), Ability->ActivationCountForTest, 0);

	// Avatar 生命周期更新（重生）：不继承旧生命周期的意图。
	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Dead);
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("reloading press is buffered again after death tag removal"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 1);

	AbilitySystemComponent->ClearActorInfo();
	TestEqual(TEXT("actor info reset clears pending input intent"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 0);
	TestEqual(TEXT("actor info reset clears the held collection"),
		AbilitySystemComponent->GetHeldInputTagCountForTest(), 0);

	DestroyInputBufferTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInputBufferLifecycleResetsHeldInputPressedTest,
	"ShootGame.Ability.InputBuffer.LifecycleResetsHeldInputPressed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInputBufferLifecycleResetsHeldInputPressedTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityInputBufferAutomationTests;

	UWorld* World = CreateInputBufferTestWorld();
	if (!TestNotNull(TEXT("input buffer test world created"), World))
	{
		DestroyInputBufferTestWorld(World);
		return false;
	}

	AShooterPlayerState* PlayerState = nullptr;
	UShooterAbilitySystemComponent* AbilitySystemComponent = CreateLocalInputContext(*this, World, PlayerState);
	UShooterInputBufferTestAbility* Ability = GrantInputBufferTestAbility(*this,
		AbilitySystemComponent, UShooterInputBufferSustainedTestAbility::StaticClass());
	if (!AbilitySystemComponent || !Ability)
	{
		DestroyInputBufferTestWorld(World);
		return false;
	}

	FGameplayAbilitySpec* Spec = AbilitySystemComponent->FindAbilitySpecFromClass(UShooterInputBufferSustainedTestAbility::StaticClass());
	if (!TestNotNull(TEXT("sustained test ability spec found"), Spec))
	{
		DestroyInputBufferTestWorld(World);
		return false;
	}

	// Spec.InputPressed 是引擎标记为 NotReplicated 的本地真值，由解释时点通过标准入口写入：
	// 采集本身不写它，生命周期边界直接复位它（不派发 Release）。
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	TestFalse(TEXT("collection alone does not write Spec.InputPressed"), Spec->InputPressed);

	AbilitySystemComponent->ProcessAbilityInputForTest();
	TestTrue(TEXT("the input pass writes Spec.InputPressed through the engine entry point"), Spec->InputPressed);
	const int32 ActivationCountBeforeInvalidate = Ability->ActivationCountForTest;

	// 生命周期边界是「旧输入意图作废」，不是真实 Release：直接复位本地真值，不派发 Release。
	AbilitySystemComponent->ClearActorInfo();
	TestFalse(TEXT("actor info reset invalidates Spec.InputPressed"), Spec->InputPressed);
	TestEqual(TEXT("invalidated held intent does not activate again"),
		Ability->ActivationCountForTest, ActivationCountBeforeInvalidate);

	DestroyInputBufferTestWorld(World);
	return true;
}

#endif
