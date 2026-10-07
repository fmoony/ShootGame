// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/ShooterAbilitySystemComponent.h"
#include "AbilitySystem/ShooterGameplayTags.h"
#include "Characters/ShooterCharacter.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState/ShooterPlayerState.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "ShootGame.h"
#include "Tests/Ability/ShooterInputBufferTestTypes.h"
#include "Tests/Equipment/ShooterWeaponPresentationTestTypes.h"
#include "UObject/UnrealType.h"

/**
 * Ability 输入生命周期的真实语义证据：ASC 的 Held 采集只表示"当前物理输入仍处于按下状态"。
 *
 * 本文件回答两个问题：
 * 1. 每个 Ability 输入在物理上到底是什么形态（可按住 / 只能瞬时触发）—— 直接读
 *    BP_ShooterCharacter 上的 Input Action 与 IMC_Weapons 的 Trigger 配置，不根据 C++ BindAction 猜；
 * 2. ASC 的 Pressed / Held / Released 三个采集集合是否与这个形态一致。
 *
 * 用例走真实入口：Character.DoReload / DoStopReload / DoSwitchWeaponInput / DoStartFiring /
 * DoStopFiring，然后推进一次 ASC 的输入解释时点（ProcessAbilityInput），
 * 只把"被驱动对象"换成可计数的测试 Ability，避免把 Reload / Equip 的 Gameplay 搬进输入层测试。
 */
namespace ShooterAbilityInputLifecycleAutomationTests
{
	UWorld* CreateInputLifecycleTestWorld()
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

	void DestroyInputLifecycleTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	/** 本机拥有者视图 + ShooterPlayerState ASC。 */
	struct FInputLifecycleFixture
	{
		AShooterWeaponPresentationTestCharacter* Character = nullptr;
		UShooterAbilitySystemComponent* AbilitySystemComponent = nullptr;
	};

