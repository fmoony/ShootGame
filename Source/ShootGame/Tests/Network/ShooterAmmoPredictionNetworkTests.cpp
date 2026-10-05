// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/Network/ShooterNetworkTestCoordinator.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/Abilities/ShooterGameplayAbility_Fire.h"
#include "AbilitySystem/ShooterAbilitySystemComponent.h"
#include "AbilitySystem/ShooterGameplayTags.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerState/ShooterPlayerState.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "Weapons/Subsystems/ShooterWeaponRuntimeSubsystem.h"
#include "Weapons/ShooterWeapon.h"
#include "ShootGame.h"

/**
 * Ammo Prediction 复现夹具：只服务 -ShootGameAmmoPredictionTest。
 *
 * 本文件不修生产语义，只在真实 Listen 会话里构造三种最小状态并同时记录
 * 服务器权威增量与拥有端本地观测：
 *   Step 1 H-A1：射击与补弹落在同一复制帧，客户端看不到 MagazineAmmo 变化。
 *   Step 2 H-A3：残留 Pending 令本地预算为 0，服务器却接受并提交该发。
 *   Step 3 H-A2：服务器仅本地持有的阻塞 Tag 触发真实 GAS Reject，
 *                拥有端在 Reject 到达前已经结束预测实例。
 * 复现成立时输出 MISMATCH 证据；夹具自身失败输出 AUTOMATION_TEST_FAILURE。
 */
namespace ShooterAmmoPredictionNetworkTests
{
	constexpr float StepTimeoutSeconds = 30.0f;
	constexpr float SampleFreshSeconds = 0.6f;
	constexpr float SettleSeconds = 1.0f;
	constexpr float RejectSettleSeconds = 1.5f;

	constexpr int32 StepSetup = 0;
	constexpr int32 StepSameValueSnapshot = 1;
	constexpr int32 StepBudgetVeto = 2;
	constexpr int32 StepLateReject = 3;
	constexpr int32 StepDone = 4;

	constexpr int32 FixtureMagazine = 1;
	constexpr int32 FixtureReserve = 5;
	constexpr int32 RejectMagazine = 10;

	bool IsFireAbility(const UGameplayAbility* Ability)
	{
		return Ability && Ability->IsA<UShooterGameplayAbility_Fire>();
	}
}

// ============================ 拥有端观测 ============================

void AShooterNetworkTestCoordinator::BindAmmoPredictionClientObservers(UAbilitySystemComponent* AbilitySystemComponent)
{
	if (HasAuthority() || !AbilitySystemComponent || AmmoPredictionObservedASC.Get() == AbilitySystemComponent)
	{
		return;
	}

	AmmoPredictionObservedASC = AbilitySystemComponent;
	AmmoPredictionActivatedHandle = AbilitySystemComponent->AbilityActivatedCallbacks.AddUObject(
		this, &AShooterNetworkTestCoordinator::HandleAmmoPredictionFireActivated);
	AmmoPredictionEndedHandle = AbilitySystemComponent->AbilityEndedCallbacks.AddUObject(
		this, &AShooterNetworkTestCoordinator::HandleAmmoPredictionFireEnded);
}

void AShooterNetworkTestCoordinator::BindAmmoPredictionServerObserver(UAbilitySystemComponent* AbilitySystemComponent)
{
	if (!HasAuthority() || !AbilitySystemComponent || AmmoPredictionObservedASC.Get() == AbilitySystemComponent)
	{
		return;
	}

	AmmoPredictionObservedASC = AbilitySystemComponent;
	AmmoPredictionFailedHandle = AbilitySystemComponent->AbilityFailedCallbacks.AddUObject(
		this, &AShooterNetworkTestCoordinator::HandleAmmoPredictionAuthorityFailed);
}

void AShooterNetworkTestCoordinator::HandleAmmoPredictionFireActivated(UGameplayAbility* Ability)
{
	if (HasAuthority() || !bAmmoPredictionMode || !ShooterAmmoPredictionNetworkTests::IsFireAbility(Ability))
	{
		return;
	}

	++AmmoPredictionOwnerFireActivations;
	AmmoPredictionLastPredictionKey = Ability->GetCurrentActivationInfo().GetActivationPredictionKey().Current;
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_OWNER_FIRE_ACTIVATED Step=%d Key=%d Count=%d"),
		AmmoPredictionClientStep, AmmoPredictionLastPredictionKey, AmmoPredictionOwnerFireActivations);
}

