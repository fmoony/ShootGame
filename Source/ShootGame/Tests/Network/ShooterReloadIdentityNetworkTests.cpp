// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/Network/ShooterNetworkTestCoordinator.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "AbilitySystem/Abilities/ShooterGameplayAbility_Fire.h"
#include "AbilitySystem/Abilities/ShooterGameplayAbility_Reload.h"
#include "AbilitySystem/ShooterAbilitySystemComponent.h"
#include "AbilitySystem/ShooterGameplayTags.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "GameFramework/PlayerState/ShooterPlayerState.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "Weapons/Subsystems/ShooterWeaponRuntimeSubsystem.h"
#include "ShootGame.h"

namespace ShooterReloadIdentityNetworkTests
{
	constexpr float StepTimeoutSeconds = 30.0f;
	constexpr int32 LastStep = 9;

	bool IsRejectStep(int32 Step)
	{
		return Step == 7 || Step == 8;
	}

	bool IsReloadAbility(const UGameplayAbility* Ability)
	{
		return Ability && Ability->IsA<UShooterGameplayAbility_Reload>();
	}
}

void AShooterNetworkTestCoordinator::BindReloadIdentityAbilityObservers(UAbilitySystemComponent* ASC)
{
	if (!ASC || ReloadIdentityObservedASC.Get() == ASC)
	{
		return;
	}

	ReloadIdentityObservedASC = ASC;
	ReloadIdentityActivatedHandle = ASC->AbilityActivatedCallbacks.AddUObject(
		this, &AShooterNetworkTestCoordinator::HandleReloadIdentityActivated);
	ReloadIdentityFailedHandle = ASC->AbilityFailedCallbacks.AddUObject(
		this, &AShooterNetworkTestCoordinator::HandleReloadIdentityFailed);
	ReloadIdentityEndedHandle = ASC->AbilityEndedCallbacks.AddUObject(
		this, &AShooterNetworkTestCoordinator::HandleReloadIdentityEnded);
}

void AShooterNetworkTestCoordinator::HandleReloadIdentityActivated(UGameplayAbility* Ability)
{
	if (!ShooterReloadIdentityNetworkTests::IsReloadAbility(Ability))
	{
		return;
	}

	const int32 Key = Ability->GetCurrentActivationInfo().GetActivationPredictionKey().Current;
	ReloadIdentityPredictionKey = Key;
	if (HasAuthority())
	{
		++ReloadIdentityAuthorityActivations;
	}
	else
	{
		++ReloadIdentityOwnerActivations;
	}
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("RELOAD_IDENTITY_ACTIVATED Step=%d Authority=%d Key=%d Count=%d"),
		HasAuthority() ? ReloadIdentityServerStep : ReloadIdentityClientStep,
		HasAuthority() ? 1 : 0,
		Key,
		HasAuthority() ? ReloadIdentityAuthorityActivations : ReloadIdentityOwnerActivations);
}

void AShooterNetworkTestCoordinator::HandleReloadIdentityFailed(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureTags)
{
	if (!HasAuthority() || !ShooterReloadIdentityNetworkTests::IsReloadAbility(Ability))
	{
		return;
	}

	// 只订阅发起者服务器 ASC，夹具不调用 CanActivateAbility；这里记录真实 GAS 请求裁决。
	UAbilitySystemComponent* ASC = ReloadIdentityObservedASC.Get();
	if (ASC && ASC->ScopedPredictionKey.IsValidKey())
	{
		++ReloadIdentityAuthorityRejects;
		ReloadIdentityPredictionKey = ASC->ScopedPredictionKey.Current;
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("RELOAD_IDENTITY_SERVER_REJECT Step=%d Key=%d Count=%d Tags=%s"),
			ReloadIdentityServerStep,
			ASC->ScopedPredictionKey.Current,
			ReloadIdentityAuthorityRejects,
			*FailureTags.ToStringSimple());
	}
}

void AShooterNetworkTestCoordinator::HandleReloadIdentityEnded(UGameplayAbility* Ability)
{
	if (HasAuthority() || !ShooterReloadIdentityNetworkTests::IsReloadAbility(Ability))
	{
		return;
	}

	const FGameplayAbilityActivationInfo Info = Ability->GetCurrentActivationInfo();
	if (Info.ActivationMode == EGameplayAbilityActivationMode::Rejected &&
		Info.GetActivationPredictionKey().Current == ReloadIdentityPredictionKey)
	{
		++ReloadIdentityOwnerRejects;
		UE_LOG(LogShootGame, Display, TEXT("RELOAD_IDENTITY_OWNER_REJECT_CONFIRMED Step=%d Key=%d Count=%d"),
			ReloadIdentityClientStep, ReloadIdentityPredictionKey, ReloadIdentityOwnerRejects);
	}
}

