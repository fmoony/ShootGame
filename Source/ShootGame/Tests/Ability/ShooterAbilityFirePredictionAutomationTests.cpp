// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/Abilities/ShooterGameplayAbility_Fire.h"
#include "AbilitySystem/ShooterGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "NiagaraSystem.h"
#include "Characters/ShooterCharacter.h"
#include "Sound/SoundWave.h"
#include "Tests/Equipment/ShooterWeaponPresentationTestTypes.h"
#include "Tests/Weapon/ShooterWeaponTestTableTypes.h"
#include "UObject/Package.h"
#include "Weapons/Data/ShooterWeaponConfigRow.h"
#include "Weapons/Projectile/ShooterProjectile.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/Subsystems/ShooterWeaponRuntimeSubsystem.h"

/**
 * P1-A：拥有者本地开火表现路径的运行时证据。
 *
 * 本文件不使用纯 CDO 断言。先建立最小 Editor Test World，生成真实 Weapon 与本地玩家 Holder，
 * 再驱动 PlayOwnerPredictedShotFeedback 并比对调用前后的权威字段。
 *
 * 世界能力边界：本世界不加载骨骼网格与 AnimBP，因此第一人称 Montage 实际播放、
 * 以及挂在 Muzzle Socket 上的 Niagara 生成都无法在这里正向断言。
 * 前者由 FirstPersonCapture 与 P1-B 网络场景覆盖；这里只覆盖可确定判定的组合。
 * Dedicated 不播放的结论按计划移到 P1-B / P1-D 网络场景。
 */
namespace ShooterAbilityFirePredictionAutomationTests
{
	/** 测试世界没有 NetDriver，GetNetMode 落到 NM_Standalone，控制器因此被判定为本地控制器。 */
	UWorld* CreatePredictionTestWorld()
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

	void DestroyPredictionTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	/**
	 * 生成并占有一个本地玩家角色。
	 * 只有同时满足 IsPlayerControlled 与 IsLocallyControlled，才构成本入口要求的拥有者本地视图。
	 *
	 * UE 5.6 的 APawn::IsPlayerControlled() 实现是 GetPlayerState() && !GetPlayerState()->IsABot()
	 * （Pawn.cpp:265-268），因此它必须有 PlayerState 才为真。
	 * 本测试世界没有 GameMode，AController::InitPlayerState() 不会生成 PlayerState，这里显式补一个。
	 */
	AShooterCharacter* SpawnLocalPlayerCharacter(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}

		APlayerController* PlayerController = World->SpawnActor<APlayerController>(FVector::ZeroVector,
			FRotator::ZeroRotator);
		if (!PlayerController)
		{
			return nullptr;
		}
		PlayerController->DispatchBeginPlay();

		if (PlayerController->PlayerState == nullptr)
		{
			FActorSpawnParameters PlayerStateSpawnParameters;
			PlayerStateSpawnParameters.Owner = PlayerController;
			PlayerStateSpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			PlayerController->PlayerState = World->SpawnActor<APlayerState>(APlayerState::StaticClass(),
				PlayerStateSpawnParameters);
		}