void AShooterNetworkTestCoordinator::HandleAmmoPredictionFireEnded(UGameplayAbility* Ability)
{
	if (HasAuthority() || !bAmmoPredictionMode || !ShooterAmmoPredictionNetworkTests::IsFireAbility(Ability))
	{
		return;
	}

	// 正常输入释放也会走到这里；只有 Rejected 结束才算拥有端观察到一次权威拒绝。
	const FGameplayAbilityActivationInfo Info = Ability->GetCurrentActivationInfo();
	if (Info.ActivationMode == EGameplayAbilityActivationMode::Rejected)
	{
		++AmmoPredictionOwnerFireRejects;
		UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_OWNER_FIRE_REJECT Step=%d Key=%d Count=%d"),
			AmmoPredictionClientStep, Info.GetActivationPredictionKey().Current, AmmoPredictionOwnerFireRejects);
	}
}

void AShooterNetworkTestCoordinator::HandleAmmoPredictionAuthorityFailed(const UGameplayAbility* Ability,
	const FGameplayTagContainer& FailureTags)
{
	if (!HasAuthority() || !bAmmoPredictionMode || !ShooterAmmoPredictionNetworkTests::IsFireAbility(Ability))
	{
		return;
	}

	++AmmoPredictionAuthorityRejects;
	// 夹具阻塞 Tag 只用于制造一次真实拒绝，观察到失败回调后立即撤掉。
	ClearAmmoPredictionServerTag();
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_SERVER_REJECT Step=%d Count=%d Tags=%s"),
		AmmoPredictionServerStep, AmmoPredictionAuthorityRejects, *FailureTags.ToStringSimple());
}

void AShooterNetworkTestCoordinator::ClearAmmoPredictionServerTag()
{
	if (!bAmmoPredictionServerTagApplied)
	{
		return;
	}

	if (UAbilitySystemComponent* AbilitySystemComponent = AmmoPredictionObservedASC.Get())
	{
		AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	}
	bAmmoPredictionServerTagApplied = false;
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_SERVER_TAG_CLEARED Step=%d"), AmmoPredictionServerStep);
}

void AShooterNetworkTestCoordinator::SampleAmmoPredictionLocalState()
{
	if (!bAmmoPredictionMode || HasAuthority())
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetOwner());
	if (!PlayerController || !PlayerController->IsLocalController() || AmmoPredictionClientStep == INDEX_NONE)
	{
		return;
	}

	if (!AmmoPredictionSubject.IsValid())
	{
		AShooterCharacter* LocalCharacter = GetShooterCharacter();
		if (LocalCharacter && LocalCharacter->IsLocallyControlled())
		{
			AmmoPredictionSubject = LocalCharacter;
		}
	}

	AShooterCharacter* Subject = AmmoPredictionSubject.Get();
	if (!Subject)
	{
		return;
	}

	if (!AmmoPredictionWeapon.IsValid())
	{
		AmmoPredictionWeapon = Subject->GetCurrentWeaponActor();
	}
	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	UShooterAbilitySystemComponent* ShooterAbilitySystemComponent = Cast<UShooterAbilitySystemComponent>(
		Subject->GetAbilitySystemComponent());
	BindAmmoPredictionClientObservers(ShooterAbilitySystemComponent);

	AShooterPlayerState* PlayerState = Subject->GetPlayerState<AShooterPlayerState>();
	FShooterAmmoPredictionObservation Sample;
	Sample.Step = AmmoPredictionClientStep;
	Sample.Subject = Subject;
	Sample.Weapon = Weapon;
	Sample.bValid = IsValid(Weapon) && PlayerState && ShooterAbilitySystemComponent &&
		Subject->GetCurrentWeaponActor() == Weapon && Weapon->GetOwner() == Subject && !Weapon->IsHidden();
	if (Sample.bValid)
	{
		Sample.MagazineAmmo = Weapon->GetBulletCount();
		Sample.ReserveAmmo = Weapon->GetReserveAmmo();
		Sample.PendingShots = Weapon->GetPendingPredictedShots();
		Sample.PredictedMagazineAmmo = Weapon->GetPredictedMagazineAmmo();
		Sample.OwnerFeedbackCount = Weapon->GetPredictedOwnerFeedbackCountForAutomationTest();
		Sample.OwnerConfirmationCount = Weapon->GetOwnerAuthorityConfirmationCountForAutomationTest();
		Sample.OwnerFireActivationCount = AmmoPredictionOwnerFireActivations;
		Sample.OwnerFireRejectCount = AmmoPredictionOwnerFireRejects;
		Sample.LastPredictionKey = AmmoPredictionLastPredictionKey;
		Sample.bFireActive = HasActiveFireAbility(Subject);
		Sample.bReloadActive = HasActiveReloadAbility(Subject);
		Sample.bReloading = ShooterAbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Reloading);
		Sample.bLocalFireCooldownReady = Weapon->IsLocalFireCooldownReady();
	}

	AmmoPredictionLatest = Sample;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= AmmoPredictionNextReportTime)
	{
		AmmoPredictionNextReportTime = Now + 0.05f;
		ServerReportAmmoPredictionSample(Sample);
	}
}