void AShooterNetworkTestCoordinator::ClientPrepareReloadIdentityStep_Implementation(int32 SubjectPlayerId, int32 Step)
{
	if (!bReloadIdentityMode)
	{
		return;
	}

	ReloadIdentityBefore = ReloadIdentityLatest;
	ReloadIdentitySubjectPlayerId = SubjectPlayerId;
	ReloadIdentityClientStep = Step;
	bReloadIdentitySawReloadState = false;
	bReloadIdentitySawRecovery = ReloadIdentityBefore.bRecovering;
	ReloadIdentityNextReportTime = 0.0f;

	AShooterCharacter* Subject = ReloadIdentitySubject.Get();
	USkeletalMeshComponent* Mesh = Subject ? Subject->GetMesh() : nullptr;
	if (Mesh)
	{
		const UAnimInstance* Anim = Mesh->GetAnimInstance();
		const FAnimNode_StateMachine* Machine = Anim
			? Anim->GetStateMachineInstanceFromName(TEXT("WeaponAction")) : nullptr;
		if (Machine)
		{
			ReloadIdentityBefore.AnimationState = Machine->GetCurrentStateName();
			ReloadIdentityBefore.AnimationTime = Machine->GetCurrentStateElapsedTime();
		}
	}
	SampleReloadIdentityLocalState();
}

void AShooterNetworkTestCoordinator::ClientSubmitReloadIdentityInput_Implementation(int32 Step, bool bFire)
{
	AShooterCharacter* Subject = ReloadIdentitySubject.Get();
	if (!bReloadIdentityMode || Step != ReloadIdentityClientStep || Step == ReloadIdentitySubmittedStep ||
		!Subject || !Subject->IsLocallyControlled() || Subject != GetShooterCharacter())
	{
		return;
	}

	ReloadIdentitySubmittedStep = Step;
	if (bFire)
	{
		// 同帧 Press + Release 走生产 ASC 解释层，保持真实单发，不调用 Weapon.Fire 或服务器实现。
		Subject->DoStartFiring();
		Subject->DoStopFiring();
	}
	else
	{
		Subject->DoReload();
	}
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("RELOAD_IDENTITY_INPUT Step=%d Kind=%s PlayerId=%d"),
		Step,
		bFire ? TEXT("FireClick") : TEXT("Reload"),
		Subject->GetPlayerState() ? Subject->GetPlayerState()->GetPlayerId() : INDEX_NONE);
}