	FInputLifecycleFixture CreateInputLifecycleFixture(FAutomationTestBase& Test, UWorld* World)
	{
		FInputLifecycleFixture Fixture;
		if (!World)
		{
			return Fixture;
		}

		APlayerController* PlayerController = World->SpawnActor<APlayerController>(FVector::ZeroVector,
			FRotator::ZeroRotator);
		AShooterWeaponPresentationTestCharacter* Character =
			World->SpawnActor<AShooterWeaponPresentationTestCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("input lifecycle PlayerController spawned"), PlayerController) ||
			!Test.TestNotNull(TEXT("input lifecycle character spawned"), Character))
		{
			return Fixture;
		}

		FActorSpawnParameters PlayerStateSpawnParameters;
		PlayerStateSpawnParameters.Owner = PlayerController;
		PlayerStateSpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AShooterPlayerState* PlayerState = World->SpawnActor<AShooterPlayerState>(
			AShooterPlayerState::StaticClass(), PlayerStateSpawnParameters);
		if (!Test.TestNotNull(TEXT("input lifecycle PlayerState spawned"), PlayerState))
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
		if (!Test.TestNotNull(TEXT("input lifecycle ASC resolved"), AbilitySystemComponent) ||
			!Test.TestTrue(TEXT("input lifecycle ASC is a locally controlled player view"),
				AbilitySystemComponent->AbilityActorInfo.IsValid() &&
				AbilitySystemComponent->AbilityActorInfo->IsLocallyControlledPlayer()))
		{
			return Fixture;
		}

		Fixture.Character = Character;
		Fixture.AbilitySystemComponent = AbilitySystemComponent;
		return Fixture;
	}

	/**
	 * 授予一个"由指定输入 Tag 驱动"的计数 Ability。
	 *
	 * 用 Spec 的动态来源标签表达输入映射，与生产里两份 GA_Equip（Next / Previous）完全同机制；
	 * 被驱动的 Ability 只是可计数的测试替身，不引入任何 Reload / Equip Gameplay。
	 */
	UShooterInputBufferTestAbility* GrantCountingAbility(FAutomationTestBase& Test,
		UShooterAbilitySystemComponent* AbilitySystemComponent, const FGameplayTag& InputTag)
	{
		if (!AbilitySystemComponent || !InputTag.IsValid())
		{
			return nullptr;
		}

		const FGameplayAbilitySpecHandle Handle = AbilitySystemComponent->GiveAbility(
			FGameplayAbilitySpec(UShooterInputBufferTestAbility::StaticClass(), /*AbilityLevel*/ 1, INDEX_NONE, nullptr));
		FGameplayAbilitySpec* Spec = AbilitySystemComponent->FindAbilitySpecFromHandle(Handle);
		if (!Test.TestNotNull(TEXT("counting ability spec granted"), Spec))
		{
			return nullptr;
		}

		Spec->GetDynamicSpecSourceTags().AddTag(InputTag);

		UShooterInputBufferTestAbility* AbilityInstance =
			Cast<UShooterInputBufferTestAbility>(Spec->GetPrimaryInstance());
		return Test.TestNotNull(TEXT("counting ability instance created"), AbilityInstance) ? AbilityInstance : nullptr;
	}

	/** 从 BP_ShooterCharacter 读取真实的 Input Action 引用；属性缺失或未配置时返回 nullptr。 */
	const UInputAction* ResolveCharacterInputAction(FAutomationTestBase& Test, const TCHAR* PropertyName)
	{
		const UClass* CharacterClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Shooter/Blueprints/Characters/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		if (!Test.TestNotNull(TEXT("BP_ShooterCharacter can be loaded"), CharacterClass))
		{
			return nullptr;
		}

		const FObjectProperty* ActionProperty = FindFProperty<FObjectProperty>(CharacterClass, PropertyName);
		if (!Test.TestNotNull(TEXT("character exposes the input action property"), ActionProperty))
		{
			return nullptr;
		}

		const AShooterCharacter* CharacterDefaults = CharacterClass->GetDefaultObject<AShooterCharacter>();
		return CharacterDefaults
			? Cast<UInputAction>(ActionProperty->GetObjectPropertyValue_InContainer(CharacterDefaults))
			: nullptr;
	}

	/** Action 自身配置的 Trigger 类名（逗号分隔）；没有配置时返回 "(implicit)"。 */
	FString DescribeActionTriggers(const UInputAction* Action)
	{
		if (!Action)
		{
			return TEXT("(no action)");
		}

		if (Action->Triggers.IsEmpty())
		{
			// 未配置显式 Trigger：Enhanced Input 按隐式 Down 处理，按下期间持续 actuated，松开产生 Completed。
			return TEXT("(implicit)");
		}

		TArray<FString> Names;
		for (const TObjectPtr<UInputTrigger>& Trigger : Action->Triggers)
		{
			Names.Add(Trigger ? Trigger->GetClass()->GetName() : TEXT("None"));
		}

		return FString::Join(Names, TEXT(","));
	}

	FString DescribeMappingTriggers(const FEnhancedActionKeyMapping& Mapping)
	{
		if (Mapping.Triggers.IsEmpty())
		{
			return TEXT("(none)");
		}

		TArray<FString> Names;
		for (const TObjectPtr<UInputTrigger>& Trigger : Mapping.Triggers)
		{
			Names.Add(Trigger ? Trigger->GetClass()->GetName() : TEXT("None"));
		}

		return FString::Join(Names, TEXT(","));
	}

	FString DescribeMappingModifiers(const FEnhancedActionKeyMapping& Mapping)
	{
		if (Mapping.Modifiers.IsEmpty())
		{
			return TEXT("(none)");
		}

		TArray<FString> Names;
		for (const TObjectPtr<UInputModifier>& Modifier : Mapping.Modifiers)
		{
			Names.Add(Modifier ? Modifier->GetClass()->GetName() : TEXT("None"));
		}

		return FString::Join(Names, TEXT(","));
	}

	/** 把 IMC 的每一条映射完整打到日志：Action / Key / Trigger / Modifier。 */
	void LogInputMappingContext(const UInputMappingContext* InputMappingContext)
	{
		if (!InputMappingContext)
		{
			return;
		}

		for (const FEnhancedActionKeyMapping& Mapping : InputMappingContext->GetMappings())
		{
			UE_LOG(
				LogShootGame,
				Display,
				TEXT("INPUT_BINDING Action=%s Key=%s ValueType=%d Triggers=%s Modifiers=%s"),
				*GetNameSafe(Mapping.Action),
				*Mapping.Key.ToString(),
				Mapping.Action ? static_cast<int32>(Mapping.Action->ValueType) : INDEX_NONE,
				*DescribeMappingTriggers(Mapping),
				*DescribeMappingModifiers(Mapping));
		}
	}
}