void AShooterNetworkTestCoordinator::ClientPrepareAmmoPredictionStep_Implementation(int32 SubjectPlayerId, int32 Step)
{
	if (!bAmmoPredictionMode)
	{
		return;
	}

	AmmoPredictionClientStep = Step;
	AmmoPredictionSubjectPlayerId = SubjectPlayerId;
	AmmoPredictionSubmittedStep = INDEX_NONE;
	SampleAmmoPredictionLocalState();
	const AShooterCharacter* LocalCharacter = GetShooterCharacter();
	const APlayerState* LocalPlayerState = LocalCharacter ? LocalCharacter->GetPlayerState() : nullptr;
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_CLIENT_STEP Step=%d SubjectPlayerId=%d LocalPlayerId=%d"),
		Step, SubjectPlayerId, LocalPlayerState ? LocalPlayerState->GetPlayerId() : INDEX_NONE);
}

void AShooterNetworkTestCoordinator::ClientSubmitAmmoPredictionFire_Implementation(int32 Step)
{
	if (!bAmmoPredictionMode || Step != AmmoPredictionClientStep || Step == AmmoPredictionSubmittedStep)
	{
		return;
	}

	AShooterCharacter* Subject = AmmoPredictionSubject.Get();
	if (!Subject || !Subject->IsLocallyControlled() || Subject != GetShooterCharacter())
	{
		return;
	}

	AmmoPredictionSubmittedStep = Step;
	// Press 与 Release 落在同一帧：这是本夹具要复现的拥有端释放时序，
	// 不直接调用 Weapon.Fire，也不走服务器实现。
	Subject->DoStartFiring();
	Subject->DoStopFiring();
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_CLIENT_FIRE Step=%d Subject=%s"), Step, *GetNameSafe(Subject));
}

void AShooterNetworkTestCoordinator::ServerReportAmmoPredictionSample_Implementation(const FShooterAmmoPredictionObservation& Observation)
{
	if (!bAmmoPredictionMode || Observation.Step != AmmoPredictionClientStep)
	{
		return;
	}

	AmmoPredictionLatest = Observation;
	AmmoPredictionLatestArrivalTime = GetWorld()->GetTimeSeconds();
	UE_LOG(LogShootGame, Display,
		TEXT("AMMO_PREDICTION_SAMPLE Step=%d Valid=%d Mag=%d Reserve=%d Pending=%d Predicted=%d ")
		TEXT("Feedback=%d Confirm=%d Activations=%d Rejects=%d FireActive=%d ReloadActive=%d ")
		TEXT("Reloading=%d CooldownReady=%d Key=%d"),
		Observation.Step,
		Observation.bValid ? 1 : 0,
		Observation.MagazineAmmo,
		Observation.ReserveAmmo,
		Observation.PendingShots,
		Observation.PredictedMagazineAmmo,
		Observation.OwnerFeedbackCount,
		Observation.OwnerConfirmationCount,
		Observation.OwnerFireActivationCount,
		Observation.OwnerFireRejectCount,
		Observation.bFireActive ? 1 : 0,
		Observation.bReloadActive ? 1 : 0,
		Observation.bReloading ? 1 : 0,
		Observation.bLocalFireCooldownReady ? 1 : 0,
		Observation.LastPredictionKey);
}

