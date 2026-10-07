// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/Abilities/ShooterGameplayAbility_Fire.h"
#include "AbilitySystem/ShooterAbilitySystemComponent.h"
#include "AbilitySystem/ShooterGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerState/ShooterPlayerState.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "NiagaraSystem.h"
#include "Characters/ShooterCharacter.h"
#include "Sound/SoundWave.h"
#include "Tests/Equipment/ShooterWeaponPresentationTestTypes.h"
#include "Tests/Weapon/ShooterWeaponTestTableTypes.h"
#include "TimerManager.h"
#include "UObject/Package.h"
#include "Weapons/Data/ShooterWeaponConfigRow.h"
#include "Weapons/Projectile/ShooterProjectile.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/Subsystems/ShooterWeaponRuntimeSubsystem.h"

/**
 * 单发语义下的拥有端预测与裁决证据。
 *
 * 本文件不使用纯 CDO 断言。先建立最小 Editor Test World，生成真实 Weapon 与本地玩家 Holder，
 * 再驱动生产入口比对调用前后的权威字段。
 *
 * 世界能力边界：本世界不加载骨骼网格与 AnimBP，因此第一人称 Montage 实际播放、
 * 以及挂在 Muzzle Socket 上的 Niagara 生成都无法在这里正向断言。
 * 前者由 FirstPersonCapture 与网络场景覆盖；这里只覆盖可确定判定的组合。
 * "一次 Activation = 一发权威 Shot"的服务器侧证据在网络夹具中取证。
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
			// 用项目自己的 PlayerState：本文件的输入策略断言需要真实存在的 Shooter ASC。
			PlayerController->PlayerState = World->SpawnActor<AShooterPlayerState>(
				AShooterPlayerState::StaticClass(), PlayerStateSpawnParameters);
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

	/** 把武器真正装备到角色：确认补播与裁决结清都要求武器仍是当前装备。 */
	bool EquipWeaponForOwner(AShooterCharacter* Character, AShooterWeapon* Weapon)
	{
		UShooterInventoryComponent* Inventory = Character ? Character->GetInventoryComponent() : nullptr;
		UShooterEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
		if (!Inventory || !Equipment || !Weapon)
		{
			return false;
		}

		return Inventory->AddWeapon(Weapon) == EShooterInventoryAddResult::Added && Equipment->EquipWeapon(Weapon);
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

	// 表现入口只负责四路 cosmetic，不自己判定/推进本地开火节拍：
	// 节拍由 GA 在"本地提交一次 Shot Attempt"处统一推进，重复调用本入口一律照播。
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
	TestTrue(TEXT("another weapon has an independent feedback cadence"),
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

	// 调用前快照：权威字段都用 WITH_DEV_AUTOMATION_TESTS 只读探针读取，
	// 不使用 FindFProperty 假装读取非反射字段。
	const float TimeOfLastShotBefore = Weapon->GetTimeOfLastShotForAutomationTest();
	const bool bRefireActiveBefore = Weapon->IsRefireTimerActiveForAutomationTest();
	const int32 AmmoBefore = Weapon->GetBulletCount();
	const int32 LifecycleBefore = static_cast<int32>( Weapon->GetLifecycleState());
	const int32 AuthorityShotsBefore = Weapon->GetAuthorityShotCountForAutomationTest();

	TestTrue(TEXT("a fresh lease has no authority shot yet"), TimeOfLastShotBefore < 0.0f);
	TestFalse(TEXT("refire timer is not active before the probe"), bRefireActiveBefore);

	const bool bFirstPredicted = Weapon->PlayOwnerPredictedShotFeedback();
	const bool bImmediatePredicted = Weapon->PlayOwnerPredictedShotFeedback();

	TestTrue(TEXT("first local prediction plays"), bFirstPredicted);
	TestTrue(TEXT("the cosmetic entry is not paced by the cadence"), bImmediatePredicted);

	// 调用后逐项比对。
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

	// 本地开火节拍只决定"这次输入是否值得形成一次本地 Shot Attempt"，
	// 表现入口只负责四路 cosmetic。本用例在武器层证明这条节拍的三条性质：
	//   1. 节拍只读本地时钟，不读服务器权威射速；
	//   2. 节拍只由 Shot Attempt 推进一次，与表现是否播放成功无关；
	//   3. 节拍不写服务器权威射速（TimeOfLastShot 不受影响）。
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
	// 权威射速被人为置为冷却中：服务器此时会拒绝开火，
	// 但本地节拍只由本地时钟决定，因此仍然报告"已就绪"。
	TestTrue(TEXT("the local fire cadence starts ready"), Weapon->IsLocalFireCooldownReady());
	Weapon->SetAuthorityRefireRemainingForAutomationTest(0.4f);
	TestFalse(TEXT("authority refuses a shot during its own cooldown"), Weapon->CanCommitAuthorityShot());
	TestTrue(TEXT("the local cadence does not read the authority refire clock"), Weapon->IsLocalFireCooldownReady());

	// ---- 性质 2：本地节拍只由"本地提交一次 Shot Attempt"推进，与表现播放成功无关 ----
	Weapon->ResetFireFeedbackCountersForAutomationTest();
	const float TimeOfLastShotBefore = Weapon->GetTimeOfLastShotForAutomationTest();

	// 表现入口本身不推进节拍：连播两次后节拍仍然 Ready。
	TestTrue(TEXT("the first local fire plays owner feedback"), Weapon->PlayOwnerPredictedShotFeedback());
	TestTrue(TEXT("a second presentation inside the cadence still plays"), Weapon->PlayOwnerPredictedShotFeedback());
	TestTrue(TEXT("the cosmetic entry never advances the cadence"), Weapon->IsLocalFireCooldownReady());
	TestEqual(TEXT("every cosmetic attempt is counted"), Weapon->GetPredictedOwnerFeedbackCountForAutomationTest(), 2);

	// Shot Attempt 处推进一次，时长按 RefireRate。
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

	// ---- 性质 4：节拍取证的语义 ----
	// 期望时间必须是"推进之前"的本地节拍终点，实际时间是本次激活时间；
	// 取证只记录事实，不改变 Gameplay 结果：节拍终点仍然等于实际激活时间 + RefireRate。
	Weapon->ResetLocalFireCadenceTraceForAutomationTest();
	Weapon->SetLocalFireCooldownRemainingForAutomationTest(0.0f);
	const float ExpectedDeadline = World->GetTimeSeconds();
	Weapon->AdvanceLocalFireCooldown(4242);

	const TArray<AShooterWeapon::FShooterLocalFireCadenceSample>& Samples = Weapon->GetLocalFireCadenceSamplesForAutomationTest();
	TestEqual(TEXT("every cadence advance appends exactly one trace sample"), Samples.Num(), 1);
	if (Samples.Num() == 1)
	{
		TestEqual(TEXT("the trace sample carries this activation's key"), Samples[0].ActivationKey, 4242);
		TestTrue(TEXT("the expected time is the pre-advance local cadence deadline"),
			FMath::IsNearlyEqual(Samples[0].ExpectedDeadline, ExpectedDeadline, 0.001f));
		TestTrue(TEXT("the lag is measured from that deadline to the actual activation"),
			Samples[0].LagSeconds >= 0.0f && Samples[0].LagSeconds < 0.1f);
		TestTrue(TEXT("the sample reports the current frame delta"), Samples[0].FrameDeltaSeconds >= 0.0f);
	}

	const AShooterWeapon::FShooterLocalFireCadenceStats TraceStats = Weapon->GetLocalFireCadenceStatsForAutomationTest();
	TestEqual(TEXT("the trace statistics describe the traced window"), TraceStats.Samples, 1);
	TestEqual(TEXT("a single sample yields no interval statistics"), TraceStats.IntervalSamples, 0);
	TestTrue(TEXT("the cadence deadline still follows the actual activation time plus RefireRate"),
		FMath::IsNearlyEqual(Weapon->GetLocalFireCooldownRemaining(), Weapon->GetRefireRate(), 0.01f));

	// ---- 性质 5：全自动保持理论射速网格的相位，半自动锚定本次击发 ----
	// 全自动：上一发只迟到了容差以内的 10ms 时，下一发目标时间就是理论网格点，
	// 而不是"本次实际时间 + RefireRate"——后者会把这一发的迟到永久写进后续每一发。
	Weapon->SetRefireRateForAutomationTest(0.1f);
	Weapon->SetFullAutoForTest(true);
	Weapon->SetLocalFireCooldownLagForAutomationTest(0.01f);
	Weapon->AdvanceLocalFireCooldown(7);
	TestTrue(TEXT("the full-auto cadence keeps the theoretical grid phase instead of the lateness"),
		FMath::IsNearlyEqual(Weapon->GetLocalFireCooldownRemaining(), 0.09f, 0.005f));

	// 迟到超过权威容差时，本地不得把下一发排到"本次实际时间 - 容差"之前：
	// 那种请求必然被权威射速门控拒绝，只会浪费一次 Activation。
	Weapon->SetLocalFireCooldownLagForAutomationTest(0.03f);
	Weapon->AdvanceLocalFireCooldown(8);
	TestTrue(TEXT("the full-auto cadence never schedules a request the authority would refuse"),
		Weapon->GetLocalFireCooldownRemaining() >= 0.1f - 0.015f - 0.005f);
	TestTrue(TEXT("the scheduled deadline is always strictly in the future, so no burst is possible"),
		Weapon->GetLocalFireCooldownRemaining() > 0.0f);

	// 长卡顿必须重新基线：落后达到一个完整节拍时不再保留相位债。
	Weapon->SetLocalFireCooldownLagForAutomationTest(0.4f);
	Weapon->AdvanceLocalFireCooldown(9);
	TestTrue(TEXT("a stall rebaselines the full-auto cadence on the current time"),
		FMath::IsNearlyEqual(Weapon->GetLocalFireCooldownRemaining(), 0.1f, 0.005f));

	// 半自动：同一个迟到场景必须按本次实际击发重新起算，
	// 否则本地会放行一次权威端必然拒绝的提前击发。
	Weapon->SetFullAutoForTest(false);
	Weapon->SetLocalFireCooldownLagForAutomationTest(0.03f);
	Weapon->AdvanceLocalFireCooldown(10);
	TestTrue(TEXT("the semi-auto cadence stays anchored on the actual shot"),
		FMath::IsNearlyEqual(Weapon->GetLocalFireCooldownRemaining(), 0.1f, 0.005f));

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
		Ability->IsOwnerFireContextValidForTest(Character, Weapon, AbilitySystemComponent));

	// 复制 Ammo 不得成为 Owner 首次开火表现的硬门：
	// 它可能落后于服务器，一旦用它二次否决，服务器已经接受并生成弹丸的那一发就永久没有反馈。
	Weapon->SetAmmoForAutomationTest(0, 0);
	TestTrue(TEXT("an empty replicated magazine must not suppress local predicted feedback"),
		Ability->IsOwnerFireContextValidForTest(Character, Weapon, AbilitySystemComponent));
	Weapon->SetAmmoForAutomationTest(10, 0);
	TestTrue(TEXT("a non-empty replicated magazine still allows local predicted feedback"),
		Ability->IsOwnerFireContextValidForTest(Character, Weapon, AbilitySystemComponent));

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
			Ability->IsOwnerFireContextValidForTest(Character, Weapon, AbilitySystemComponent));
		AbilitySystemComponent->RemoveLooseGameplayTag(NonSuppressingStates[Index]);
	}

	// 空弹匣 + 换弹中同样必须放行：这正是"服务器完成换弹、客户端镜像还没跟上"的窗口。
	Weapon->SetAmmoForAutomationTest(0, 0);
	AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	TestTrue(TEXT("a reload transaction with an empty mirror still allows local predicted feedback"),
		Ability->IsOwnerFireContextValidForTest(Character, Weapon, AbilitySystemComponent));
	AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	TestTrue(TEXT("clearing the reload transaction keeps local predicted feedback allowed"),
		Ability->IsOwnerFireContextValidForTest(Character, Weapon, AbilitySystemComponent));
	Weapon->SetAmmoForAutomationTest(10, 0);

	Weapon->SetActorHiddenInGame(true);
	TestFalse(TEXT("hidden weapon blocks local predicted feedback"),
		Ability->IsOwnerFireContextValidForTest(Character, Weapon, AbilitySystemComponent));
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
		Ability->IsOwnerFireContextValidForTest(Character, Weapon, AbilitySystemComponent));

	DestroyPredictionTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFirePredictionAuthorityRefireGateTest,
	"ShootGame.Ability.Fire.Prediction.AuthorityRefireGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFirePredictionAuthorityRefireGateTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFirePredictionAutomationTests;

	// 权威射速是服务器自己的判据：半自动与全自动共用同一个"距上一次真实提交是否已满 RefireRate"。
	// 一次 Activation 只提交一发，因此这个判据直接决定这次 Activation 是 Accept 还是 Reject，
	// 不存在"接受 Ability 但最终没有开枪"的中间态。
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

	// ---- 首次取用：权威射速必须已就绪，第一发不得被冷却误挡 ----
	AShooterWeaponLifecycleTestWeapon* SemiAuto = Cast<AShooterWeaponLifecycleTestWeapon>(
		AcquireFeedbackTestWeapon(World, Character, nullptr, nullptr, nullptr, 0.0f,
			AShooterWeaponLifecycleTestWeapon::StaticClass()));
	if (!TestNotNull(TEXT("semi-auto weapon acquired"), SemiAuto))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	TestFalse(TEXT("acquired weapon is semi-auto"), SemiAuto->IsFullAuto());
	TestTrue(TEXT("a fresh lease has no authority shot time"), SemiAuto->GetTimeOfLastShotForAutomationTest() < 0.0f);
	TestTrue(TEXT("first shot is allowed right after acquire"), SemiAuto->CanCommitAuthorityShot());

	// ---- 动态输入策略：生产的 GA_Fire 必须由当前武器的连发语义决定输入边沿 ----
	// 半自动走按下沿（一次按下最多一次动作边界），全自动走按住持续（由输入层按节拍反复激活）。
	// 这条映射是"一次 Activation 一发"在输入侧的入口，因此在这里用生产 Ability 类直接取证。
	if (!TestTrue(TEXT("the semi-auto weapon is equipped as the current weapon"), EquipWeaponForOwner(Character, SemiAuto)))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	AShooterPlayerState* ShooterPlayerState = Character->GetPlayerState<AShooterPlayerState>();
	UShooterAbilitySystemComponent* AbilitySystemComponent = ShooterPlayerState
		? Cast<UShooterAbilitySystemComponent>(ShooterPlayerState->GetAbilitySystemComponent())
		: nullptr;
	if (!TestNotNull(TEXT("the character PlayerState exposes the shooter ASC"), AbilitySystemComponent))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}
	// 本测试世界没有 GameMode，ASC 的 ActorInfo 需要显式建立；建立失败必须显式失败而不是静默跳过断言。
	ShooterPlayerState->InitializeAbilityActorInfo(Character);
	const FGameplayAbilityActorInfo* ActorInfo = AbilitySystemComponent->AbilityActorInfo.Get();
	if (!TestNotNull(TEXT("the shooter ASC exposes a ready AbilityActorInfo"), ActorInfo))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	UShooterGameplayAbility_Fire* Ability = NewObject<UShooterGameplayAbility_Fire>();
	TestTrue(TEXT("a semi-auto weapon maps to the single press-edge policy"),
		Ability->GetActivationPolicy(ActorInfo) == EShooterAbilityActivationPolicy::OnInputTriggered);

	SemiAuto->SetFullAutoForTest(true);
	TestTrue(TEXT("a full-auto weapon maps to the held-input policy"),
		Ability->GetActivationPolicy(ActorInfo) == EShooterAbilityActivationPolicy::WhileInputActive);
	SemiAuto->SetFullAutoForTest(false);
	TestTrue(TEXT("switching back to semi-auto restores the press-edge policy"),
		Ability->GetActivationPolicy(ActorInfo) == EShooterAbilityActivationPolicy::OnInputTriggered);

	// 冷却期内：两种模式都必须拒绝。
	SemiAuto->SetAuthorityRefireRemainingForAutomationTest(0.2f);
	TestFalse(TEXT("semi-auto cooldown rejects an immediate shot"), SemiAuto->CanCommitAuthorityShot());
	TestEqual(TEXT("rejected cooldown shot consumes no ammo"), SemiAuto->GetBulletCount(), 10);
	TestEqual(TEXT("rejected cooldown shot records no authority shot"),
		SemiAuto->GetAuthorityShotCountForAutomationTest(), 0);

	// ---- 容差：只吸收节拍抖动，不接受明显提前的开火 ----
	SemiAuto->SetAuthorityRefireRemainingForAutomationTest(SemiAuto->GetRefireRate());
	TestFalse(TEXT("a shot clearly early in the cadence is refused"), SemiAuto->CanCommitAuthorityShot());
	SemiAuto->SetAuthorityRefireRemainingForAutomationTest(0.005f);
	TestTrue(TEXT("a shot within the jitter tolerance is accepted"), SemiAuto->CanCommitAuthorityShot());
	SemiAuto->SetAuthorityRefireRemainingForAutomationTest(0.0f);
	TestTrue(TEXT("a shot exactly on the cadence boundary is accepted"), SemiAuto->CanCommitAuthorityShot());

	// ---- 全自动使用同一判据：服务器独立判定射速，而不是"冷却期一律放行" ----
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
	TestTrue(TEXT("full auto is ready on a fresh lease"), FullAuto->CanCommitAuthorityShot());

	FullAuto->SetAuthorityRefireRemainingForAutomationTest(0.2f);
	TestFalse(TEXT("full auto also refuses a shot that is early in the cadence"), FullAuto->CanCommitAuthorityShot());

	DestroyPredictionTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFirePredictionShotVerdictBudgetTest,
	"ShootGame.Ability.Fire.Prediction.ShotVerdictBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFirePredictionShotVerdictBudgetTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFirePredictionAutomationTests;

	// 一次 GA_Fire = 一发 Shot = 一个 PredictionKey：预算的结清只需要这一个键上的裁决，
	// 不再需要"这一轮预测了几发 / 服务器提交了几发"的整轮对账。
	UWorld* World = CreatePredictionTestWorld();
	if (!TestNotNull(TEXT("prediction test world created"), World))
	{
		return false;
	}

	AShooterCharacter* Character = SpawnLocalPlayerCharacter(World);
	AShooterWeapon* Weapon = AcquireFeedbackTestWeapon(World, Character, nullptr, nullptr, nullptr, 5.0f);
	if (!TestNotNull(TEXT("local player character spawned"), Character) ||
		!TestNotNull(TEXT("verdict test weapon acquired"), Weapon))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	UShooterGameplayAbility_Fire* Ability = NewObject<UShooterGameplayAbility_Fire>();
	if (!TestNotNull(TEXT("fire ability created"), Ability))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	// ---- 情形 1：预测 + Committed → 预算结清一次，不重复播放 ----
	Weapon->ResetFireFeedbackCountersForAutomationTest();
	Weapon->SetPendingPredictedShotsForAutomationTest(1);
	Ability->RegisterShotRecordForTest(/*PredictionKey*/ 101, Weapon, /*bBudgetConsumed*/ true, /*bFeedbackPlayed*/ true);
	TestTrue(TEXT("the shot record is registered"), Ability->HasShotRecordForTest(101));
	TestTrue(TEXT("the shot record records the consumed prediction budget"),
		Ability->IsShotRecordBudgetConsumedForTest(101));
	TestEqual(TEXT("one prediction is outstanding"), Ability->GetUnresolvedShotRecordCountForTest(), 1);

	Ability->HandleAuthorityShotVerdictForTest(101, /*bCommitted*/ true);
	TestEqual(TEXT("a committed prediction settles the local budget"), Weapon->GetPendingPredictedShots(), 0);
	TestTrue(TEXT("the committed shot record is resolved"), Ability->IsShotRecordResolvedForTest(101));
	TestEqual(TEXT("no confirmed owner replay is requested for an already predicted shot"),
		Ability->GetOwnerConfirmedReplayRequestCountForTest(), 0);
	TestEqual(TEXT("no confirmed owner replay is played for an already predicted shot"),
		Weapon->GetOwnerConfirmedReplayCountForAutomationTest(), 0);

	// 幂等：重复裁决不得二次结清或二次补播。
	Ability->HandleAuthorityShotVerdictForTest(101, /*bCommitted*/ true);
	TestEqual(TEXT("a duplicated verdict cannot settle the budget twice"), Weapon->GetPendingPredictedShots(), 0);
	TestEqual(TEXT("a duplicated verdict cannot request owner replay"),
		Ability->GetOwnerConfirmedReplayRequestCountForTest(), 0);

	// ---- 情形 2：预测 + Rejected → 只退款一次，不补播 ----
	Weapon->SetPendingPredictedShotsForAutomationTest(1);
	Ability->RegisterShotRecordForTest(/*PredictionKey*/ 102, Weapon, /*bBudgetConsumed*/ true, /*bFeedbackPlayed*/ true);
	Ability->HandleAuthorityShotVerdictForTest(102, /*bCommitted*/ false);
	TestEqual(TEXT("a rejected prediction refunds the local budget"), Weapon->GetPendingPredictedShots(), 0);
	TestTrue(TEXT("the rejected shot record is resolved"), Ability->IsShotRecordResolvedForTest(102));
	TestEqual(TEXT("a rejected shot never requests owner replay"),
		Ability->GetOwnerConfirmedReplayRequestCountForTest(), 0);

	Ability->HandleAuthorityShotVerdictForTest(102, /*bCommitted*/ false);
	TestEqual(TEXT("a duplicated rejection refunds only once"), Weapon->GetPendingPredictedShots(), 0);

	// ---- 情形 3：未预测 + Rejected → 什么都不发生 ----
	Weapon->SetPendingPredictedShotsForAutomationTest(0);
	Ability->RegisterShotRecordForTest(/*PredictionKey*/ 103, Weapon, /*bBudgetConsumed*/ false, /*bFeedbackPlayed*/ false);
	Ability->HandleAuthorityShotVerdictForTest(103, /*bCommitted*/ false);
	TestEqual(TEXT("an unpredicted rejection leaves the budget untouched"), Weapon->GetPendingPredictedShots(), 0);
	TestEqual(TEXT("an unpredicted rejection plays nothing"), Ability->GetOwnerConfirmedReplayRequestCountForTest(), 0);

	// ---- 情形 4：记录不存在时的裁决必须完全无副作用 ----
	Ability->HandleAuthorityShotVerdictForTest(/*PredictionKey*/ 999, /*bCommitted*/ false);
	Ability->HandleAuthorityShotVerdictForTest(/*PredictionKey*/ 999, /*bCommitted*/ true);
	TestEqual(TEXT("an unknown key cannot change the budget"), Weapon->GetPendingPredictedShots(), 0);
	TestEqual(TEXT("an unknown key cannot request owner replay"),
		Ability->GetOwnerConfirmedReplayRequestCountForTest(), 0);

	DestroyPredictionTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFirePredictionUnpredictedCommittedNoReplayTest,
	"ShootGame.Ability.Fire.Prediction.UnpredictedCommittedNoReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFirePredictionUnpredictedCommittedNoReplayTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFirePredictionAutomationTests;

	// 情形 5：拥有端因为本地弹药预算为 0 而没有提前表现，但请求仍然到达服务器并被接受。
	// 这一发只结清权威与本地状态，不得在 verdict 到达后补播历史 Owner cosmetic。
	UWorld* World = CreatePredictionTestWorld();
	if (!TestNotNull(TEXT("prediction test world created"), World))
	{
		return false;
	}

	AShooterCharacter* Character = SpawnLocalPlayerCharacter(World);
	AShooterWeapon* Weapon = AcquireFeedbackTestWeapon(World, Character, nullptr, nullptr, nullptr, 5.0f);
	if (!TestNotNull(TEXT("local player character spawned"), Character) ||
		!TestNotNull(TEXT("unpredicted acceptance test weapon acquired"), Weapon))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	if (!TestTrue(TEXT("weapon is equipped as the current weapon"), EquipWeaponForOwner(Character, Weapon)))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	UShooterGameplayAbility_Fire* Ability = NewObject<UShooterGameplayAbility_Fire>();
	if (!TestNotNull(TEXT("fire ability created"), Ability))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	Weapon->ResetFireFeedbackCountersForAutomationTest();
	Weapon->SetPendingPredictedShotsForAutomationTest(0);

	const int32 AmmoBefore = Weapon->GetBulletCount();
	const int32 ProjectilesBefore = CountProjectiles(World);
	const float TimeOfLastShotBefore = Weapon->GetTimeOfLastShotForAutomationTest();
	const bool bCadenceReadyBefore = Weapon->IsLocalFireCooldownReady();

	// 未预测：本次 Activation 没有提前表现，也没有占用预算。
	Ability->RegisterShotRecordForTest(/*PredictionKey*/ 201, Weapon, /*bBudgetConsumed*/ false, /*bFeedbackPlayed*/ false);
	// 模拟 K+1 / K+2 已经进入当前第一视角时间线：后续本地预测反馈可以正常发生。
	TestTrue(TEXT("the later shot K+1 can use the local predicted feedback path"),
		Weapon->PlayOwnerPredictedShotFeedback());
	TestTrue(TEXT("the later shot K+2 can use the local predicted feedback path"),
		Weapon->PlayOwnerPredictedShotFeedback());
	const int32 PredictedFeedbackBeforeVerdict = Weapon->GetPredictedOwnerFeedbackCountForAutomationTest();
	const int32 MontageBeforeVerdict = Character->GetOwnerLocalMontageCountForAutomationTest();
	const int32 RecoilBeforeVerdict = Character->GetOwnerLocalRecoilCountForAutomationTest();
	const int32 MuzzleBeforeVerdict = Weapon->GetOwnerMuzzleFeedbackCountForAutomationTest();
	const int32 SoundBeforeVerdict = Weapon->GetOwnerSoundFeedbackCountForAutomationTest();
	Ability->HandleAuthorityShotVerdictForTest(201, /*bCommitted*/ true);

	TestEqual(TEXT("an unpredicted committed shot requests no owner replay"),
		Ability->GetOwnerConfirmedReplayRequestCountForTest(), 0);
	TestEqual(TEXT("the confirmed owner replay count remains zero"),
		Weapon->GetOwnerConfirmedReplayCountForAutomationTest(), 0);
	TestEqual(TEXT("the old verdict does not add a predicted owner feedback event"),
		Weapon->GetPredictedOwnerFeedbackCountForAutomationTest(), PredictedFeedbackBeforeVerdict);
	TestEqual(TEXT("the old verdict does not insert a montage after K+1/K+2"),
		Character->GetOwnerLocalMontageCountForAutomationTest(), MontageBeforeVerdict);
	TestEqual(TEXT("the old verdict does not insert recoil after K+1/K+2"),
		Character->GetOwnerLocalRecoilCountForAutomationTest(), RecoilBeforeVerdict);
	TestEqual(TEXT("the old verdict does not insert muzzle after K+1/K+2"),
		Weapon->GetOwnerMuzzleFeedbackCountForAutomationTest(), MuzzleBeforeVerdict);
	TestEqual(TEXT("the old verdict does not insert sound after K+1/K+2"),
		Weapon->GetOwnerSoundFeedbackCountForAutomationTest(), SoundBeforeVerdict);
	TestEqual(TEXT("an unpredicted committed shot leaves the budget untouched"), Weapon->GetPendingPredictedShots(), 0);
	TestEqual(TEXT("owner replay settlement never consumes magazine ammo"), Weapon->GetBulletCount(), AmmoBefore);
	TestEqual(TEXT("owner replay settlement never spawns a projectile"), CountProjectiles(World), ProjectilesBefore);
	TestEqual(TEXT("owner replay settlement never advances the local fire cadence"),
		Weapon->IsLocalFireCooldownReady(), bCadenceReadyBefore);
	TestEqual(TEXT("owner replay settlement never writes the authority shot time"),
		Weapon->GetTimeOfLastShotForAutomationTest(), TimeOfLastShotBefore);

	// 幂等：重复 Committed 不得补播第二次。
	Ability->HandleAuthorityShotVerdictForTest(201, /*bCommitted*/ true);
	TestEqual(TEXT("a duplicated committed verdict cannot request owner replay"),
		Ability->GetOwnerConfirmedReplayRequestCountForTest(), 0);
	TestEqual(TEXT("a duplicated committed verdict cannot add owner replay"),
		Weapon->GetOwnerConfirmedReplayCountForAutomationTest(), 0);

	// ---- 情形 6：预算被消费但表现通道当时不可用 → Committed 仍然只结清预算 ----
	// 预算与表现是两个独立事实：表现提交失败不得让这一发的预算永远挂着。
	Weapon->ResetFireFeedbackCountersForAutomationTest();
	Weapon->SetPendingPredictedShotsForAutomationTest(1);
	Ability->RegisterShotRecordForTest(/*PredictionKey*/ 202, Weapon, /*bBudgetConsumed*/ true,
		/*bFeedbackPlayed*/ false);
	TestTrue(TEXT("the record remembers the consumed budget"), Ability->IsShotRecordBudgetConsumedForTest(202));
	TestFalse(TEXT("the record remembers that no feedback was played"),
		Ability->IsShotRecordFeedbackPlayedForTest(202));

	Ability->HandleAuthorityShotVerdictForTest(202, /*bCommitted*/ true);
	TestEqual(TEXT("a committed shot still settles a budget whose feedback failed"),
		Weapon->GetPendingPredictedShots(), 0);
	TestEqual(TEXT("a committed shot does not replay feedback that was never played"),
		Ability->GetOwnerConfirmedReplayRequestCountForTest(), 0);

	// 同一事实的 Reject 面：预算必须被退还，且完全不补播。
	Weapon->ResetFireFeedbackCountersForAutomationTest();
	Weapon->SetPendingPredictedShotsForAutomationTest(1);
	Ability->RegisterShotRecordForTest(/*PredictionKey*/ 203, Weapon, /*bBudgetConsumed*/ true,
		/*bFeedbackPlayed*/ false);
	const int32 ReplayBeforeReject = Ability->GetOwnerConfirmedReplayRequestCountForTest();
	Ability->HandleAuthorityShotVerdictForTest(203, /*bCommitted*/ false);
	TestEqual(TEXT("a rejected shot refunds the budget even when its feedback never played"),
		Weapon->GetPendingPredictedShots(), 0);
	TestEqual(TEXT("a rejected shot never replays the missing feedback"),
		Ability->GetOwnerConfirmedReplayRequestCountForTest(), ReplayBeforeReject);

	DestroyPredictionTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFirePredictionShotRecordLifetimeTest,
	"ShootGame.Ability.Fire.Prediction.ShotRecordLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFirePredictionShotRecordLifetimeTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFirePredictionAutomationTests;

	// 迟到的旧裁决不得修改新上下文的预算，也不得在新武器上补播表现。
	UWorld* World = CreatePredictionTestWorld();
	if (!TestNotNull(TEXT("prediction test world created"), World))
	{
		return false;
	}

	AShooterCharacter* Character = SpawnLocalPlayerCharacter(World);
	AShooterWeapon* Weapon = AcquireFeedbackTestWeapon(World, Character, nullptr, nullptr, nullptr, 5.0f);
	if (!TestNotNull(TEXT("local player character spawned"), Character) ||
		!TestNotNull(TEXT("lifetime test weapon acquired"), Weapon))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	UShooterGameplayAbility_Fire* Ability = NewObject<UShooterGameplayAbility_Fire>();
	if (!TestNotNull(TEXT("fire ability created"), Ability))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	// ---- 生命周期边界：Owner / 池边界作废该武器的记录 ----
	Weapon->SetPendingPredictedShotsForAutomationTest(1);
	Ability->RegisterShotRecordForTest(/*PredictionKey*/ 301, Weapon, /*bBudgetConsumed*/ true, /*bFeedbackPlayed*/ true);
	TestEqual(TEXT("one unresolved shot record is retained"), Ability->GetUnresolvedShotRecordCountForTest(), 1);

	Ability->InvalidateWeaponPredictionContext(Weapon);
	TestEqual(TEXT("an invalidated context leaves no unresolved record"),
		Ability->GetUnresolvedShotRecordCountForTest(), 0);

	// 迟到的裁决在记录失效后不得退款、不得补播。
	Weapon->SetPendingPredictedShotsForAutomationTest(1);
	Ability->HandleAuthorityShotVerdictForTest(301, /*bCommitted*/ false);
	TestEqual(TEXT("a late rejection after invalidation cannot refund"), Weapon->GetPendingPredictedShots(), 1);
	Ability->HandleAuthorityShotVerdictForTest(301, /*bCommitted*/ true);
	TestEqual(TEXT("a late committed verdict after invalidation cannot replay owner feedback"),
		Ability->GetOwnerConfirmedReplayRequestCountForTest(), 0);

	// ---- 池归还 / 重新取用：代次变化后旧记录不再参与结清 ----
	Weapon->SetPendingPredictedShotsForAutomationTest(1);
	Ability->RegisterShotRecordForTest(/*PredictionKey*/ 302, Weapon, /*bBudgetConsumed*/ true, /*bFeedbackPlayed*/ true);
	TestEqual(TEXT("a new record is registered for the new lease"), Ability->GetUnresolvedShotRecordCountForTest(), 1);

	Weapon->OnAcquiredFromWeaponPool();
	TestEqual(TEXT("re-acquiring the weapon invalidates the old generation's records"),
		Ability->GetUnresolvedShotRecordCountForTest(), 0);

	DestroyPredictionTestWorld(World);
	return true;
}