/**
 * 输入形态契约：Ability 输入各自是"可按住"还是"只能瞬时触发"，由真实资产配置决定。
 *
 * 结论（读自 BP_ShooterCharacter + IA_* + IMC_Weapons，不来自 C++ BindAction）：
 * - IA_Shoot：显式 [Pressed, Released] → 按下期间保持 actuated，是 level input；
 * - IA_Reload：无显式 Trigger（隐式 Down）→ 按住期间持续 actuated，是 level input；
 * - IA_SwapWeapon：显式 [Pressed]（Axis1D）→ 单帧脉冲，是 edge-only input。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAbilityInputBindingSemanticsTest,
	"ShootGame.Ability.InputLifecycle.BindingSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityInputBindingSemanticsTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityInputLifecycleAutomationTests;

	const UInputAction* FireAction = ResolveCharacterInputAction(*this, TEXT("FireAction"));
	const UInputAction* ReloadAction = ResolveCharacterInputAction(*this, TEXT("ReloadAction"));
	const UInputAction* SwitchWeaponAction = ResolveCharacterInputAction(*this, TEXT("SwitchWeaponAction"));
	if (!TestNotNull(TEXT("FireAction configured"), FireAction) ||
		!TestNotNull(TEXT("ReloadAction configured"), ReloadAction) ||
		!TestNotNull(TEXT("SwitchWeaponAction configured"), SwitchWeaponAction))
	{
		return false;
	}

	UE_LOG(LogShootGame, Display, TEXT("INPUT_ACTION Fire=%s ValueType=%d Triggers=%s"),
		*GetNameSafe(FireAction), static_cast<int32>(FireAction->ValueType), *DescribeActionTriggers(FireAction));
	UE_LOG(LogShootGame, Display, TEXT("INPUT_ACTION Reload=%s ValueType=%d Triggers=%s"),
		*GetNameSafe(ReloadAction), static_cast<int32>(ReloadAction->ValueType), *DescribeActionTriggers(ReloadAction));
	UE_LOG(LogShootGame, Display, TEXT("INPUT_ACTION SwitchWeapon=%s ValueType=%d Triggers=%s"),
		*GetNameSafe(SwitchWeaponAction), static_cast<int32>(SwitchWeaponAction->ValueType), *DescribeActionTriggers(SwitchWeaponAction));

	// ---- Fire：level input（按下期间保持 actuated） ----
	TestEqual(TEXT("fire action configures exactly two explicit triggers"), FireAction->Triggers.Num(), 2);
	TestTrue(TEXT("fire action detects the press edge"),
		FireAction->Triggers.ContainsByPredicate([](const TObjectPtr<UInputTrigger>& Trigger)
		{
			return Trigger && Trigger->IsA<UInputTriggerPressed>();
		}));
	TestTrue(TEXT("fire action stays actuated until the physical release"),
		FireAction->Triggers.ContainsByPredicate([](const TObjectPtr<UInputTrigger>& Trigger)
		{
			return Trigger && Trigger->IsA<UInputTriggerReleased>();
		}));

	// ---- Reload：level input（隐式 Down） ----
	TestEqual(TEXT("reload action is a boolean key action"),
		static_cast<int32>(ReloadAction->ValueType), static_cast<int32>(EInputActionValueType::Boolean));
	TestTrue(TEXT("reload action relies on the implicit down trigger"), ReloadAction->Triggers.IsEmpty());

	// ---- Equip.Next / Previous：edge-only input（显式 Pressed 单帧脉冲） ----
	TestEqual(TEXT("swap weapon action is an axis action"),
		static_cast<int32>(SwitchWeaponAction->ValueType), static_cast<int32>(EInputActionValueType::Axis1D));
	TestEqual(TEXT("swap weapon action configures exactly one explicit trigger"),
		SwitchWeaponAction->Triggers.Num(), 1);
	TestTrue(TEXT("swap weapon action is a one-shot pressed trigger"),
		SwitchWeaponAction->Triggers[0] && SwitchWeaponAction->Triggers[0]->IsA<UInputTriggerPressed>());

	// ---- 输入面收敛：IMC 只驱动这三个 Ability 输入，不存在第四个可能永久 Held 的 Tag ----
	const UInputMappingContext* InputMappingContext = LoadObject<UInputMappingContext>(nullptr,
		TEXT("/Game/Shooter/Input/IMC_Weapons.IMC_Weapons"));
	if (!TestNotNull(TEXT("IMC_Weapons can be loaded"), InputMappingContext))
	{
		return false;
	}

	LogInputMappingContext(InputMappingContext);

	TSet<const UInputAction*> MappedActions;
	bool bFoundReloadKey = false;
	bool bFoundNextWheel = false;
	bool bFoundPreviousWheel = false;
	for (const FEnhancedActionKeyMapping& Mapping : InputMappingContext->GetMappings())
	{
		if (!Mapping.Action)
		{
			continue;
		}

		MappedActions.Add(Mapping.Action);

		if (Mapping.Action == ReloadAction && Mapping.Key == EKeys::R)
		{
			bFoundReloadKey = true;
		}

		if (Mapping.Action == SwitchWeaponAction && Mapping.Key == EKeys::MouseScrollDown)
		{
			bFoundNextWheel = true;
		}

		if (Mapping.Action == SwitchWeaponAction && Mapping.Key == EKeys::MouseScrollUp)
		{
			bFoundPreviousWheel = true;
		}
	}

	TestEqual(TEXT("the mapping context drives exactly the three ability inputs"), MappedActions.Num(), 3);
	TestTrue(TEXT("the mapping context drives IA_Shoot"), MappedActions.Contains(FireAction));
	TestTrue(TEXT("the mapping context drives IA_Reload"), MappedActions.Contains(ReloadAction));
	TestTrue(TEXT("the mapping context drives IA_SwapWeapon"), MappedActions.Contains(SwitchWeaponAction));
	TestTrue(TEXT("IA_Reload is bound to a physical key with a real release"), bFoundReloadKey);
	TestTrue(TEXT("mouse wheel down requests the next weapon"), bFoundNextWheel);
	TestTrue(TEXT("mouse wheel up requests the previous weapon"), bFoundPreviousWheel);

	// ---- 真实注册的绑定：每个输入的按压生命周期必须在 Character 上闭环 ----
	// 自动化世界没有 LocalPlayer，因此不驱动 Enhanced Input 的运行时求值；
	// 这里直接检查生产绑定清单本身：注册了哪些 Action 的哪些 TriggerEvent。
	UWorld* BindingWorld = CreateInputLifecycleTestWorld();
	if (!TestNotNull(TEXT("input lifecycle binding world created"), BindingWorld))
	{
		return false;
	}

	const FInputLifecycleFixture BindingFixture = CreateInputLifecycleFixture(*this, BindingWorld);
	if (!BindingFixture.Character)
	{
		DestroyInputLifecycleTestWorld(BindingWorld);
		return false;
	}

	// 测试类型是纯 C++ 类，输入 Action 引用来自 BP_ShooterCharacter（与生产同一批资产），
	// 这里显式注入，然后再注册生产绑定。
	BindingFixture.Character->SetAbilityInputActionsForTest(const_cast<UInputAction*>(FireAction),
		const_cast<UInputAction*>(ReloadAction), const_cast<UInputAction*>(SwitchWeaponAction));

	UEnhancedInputComponent* InputComponent = NewObject<UEnhancedInputComponent>(BindingFixture.Character);
	BindingFixture.Character->SetupPlayerInputComponentForTest(InputComponent);

	int32 ReloadStarted = 0;
	int32 ReloadCompleted = 0;
	int32 ReloadCanceled = 0;
	int32 SwitchStarted = 0;
	int32 SwitchCompleted = 0;
	int32 FireStarted = 0;
	int32 FireCompleted = 0;
	for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : InputComponent->GetActionEventBindings())
	{
		if (!Binding)
		{
			continue;
		}

		const UInputAction* BoundAction = Binding->GetAction();
		const ETriggerEvent BoundEvent = Binding->GetTriggerEvent();
		if (BoundAction == ReloadAction)
		{
			ReloadStarted += BoundEvent == ETriggerEvent::Started ? 1 : 0;
			ReloadCompleted += BoundEvent == ETriggerEvent::Completed ? 1 : 0;
			ReloadCanceled += BoundEvent == ETriggerEvent::Canceled ? 1 : 0;
		}
		else if (BoundAction == SwitchWeaponAction)
		{
			SwitchStarted += BoundEvent == ETriggerEvent::Started ? 1 : 0;
			SwitchCompleted += BoundEvent == ETriggerEvent::Completed ? 1 : 0;
		}
		else if (BoundAction == FireAction)
		{
			FireStarted += BoundEvent == ETriggerEvent::Started ? 1 : 0;
			FireCompleted += BoundEvent == ETriggerEvent::Completed ? 1 : 0;
		}
	}

	TestEqual(TEXT("reload registers the press edge"), ReloadStarted, 1);
	TestEqual(TEXT("reload registers the physical release"), ReloadCompleted, 1);
	TestEqual(TEXT("reload registers the cancelled lifecycle end"), ReloadCanceled, 1);
	TestEqual(TEXT("swap weapon registers only the press edge"), SwitchStarted, 1);
	TestEqual(TEXT("swap weapon registers no release (edge-only input)"), SwitchCompleted, 0);
	TestEqual(TEXT("fire keeps its press edge"), FireStarted, 1);
	TestEqual(TEXT("fire keeps its physical release"), FireCompleted, 1);

	DestroyInputLifecycleTestWorld(BindingWorld);
	return true;
}

/** 换弹输入：按下产生一次尝试，松开（Completed / Canceled 共用入口）必须回收 Held。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAbilityInputReloadEdgeLifecycleTest,
	"ShootGame.Ability.InputLifecycle.ReloadEdgeLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityInputReloadEdgeLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityInputLifecycleAutomationTests;

	UWorld* World = CreateInputLifecycleTestWorld();
	if (!TestNotNull(TEXT("input lifecycle test world created"), World))
	{
		return false;
	}

	const FInputLifecycleFixture Fixture = CreateInputLifecycleFixture(*this, World);
	UShooterInputBufferTestAbility* Ability = Fixture.AbilitySystemComponent
		? GrantCountingAbility(*this, Fixture.AbilitySystemComponent, ShooterGameplayTags::Input_Reload)
		: nullptr;
	if (!Fixture.Character || !Ability)
	{
		DestroyInputLifecycleTestWorld(World);
		return false;
	}

	// ---- Started：本帧出现 Press edge，并且输入确实处于按住状态 ----
	Fixture.Character->DoReload();
	TestTrue(TEXT("the reload press edge is recorded in this frame"),
		Fixture.AbilitySystemComponent->IsInputTagPressedThisFrame(ShooterGameplayTags::Input_Reload));
	TestTrue(TEXT("the reload input is held while the key is down"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Reload));

	Fixture.AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("the reload press edge attempts the ability exactly once"), Ability->ActivationCountForTest, 1);

	// ---- 仍然按住：Pressed 已被解释层消费，Held 保持 ----
	Fixture.AbilitySystemComponent->ProcessAbilityInputForTest();
	TestFalse(TEXT("the press edge is consumed by the input pass"),
		Fixture.AbilitySystemComponent->IsInputTagPressedThisFrame(ShooterGameplayTags::Input_Reload));
	TestTrue(TEXT("the reload input stays held while the key stays down"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Reload));
	TestEqual(TEXT("holding alone never re-activates a press-triggered ability"), Ability->ActivationCountForTest, 1);

	// ---- Completed / Canceled：真实松开必须回收输入意图 ----
	Fixture.Character->DoStopReload();
	TestFalse(TEXT("the physical release clears the held intent immediately"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Reload));
	TestTrue(TEXT("the release edge is recorded in this frame"),
		Fixture.AbilitySystemComponent->IsInputTagReleasedThisFrameForTest(ShooterGameplayTags::Input_Reload));

	Fixture.AbilitySystemComponent->ProcessAbilityInputForTest();
	TestFalse(TEXT("the reload input is not held any more on the next frame"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Reload));
	TestFalse(TEXT("the release edge is consumed by the next input pass"),
		Fixture.AbilitySystemComponent->IsInputTagReleasedThisFrameForTest(ShooterGameplayTags::Input_Reload));
	TestEqual(TEXT("a release never activates anything"), Ability->ActivationCountForTest, 1);
	TestEqual(TEXT("no held input tag survives anywhere"),
		Fixture.AbilitySystemComponent->GetHeldInputTagCountForTest(), 0);
	TestEqual(TEXT("no superseded press window is left behind"),
		Fixture.AbilitySystemComponent->GetBufferedInputCountForTest(), 0);

	// ---- 再次按下：仍然是一次输入一次尝试 ----
	Fixture.Character->DoReload();
	Fixture.AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("a second press is a second independent attempt"), Ability->ActivationCountForTest, 2);

	DestroyInputLifecycleTestWorld(World);
	return true;
}

/** 切枪 Next：一次输入 = 一个 edge = 一次请求，永不进入 Held。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAbilityInputEquipNextEdgeTest,
	"ShootGame.Ability.InputLifecycle.EquipNextEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityInputEquipNextEdgeTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityInputLifecycleAutomationTests;

	UWorld* World = CreateInputLifecycleTestWorld();
	if (!TestNotNull(TEXT("input lifecycle test world created"), World))
	{
		return false;
	}

	const FInputLifecycleFixture Fixture = CreateInputLifecycleFixture(*this, World);
	UShooterInputBufferTestAbility* Ability = Fixture.AbilitySystemComponent
		? GrantCountingAbility(*this, Fixture.AbilitySystemComponent, ShooterGameplayTags::Input_Equip_Next)
		: nullptr;
	if (!Fixture.Character || !Ability)
	{
		DestroyInputLifecycleTestWorld(World);
		return false;
	}

	// 与 Enhanced Input 的 Axis1D Started 事件一致：正值表示下一把。
	Fixture.Character->DoSwitchWeaponInput(FInputActionValue(/*Axis1D*/ 1.0f));
	TestTrue(TEXT("the equip-next edge is recorded in this frame"),
		Fixture.AbilitySystemComponent->IsInputTagPressedThisFrame(ShooterGameplayTags::Input_Equip_Next));
	TestFalse(TEXT("an edge-only input never enters the held collection"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Equip_Next));

	Fixture.AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("one equip-next input attempts one activation"), Ability->ActivationCountForTest, 1);

	Fixture.AbilitySystemComponent->ProcessAbilityInputForTest();
	TestFalse(TEXT("the next frame has no press edge"),
		Fixture.AbilitySystemComponent->IsInputTagPressedThisFrame(ShooterGameplayTags::Input_Equip_Next));
	TestFalse(TEXT("the next frame has no held intent"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Equip_Next));
	TestEqual(TEXT("no further attempt without a new input"), Ability->ActivationCountForTest, 1);
	TestEqual(TEXT("no held input tag survives anywhere"),
		Fixture.AbilitySystemComponent->GetHeldInputTagCountForTest(), 0);

	DestroyInputLifecycleTestWorld(World);
	return true;
}