void AShooterNetworkTestCoordinator::SampleReloadIdentityLocalState()
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!bReloadIdentityMode || ReloadIdentityClientStep == INDEX_NONE || !PC || !PC->IsLocalController())
	{
		return;
	}

	FShooterReloadIdentityObservation Sample;
	Sample.Step = ReloadIdentityClientStep;
	if (Sample.Step == 0 && !ReloadIdentitySubject.IsValid())
	{
		// 测试目标以稳定 PlayerId 指定，不能让准备 RPC 的未解析 Actor 参数永久变成空指针。
		for (TActorIterator<AShooterCharacter> It(GetWorld()); It; ++It)
		{
			const APlayerState* CandidatePS = It->GetPlayerState();
			if (CandidatePS && CandidatePS->GetPlayerId() == ReloadIdentitySubjectPlayerId)
			{
				ReloadIdentitySubject = *It;
				break;
			}
		}
	}
	Sample.Subject = ReloadIdentitySubject.Get();
	if (Sample.Step == 0 && Sample.Subject && !ReloadIdentityWeapon.IsValid())
	{
		// 首次可靠 RPC 不等待跨 Actor 引用解析；从正式 Equipment 获取后固定目标，后续步骤不重新选枪。
		AShooterWeapon* Candidate = Sample.Subject->GetCurrentWeaponActor();
		if (IsValid(Candidate) && Candidate->GetOwner() == Sample.Subject)
		{
			ReloadIdentityWeapon = Candidate;
		}
	}
	Sample.Weapon = ReloadIdentityWeapon.Get();
	AShooterCharacter* Subject = Sample.Subject;
	AShooterWeapon* Weapon = Sample.Weapon;
	USkeletalMeshComponent* Mesh = Subject ? Subject->GetMesh() : nullptr;
	if (Mesh && !ReloadIdentityTickMesh.IsValid())
	{
		// NullRHI 仍求值正式动画图；仅修改夹具网格的 Tick 设置，清理时恢复。
		ReloadIdentityTickMesh = Mesh;
		ReloadIdentityPreviousTickOption = static_cast<uint8>(Mesh->VisibilityBasedAnimTickOption);
		bReloadIdentityPreviousMeshTick = Mesh->IsComponentTickEnabled();
		bReloadIdentityPreviousUpdateOptimization = Mesh->bEnableUpdateRateOptimizations;
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->bEnableUpdateRateOptimizations = false;
		Mesh->SetComponentTickEnabled(true);
	}
	if (Subject && Subject->IsLocallyControlled() && !HasAuthority())
	{
		BindReloadIdentityAbilityObservers(Subject->GetAbilitySystemComponent());
	}
	AShooterPlayerState* PS = Subject ? Subject->GetPlayerState<AShooterPlayerState>() : nullptr;
	UAbilitySystemComponent* ASC = Subject ? Subject->GetAbilitySystemComponent() : nullptr;
	UShooterThirdPersonAnimInstance* Anim = Subject && Subject->GetMesh()
		? Cast<UShooterThirdPersonAnimInstance>(Subject->GetMesh()->GetAnimInstance())
		: nullptr;
	const FAnimNode_StateMachine* Machine = Anim
		? Anim->GetStateMachineInstanceFromName(TEXT("WeaponAction")) : nullptr;
	Sample.bOwner = Subject && Subject->IsLocallyControlled();
	Sample.bValid = IsValid(Subject) && IsValid(Weapon) && PS && ASC && Anim && Machine &&
		Subject->GetCurrentWeaponActor() == Weapon && Weapon->GetOwner() == Subject && !Weapon->IsHidden();
	if (Sample.bOwner)
	{
		Sample.bValid &= ASC && ASC->GetAvatarActor() == Subject && PS && ASC->FindAbilitySpecFromClass(PS->GetReloadAbilityClass()) &&
			ASC->FindAbilitySpecFromClass(PS->GetFireAbilityClass());
	}
	if (Sample.bValid)
	{
		Sample.ReloadId = PS->GetReloadId();
		Sample.ObservedReloadId = Anim->GetObservedReloadId();
		Sample.NewPresentationCount = Anim->GetNewReloadPresentationCountForAutomationTest();
		Sample.EntryCount = Anim->GetReloadPresentationEntryCountForAutomationTest();
		Sample.bReloading = Anim->bIsReloading;
		Sample.bRecovering = Anim->bReloadPresentationRecovering;
		Sample.AnimationState = Machine->GetCurrentStateName();
		const float StateTimeNow = Machine->GetCurrentStateElapsedTime();
		const bool bNewEntry = Sample.EntryCount > ReloadIdentityBefore.EntryCount &&
			(Sample.bOwner || Sample.ObservedReloadId != ReloadIdentityBefore.ObservedReloadId);
		const bool bActualReload = Sample.AnimationState == FName(TEXT("Reload")) && Sample.bReloading && !Sample.bRecovering && bNewEntry;
		Sample.AnimationTime = bReloadIdentitySawReloadState && ReloadIdentityLatest.Step == Sample.Step
			? ReloadIdentityLatest.AnimationTime : StateTimeNow;
		if (bActualReload && !bReloadIdentitySawReloadState)
		{
			// 记录实际状态机的新一轮起点，后续保留该时间；不是只读取 NewReload 的输入侧计数。
			UE_LOG(LogShootGame, Display,
				TEXT("RELOAD_IDENTITY_MACHINE_ENTER Step=%d Owner=%d Id=%u Entries=%d->%d ")
				TEXT("PreviousState=%s PreviousTime=%.3f PreviousReloading=%d PreviousRecovery=%d RestartTime=%.3f"),
				Sample.Step,
				Sample.bOwner ? 1 : 0,
				Sample.ObservedReloadId,
				ReloadIdentityBefore.EntryCount,
				Sample.EntryCount,
				*ReloadIdentityBefore.AnimationState.ToString(),
				ReloadIdentityBefore.AnimationTime,
				ReloadIdentityBefore.bReloading ? 1 : 0,
				ReloadIdentityBefore.bRecovering ? 1 : 0,
				StateTimeNow);
			bReloadIdentitySawReloadState = true;
		}
		bReloadIdentitySawRecovery |= Sample.bRecovering;
		Sample.bSawReloadState = bReloadIdentitySawReloadState;
		Sample.bSawRecovery = bReloadIdentitySawRecovery;
		if (Sample.bOwner)
		{
			Sample.bActive = HasActiveReloadAbility(Subject);
			Sample.OwnerActivationCount = ReloadIdentityOwnerActivations;
			Sample.OwnerRejectCount = ReloadIdentityOwnerRejects;
			Sample.PredictionKey = ReloadIdentityPredictionKey;
			Sample.MagazineAmmo = Weapon->GetBulletCount();
			Sample.ReserveAmmo = Weapon->GetReserveAmmo();
			Sample.OwnerFireFeedbackCount = Weapon->GetPredictedOwnerFeedbackCountForAutomationTest();
			Sample.OwnerFireConfirmationCount = Weapon->GetOwnerAuthorityConfirmationCountForAutomationTest();
		}
	}

	// 每帧保存首次真实进入的时间，RPC 限频不能让下一帧覆盖这份重启起点证据。
	ReloadIdentityLatest = Sample;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= ReloadIdentityNextReportTime)
	{
		if (!Sample.bValid && FMath::Fmod(Now, 1.0f) < 0.05f)
		{
			UE_LOG(LogShootGame, Display,
				TEXT("RELOAD_IDENTITY_INVALID Subject=%s Weapon=%s Current=%s PS=%s ASC=%s Avatar=%s ")
				TEXT("Anim=%s Machine=%d Owner=%s Hidden=%d"),
				*GetNameSafe(Subject),
				*GetNameSafe(Weapon),
				*GetNameSafe(Subject ? Subject->GetCurrentWeaponActor() : nullptr),
				*GetNameSafe(PS),
				*GetNameSafe(ASC),
				*GetNameSafe(ASC ? ASC->GetAvatarActor() : nullptr),
				Anim ? *Anim->GetClass()->GetName() : TEXT("None"),
				Machine ? 1 : 0,
				*GetNameSafe(Weapon ? Weapon->GetOwner() : nullptr),
				Weapon && Weapon->IsHidden());
		}
		ReloadIdentityNextReportTime = Now + 0.05f;
		ServerReportReloadIdentitySample(Sample);
	}
}