		AShooterCharacter* Character = World->SpawnActor<AShooterWeaponPresentationTestCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Character)
		{
			return nullptr;
		}
		Character->DispatchBeginPlay();

		PlayerController->Possess(Character);
		return Character;
	}

	/** 用指定表现资产组合从运行时池取出一把测试武器；返回 nullptr 表示取用失败。 */
	AShooterWeapon* AcquireFeedbackTestWeapon(UWorld* World, AActor* WeaponOwner, UAnimMontage* Montage,
		UNiagaraSystem* Muzzle, USoundBase* Sound, float Recoil,
		TSubclassOf<AShooterWeapon> WeaponClass = AShooterWeaponPresentationTestWeaponPrimary::StaticClass())
	{
		UShooterWeaponRuntimeSubsystem* Runtime = World
			? World->GetSubsystem<UShooterWeaponRuntimeSubsystem>()
			: nullptr;
		UDataTable* Table = GetOrInjectRuntimeTestTable(World);
		if (!Runtime || !Table || !WeaponOwner || !WeaponClass)
		{
			return nullptr;
		}

		FShooterWeaponConfigRow Row = MakeTestWeaponRow(WeaponClass, /*MagazineSize*/ 10,
			/*InitialReserveAmmo*/ -1, /*InitialPoolSize*/ 1);
		Row.RefireRate = 0.5f;
		Row.FiringMontage = Montage;
		Row.MuzzleFlash = Muzzle;
		Row.FireSound = Sound;
		Row.FiringRecoil = Recoil;

		const FName RowName = AddTestWeaponRow(Table, Row);
		Runtime->InitializeWeaponRuntimeForTest();

		AShooterWeapon* Weapon = Runtime->AcquireWeapon(RowName, WeaponOwner, nullptr);
		if (Weapon && !Weapon->HasActorBegunPlay())
		{
			// 本世界不自动投递 BeginPlay；WeaponOwner 绑定依赖它。
			Weapon->DispatchBeginPlay();
		}
		return Weapon;
	}

	/** 世界内当前弹丸数量：证明本地表现路径不生成弹丸。 */
	int32 CountProjectiles(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AShooterProjectile> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFirePredictionLocalFeedbackCosmeticOnlyTest,
	"ShootGame.Ability.Fire.Prediction.LocalFeedbackCosmeticOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFirePredictionLocalFeedbackCosmeticOnlyTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFirePredictionAutomationTests;

	UWorld* World = CreatePredictionTestWorld();
	if (!TestNotNull(TEXT("prediction test world created"), World))
	{
		return false;
	}

	AShooterCharacter* Character = SpawnLocalPlayerCharacter(World);
	if (!TestNotNull(TEXT("local player character spawned"), Character))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	// 前置：本地玩家视图必须成立，否则本用例没有验证对象，应显式失败而不是静默通过。
	const bool bLocalPlayerView = Character->IsPlayerControlled() && Character->IsLocallyControlled();
	if (!TestTrue(TEXT("character is both player controlled and locally controlled"), bLocalPlayerView))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	// ---- 组合 1：四项表现资产全空 → 提前返回，不提交任何内容 ----
	AShooterWeapon* SilentWeapon = AcquireFeedbackTestWeapon(World, Character, nullptr, nullptr, nullptr, 0.0f);
	if (!TestNotNull(TEXT("all-missing test weapon acquired"), SilentWeapon))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	SilentWeapon->ResetFireFeedbackCountersForAutomationTest();
	TestFalse(TEXT("all-missing feedback submits nothing"), SilentWeapon->PlayOwnerPredictedShotFeedback());
	TestEqual(TEXT("all-missing feedback leaves counter unchanged"),
		SilentWeapon->GetPredictedOwnerFeedbackCountForAutomationTest(), 0);

	// ---- 组合 2：只有 Recoil → 缺失的 Montage / Muzzle / Sound 不得连带吞掉 Recoil ----
	AShooterWeapon* RecoilWeapon = AcquireFeedbackTestWeapon(World, Character, nullptr, nullptr, nullptr, 5.0f);
	if (!TestNotNull(TEXT("recoil-only test weapon acquired"), RecoilWeapon))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	const int32 AmmoBefore = RecoilWeapon->GetBulletCount();
	const int32 ProjectilesBefore = CountProjectiles(World);
	RecoilWeapon->ResetFireFeedbackCountersForAutomationTest();

	const bool bFirstSubmit = RecoilWeapon->PlayOwnerPredictedShotFeedback();
	const bool bSecondSubmit = RecoilWeapon->PlayOwnerPredictedShotFeedback();
	const bool bThirdSubmit = RecoilWeapon->PlayOwnerPredictedShotFeedback();

	// 表现入口只负责四路 cosmetic，不再自己判定/推进本地开火节拍：
	// 节拍由 GA 在"本地 Shot Attempt 被接受"处统一推进，重复调用本入口一律照播。
	TestTrue(TEXT("first recoil-only feedback is submitted"), bFirstSubmit);
	TestTrue(TEXT("the cosmetic entry does not pace itself with the cadence"), bSecondSubmit);
	TestTrue(TEXT("the cosmetic entry keeps playing on every attempt"), bThirdSubmit);
	TestEqual(TEXT("every cosmetic attempt is counted"),
		RecoilWeapon->GetPredictedOwnerFeedbackCountForAutomationTest(), 3);
	TestEqual(TEXT("recoil-only feedback records the recoil channel"),
		Character->GetOwnerLocalRecoilCountForAutomationTest(), 3);
	TestEqual(TEXT("recoil-only feedback does not record montage"),
		Character->GetOwnerLocalMontageCountForAutomationTest(), 0);
	TestEqual(TEXT("local feedback does not consume magazine ammo"), RecoilWeapon->GetBulletCount(), AmmoBefore);
	TestEqual(TEXT("local feedback does not spawn a projectile"), CountProjectiles(World), ProjectilesBefore);
	TestEqual(TEXT("local feedback does not record an authority shot"),
		RecoilWeapon->GetAuthorityShotCountForAutomationTest(), 0);

	// 本地节拍只由 Shot Attempt 推进，且时长按本武器 RefireRate。
	TestTrue(TEXT("the cadence starts ready on a fresh weapon"), RecoilWeapon->IsLocalFireCooldownReady());
	RecoilWeapon->AdvanceLocalFireCooldown();
	TestFalse(TEXT("advancing the cadence closes it"), RecoilWeapon->IsLocalFireCooldownReady());
	TestTrue(TEXT("the cadence duration follows RefireRate"),
		FMath::IsNearlyEqual(RecoilWeapon->GetLocalFireCooldownRemaining(), RecoilWeapon->GetRefireRate(), 0.01f));

	// ---- 组合 3：只有 Sound → Recoil 为零不得连带吞掉 Sound ----
	USoundWave* TransientSound = NewObject<USoundWave>(GetTransientPackage());
	AShooterWeapon* SoundWeapon = AcquireFeedbackTestWeapon(World, Character, nullptr, nullptr, TransientSound, 0.0f,
		AShooterWeaponPresentationTestWeaponSecondary::StaticClass());
	if (!TestNotNull(TEXT("sound-only test weapon acquired"), SoundWeapon))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	SoundWeapon->ResetFireFeedbackCountersForAutomationTest();
	TestTrue(TEXT("another weapon has an independent feedback cooldown"),
		SoundWeapon->PlayOwnerPredictedShotFeedback());
	TestEqual(TEXT("sound-only feedback counted once"),
		SoundWeapon->GetPredictedOwnerFeedbackCountForAutomationTest(), 1);
	TestEqual(TEXT("sound-only feedback records the sound channel"),
		SoundWeapon->GetOwnerSoundFeedbackCountForAutomationTest(), 1);
	TestEqual(TEXT("sound-only feedback does not record muzzle"),
		SoundWeapon->GetOwnerMuzzleFeedbackCountForAutomationTest(), 0);

	// ---- 组合 4：Montage 与 Muzzle 存在但世界没有骨骼网格 → 仍不得吞掉 Recoil，也不得崩溃 ----
	UAnimMontage* TransientMontage = NewObject<UAnimMontage>(GetTransientPackage());
	UNiagaraSystem* TransientMuzzle = NewObject<UNiagaraSystem>(GetTransientPackage());
	AShooterWeapon* MeshlessWeapon = AcquireFeedbackTestWeapon(World, Character, TransientMontage,
		TransientMuzzle, nullptr, 5.0f);
	if (!TestNotNull(TEXT("meshless presentation test weapon acquired"), MeshlessWeapon))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	MeshlessWeapon->ResetFireFeedbackCountersForAutomationTest();
	TestTrue(TEXT("unusable muzzle and montage do not swallow recoil"),
		MeshlessWeapon->PlayOwnerPredictedShotFeedback());
	TestEqual(TEXT("meshless presentation feedback counted once"),
		MeshlessWeapon->GetPredictedOwnerFeedbackCountForAutomationTest(), 1);
	TestEqual(TEXT("missing first-person socket does not claim a muzzle effect"),
		MeshlessWeapon->GetOwnerMuzzleFeedbackCountForAutomationTest(), 0);
	TestEqual(TEXT("missing AnimInstance does not claim a montage"),
		Character->GetOwnerLocalMontageCountForAutomationTest(), 0);

	DestroyPredictionTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFirePredictionLocalFeedbackStatelessTest,
	"ShootGame.Ability.Fire.Prediction.LocalFeedbackStateless",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFirePredictionLocalFeedbackStatelessTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFirePredictionAutomationTests;

	UWorld* World = CreatePredictionTestWorld();
	if (!TestNotNull(TEXT("prediction test world created"), World))
	{
		return false;
	}

	AShooterCharacter* Character = SpawnLocalPlayerCharacter(World);
	if (!TestNotNull(TEXT("local player character spawned"), Character))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	AShooterWeapon* Weapon = AcquireFeedbackTestWeapon(World, Character, nullptr, nullptr, nullptr, 5.0f);
	if (!TestNotNull(TEXT("recoil-only test weapon acquired"), Weapon))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	// 调用前快照：三个权威字段都用 WITH_DEV_AUTOMATION_TESTS 只读探针读取，
	// 不使用 FindFProperty 假装读取非反射字段。
	const bool bFiringBefore = Weapon->IsFiringForAutomationTest();
	const float TimeOfLastShotBefore = Weapon->GetTimeOfLastShotForAutomationTest();
	const bool bRefireActiveBefore = Weapon->IsRefireTimerActiveForAutomationTest();
	const int32 AmmoBefore = Weapon->GetBulletCount();
	const int32 LifecycleBefore = static_cast<int32>(Weapon->GetLifecycleState());
	const int32 AuthorityShotsBefore = Weapon->GetAuthorityShotCountForAutomationTest();

	TestFalse(TEXT("weapon is not firing before the probe"), bFiringBefore);
	TestFalse(TEXT("refire timer is not active before the probe"), bRefireActiveBefore);

	const bool bFirstPredicted = Weapon->PlayOwnerPredictedShotFeedback();
	const bool bImmediatePredicted = Weapon->PlayOwnerPredictedShotFeedback();

	TestTrue(TEXT("first local prediction plays"), bFirstPredicted);
	TestTrue(TEXT("the cosmetic entry is not paced by the cadence"), bImmediatePredicted);

	// 调用后逐项比对。
	TestTrue(TEXT("bIsFiring unchanged after local feedback"), Weapon->IsFiringForAutomationTest() == bFiringBefore);
	TestEqual(TEXT("TimeOfLastShot unchanged after local feedback"),
		Weapon->GetTimeOfLastShotForAutomationTest(), TimeOfLastShotBefore);
	TestTrue(TEXT("RefireTimer active state unchanged after local feedback"),
		Weapon->IsRefireTimerActiveForAutomationTest() == bRefireActiveBefore);
	TestFalse(TEXT("RefireTimer stays inactive"), Weapon->IsRefireTimerActiveForAutomationTest());
	TestEqual(TEXT("magazine ammo unchanged after local feedback"), Weapon->GetBulletCount(), AmmoBefore);
	TestEqual(TEXT("weapon lifecycle state unchanged after local feedback"),
		static_cast<int32>(Weapon->GetLifecycleState()), LifecycleBefore);
	TestEqual(TEXT("authority shot counter unchanged after local feedback"),
		Weapon->GetAuthorityShotCountForAutomationTest(), AuthorityShotsBefore);
	TestEqual(TEXT("the cosmetic entry itself never touches the local fire cadence"),
		Weapon->GetPredictedOwnerFeedbackCountForAutomationTest(), 2);
	TestTrue(TEXT("the cadence is still ready because no Shot Attempt advanced it"),
		Weapon->IsLocalFireCooldownReady());

	DestroyPredictionTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFirePredictionLocalFireCadenceTest,
	"ShootGame.Ability.Fire.Prediction.LocalFireCadence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFirePredictionLocalFireCadenceTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFirePredictionAutomationTests;

	// 本地开火节拍只决定"这次输入是否构成有效 Local Shot Attempt"，
	// 表现入口只负责四路 cosmetic。本用例在武器层证明这条节拍的三条性质：
	//   1. 节拍只读本地时钟，不读服务器权威射速；
	//   2. 节拍只由 Shot Attempt 推进一次，与表现是否播放成功无关；
	//   3. 节拍不写服务器权威射速（TimeOfLastShot / RefireTimer 均不受影响）。
	UWorld* World = CreatePredictionTestWorld();
	if (!TestNotNull(TEXT("prediction test world created"), World))
	{
		return false;
	}

	AShooterCharacter* Character = SpawnLocalPlayerCharacter(World);
	AShooterWeaponLifecycleTestWeapon* Weapon = Cast<AShooterWeaponLifecycleTestWeapon>(
		AcquireFeedbackTestWeapon(World, Character, nullptr, nullptr, nullptr, 5.0f,
			AShooterWeaponLifecycleTestWeapon::StaticClass()));
	if (!TestNotNull(TEXT("local player character spawned"), Character) ||
		!TestNotNull(TEXT("semi-auto lifecycle test weapon acquired"), Weapon))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	TestFalse(TEXT("probe weapon is semi-auto"), Weapon->IsFullAuto());

	// ---- 性质 1：本地节拍不读服务器权威射速 ----
	// 权威 RefireTimer 被人为置为冷却中：服务器此时会拒绝开火，
	// 但本地节拍只由本地时钟决定，因此仍然报告"已越过"。
	TestTrue(TEXT("the local fire cadence starts ready"), Weapon->IsLocalFireCooldownReady());
	Weapon->ArmRefireTimerForTest();
	TestTrue(TEXT("the authority refire timer is active"), Weapon->IsRefireTimerActiveForAutomationTest());
	TestFalse(TEXT("authority refuses a semi-auto shot during its own cooldown"), Weapon->CanStartSemiAutoShotNow());
	TestTrue(TEXT("the local cadence does not read the authority refire timer"), Weapon->IsLocalFireCooldownReady());

	// ---- 性质 2：本地节拍只由"本地 Shot Attempt 被接受"推进，与表现播放成功无关 ----
	Weapon->ResetFireFeedbackCountersForAutomationTest();
	const float TimeOfLastShotBefore = Weapon->GetTimeOfLastShotForAutomationTest();
	const bool bRefireActiveBefore = Weapon->IsRefireTimerActiveForAutomationTest();

	// 表现入口本身不推进节拍：连播三次后节拍仍然 Ready。
	TestTrue(TEXT("the first local fire plays owner feedback"), Weapon->PlayOwnerPredictedShotFeedback());
	TestTrue(TEXT("a second presentation inside the cadence still plays"), Weapon->PlayOwnerPredictedShotFeedback());
	TestTrue(TEXT("the cosmetic entry never advances the cadence"), Weapon->IsLocalFireCooldownReady());
	TestEqual(TEXT("every cosmetic attempt is counted"), Weapon->GetPredictedOwnerFeedbackCountForAutomationTest(), 2);

	// Shot Attempt 被接受处推进一次，时长按 RefireRate。
	Weapon->AdvanceLocalFireCooldown();
	TestFalse(TEXT("the cadence closes when the Shot Attempt advances it"), Weapon->IsLocalFireCooldownReady());
	TestTrue(TEXT("the cadence duration follows RefireRate"),
		FMath::IsNearlyEqual(Weapon->GetLocalFireCooldownRemaining(), Weapon->GetRefireRate(), 0.01f));

	// ---- 性质 2b：不得因为表现被拒就继续推进节拍 ----
	// 节拍只能随时间自然减少；按调用次数漂移会把后续每一发真实射击的表现挡死。
	const float RemainingBefore = Weapon->GetLocalFireCooldownRemaining();
	TestTrue(TEXT("a presentation inside the cadence is still submitted"), Weapon->PlayOwnerPredictedShotFeedback());
	const float RemainingAfter = Weapon->GetLocalFireCooldownRemaining();
	TestTrue(TEXT("the cosmetic entry must not push the local cadence further into the future"),
		RemainingAfter <= RemainingBefore);

	// ---- 性质 3：本地节拍不写服务器权威射速 ----
	TestEqual(TEXT("a local fire leaves TimeOfLastShot untouched"),
		Weapon->GetTimeOfLastShotForAutomationTest(), TimeOfLastShotBefore);
	TestTrue(TEXT("a local fire leaves the authority refire timer untouched"),
		Weapon->IsRefireTimerActiveForAutomationTest() == bRefireActiveBefore);
	TestTrue(TEXT("the armed authority refire timer is still the one from the probe"),
		Weapon->IsRefireTimerActiveForAutomationTest());

	DestroyPredictionTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFirePredictionKnownBlockersTest,
	"ShootGame.Ability.Fire.Prediction.KnownBlockers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFirePredictionKnownBlockersTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFirePredictionAutomationTests;

	UWorld* World = CreatePredictionTestWorld();
	if (!TestNotNull(TEXT("prediction test world created"), World))
	{
		return false;
	}

	AShooterCharacter* Character = SpawnLocalPlayerCharacter(World);
	AShooterWeapon* Weapon = AcquireFeedbackTestWeapon(World, Character, nullptr, nullptr, nullptr, 5.0f);
	UShooterInventoryComponent* Inventory = Character ? Character->GetInventoryComponent() : nullptr;
	UShooterEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	if (!TestNotNull(TEXT("local player character spawned"), Character) ||
		!TestNotNull(TEXT("feedback test weapon acquired"), Weapon) ||
		!TestNotNull(TEXT("inventory component exists"), Inventory) ||
		!TestNotNull(TEXT("equipment component exists"), Equipment))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	if (!TestTrue(TEXT("weapon added to inventory"),
		Inventory->AddWeapon(Weapon) == EShooterInventoryAddResult::Added) ||
		!TestTrue(TEXT("weapon equipped"), Equipment->EquipWeapon(Weapon)))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	UAbilitySystemComponent* AbilitySystemComponent = NewObject<UAbilitySystemComponent>(Character);
	UShooterGameplayAbility_Fire* Ability = NewObject<UShooterGameplayAbility_Fire>();
	if (!TestNotNull(TEXT("ability system component created"), AbilitySystemComponent) ||
		!TestNotNull(TEXT("fire ability created"), Ability))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	TestTrue(TEXT("valid current weapon allows local predicted feedback"),
		Ability->IsOwnerPredictedFeedbackAllowedForTest(Character, Weapon, AbilitySystemComponent));

	// 复制 Ammo 不得成为 Owner 首次开火表现的硬门：
	// 它可能落后于服务器，一旦用它二次否决，服务器已经接受并生成弹丸的那一发就永久没有反馈。
	Weapon->SetAmmoForAutomationTest(0, 0);
	TestTrue(TEXT("an empty replicated magazine must not suppress local predicted feedback"),
		Ability->IsOwnerPredictedFeedbackAllowedForTest(Character, Weapon, AbilitySystemComponent));
	Weapon->SetAmmoForAutomationTest(10, 0);
	TestTrue(TEXT("a non-empty replicated magazine still allows local predicted feedback"),
		Ability->IsOwnerPredictedFeedbackAllowedForTest(Character, Weapon, AbilitySystemComponent));

	// 复制的 Gameplay 状态同样不得单独抑制表现，全部交由服务器权威 Reject。
	const FGameplayTag NonSuppressingStates[] = {
		ShooterGameplayTags::State_Reloading,
		ShooterGameplayTags::State_Equipping,
		ShooterGameplayTags::State_Dead,
	};
	const TCHAR* NonSuppressingMessages[] = {
		TEXT("a stale reloading state must not suppress local predicted feedback"),
		TEXT("a stale equipping state must not suppress local predicted feedback"),
		TEXT("a stale dead state must not suppress local predicted feedback"),
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(NonSuppressingStates); ++Index)
	{
		AbilitySystemComponent->AddLooseGameplayTag(NonSuppressingStates[Index]);
		TestTrue(NonSuppressingMessages[Index],
			Ability->IsOwnerPredictedFeedbackAllowedForTest(Character, Weapon, AbilitySystemComponent));
		AbilitySystemComponent->RemoveLooseGameplayTag(NonSuppressingStates[Index]);
	}

	// 空弹匣 + 换弹中同样必须放行：这正是"服务器完成换弹、客户端镜像还没跟上"的窗口。
	Weapon->SetAmmoForAutomationTest(0, 0);
	AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	TestTrue(TEXT("a reload transaction with an empty mirror still allows local predicted feedback"),
		Ability->IsOwnerPredictedFeedbackAllowedForTest(Character, Weapon, AbilitySystemComponent));
	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	TestTrue(TEXT("clearing the reload transaction keeps local predicted feedback allowed"),
		Ability->IsOwnerPredictedFeedbackAllowedForTest(Character, Weapon, AbilitySystemComponent));
	Weapon->SetAmmoForAutomationTest(10, 0);

	Weapon->SetActorHiddenInGame(true);
	TestFalse(TEXT("hidden weapon blocks local predicted feedback"),
		Ability->IsOwnerPredictedFeedbackAllowedForTest(Character, Weapon, AbilitySystemComponent));
	Weapon->SetActorHiddenInGame(false);

	AShooterWeapon* OtherWeapon = AcquireFeedbackTestWeapon(World, Character, nullptr, nullptr, nullptr, 5.0f,
		AShooterWeaponPresentationTestWeaponSecondary::StaticClass());
	if (!TestNotNull(TEXT("second feedback test weapon acquired"), OtherWeapon) ||
		!TestTrue(TEXT("second weapon added to inventory"),
			Inventory->AddWeapon(OtherWeapon) == EShooterInventoryAddResult::Added) ||
		!TestTrue(TEXT("second weapon equipped"), Equipment->EquipWeapon(OtherWeapon)))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	Weapon->SetActorHiddenInGame(false);
	TestFalse(TEXT("weapon that is no longer current blocks local predicted feedback"),
		Ability->IsOwnerPredictedFeedbackAllowedForTest(Character, Weapon, AbilitySystemComponent));

	DestroyPredictionTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFirePredictionFirstShotReadyAfterAcquireTest,
	"ShootGame.Ability.Fire.Prediction.FirstShotReadyAfterAcquire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFirePredictionFirstShotReadyAfterAcquireTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFirePredictionAutomationTests;

	UWorld* World = CreatePredictionTestWorld();
	if (!TestNotNull(TEXT("prediction test world created"), World))
	{
		return false;
	}

	AShooterCharacter* Character = SpawnLocalPlayerCharacter(World);
	if (!TestNotNull(TEXT("local player character spawned"), Character))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	// 用生命周期测试武器暴露 RefireTimer，直接驱动冷却状态。
	// 修正 11：首次取用时 RefireTimer 未激活，因此可以直接开火；
	// 不能用 TimeOfLastShot == 0.0f 作哨兵，池取用与归还都会把它复位为 0。
	AShooterWeaponLifecycleTestWeapon* Weapon = Cast<AShooterWeaponLifecycleTestWeapon>(
		AcquireFeedbackTestWeapon(World, Character, nullptr, nullptr, nullptr, 0.0f,
			AShooterWeaponLifecycleTestWeapon::StaticClass()));
	if (!TestNotNull(TEXT("semi-auto lifecycle test weapon acquired"), Weapon))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	TestFalse(TEXT("acquired weapon is semi-auto"), Weapon->IsFullAuto());
	TestFalse(TEXT("refire timer is inactive right after acquire"), Weapon->IsRefireTimerActiveForAutomationTest());
	TestEqual(TEXT("acquire resets TimeOfLastShot to zero"), Weapon->GetTimeOfLastShotForAutomationTest(), 0.0f);
	TestTrue(TEXT("first shot is allowed right after acquire"), Weapon->CanStartSemiAutoShotNow());

	// 冷却期：RefireTimer 活动 → 半自动必须拒绝立即开火。
	Weapon->ArmRefireTimerForTest();
	TestTrue(TEXT("refire timer is active while cooling down"), Weapon->IsRefireTimerActiveForAutomationTest());
	TestFalse(TEXT("semi-auto is not allowed to fire during cooldown"), Weapon->CanStartSemiAutoShotNow());

	DestroyPredictionTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFirePredictionRejectRefireCooldownTest,
	"ShootGame.Ability.Fire.Prediction.RejectRefireCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFirePredictionRejectRefireCooldownTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFirePredictionAutomationTests;

	UWorld* World = CreatePredictionTestWorld();
	if (!TestNotNull(TEXT("prediction test world created"), World))
	{
		return false;
	}

	AShooterCharacter* Character = SpawnLocalPlayerCharacter(World);
	if (!TestNotNull(TEXT("local player character spawned"), Character))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	// 半自动：冷却期资格查询必须为 false，服务器 CanActivateAbility 据此拒绝重复激活。
	AShooterWeaponLifecycleTestWeapon* SemiAuto = Cast<AShooterWeaponLifecycleTestWeapon>(
		AcquireFeedbackTestWeapon(World, Character, nullptr, nullptr, nullptr, 0.0f,
			AShooterWeaponLifecycleTestWeapon::StaticClass()));
	if (!TestNotNull(TEXT("semi-auto weapon acquired"), SemiAuto))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	SemiAuto->ArmRefireTimerForTest();
	TestFalse(TEXT("semi-auto cooldown rejects immediate shot"), SemiAuto->CanStartSemiAutoShotNow());
	TestEqual(TEXT("rejected cooldown shot consumes no ammo"), SemiAuto->GetBulletCount(), 10);
	TestEqual(TEXT("rejected cooldown shot records no authority shot"),
		SemiAuto->GetAuthorityShotCountForAutomationTest(), 0);

	// 全自动：不参与射速资格查询，冷却期仍允许激活；真实补射时机由权威 RefireTimer 决定。
	AShooterWeaponLifecycleTestWeapon* FullAuto = Cast<AShooterWeaponLifecycleTestWeapon>(
		AcquireFeedbackTestWeapon(World, Character, nullptr, nullptr, nullptr, 0.0f,
			AShooterWeaponLifecycleTestWeapon::StaticClass()));
	if (!TestNotNull(TEXT("full-auto weapon acquired"), FullAuto))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	FullAuto->SetFullAutoForTest(true);
	TestTrue(TEXT("full-auto weapon reports full auto"), FullAuto->IsFullAuto());
	TestFalse(TEXT("full auto does not participate in the semi-auto readiness query"),
		FullAuto->CanStartSemiAutoShotNow());

	DestroyPredictionTestWorld(World);
	return true;
}

#endif