/** 切枪 Previous：与 Next 同一语义，方向相反。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAbilityInputEquipPreviousEdgeTest,
	"ShootGame.Ability.InputLifecycle.EquipPreviousEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityInputEquipPreviousEdgeTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityInputLifecycleAutomationTests;

	UWorld* World = CreateInputLifecycleTestWorld();
	if (!TestNotNull(TEXT("input lifecycle test world created"), World))
	{
		return false;
	}

	const FInputLifecycleFixture Fixture = CreateInputLifecycleFixture(*this, World);
	UShooterInputBufferTestAbility* Ability = Fixture.AbilitySystemComponent
		? GrantCountingAbility(*this, Fixture.AbilitySystemComponent, ShooterGameplayTags::Input_Equip_Previous)
		: nullptr;
	if (!Fixture.Character || !Ability)
	{
		DestroyInputLifecycleTestWorld(World);
		return false;
	}

	Fixture.Character->DoSwitchWeaponInput(FInputActionValue(/*Axis1D*/ -1.0f));
	TestTrue(TEXT("the equip-previous edge is recorded in this frame"),
		Fixture.AbilitySystemComponent->IsInputTagPressedThisFrame(ShooterGameplayTags::Input_Equip_Previous));
	TestFalse(TEXT("an edge-only input never enters the held collection"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Equip_Previous));

	Fixture.AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("one equip-previous input attempts one activation"), Ability->ActivationCountForTest, 1);

	Fixture.AbilitySystemComponent->ProcessAbilityInputForTest();
	TestFalse(TEXT("the next frame has no held intent"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Equip_Previous));
	TestEqual(TEXT("no further attempt without a new input"), Ability->ActivationCountForTest, 1);
	TestEqual(TEXT("no held input tag survives anywhere"),
		Fixture.AbilitySystemComponent->GetHeldInputTagCountForTest(), 0);

	DestroyInputLifecycleTestWorld(World);
	return true;
}