void AShooterNetworkTestCoordinator::ServerReportReloadIdentitySample_Implementation(const FShooterReloadIdentityObservation& Observation)
{
	if (!bReloadIdentityMode || Observation.Step != ReloadIdentityClientStep)
	{
		return;
	}

	ReloadIdentityLatest = Observation;
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	const TCHAR* ObservationRole = Observation.bOwner ? TEXT("Owner")
		: PC && PC->IsLocalController() ? TEXT("HostObserver") : TEXT("ClientObserver");
	UE_LOG(LogShootGame, Display,
		TEXT("RELOAD_IDENTITY_SAMPLE Step=%d Role=%s Valid=%d Id=%u Observed=%u New=%d Entries=%d ")
		TEXT("Activations=%d Rejects=%d Key=%d Active=%d Reloading=%d Recovery=%d State=%s Time=%.3f Seen=%d"),
		Observation.Step,
		ObservationRole,
		Observation.bValid ? 1 : 0,
		Observation.ReloadId,
		Observation.ObservedReloadId,
		Observation.NewPresentationCount,
		Observation.EntryCount,
		Observation.OwnerActivationCount,
		Observation.OwnerRejectCount,
		Observation.PredictionKey,
		Observation.bActive ? 1 : 0,
		Observation.bReloading ? 1 : 0,
		Observation.bRecovering ? 1 : 0,
		*Observation.AnimationState.ToString(),
		Observation.AnimationTime,
		Observation.bSawReloadState ? 1 : 0);
}

