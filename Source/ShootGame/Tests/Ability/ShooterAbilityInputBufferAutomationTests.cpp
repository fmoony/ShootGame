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
 * 这里使用生产 ASC 的真实输入路径（AbilityInputTagPressed / 失败分类 / 安全时点消费），
 * 只把被驱动对象换成测试 Ability，避免把 Weapon / Ammo / Inventory 规则搬进测试。
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
	 * 只有同时满足 IsPlayerControlled 与本地控制器，ASC 才会进入输入缓冲路径。
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
	AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	TestEqual(TEXT("transient blocked press is buffered"), AbilitySystemComponent->GetBufferedInputCountForTest(), 1);
	TestEqual(TEXT("blocked press does not activate"), Ability->ActivationCountForTest, 0);

	AbilitySystemComponent->AbilityInputTagReleased(ShooterGameplayTags::Input_Fire);
	TestEqual(TEXT("release keeps the press edge until it expires"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 1);

	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->ProcessBufferedInputsForTest();

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
	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->ProcessBufferedInputsForTest();

	TestEqual(TEXT("held buffered press activates once"), Ability->ActivationCountForTest, 1);
	TestEqual(TEXT("a still held press is not released by the buffer"),
		AbilitySystemComponent->GetActiveAbilityCountForClass(UShooterInputBufferTestAbility::StaticClass()), 1);

	// 真实松开仍然走标准释放路径。
	AbilitySystemComponent->AbilityInputTagReleased(ShooterGameplayTags::Input_Fire);
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
	TestEqual(TEXT("hard failure is not buffered"), AbilitySystemComponent->GetBufferedInputCountForTest(), 0);

	AbilitySystemComponent->AbilityInputTagReleased(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessBufferedInputsForTest();
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
	TestEqual(TEXT("self blocked press is dropped"), AbilitySystemComponent->GetBufferedInputCountForTest(), 0);

	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->ProcessBufferedInputsForTest();
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
	AbilitySystemComponent->AbilityInputTagReleased(ShooterGameplayTags::Input_Fire);

	// 窗口按绝对时间过期，且重试不得续期。
	AbilitySystemComponent->ExpireBufferedInputsForTest();
	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->ProcessBufferedInputsForTest();

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
	TestEqual(TEXT("context carrying press is buffered"), AbilitySystemComponent->GetBufferedInputCountForTest(), 1);

	// 上下文变化（换枪提交 / 武器归还池）：旧武器的按下沿不得在新上下文上消费。
	Ability->SetInputContextForTest(AbilitySystemComponent);
	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->ProcessBufferedInputsForTest();

	TestEqual(TEXT("context change drops the press"), AbilitySystemComponent->GetBufferedInputCountForTest(), 0);
	TestEqual(TEXT("context changed press never activates"), Ability->ActivationCountForTest, 0);

	DestroyInputBufferTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInputBufferHeldRepeatRetriesAfterBlockerTest,
	"ShootGame.Ability.InputBuffer.HeldRepeatRetriesAfterBlocker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInputBufferHeldRepeatRetriesAfterBlockerTest::RunTest(const FString& Parameters)
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

	// 走生产同步路径：HeldRepeat 是 Spec 上的通用输入行为标签（生产由装备入口按当前武器写入）。
	AbilitySystemComponent->SetHeldRepeatInputBehavior(ShooterGameplayTags::Input_Fire, true);

	// 1. 按住时被短暂阻塞：阻塞解除后消费一次并启动。
	AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	TestEqual(TEXT("held press is buffered while transiently blocked"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 1);
	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->ProcessBufferedInputsForTest();
	TestEqual(TEXT("held repeat input starts once after the block ends"), Ability->ActivationCountForTest, 1);

	// 2. 动作被取消但玩家仍按住：下一个安全时点允许再尝试一次（无计数、无 Timer）。
	AbilitySystemComponent->CancelAbilitiesByTag(ShooterGameplayTags::Input_Fire);
	TestEqual(TEXT("cancel ends the running instance"),
		AbilitySystemComponent->GetActiveAbilityCountForClass(UShooterInputBufferSustainedTestAbility::StaticClass()), 0);

	AbilitySystemComponent->ProcessBufferedInputsForTest();
	TestEqual(TEXT("held input is retried once after a cancel"), Ability->ActivationCountForTest, 2);
	TestEqual(TEXT("a retry never creates a pending buffered entry"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 0);

	// 3. 语义切回单发（生产对应换到单发武器）：移除行为标签后即使仍按住也不再重试。
	AbilitySystemComponent->SetHeldRepeatInputBehavior(ShooterGameplayTags::Input_Fire, false);
	AbilitySystemComponent->CancelAbilitiesByTag(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessBufferedInputsForTest();
	TestEqual(TEXT("removing the behavior tag stops held retries"), Ability->ActivationCountForTest, 2);

	DestroyInputBufferTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInputBufferHeldWithoutBehaviorTagIsNotRetriedTest,
	"ShootGame.Ability.InputBuffer.HeldWithoutBehaviorTagIsNotRetried",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInputBufferHeldWithoutBehaviorTagIsNotRetriedTest::RunTest(const FString& Parameters)
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

	// 单发语义：一次按下只对应一次本地动作边界；一直按住不得跨阻塞补枪。
	// 这里刻意不写 HeldRepeat 标签，证明「是否重试」只由 Spec 行为标签决定。
	AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->ProcessBufferedInputsForTest();
	TestEqual(TEXT("the buffered press edge is consumed exactly once"), Ability->ActivationCountForTest, 1);

	// 动作结束但仍按住：没有行为标签时不得自动重试。
	AbilitySystemComponent->CancelAbilitiesByTag(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->ProcessBufferedInputsForTest();
	TestEqual(TEXT("a held input without the behavior tag is never retried"), Ability->ActivationCountForTest, 1);

	// 反向证明：同一个 Spec 加上行为标签后，同一时点就会重试。
	AbilitySystemComponent->SetHeldRepeatInputBehavior(ShooterGameplayTags::Input_Fire, true);
	AbilitySystemComponent->ProcessBufferedInputsForTest();
	TestEqual(TEXT("the same held input is retried once the behavior tag is present"),
		Ability->ActivationCountForTest, 2);

	DestroyInputBufferTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInputBufferHeldRepeatReleasedPressIsNotRetriedTest,
	"ShootGame.Ability.InputBuffer.HeldRepeatReleasedPressIsNotRetried",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInputBufferHeldRepeatReleasedPressIsNotRetriedTest::RunTest(const FString& Parameters)
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

	AbilitySystemComponent->SetHeldRepeatInputBehavior(ShooterGameplayTags::Input_Fire, true);

	// 按住型输入已经松开：阻塞解除后不得自动开始，也不得重试。
	AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->AbilityInputTagReleased(ShooterGameplayTags::Input_Fire);
	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->ProcessBufferedInputsForTest();

	TestEqual(TEXT("a released held press does not start"), Ability->ActivationCountForTest, 0);
	TestEqual(TEXT("released held press leaves no pending entry"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 0);

	AbilitySystemComponent->ProcessBufferedInputsForTest();
	TestEqual(TEXT("a released held press is never retried"), Ability->ActivationCountForTest, 0);

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

	// 死亡：State.Dead 不是可缓冲的阻塞，因此死亡状态下的按下不进入缓冲；
	// 待消费意图的清理由宿主在死亡边界显式调用（ShooterCharacter::ApplyDeathState 走同一入口）。
	AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	TestEqual(TEXT("reloading press is buffered before death"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 1);

	AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Dead);
	AbilitySystemComponent->ClearBufferedInputs();
	TestEqual(TEXT("the death boundary clears pending input intent"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 0);

	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	TestEqual(TEXT("dead press is not buffered"), AbilitySystemComponent->GetBufferedInputCountForTest(), 0);
	TestEqual(TEXT("dead press never activates"), Ability->ActivationCountForTest, 0);

	// Avatar 生命周期更新（重生）：不继承旧生命周期的意图。
	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Dead);
	AbilitySystemComponent->AbilityInputTagPressed(ShooterGameplayTags::Input_Fire);
	TestEqual(TEXT("reloading press is buffered again after death tag removal"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 1);
	AbilitySystemComponent->ClearActorInfo();
	TestEqual(TEXT("actor info reset clears pending input intent"),
		AbilitySystemComponent->GetBufferedInputCountForTest(), 0);

	DestroyInputBufferTestWorld(World);
	return true;
}

#endif