// ============================ 服务器阶段 ============================

bool AShooterNetworkTestCoordinator::IsAmmoPredictionClientSampleFresh(int32 Step) const
{
	const UWorld* World = GetWorld();
	return World && AmmoPredictionLatest.Step == Step &&
		World->GetTimeSeconds() - AmmoPredictionLatestArrivalTime <= ShooterAmmoPredictionNetworkTests::SampleFreshSeconds;
}

void AShooterNetworkTestCoordinator::ConcludeAmmoPredictionCase(const TCHAR* CaseName, bool bConverged, const FString& Detail)
{
	if (bConverged)
	{
		++AmmoPredictionConvergedCount;
		UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_CONVERGED Case=%s %s"), CaseName, *Detail);
		return;
	}

	++AmmoPredictionMismatchCount;
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_MISMATCH Case=%s %s"), CaseName, *Detail);
}

void AShooterNetworkTestCoordinator::StartAmmoPredictionStep(int32 Step)
{
	AmmoPredictionServerStep = Step;
	AmmoPredictionClientStep = Step;
	AmmoPredictionStepStartTime = GetWorld()->GetTimeSeconds();
	AmmoPredictionSettleStartTime = AmmoPredictionStepStartTime;
	AmmoPredictionBefore = AmmoPredictionLatest;
	bAmmoPredictionSettleStarted = false;
	bAmmoPredictionStepCommandSent = false;
	bAmmoPredictionRejectObserved = false;
	bAmmoPredictionLateRejectFixtureSet = false;
	bAmmoPredictionSameValueArmed = false;

	const AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	AmmoPredictionAuthorityShotsBefore = Weapon ? Weapon->GetAuthorityShotCountForAutomationTest() : 0;
	AmmoPredictionAuthorityRejectsBefore = AmmoPredictionAuthorityRejects;
	AmmoPredictionProjectilesBefore = ProjectileSpawnCount;
	AmmoPredictionMagazineBefore = Weapon ? Weapon->GetBulletCount() : 0;
	AmmoPredictionReserveBefore = Weapon ? Weapon->GetReserveAmmo() : 0;

	ClientPrepareAmmoPredictionStep(AmmoPredictionSubjectPlayerId, Step);
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("AMMO_PREDICTION_STEP_START Step=%d Mag=%d Reserve=%d AuthorityShots=%d AuthorityRejects=%d"),
		Step,
		AmmoPredictionMagazineBefore,
		AmmoPredictionReserveBefore,
		AmmoPredictionAuthorityShotsBefore,
		AmmoPredictionAuthorityRejectsBefore);
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionSameValueStep()
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	if (!Weapon)
	{
		return;
	}

	if (!bAmmoPredictionStepCommandSent)
	{
		bAmmoPredictionStepCommandSent = true;
		bAmmoPredictionSameValueArmed = true;
		ClientSubmitAmmoPredictionFire(StepSameValueSnapshot);
		return;
	}

	const int32 ShotDelta = Weapon->GetAuthorityShotCountForAutomationTest() - AmmoPredictionAuthorityShotsBefore;
	const int32 ProjectileDelta = ProjectileSpawnCount - AmmoPredictionProjectilesBefore;
	if (!bAmmoPredictionSettleStarted)
	{
		if (ShotDelta == 0 && ProjectileDelta == 0)
		{
			return;
		}

		// 同帧补弹必须把弹匣恢复到步骤起点值；否则夹具没有构造出同值快照。
		if (ShotDelta != 1 || ProjectileDelta != 1 || Weapon->GetBulletCount() != AmmoPredictionMagazineBefore)
		{
			FailTest(FString::Printf(TEXT("Same-value fixture failed: Shots=%d Projectiles=%d Mag=%d ExpectedMag=%d"),
				ShotDelta, ProjectileDelta, Weapon->GetBulletCount(), AmmoPredictionMagazineBefore));
			bAmmoPredictionFinished = true;
			return;
		}

		AmmoPredictionSettleStartTime = GetWorld()->GetTimeSeconds();
		bAmmoPredictionSettleStarted = true;
		return;
	}

	if (!IsAmmoPredictionClientSampleFresh(StepSameValueSnapshot) ||
		GetWorld()->GetTimeSeconds() - AmmoPredictionSettleStartTime < SettleSeconds)
	{
		return;
	}

	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;
	const bool bConverged = Sample.bValid && Sample.MagazineAmmo == AmmoPredictionMagazineBefore &&
		Sample.PendingShots == 0 && Sample.PredictedMagazineAmmo == AmmoPredictionMagazineBefore;
	const FString Detail = FString::Printf(
		TEXT("ServerMag=%d ServerReserve=%d Shots=%d Projectiles=%d OwnerMag=%d Pending=%d Predicted=%d ")
		TEXT("Feedback=%d Confirm=%d"),
		Weapon->GetBulletCount(),
		Weapon->GetReserveAmmo(),
		ShotDelta,
		ProjectileDelta,
		Sample.MagazineAmmo,
		Sample.PendingShots,
		Sample.PredictedMagazineAmmo,
		Sample.OwnerFeedbackCount,
		Sample.OwnerConfirmationCount);
	ConcludeAmmoPredictionCase(TEXT("SameValueSnapshot"), bConverged, Detail);
	StartAmmoPredictionStep(StepBudgetVeto);
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionBudgetVetoStep()
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	if (!Weapon)
	{
		return;
	}

	if (!bAmmoPredictionStepCommandSent)
	{
		if (!IsAmmoPredictionClientSampleFresh(StepBudgetVeto))
		{
			return;
		}
		const FShooterAmmoPredictionObservation& Ready = AmmoPredictionLatest;
		if (!Ready.bValid || !Ready.bLocalFireCooldownReady || Ready.bFireActive || Ready.bReloadActive)
		{
			return;
		}
		bAmmoPredictionStepCommandSent = true;
		ClientSubmitAmmoPredictionFire(StepBudgetVeto);
		return;
	}

	const int32 ShotDelta = Weapon->GetAuthorityShotCountForAutomationTest() - AmmoPredictionAuthorityShotsBefore;
	if (!bAmmoPredictionSettleStarted)
	{
		if (ShotDelta < 1)
		{
			return;
		}
		if (ProjectileSpawnCount - AmmoPredictionProjectilesBefore != 1)
		{
			FailTest(TEXT("Budget-veto fixture expected exactly one authority projectile"));
			bAmmoPredictionFinished = true;
			return;
		}
		AmmoPredictionSettleStartTime = GetWorld()->GetTimeSeconds();
		bAmmoPredictionSettleStarted = true;
		return;
	}

	if (!IsAmmoPredictionClientSampleFresh(StepBudgetVeto) || GetWorld()->GetTimeSeconds() - AmmoPredictionSettleStartTime < SettleSeconds)
	{
		return;
	}

	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;
	const int32 ActivationDelta = Sample.OwnerFireActivationCount - AmmoPredictionBefore.OwnerFireActivationCount;
	const int32 FeedbackDelta = Sample.OwnerFeedbackCount - AmmoPredictionBefore.OwnerFeedbackCount;
	const int32 ConfirmationDelta = Sample.OwnerConfirmationCount - AmmoPredictionBefore.OwnerConfirmationCount;
	if (ActivationDelta < 1)
	{
		// 没有建立本地动作边界：这是证据不足，不是 H-A3 的预算否决。
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("AMMO_PREDICTION_EVIDENCE_GAP Case=BudgetVeto Reason=NoLocalActivation Shots=%d Mag=%d"),
			ShotDelta,
			Sample.MagazineAmmo);
	}
	else
	{
		const bool bConverged = FeedbackDelta >= 1 && ConfirmationDelta >= 1;
		const FString Detail = FString::Printf(
			TEXT("AuthorityShots=%d ServerMag=%d OwnerMag=%d Pending=%d Predicted=%d ")
			TEXT("ActivationDelta=%d FeedbackDelta=%d ConfirmationDelta=%d"),
			ShotDelta,
			Weapon->GetBulletCount(),
			Sample.MagazineAmmo,
			Sample.PendingShots,
			Sample.PredictedMagazineAmmo,
			ActivationDelta,
			FeedbackDelta,
			ConfirmationDelta);
		ConcludeAmmoPredictionCase(TEXT("BudgetVeto"), bConverged, Detail);
	}
	StartAmmoPredictionStep(StepLateReject);
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionLateRejectStep()
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterCharacter* Subject = AmmoPredictionSubject.Get();
	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	if (!Subject || !Weapon)
	{
		return;
	}

	if (!bAmmoPredictionLateRejectFixtureSet)
	{
		bAmmoPredictionLateRejectFixtureSet = true;
		SetReloadTestAmmo(Weapon, RejectMagazine, 0);
		return;
	}

	if (!bAmmoPredictionStepCommandSent)
	{
		if (!IsAmmoPredictionClientSampleFresh(StepLateReject))
		{
			return;
		}
		const FShooterAmmoPredictionObservation& Ready = AmmoPredictionLatest;
		if (!Ready.bValid || Ready.MagazineAmmo != RejectMagazine || Ready.PendingShots != 0 ||
			Ready.bFireActive || Ready.bReloadActive || Ready.bReloading || !Ready.bLocalFireCooldownReady)
		{
			return;
		}

		// 服务器已知、拥有者尚未收到的阻塞状态：Loose Tag 不复制，
		// 拥有端仍会建立本地预测消费，服务器会在 CanActivateAbility 处真实拒绝。
		if (UAbilitySystemComponent* AbilitySystemComponent = Subject->GetAbilitySystemComponent())
		{
			AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Reloading);
			bAmmoPredictionServerTagApplied = true;
		}
		bAmmoPredictionStepCommandSent = true;
		ClientSubmitAmmoPredictionFire(StepLateReject);
		return;
	}

	if (!bAmmoPredictionRejectObserved)
	{
		if (AmmoPredictionAuthorityRejects - AmmoPredictionAuthorityRejectsBefore >= 1)
		{
			bAmmoPredictionRejectObserved = true;
			AmmoPredictionSettleStartTime = GetWorld()->GetTimeSeconds();
			ClearAmmoPredictionServerTag();
			return;
		}

		if (Weapon->GetAuthorityShotCountForAutomationTest() - AmmoPredictionAuthorityShotsBefore >= 1)
		{
			FailTest(TEXT("Late-reject fixture was accepted by the server; the server-only block did not apply"));
			ClearAmmoPredictionServerTag();
			bAmmoPredictionFinished = true;
		}
		return;
	}

	if (!IsAmmoPredictionClientSampleFresh(StepLateReject) ||
		GetWorld()->GetTimeSeconds() - AmmoPredictionSettleStartTime < RejectSettleSeconds)
	{
		return;
	}

	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;
	const int32 ActivationDelta = Sample.OwnerFireActivationCount - AmmoPredictionBefore.OwnerFireActivationCount;
	const int32 FeedbackDelta = Sample.OwnerFeedbackCount - AmmoPredictionBefore.OwnerFeedbackCount;
	if (ActivationDelta < 1 || FeedbackDelta < 1)
	{
		// 没有建立本地预测消费就无法验证退还；这是证据不足，不是 H-A2 复现。
		UE_LOG(LogShootGame, Display,
			TEXT("AMMO_PREDICTION_EVIDENCE_GAP Case=LateReject Reason=NoLocalPrediction ")
			TEXT("ActivationDelta=%d FeedbackDelta=%d ServerMag=%d"),
			ActivationDelta, FeedbackDelta, Weapon->GetBulletCount());
	}
	else
	{
		const bool bConverged = Sample.PendingShots == 0 &&
			Sample.PredictedMagazineAmmo == RejectMagazine && Sample.MagazineAmmo == RejectMagazine;
		const FString Detail = FString::Printf(
			TEXT("ServerMag=%d ServerShots=%d LocalRejects=%d OwnerMag=%d Pending=%d Predicted=%d ")
			TEXT("ActivationDelta=%d FeedbackDelta=%d"),
			Weapon->GetBulletCount(),
			Weapon->GetAuthorityShotCountForAutomationTest() - AmmoPredictionAuthorityShotsBefore,
			AmmoPredictionAuthorityRejects - AmmoPredictionAuthorityRejectsBefore,
			Sample.MagazineAmmo,
			Sample.PendingShots,
			Sample.PredictedMagazineAmmo,
			ActivationDelta,
			FeedbackDelta);
		ConcludeAmmoPredictionCase(TEXT("LateReject"), bConverged, Detail);
		if (bConverged)
		{
			// H-A2 修复的定向验收标记：H-A1 / H-A3 仍保持 MISMATCH 观察，不由本标记代替。
			UE_LOG(LogShootGame, Display, TEXT("AUTOMATION_TEST_AMMO_PREDICTION_H_A2_SUCCESS"));
		}
	}
	StartAmmoPredictionStep(StepDone);
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionServerPhase()
{
	using namespace ShooterAmmoPredictionNetworkTests;
	if (bAmmoPredictionFinished || !HasAuthority())
	{
		return;
	}

	// 只由「归属远端被试客户端」的服务器端 Coordinator 驱动：Listen Host 自身只提供观察。
	APlayerController* Driver = nullptr;
	int32 PlayerCount = 0;
	int32 LocalCount = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		if (!PlayerController || !PlayerController->PlayerState)
		{
			continue;
		}
		++PlayerCount;
		LocalCount += PlayerController->IsLocalController() ? 1 : 0;
		if (!PlayerController->IsLocalController() &&
			(!Driver || PlayerController->PlayerState->GetPlayerId() < Driver->PlayerState->GetPlayerId()))
		{
			Driver = PlayerController;
		}
	}

	if (GetOwner() != Driver)
	{
		const APlayerController* OwnerPlayerController = Cast<APlayerController>(GetOwner());
		if (bAmmoPredictionSetup || (!Driver && OwnerPlayerController && OwnerPlayerController->IsLocalController() &&
			GetWorld()->GetTimeSeconds() - TestStartTime > StepTimeoutSeconds))
		{
			FailTest(TEXT("Ammo prediction lost or never received its selected remote driver"));
			bAmmoPredictionFinished = true;
		}
		return;
	}

	if (!bAmmoPredictionSetup)
	{
		if (GetWorld()->GetTimeSeconds() - TestStartTime > StepTimeoutSeconds)
		{
			FailTest(TEXT("Ammo prediction two-player Coordinator setup timed out"));
			bAmmoPredictionFinished = true;
			return;
		}
		if (PlayerCount != 2 || LocalCount != 1 || !GetShooterCharacter())
		{
			return;
		}

		AShooterCharacter* Subject = Cast<AShooterCharacter>(Driver->GetPawn());
		if (!Subject)
		{
			return;
		}

		for (TActorIterator<AShooterNPC> It(GetWorld()); It; ++It)
		{
			It->StopShooting();
			if (AController* NpcController = It->GetController())
			{
				NpcController->Destroy();
			}
		}

		UShooterWeaponRuntimeSubsystem* Runtime = GetWorld()->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
		UShooterInventoryComponent* Inventory = Subject->GetInventoryComponent();
		UShooterEquipmentComponent* Equipment = Subject->GetEquipmentComponent();
		AShooterWeapon* Weapon = Subject->GetCurrentWeaponActor();
		if (Runtime && Inventory && Equipment && (!Weapon || Weapon->GetWeaponId() != FName(TEXT("Rifle"))))
		{
			Weapon = Inventory->FindWeaponByWeaponId(TEXT("Rifle"));
			if (!Weapon)
			{
				Weapon = Runtime->AcquireWeapon(TEXT("Rifle"), Subject, Subject);
				if (Weapon && Inventory->AddWeapon(Weapon) != EShooterInventoryAddResult::Added)
				{
					Runtime->ReleaseWeapon(Weapon);
					Weapon = nullptr;
				}
			}
			if (Weapon)
			{
				Equipment->EquipWeapon(Weapon);
			}
		}

		if (!Weapon || !Inventory || !Inventory->ContainsWeapon(Weapon) ||
			Subject->GetCurrentWeaponActor() != Weapon || !Subject->GetAbilitySystemComponent())
		{
			FailTest(TEXT("Ammo prediction could not equip the production Rifle"));
			bAmmoPredictionFinished = true;
			return;
		}

		AmmoPredictionSubject = Subject;
		AmmoPredictionWeapon = Weapon;
		AmmoPredictionSubjectPlayerId = Driver->PlayerState->GetPlayerId();
		BindAmmoPredictionServerObserver(Subject->GetAbilitySystemComponent());
		SetReloadTestAmmo(Weapon, FixtureMagazine, FixtureReserve);
		bAmmoPredictionSetup = true;
		StartAmmoPredictionStep(StepSetup);
		return;
	}

	AShooterCharacter* Subject = AmmoPredictionSubject.Get();
	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	if (!Subject || !Weapon || Subject->GetCurrentWeaponActor() != Weapon || Weapon->GetOwner() != Subject)
	{
		FailTest(TEXT("Ammo prediction lost the pinned Avatar or Weapon"));
		bAmmoPredictionFinished = true;
		return;
	}

	if (GetWorld()->GetTimeSeconds() - AmmoPredictionStepStartTime > StepTimeoutSeconds)
	{
		FailTest(FString::Printf(TEXT("Ammo prediction step timed out: Step=%d Mag=%d Reserve=%d Rejects=%d"),
			AmmoPredictionServerStep, Weapon->GetBulletCount(), Weapon->GetReserveAmmo(),
			AmmoPredictionAuthorityRejects));
		ClearAmmoPredictionServerTag();
		bAmmoPredictionFinished = true;
		return;
	}

	switch (AmmoPredictionServerStep)
	{
	case StepSetup:
		if (IsAmmoPredictionClientSampleFresh(StepSetup))
		{
			const FShooterAmmoPredictionObservation& Ready = AmmoPredictionLatest;
			if (Ready.bValid && Ready.MagazineAmmo == FixtureMagazine && Ready.PendingShots == 0 &&
				!Ready.bFireActive && !Ready.bReloadActive && !Ready.bReloading)
			{
				StartAmmoPredictionStep(StepSameValueSnapshot);
			}
		}
		return;
	case StepSameValueSnapshot:
		RunAmmoPredictionSameValueStep();
		return;
	case StepBudgetVeto:
		RunAmmoPredictionBudgetVetoStep();
		return;
	case StepLateReject:
		RunAmmoPredictionLateRejectStep();
		return;
	case StepDone:
		if (!bAmmoPredictionStepCommandSent)
		{
			bAmmoPredictionStepCommandSent = true;
			ClearAmmoPredictionServerTag();
			UE_LOG(
				LogShootGame,
				Display,
				TEXT("AUTOMATION_TEST_AMMO_PREDICTION_DONE Cases=3 Converged=%d Mismatches=%d"),
				AmmoPredictionConvergedCount,
				AmmoPredictionMismatchCount);
			bAmmoPredictionFinished = true;
		}
		return;
	default:
		return;
	}
}