void AShooterNetworkTestCoordinator::StartReloadIdentityStep(int32 Step)
{
	AShooterWeapon* Weapon = ReloadIdentityWeapon.Get();
	AShooterCharacter* Subject = ReloadIdentitySubject.Get();
	AShooterPlayerState* PS = Subject ? Subject->GetPlayerState<AShooterPlayerState>() : nullptr;
	if (!Weapon || !Subject || !PS)
	{
		FailTest(TEXT("Reload identity step lost its authority target"));
		bReloadIdentityFinished = true;
		return;
	}

	const int32 Size = Weapon->GetMagazineSize();
	bool bFixtureValid = Size > 5;
	if (Step == 1)
	{
		bFixtureValid &= SetReloadTestAmmo(Weapon, Size - 5, 2);
	}
	else if (Step == 2)
	{
		// 第一轮只转移两发，仍缺三发；只补服务器备弹，随后立即发出第二次生产输入。
		bFixtureValid &= SetReloadTestAmmo(Weapon, Weapon->GetBulletCount(), 20);
	}
	else if (Step == 5 || Step == 9)
	{
		bFixtureValid &= SetReloadTestAmmo(Weapon, Size - 5, 20);
	}
	else if (Step == 7)
	{
		bFixtureValid &= SetReloadTestAmmo(Weapon, Size, 20);
	}
	else if (Step == 8)
	{
		bFixtureValid &= SetReloadTestAmmo(Weapon, Size - 5, 0);
	}
	if (!bFixtureValid)
	{
		FailTest(TEXT("Reload identity ammo fixture failed"));
		bReloadIdentityFinished = true;
		return;
	}

	ReloadIdentityServerStep = Step;
	ReloadIdentityStepStartTime = GetWorld()->GetTimeSeconds();
	ReloadIdentityIdBefore = PS->GetReloadId();
	ReloadIdentityMagazineBefore = Weapon->GetBulletCount();
	ReloadIdentityReserveBefore = Weapon->GetReserveAmmo();
	ReloadIdentityShotsBefore = Weapon->GetAuthorityShotCountForAutomationTest();
	ReloadIdentityProjectilesBefore = ProjectileSpawnCount;
	ReloadIdentityAuthorityActivationsBefore = ReloadIdentityAuthorityActivations;
	ReloadIdentityAuthorityRejectsBefore = ReloadIdentityAuthorityRejects;
	bReloadIdentityCancelSent = false;
	for (const TWeakObjectPtr<AShooterNetworkTestCoordinator>& Participant : ReloadIdentityParticipants)
	{
		AShooterNetworkTestCoordinator* Coordinator = Participant.Get();
		Coordinator->ReloadIdentityBefore = Coordinator->ReloadIdentityLatest;
		Coordinator->ReloadIdentityClientStep = Step;
		Coordinator->ClientPrepareReloadIdentityStep(PS->GetPlayerId(), Step);
	}
	ClientSubmitReloadIdentityInput(Step, Step == 3);
	UE_LOG(LogShootGame, Display, TEXT("RELOAD_IDENTITY_STEP_START Step=%d Id=%u Mag=%d Reserve=%d"),
		Step, ReloadIdentityIdBefore, ReloadIdentityMagazineBefore, ReloadIdentityReserveBefore);
}