/**
 * 每把玩家持有的武器各拥有一份 GA_Fire Spec（SourceObject = 该 WeaponActor）。
 *
 * 这条生命周期是 Weapon Action Identity 的基础：AbilitySpecHandle 随激活请求一起过网络，
 * 服务器因此不需要任何新增字段就能知道"这次 Fire Action 属于哪把枪"。
 * 覆盖：Add → 授予、重复 Add 不重复授予、Remove → 撤销、Clear → 全部撤销、重新 Add → 重新授予。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFireSpecPerWeaponLifecycleTest,
	"ShootGame.Ability.Fire.Grant.PerWeaponSpecLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFireSpecPerWeaponLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFirePredictionAutomationTests;

	UWorld* World = CreatePredictionTestWorld();
	if (!TestNotNull(TEXT("per-weapon spec test world created"), World))
	{
		return false;
	}

	AShooterCharacter* Character = SpawnLocalPlayerCharacter(World);
	UShooterInventoryComponent* Inventory = Character ? Character->GetInventoryComponent() : nullptr;
	AShooterPlayerState* ShooterPlayerState = Character ? Character->GetPlayerState<AShooterPlayerState>() : nullptr;
	if (!TestNotNull(TEXT("per-weapon spec character spawned"), Character) ||
		!TestNotNull(TEXT("inventory component exists"), Inventory) ||
		!TestNotNull(TEXT("shooter PlayerState exists"), ShooterPlayerState))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	TestEqual(TEXT("a fresh player holds no Fire spec"), ShooterPlayerState->GetFireAbilitySpecCount(), 0);

	// ---- Add → 每把武器各授予一份，SourceObject 就是该武器 ----
	AShooterWeapon* FirstWeapon = GrantTestWeapon(World, Inventory, AShooterWeaponPresentationTestWeaponPrimary::StaticClass());
	AShooterWeapon* SecondWeapon = GrantTestWeapon(World, Inventory, AShooterWeaponPresentationTestWeaponSecondary::StaticClass());
	if (!TestNotNull(TEXT("first weapon granted"), FirstWeapon) || !TestNotNull(TEXT("second weapon granted"), SecondWeapon))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	const FGameplayAbilitySpec* FirstSpec = ShooterPlayerState->FindFireAbilitySpecForWeapon(FirstWeapon);
	const FGameplayAbilitySpec* SecondSpec = ShooterPlayerState->FindFireAbilitySpecForWeapon(SecondWeapon);
	TestEqual(TEXT("two held weapons own two Fire specs"), ShooterPlayerState->GetFireAbilitySpecCount(), 2);
	TestNotNull(TEXT("first weapon has its own Fire spec"), FirstSpec);
	TestNotNull(TEXT("second weapon has its own Fire spec"), SecondSpec);
	if (FirstSpec && SecondSpec)
	{
		TestTrue(TEXT("first spec SourceObject is the first weapon"), FirstSpec->SourceObject.Get() == FirstWeapon);
		TestTrue(TEXT("second spec SourceObject is the second weapon"), SecondSpec->SourceObject.Get() == SecondWeapon);
		TestTrue(TEXT("different weapons own different spec handles"), FirstSpec->Handle != SecondSpec->Handle);
		const bool bFirstIsFire = FirstSpec->Ability && FirstSpec->Ability->IsA<UShooterGameplayAbility_Fire>();
		const bool bSecondIsFire = SecondSpec->Ability && SecondSpec->Ability->IsA<UShooterGameplayAbility_Fire>();
		TestTrue(TEXT("both specs run the production GA_Fire class"), bFirstIsFire && bSecondIsFire);
	}

	// 幂等：同一把武器重复授予不得新增 Spec（幂等键是 AbilityClass + SourceObject）。
	ShooterPlayerState->GrantFireAbilityForWeapon(FirstWeapon);
	ShooterPlayerState->GrantFireAbilityForWeapon(FirstWeapon);
	TestEqual(TEXT("re-granting the same weapon stays idempotent"), ShooterPlayerState->GetFireAbilitySpecCount(), 2);

	// ---- Remove → 只撤销该武器的那一份 ----
	TestTrue(TEXT("first weapon removed from inventory"), Inventory->RemoveWeapon(FirstWeapon));
	TestEqual(TEXT("removing a weapon revokes its Fire spec"), ShooterPlayerState->GetFireAbilitySpecCount(), 1);
	const FGameplayAbilitySpec* RemovedWeaponSpec = ShooterPlayerState->FindFireAbilitySpecForWeapon(FirstWeapon);
	TestNull(TEXT("the removed weapon has no Fire spec"), RemovedWeaponSpec);
	TestNotNull(TEXT("the remaining weapon keeps its Fire spec"),
		ShooterPlayerState->FindFireAbilitySpecForWeapon(SecondWeapon));

	// ---- Clear → 全部撤销，且 Spec 不再指向已失效武器 ----
	Inventory->ClearInventory();
	TestEqual(TEXT("clearing the inventory revokes every Fire spec"), ShooterPlayerState->GetFireAbilitySpecCount(), 0);
	const FGameplayAbilitySpec* ClearedWeaponSpec = ShooterPlayerState->FindFireAbilitySpecForWeapon(SecondWeapon);
	TestNull(TEXT("no Fire spec survives the cleared inventory"), ClearedWeaponSpec);

	// ---- 重新 Add → 重新授予一份（旧 Spec 撤干净后不得残留 / 不得重复） ----
	AShooterWeapon* RegrantedWeapon = GrantTestWeapon(World, Inventory, AShooterWeaponPresentationTestWeaponPrimary::StaticClass());
	if (TestNotNull(TEXT("weapon granted again after clear"), RegrantedWeapon))
	{
		TestEqual(TEXT("re-adding a weapon grants exactly one new Fire spec"),
			ShooterPlayerState->GetFireAbilitySpecCount(), 1);
		TestNotNull(TEXT("the re-added weapon owns a Fire spec"),
			ShooterPlayerState->FindFireAbilitySpecForWeapon(RegrantedWeapon));
	}

	DestroyPredictionTestWorld(World);
	return true;
}

/**
 * Spec 生命周期结束时的预测债务清算：退款恰好一次、HUD 预测结清、记录清零，并且可重入。
 *
 * 这里直接驱动生产 override（UGameplayAbility::OnRemoveAbility），证明的是"清理本身的幂等性与范围"；
 * 网络侧的真实 Spec Removal（Inventory Remove → Spec 撤销 → GAS 复制删除）由
 * -ShootGameWeaponContextTest 的 SpecRemovalInFlight / PoolReuseRebind 用例覆盖。
 *
 * 两个用例把"真实退款"与"代次跳过"分开，防止再次出现"报告退过款、实际一次都没退"：
 *   Case A（同代次）：记录占过预算、显示激活存在 → Retired=1、Refunded=1、Pending 与 HUD 都被结清；
 *   Case B（旧代次）：记录属于上一轮租用 → Retired=1 但 Refunded=0、GenerationSkipped=1，
 *                     当前 Pending 与当前 HUD 显示激活一律不动。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFireSpecRemovalPredictionRetirementTest,
	"ShootGame.Ability.Fire.Prediction.SpecRemovalRetiresPredictionDebt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFireSpecRemovalPredictionRetirementTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityFirePredictionAutomationTests;

	UWorld* World = CreatePredictionTestWorld();
	if (!TestNotNull(TEXT("spec removal test world created"), World))
	{
		return false;
	}

	AShooterCharacter* Character = SpawnLocalPlayerCharacter(World);
	UShooterInventoryComponent* Inventory = Character ? Character->GetInventoryComponent() : nullptr;
	AShooterPlayerState* ShooterPlayerState = Character ? Character->GetPlayerState<AShooterPlayerState>() : nullptr;
	if (!TestNotNull(TEXT("spec removal character spawned"), Character) ||
		!TestNotNull(TEXT("inventory exists"), Inventory) ||
		!TestNotNull(TEXT("PlayerState exists"), ShooterPlayerState))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	const TSubclassOf<AShooterWeapon> WeaponClass = AShooterWeaponPresentationTestWeaponPrimary::StaticClass();
	AShooterWeapon* Weapon = GrantTestWeapon(World, Inventory, WeaponClass);
	if (!TestNotNull(TEXT("weapon granted"), Weapon))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	const FGameplayAbilitySpec* Spec = ShooterPlayerState->FindFireAbilitySpecForWeapon(Weapon);
	if (!TestNotNull(TEXT("weapon owns a Fire spec"), Spec))
	{
		DestroyPredictionTestWorld(World);
		return false;
	}

	UShooterGameplayAbility_Fire* Ability = NewObject<UShooterGameplayAbility_Fire>(Character);

	// ---- Case A：同代次的未结记录，预算与显示激活都真实存在 ----
	// 本测试世界是权威端，没有拥有者预测视图，因此预算与显示激活用测试入口建立起点：
	// 它们构造的正是"拥有端已经提前占过预算并显示扣减"这一状态。
	const int32 CaseAKey = 401;
	Ability->RegisterShotRecordForTest(CaseAKey, Weapon, /*bBudgetConsumed*/ true, /*bFeedbackPlayed*/ true);
	Weapon->SetPendingPredictedShotsForAutomationTest(1);
	Weapon->SeedAmmoDisplayActivationForAutomationTest(CaseAKey);

	TestEqual(TEXT("case A keeps one unresolved record"), Ability->GetUnresolvedShotRecordCountForTest(), 1);
	TestEqual(TEXT("case A fixture really holds one predicted shot"), Weapon->GetPendingPredictedShots(), 1);
	TestEqual(TEXT("case A fixture really holds one display activation"), Weapon->GetUnsettledAmmoDisplayCount(), 1);

	const int32 RetiredBeforeCaseA = Ability->GetSpecRemovalRetiredRecordCountForTest();
	const int32 RefundedBeforeCaseA = Ability->GetSpecRemovalRefundedCountForTest();
	const int32 AuthorityRejectsBeforeCaseA = Ability->GetAuthorityRejectCountForTest();

	// 生产生命周期入口：引擎在实例 MarkAsGarbage 之前调用 OnRemoveAbility。
	Ability->HandleSpecRemovalForTest(*Spec);

	TestEqual(TEXT("case A retires the owned record"), Ability->GetUnresolvedShotRecordCountForTest(), 0);
	TestFalse(TEXT("case A drops the record from the instance"), Ability->HasShotRecordForTest(CaseAKey));
	TestEqual(TEXT("case A retires exactly one record"),
		Ability->GetSpecRemovalRetiredRecordCountForTest() - RetiredBeforeCaseA, 1);
	TestEqual(TEXT("case A really refunds the consumed budget once"),
		Ability->GetSpecRemovalRefundedCountForTest() - RefundedBeforeCaseA, 1);
	TestEqual(TEXT("case A reports no generation skip"), Ability->GetSpecRemovalGenerationSkippedCountForTest(), 0);
	TestEqual(TEXT("case A settles the predicted budget"), Weapon->GetPendingPredictedShots(), 0);
	TestEqual(TEXT("case A settles the display activation"), Weapon->GetUnsettledAmmoDisplayCount(), 0);
	// 生命周期退休不是服务器拒绝：任何 Reject 统计都不得因此增长。
	TestEqual(TEXT("case A does not count as a server reject"),
		Ability->GetAuthorityRejectCountForTest(), AuthorityRejectsBeforeCaseA);

	// 幂等：再次进入（例如 EndAbility 之后又走一次移除路径）不得二次退款 / 二次结清。
	Ability->HandleSpecRemovalForTest(*Spec);
	TestEqual(TEXT("a second removal stays idempotent"),
		Ability->GetSpecRemovalRetiredRecordCountForTest() - RetiredBeforeCaseA, 1);
	TestEqual(TEXT("a second removal refunds nothing"),
		Ability->GetSpecRemovalRefundedCountForTest() - RefundedBeforeCaseA, 1);
	TestEqual(TEXT("a second removal leaves the budget settled"), Weapon->GetPendingPredictedShots(), 0);

	// ---- Case B：旧代次记录（上一轮租用留下的债务） ----
	// 真实生命周期边界：归还武器池 = ClearWeaponOwner，代次 +1，旧预算与旧显示激活整体作废。
	const int32 CaseBKey = 402;
	Ability->RegisterShotRecordForTest(CaseBKey, Weapon, /*bBudgetConsumed*/ true, /*bFeedbackPlayed*/ true);
	Weapon->OnReleasedToWeaponPool();

	// 归还之后建立"当前生命周期"的状态：一笔未结预算与一次显示激活，用于验证它们不被旧记录牵动。
	const int32 CurrentLifecycleDisplayKey = 900;
	Weapon->SetPendingPredictedShotsForAutomationTest(1);
	Weapon->SeedAmmoDisplayActivationForAutomationTest(CurrentLifecycleDisplayKey);

	const int32 RetiredBeforeCaseB = Ability->GetSpecRemovalRetiredRecordCountForTest();
	const int32 RefundedBeforeCaseB = Ability->GetSpecRemovalRefundedCountForTest();
	const int32 AuthorityRejectsBeforeCaseB = Ability->GetAuthorityRejectCountForTest();

	Ability->HandleSpecRemovalForTest(*Spec);

	TestFalse(TEXT("case B drops the stale record from the instance"), Ability->HasShotRecordForTest(CaseBKey));
	TestEqual(TEXT("case B leaves the instance with no unresolved records"),
		Ability->GetUnresolvedShotRecordCountForTest(), 0);
	TestEqual(TEXT("case B retires exactly one stale record"),
		Ability->GetSpecRemovalRetiredRecordCountForTest() - RetiredBeforeCaseB, 1);
	TestEqual(TEXT("case B really refunds nothing"),
		Ability->GetSpecRemovalRefundedCountForTest() - RefundedBeforeCaseB, 0);
	TestEqual(TEXT("case B reports exactly one generation skip"),
		Ability->GetSpecRemovalGenerationSkippedCountForTest(), 1);
	TestEqual(TEXT("case B leaves the current predicted budget untouched"), Weapon->GetPendingPredictedShots(), 1);
	TestEqual(TEXT("case B leaves the current display activation untouched"),
		Weapon->GetUnsettledAmmoDisplayCount(), 1);
	TestEqual(TEXT("case B does not count as a server reject"),
		Ability->GetAuthorityRejectCountForTest(), AuthorityRejectsBeforeCaseB);

	DestroyPredictionTestWorld(World);
	return true;
}

#endif