void AShooterNetworkTestCoordinator::CleanupAmmoPredictionTest()
{
	ClearAmmoPredictionServerTag();
	if (UAbilitySystemComponent* AbilitySystemComponent = AmmoPredictionObservedASC.Get())
	{
		AbilitySystemComponent->AbilityActivatedCallbacks.Remove(AmmoPredictionActivatedHandle);
		AbilitySystemComponent->AbilityEndedCallbacks.Remove(AmmoPredictionEndedHandle);
		AbilitySystemComponent->AbilityFailedCallbacks.Remove(AmmoPredictionFailedHandle);
	}
	AmmoPredictionActivatedHandle.Reset();
	AmmoPredictionEndedHandle.Reset();
	AmmoPredictionFailedHandle.Reset();
	AmmoPredictionObservedASC.Reset();
}

#else

void AShooterNetworkTestCoordinator::ClientPrepareAmmoPredictionStep_Implementation(int32 SubjectPlayerId, int32 Step)
{
}

void AShooterNetworkTestCoordinator::ClientSubmitAmmoPredictionFire_Implementation(int32 Step)
{
}

void AShooterNetworkTestCoordinator::ServerReportAmmoPredictionSample_Implementation(const FShooterAmmoPredictionObservation& Observation)
{
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionServerPhase()
{
}

void AShooterNetworkTestCoordinator::SampleAmmoPredictionLocalState()
{
}

#endif