void AShooterNetworkTestCoordinator::RunReloadIdentityServerPhase()
{
	using namespace ShooterReloadIdentityNetworkTests;
	if (bReloadIdentityFinished)
	{
		return;
	}

	// Listen 的三个 PostLogin 各自创建原有类；按 PlayerId 选唯一远端发起者，不使用首个模拟代理。
	APlayerController* Driver = nullptr;
	int32 PlayerCount = 0;
	int32 LocalCount = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->PlayerState)
		{
			continue;
		}
		++PlayerCount;
		LocalCount += PC->IsLocalController() ? 1 : 0;
		if (!PC->IsLocalController() && (!Driver || PC->PlayerState->GetPlayerId() <
			Driver->PlayerState->GetPlayerId()))
		{
			Driver = PC;
		}
	}
	if (GetOwner() != Driver)
	{
		const APlayerController* OwnerPC = Cast<APlayerController>(GetOwner());
		if (bReloadIdentitySetup || (!Driver && OwnerPC && OwnerPC->IsLocalController() &&
			GetWorld()->GetTimeSeconds() - TestStartTime > StepTimeoutSeconds))
		{
			FailTest(TEXT("Reload identity lost or never received its selected remote driver"));
			bReloadIdentityFinished = true;
		}
		return;
	}

	if (!bReloadIdentitySetup)
	{
		if (GetWorld()->GetTimeSeconds() - TestStartTime > StepTimeoutSeconds)
		{
			FailTest(TEXT("Reload identity three-player Coordinator setup timed out"));
			bReloadIdentityFinished = true;
			return;
		}
		if (PlayerCount != 3 || LocalCount != 1 || !GetShooterCharacter())
		{
			if (GetWorld()->GetTimeSeconds() - TestStartTime > StepTimeoutSeconds)
			{
				FailTest(TEXT("Reload identity requires one Listen host and two remote clients"));
				bReloadIdentityFinished = true;
			}
			return;
		}

		ReloadIdentityParticipants.Reset();
		ReloadIdentityParticipants.Add(this);
		for (TActorIterator<AShooterNetworkTestCoordinator> It(GetWorld()); It; ++It)
		{
			APlayerController* PC = Cast<APlayerController>(It->GetOwner());
			if (*It != this && It->bReloadIdentityMode && PC && PC->GetPawn())
			{
				ReloadIdentityParticipants.Add(*It);
			}
		}
		if (ReloadIdentityParticipants.Num() != 3)
		{
			return;
		}

		for (TActorIterator<AShooterNPC> It(GetWorld()); It; ++It)
		{
			It->StopShooting();
			if (AController* Controller = It->GetController())
			{
				Controller->Destroy();
			}
		}
		AShooterCharacter* Subject = GetShooterCharacter();
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
			FailTest(TEXT("Reload identity could not equip the production Rifle"));
			bReloadIdentityFinished = true;
			return;
		}

		ReloadIdentitySubject = Subject;
		ReloadIdentityWeapon = Weapon;
		BindReloadIdentityAbilityObservers(Subject->GetAbilitySystemComponent());
		bReloadIdentitySetup = true;
		ReloadIdentityStepStartTime = GetWorld()->GetTimeSeconds();
		for (const TWeakObjectPtr<AShooterNetworkTestCoordinator>& Participant : ReloadIdentityParticipants)
		{
			Participant->ReloadIdentitySubject = Subject;
			Participant->ReloadIdentityWeapon = Weapon;
			Participant->ReloadIdentityClientStep = 0;
			Participant->ClientPrepareReloadIdentityStep(Driver->PlayerState->GetPlayerId(), 0);
		}
		UE_LOG(LogShootGame, Display, TEXT("RELOAD_IDENTITY_SETUP Players=3 Owner=%d Observers=2 Weapon=%s"),
			Driver->PlayerState->GetPlayerId(), *GetNameSafe(Weapon));
		return;
	}

	if (PlayerCount != 3 || LocalCount != 1 || !ReloadIdentitySubject.IsValid() || !ReloadIdentityWeapon.IsValid())
	{
		FailTest(TEXT("Reload identity participant or target lifetime changed"));
		bReloadIdentityFinished = true;
		return;
	}
	if (GetWorld()->GetTimeSeconds() - ReloadIdentityStepStartTime > StepTimeoutSeconds)
	{
		FailTest(FString::Printf(TEXT("Reload identity timed out: Step=%d Id=%u Accepts=%d Rejects=%d Cancel=%d"),
			ReloadIdentityServerStep,
			ReloadIdentityIdBefore,
			ReloadIdentityAuthorityActivations,
			ReloadIdentityAuthorityRejects,
			bReloadIdentityCancelSent ? 1 : 0));
		bReloadIdentityFinished = true;
		return;
	}

	AShooterCharacter* Subject = ReloadIdentitySubject.Get();
	AShooterWeapon* Weapon = ReloadIdentityWeapon.Get();
	AShooterPlayerState* PS = Subject->GetPlayerState<AShooterPlayerState>();
	UAbilitySystemComponent* ASC = Subject->GetAbilitySystemComponent();
	if (!PS || !ASC || Subject->GetCurrentWeaponActor() != Weapon || Weapon->GetOwner() != Subject)
	{
		FailTest(TEXT("Reload identity lost the pinned Avatar, ASC or Weapon"));
		bReloadIdentityFinished = true;
		return;
	}

	const int32 Step = ReloadIdentityServerStep;
	const bool bFire = Step == 3;
	const bool bReject = IsRejectStep(Step);
	const bool bAccept = Step > 0 && !bFire && !bReject;
	uint32 ExpectedId = ReloadIdentityIdBefore + (bAccept ? 1u : 0u);
	if (bAccept && ExpectedId == 0)
	{
		ExpectedId = 1;
	}
	bool bAllObserved = true;
	bool bOwnerInactive = false;
	int32 OwnerSamples = 0;
	int32 ObserverSamples = 0;
	for (const TWeakObjectPtr<AShooterNetworkTestCoordinator>& Participant : ReloadIdentityParticipants)
	{
		if (!Participant.IsValid())
		{
			FailTest(TEXT("Reload identity lost an observer Coordinator"));
			bReloadIdentityFinished = true;
			return;
		}
		const FShooterReloadIdentityObservation& Now = Participant->ReloadIdentityLatest;
		const FShooterReloadIdentityObservation& Before = Participant->ReloadIdentityBefore;
		if (Now.Step != Step)
		{
			bAllObserved = false;
			continue;
		}
		if (!Now.bValid || Now.Subject != Subject || Now.Weapon != Weapon)
		{
			if (Step > 0)
			{
				FailTest(TEXT("Reload identity observation lost its pinned target or actual state machine"));
				bReloadIdentityFinished = true;
				return;
			}
			bAllObserved = false;
			continue;
		}
		if (Now.bOwner != (Participant.Get() == this))
		{
			FailTest(TEXT("Reload identity observation came from the wrong owning view"));
			bReloadIdentityFinished = true;
			return;
		}
		if (Step == 0)
		{
			bAllObserved &= !Now.bReloading && !Now.bRecovering;
			continue;
		}
		if (Now.bOwner)
		{
			++OwnerSamples;
			bOwnerInactive = !Now.bActive && !Now.bReloading;
			bAllObserved &= Now.OwnerActivationCount - Before.OwnerActivationCount == (bFire ? 0 : 1);
			bAllObserved &= Now.OwnerRejectCount - Before.OwnerRejectCount == (bReject ? 1 : 0);
			bAllObserved &= Now.NewPresentationCount == Before.NewPresentationCount && Now.ObservedReloadId == Before.ObservedReloadId;
			if (!bFire)
			{
				bAllObserved &= Now.PredictionKey > 0 && Now.PredictionKey == ReloadIdentityPredictionKey;
			}
			if (bFire)
			{
				bAllObserved &= Now.OwnerFireFeedbackCount - Before.OwnerFireFeedbackCount == 1 &&
					Now.OwnerFireConfirmationCount - Before.OwnerFireConfirmationCount == 1;
			}
			else if (!bReject)
			{
				bAllObserved &= Now.EntryCount - Before.EntryCount == 1 && Now.bSawReloadState;
			}
			else
			{
				// Reject 可在第一次骨骼求值前返回，但必须已真实建立本地预测激活。
				const int32 Entries = Now.EntryCount - Before.EntryCount;
				bAllObserved &= Entries >= 0 && Entries <= 1 && Now.PredictionKey > 0;
			}
		}
		else
		{
			++ObserverSamples;
			const int32 ExpectedNew = bFire || bReject ? 0 : 1;
			bAllObserved &= Now.ReloadId == ExpectedId && Now.ObservedReloadId == ExpectedId &&
				Now.NewPresentationCount - Before.NewPresentationCount == ExpectedNew && Now.EntryCount - Before.EntryCount == ExpectedNew;
			if (ExpectedNew > 0)
			{
				bAllObserved &= Now.bSawReloadState && Now.AnimationTime <= 0.25f;
			}
		}
	}
	if (Step == 0)
	{
		if (bAllObserved && !HasActiveReloadAbility(Subject) && !ASC->HasMatchingGameplayTag(ShooterGameplayTags::State_Reloading))
		{
			StartReloadIdentityStep(1);
		}
		return;
	}
	bAllObserved &= OwnerSamples == 1 && ObserverSamples == 2;

	const int32 AcceptDelta = ReloadIdentityAuthorityActivations - ReloadIdentityAuthorityActivationsBefore;
	const int32 RejectDelta = ReloadIdentityAuthorityRejects - ReloadIdentityAuthorityRejectsBefore;
	const int32 ShotDelta = Weapon->GetAuthorityShotCountForAutomationTest() - ReloadIdentityShotsBefore;
	const int32 ProjectileDelta = ProjectileSpawnCount - ReloadIdentityProjectilesBefore;
	const int32 Transfer = bFire || bReject ? 0
		: FMath::Min(Weapon->GetMagazineSize() - ReloadIdentityMagazineBefore, ReloadIdentityReserveBefore);
	const bool bServerActive = HasActiveReloadAbility(Subject);
	const bool bServerReloading = ASC->HasMatchingGameplayTag(ShooterGameplayTags::State_Reloading);
	const bool bAuthorityCounts = PS->GetReloadId() == ExpectedId && AcceptDelta == (bFire || bReject ? 0 : 1) &&
		RejectDelta == (bReject ? 1 : 0) && ShotDelta == (bFire ? 1 : 0) && ProjectileDelta == (bFire ? 1 : 0);

	if (Step == 5 && !bReloadIdentityCancelSent)
	{
		if (bAllObserved && bAuthorityCounts && bServerActive && bServerReloading)
		{
			FGameplayTagContainer ReloadTags(ShooterGameplayTags::Input_Reload);
			ASC->CancelAbilities(&ReloadTags);
			bReloadIdentityCancelSent = true;
			UE_LOG(LogShootGame, Display, TEXT("RELOAD_IDENTITY_SERVER_CANCEL Step=5 Id=%u"), ExpectedId);
		}
		return;
	}

	const int32 ExpectedTransfer = Step == 5 ? 0 : Transfer;
	const bool bAmmoValid = Weapon->GetBulletCount() == ReloadIdentityMagazineBefore + ExpectedTransfer -
		(bFire ? 1 : 0) && Weapon->GetReserveAmmo() == ReloadIdentityReserveBefore - ExpectedTransfer;
	if (!bAllObserved || !bAuthorityCounts || !bAmmoValid || !bOwnerInactive || bServerActive || bServerReloading ||
		HasActiveFireAbility(Subject))
	{
		return;
	}

	// 正样本与两个独立观察者都满足后才推进；不等待 Remote 观察到 false，保留快速重入风险窗口。
	UE_LOG(LogShootGame, Display,
		TEXT("RELOAD_IDENTITY_STEP_SUCCESS Step=%d Id=%u->%u Accept=%d Reject=%d Mag=%d->%d ")
		TEXT("Reserve=%d->%d Shots=%d Projectiles=%d Owner=1 Observers=2"),
		Step,
		ReloadIdentityIdBefore,
		PS->GetReloadId(),
		AcceptDelta,
		RejectDelta,
		ReloadIdentityMagazineBefore,
		Weapon->GetBulletCount(),
		ReloadIdentityReserveBefore,
		Weapon->GetReserveAmmo(),
		ShotDelta,
		ProjectileDelta);
	const int32 Case = Step == 2 ? 1 : Step == 4 ? 2 : Step == 6 ? 3 : Step == LastStep ? 4 : 0;
	if (Case > 0)
	{
		UE_LOG(LogShootGame, Display, TEXT("AUTOMATION_TEST_RELOAD_IDENTITY_CASE_SUCCESS Case=%d Id=%u"),
			Case, PS->GetReloadId());
	}
	if (Step == LastStep)
	{
		for (const TWeakObjectPtr<AShooterNetworkTestCoordinator>& Participant : ReloadIdentityParticipants)
		{
			if (Participant.IsValid())
			{
				Participant->bReloadIdentityFinished = true;
			}
		}
		UE_LOG(LogShootGame, Display, TEXT("AUTOMATION_TEST_RELOAD_IDENTITY_SUCCESS Cases=4 Players=3 Observers=2"));
		return;
	}
	StartReloadIdentityStep(Step + 1);
}