/** 开火对照：level input 仍然按住即 Held，松开即清除。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAbilityInputFireHeldControlTest,
	"ShootGame.Ability.InputLifecycle.FireHeldControl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityInputFireHeldControlTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityInputLifecycleAutomationTests;

	UWorld* World = CreateInputLifecycleTestWorld();
	if (!TestNotNull(TEXT("input lifecycle test world created"), World))
	{
		return false;
	}

	const FInputLifecycleFixture Fixture = CreateInputLifecycleFixture(*this, World);
	UShooterInputBufferTestAbility* Ability = Fixture.AbilitySystemComponent
		? GrantCountingAbility(*this, Fixture.AbilitySystemComponent, ShooterGameplayTags::Input_Fire)
		: nullptr;
	if (!Fixture.Character || !Ability)
	{
		DestroyInputLifecycleTestWorld(World);
		return false;
	}

	Fixture.Character->DoStartFiring();
	TestTrue(TEXT("the fire press edge is recorded in this frame"),
		Fixture.AbilitySystemComponent->IsInputTagPressedThisFrame(ShooterGameplayTags::Input_Fire));
	TestTrue(TEXT("the fire input is held while the key is down"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Fire));

	Fixture.AbilitySystemComponent->ProcessAbilityInputForTest();
	TestEqual(TEXT("the fire press edge attempts the ability"), Ability->ActivationCountForTest, 1);

	// 下一帧仍按住：Held 必须保持。
	Fixture.AbilitySystemComponent->ProcessAbilityInputForTest();
	TestTrue(TEXT("the fire input stays held across frames"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Fire));
	TestFalse(TEXT("the fire press edge is consumed by the input pass"),
		Fixture.AbilitySystemComponent->IsInputTagPressedThisFrame(ShooterGameplayTags::Input_Fire));

	Fixture.Character->DoStopFiring();
	TestFalse(TEXT("the physical release clears the held intent immediately"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Fire));

	Fixture.AbilitySystemComponent->ProcessAbilityInputForTest();
	TestFalse(TEXT("the fire input is not held any more on the next frame"),
		Fixture.AbilitySystemComponent->IsInputTagHeld(ShooterGameplayTags::Input_Fire));
	TestEqual(TEXT("no held input tag survives anywhere"),
		Fixture.AbilitySystemComponent->GetHeldInputTagCountForTest(), 0);

	DestroyInputLifecycleTestWorld(World);
	return true;
}

#endif