void AShooterNetworkTestCoordinator::CleanupReloadIdentityTest()
{
	if (UAbilitySystemComponent* ASC = ReloadIdentityObservedASC.Get())
	{
		ASC->AbilityActivatedCallbacks.Remove(ReloadIdentityActivatedHandle);
		ASC->AbilityFailedCallbacks.Remove(ReloadIdentityFailedHandle);
		ASC->AbilityEndedCallbacks.Remove(ReloadIdentityEndedHandle);
	}
	ReloadIdentityObservedASC.Reset();
	if (USkeletalMeshComponent* Mesh = ReloadIdentityTickMesh.Get())
	{
		Mesh->VisibilityBasedAnimTickOption = static_cast<EVisibilityBasedAnimTickOption>(ReloadIdentityPreviousTickOption);
		Mesh->bEnableUpdateRateOptimizations = bReloadIdentityPreviousUpdateOptimization;
		Mesh->SetComponentTickEnabled(bReloadIdentityPreviousMeshTick);
	}
	ReloadIdentityTickMesh.Reset();
}

#else

void AShooterNetworkTestCoordinator::ClientPrepareReloadIdentityStep_Implementation(int32 SubjectPlayerId, int32 Step)
{
}

void AShooterNetworkTestCoordinator::ClientSubmitReloadIdentityInput_Implementation(int32 Step, bool bFire)
{
}

void AShooterNetworkTestCoordinator::ServerReportReloadIdentitySample_Implementation(const FShooterReloadIdentityObservation& Observation)
{
}

void AShooterNetworkTestCoordinator::RunReloadIdentityServerPhase()
{
}

void AShooterNetworkTestCoordinator::SampleReloadIdentityLocalState()
{
}

void AShooterNetworkTestCoordinator::CleanupReloadIdentityTest()
{
}

#endif
