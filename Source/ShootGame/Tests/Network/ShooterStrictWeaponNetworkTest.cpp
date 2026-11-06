// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/Network/ShooterNetworkTestCoordinator.h"

#include "AbilitySystem/ShooterGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/NetConnection.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState/ShooterPlayerState.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "ShootGame.h"
#include "TimerManager.h"
#include "Weapons/Data/ShooterWeaponConfigRow.h"
#include "Weapons/Data/ShooterWeaponTable.h"
#include "Weapons/Subsystems/ShooterWeaponRuntimeSubsystem.h"

namespace
{
	const FName StrictRifle(TEXT("Rifle"));
	const FName StrictPistol(TEXT("Pistol"));
	constexpr float StrictTimeout = 60.0f;

	const TCHAR* DescribeStrictStep(EShooterStrictStep Step)
	{
		switch (Step)
		{
		case EShooterStrictStep::Ready: return TEXT("Ready");
		case EShooterStrictStep::Switch: return TEXT("Switch");
		case EShooterStrictStep::Fire: return TEXT("Fire");
		case EShooterStrictStep::Reload: return TEXT("Reload");
		case EShooterStrictStep::Death: return TEXT("Death");
		}
		return TEXT("Unknown");
	}

	FShooterRemotePresentationDeltasForTest SampleStrictPresentation(const AShooterCharacter* Pawn, const AShooterWeapon* Weapon)
	{
		return {Weapon->GetRemoteConfirmedFeedbackCountForAutomationTest(),
			Pawn->GetRemoteConfirmedMontageCountForAutomationTest(),
			Weapon->GetRemoteMuzzleFeedbackCountForAutomationTest(),
			Weapon->GetRemoteSoundFeedbackCountForAutomationTest()};
	}
}

bool AShooterNetworkTestCoordinator::PrepareStrictWeapons(AShooterCharacter* Pawn)
{
	UShooterWeaponRuntimeSubsystem* Runtime = GetWorld()->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
	UShooterInventoryComponent* Inventory = Pawn->GetInventoryComponent();
	if (!Runtime || !Inventory || !Pawn->GetEquipmentComponent() || Inventory->GetWeaponCount() != 0)
	{
		return false;
	}

	for (FName WeaponId : {StrictRifle, StrictPistol})
	{
		AShooterWeapon* Weapon = Runtime->AcquireWeapon(WeaponId, Pawn, Pawn);
		if (!Weapon || Inventory->AddWeapon(Weapon) != EShooterInventoryAddResult::Added)
		{
			if (Weapon)
			{
				Runtime->ReleaseWeapon(Weapon);
			}
			return false;
		}
	}

	AShooterWeapon* Pistol = Inventory->FindWeaponByWeaponId(StrictPistol);
	Pawn->GetEquipmentComponent()->EquipWeapon(Pistol);
	return Pawn->GetCurrentWeaponActor() == Pistol;
}

void AShooterNetworkTestCoordinator::IssueStrictStep(EShooterStrictStep Step, AShooterCharacter* Pawn, AShooterWeapon* Weapon)
{
	const AShooterPlayerState* State = Pawn->GetPlayerState<AShooterPlayerState>();
	const int32 Sequence = StrictCommand.Sequence + 1;
	for (const TWeakObjectPtr<AShooterNetworkTestCoordinator>& Participant : StrictParticipants)
	{
		AShooterNetworkTestCoordinator* Coordinator = Participant.Get();
		Coordinator->StrictCommand = {Sequence, State->GetPlayerId(), Step, Pawn, Weapon,
			Weapon ? Weapon->GetBulletCount() : INDEX_NONE, State->GetReloadId()};
		Coordinator->ForceNetUpdate();
	}

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("STRICT_WEAPON_ISSUE Seq=%d Step=%s PlayerId=%d Pawn=%s Weapon=%s WeaponId=%s Time=%.3f"),
		Sequence,
		DescribeStrictStep(Step),
		State->GetPlayerId(),
		*GetNameSafe(Pawn),
		*GetNameSafe(Weapon),
		Weapon ? *Weapon->GetWeaponId().ToString() : TEXT("None"),
		GetWorld()->GetTimeSeconds());
}

void AShooterNetworkTestCoordinator::PollStrictAuthority()
{
	// 由 PlayerId 最小的已连接玩家编排，固定名单后才开始发送指令。
	if (StrictParticipants.IsEmpty())
	{
		TArray<AShooterNetworkTestCoordinator*> Participants;
		for (TActorIterator<AShooterNetworkTestCoordinator> It(GetWorld()); It; ++It)
		{
			if (It->GetTestPlayerIdForTest() != INDEX_NONE && It->GetShooterCharacter())
			{
				Participants.Add(*It);
			}
		}

		Participants.Sort([](const AShooterNetworkTestCoordinator& A, const AShooterNetworkTestCoordinator& B)
		{
			return A.GetTestPlayerIdForTest() < B.GetTestPlayerIdForTest();
		});

		if (Participants.IsEmpty() || Participants[0] != this)
		{
			return;
		}
		if (Participants.Num() != StrictParticipantCount)
		{
			if (GetWorld()->GetTimeSeconds() - TestStartTime >= StrictTimeout)
			{
				FailTest(TEXT("Strict weapon roster did not reach the declared size"));
			}
			return;
		}
		for (AShooterNetworkTestCoordinator* Participant : Participants)
		{
			StrictParticipants.Add(Participant);
			const APlayerController* Controller = CastChecked<APlayerController>(Participant->GetOwner());
			UE_LOG(
				LogShootGame,
				Display,
				TEXT("STRICT_WEAPON_ROSTER %s Local=%d Connection=%s"),
				*Participant->DescribeTestIdentityForTest(),
				Controller->IsLocalController(),
				*GetNameSafe(Controller->GetNetConnection()));
		}
		StrictScenarioStart = GetWorld()->GetTimeSeconds();
	}

	if (GetWorld()->GetTimeSeconds() - StrictScenarioStart >= StrictTimeout)
	{
		FString Pending;
		for (const auto& Participant : StrictParticipants)
		{
			if (Participant.IsValid() && Participant->StrictAcknowledgedSequence != StrictCommand.Sequence)
			{
				Pending += FString::Printf(TEXT(" %d"), Participant->GetTestPlayerIdForTest());
			}
		}
		FailTest(FString::Printf(TEXT("Strict weapon timeout Seq=%d Step=%s Subject=%d Pending=[%s]"),
			StrictCommand.Sequence, DescribeStrictStep(StrictCommand.Step), StrictCommand.PlayerId, *Pending));
		return;
	}

	if (StrictParticipants.ContainsByPredicate([](const auto& Participant) { return !Participant.IsValid(); }))
	{
		FailTest(TEXT("Strict weapon roster changed during the scenario"));
		return;
	}

	const bool bAllAcknowledged = !StrictParticipants.ContainsByPredicate([this](const auto& Participant)
	{
		return Participant->StrictAcknowledgedSequence != StrictCommand.Sequence;
	});
	if (!bAllAcknowledged)
	{
		return;
	}

	AShooterNetworkTestCoordinator* Subject = StrictParticipants[StrictParticipantIndex].Get();
	AShooterCharacter* Pawn = Subject->GetShooterCharacter();
	if (bStrictAwaitingReady)
	{
		if (!Pawn || (bStrictAfterRespawn && (Pawn == Subject->StrictRetiredPawn.Get() || Pawn->IsDead())))
		{
			return;
		}
		if (!PrepareStrictWeapons(Pawn))
		{
			FailTest(TEXT("Strict weapon preparation failed on the current pawn"));
			return;
		}
		bStrictAwaitingReady = false;
		IssueStrictStep(EShooterStrictStep::Ready, Pawn, Pawn->GetCurrentWeaponActor());
		return;
	}

	// 全员确认死亡后开始下一人的轮次，重生计时照常运行。
	if (StrictCommand.Step == EShooterStrictStep::Death)
	{
		AdvanceStrictSubject();
		return;
	}

	AShooterWeapon* Weapon = Pawn ? Pawn->GetCurrentWeaponActor() : nullptr;
	if (!Pawn || Pawn != StrictCommand.Pawn || Pawn->IsDead() || !Weapon)
	{
		FailTest(TEXT("Strict weapon subject changed before the death step"));
		return;
	}
	if (StrictCommand.Step == EShooterStrictStep::Reload)
	{
		const AShooterPlayerState* State = Pawn->GetPlayerState<AShooterPlayerState>();
		if (!Weapon || Weapon != StrictCommand.Weapon || !State || State->GetReloadId() == StrictCommand.ReloadId ||
			Weapon->GetBulletCount() != StrictCommand.Magazine + 1 || Weapon->GetReserveAmmo() != StrictReserveBefore - 1 ||
			Weapon->GetAuthorityShotCountForAutomationTest() != StrictAuthorityShotsBefore + 1)
		{
			FailTest(TEXT("Strict reload did not complete on the same authoritative weapon"));
			return;
		}
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("STRICT_WEAPON_RELOAD_COMMIT Seq=%d PlayerId=%d Weapon=%s Mag=%d Reserve=%d"),
			StrictCommand.Sequence,
			StrictCommand.PlayerId,
			*GetNameSafe(Weapon),
			Weapon->GetBulletCount(),
			Weapon->GetReserveAmmo());
	}
	if (StrictCommand.Step == EShooterStrictStep::Reload && StrictRound == 3)
	{
		AdvanceStrictSubject();
		return;
	}

	switch (StrictCommand.Step)
	{
	case EShooterStrictStep::Ready:
	case EShooterStrictStep::Reload:
	{
		if (StrictRound == 2 && StrictCommand.Step == EShooterStrictStep::Reload)
		{
			Subject->StrictRetiredPawn = Pawn;
			IssueStrictStep(EShooterStrictStep::Death, Pawn, nullptr);
			UGameplayStatics::ApplyDamage(Pawn, Pawn->GetHealthAttributeValue() * 2.0f, nullptr, this, nullptr);
			return;
		}

		++StrictRound;
		const FName NextId = Weapon->GetWeaponId() == StrictPistol ? StrictRifle : StrictPistol;
		IssueStrictStep(EShooterStrictStep::Switch, Pawn, Pawn->GetInventoryComponent()->FindWeaponByWeaponId(NextId));
		return;
	}
	case EShooterStrictStep::Switch:
		if (!Weapon || Weapon != StrictCommand.Weapon ||
			Pawn->GetInventoryComponent()->FindWeaponByWeaponId(Weapon->GetWeaponId()) != Weapon)
		{
			FailTest(TEXT("Strict switch acknowledgement disagreed with the authority equipment or inventory"));
			return;
		}
		UE_LOG(LogShootGame, Display, TEXT("STRICT_WEAPON_SWITCH_COMMIT Seq=%d PlayerId=%d Pawn=%s Weapon=%s"),
			StrictCommand.Sequence, StrictCommand.PlayerId, *GetNameSafe(Pawn), *GetNameSafe(Weapon));
		StrictAuthorityShotsBefore = Weapon->GetAuthorityShotCountForAutomationTest();
		StrictProjectilesBefore = Subject->ProjectileSpawnCount;
		IssueStrictStep(EShooterStrictStep::Fire, Pawn, Weapon);
		return;
	case EShooterStrictStep::Fire:
		if (Weapon != StrictCommand.Weapon ||
			Weapon->GetAuthorityShotCountForAutomationTest() != StrictAuthorityShotsBefore + 1 ||
			Subject->ProjectileSpawnCount != StrictProjectilesBefore + 1 ||
			Weapon->GetBulletCount() != StrictCommand.Magazine - 1)
		{
			FailTest(TEXT("Strict fire did not produce exactly one authoritative shot, projectile and ammo debit"));
			return;
		}
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("STRICT_WEAPON_FIRE_COMMIT Seq=%d PlayerId=%d Shot=%d Projectiles=%d"),
			StrictCommand.Sequence,
			StrictCommand.PlayerId,
			Weapon->GetAuthorityShotCountForAutomationTest(),
			Subject->ProjectileSpawnCount - StrictProjectilesBefore);
		StrictReserveBefore = Weapon->GetReserveAmmo();
		IssueStrictStep(EShooterStrictStep::Reload, Pawn, Weapon);
		return;
	case EShooterStrictStep::Death:
		return;
	}
}

void AShooterNetworkTestCoordinator::AdvanceStrictSubject()
{
	++StrictParticipantIndex;
	if (StrictParticipantIndex == StrictParticipants.Num())
	{
		if (bStrictAfterRespawn)
		{
			for (const auto& Participant : StrictParticipants)
			{
				UE_LOG(
					LogShootGame,
					Display,
					TEXT("AUTOMATION_TEST_STRICT_WEAPON_SUCCESS PlayerId=%d Rounds=3 Respawn=1"),
					Participant->GetTestPlayerIdForTest());
			}
			GetWorldTimerManager().ClearTimer(PollTimer);
			return;
		}
		StrictParticipantIndex = 0;
		bStrictAfterRespawn = true;
	}
	StrictRound = bStrictAfterRespawn ? 2 : 0;
	bStrictAwaitingReady = true;
}

void AShooterNetworkTestCoordinator::ClearStrictMontageObserver()
{
	if (UAnimInstance* Anim = StrictAnimInstance.Get())
	{
		Anim->OnMontageEnded.RemoveDynamic(this, &AShooterNetworkTestCoordinator::OnStrictMontageEnded);
	}
	StrictAnimInstance.Reset();
	StrictFireMontage.Reset();
	StrictMontageEnds = 0;
}

void AShooterNetworkTestCoordinator::OnStrictMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage == StrictFireMontage.Get() && !bInterrupted)
	{
		++StrictMontageEnds;
	}
}

bool AShooterNetworkTestCoordinator::CaptureStrictFireBaseline(AShooterCharacter* Pawn, AShooterWeapon* Weapon)
{
	const FShooterWeaponConfigRow* Row = ShooterWeaponTable::FindWeaponRow(
		ShooterWeaponTable::ResolveWeaponTable(), Weapon->GetWeaponId());
	UAnimInstance* Anim = Pawn->GetMesh()->GetAnimInstance();
	if (!Anim || !Row || !Row->FiringMontage)
	{
		return false;
	}

	ClearStrictMontageObserver();
	StrictAnimInstance = Anim;
	StrictFireMontage = Row->FiringMontage;
	Anim->OnMontageEnded.AddDynamic(this, &AShooterNetworkTestCoordinator::OnStrictMontageEnded);
	StrictRemoteBaseline = SampleStrictPresentation(Pawn, Weapon);
	StrictOwnerFeedbackBefore = Weapon->GetPredictedOwnerFeedbackCountForAutomationTest();
	StrictOwnerConfirmationBefore = Weapon->GetOwnerAuthorityConfirmationCountForAutomationTest();
	return true;
}

void AShooterNetworkTestCoordinator::PollStrictLocal()
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	AShooterCharacter* Pawn = StrictCommand.Pawn.Get();
	if (!Controller || !Controller->IsLocalController() || !Pawn || StrictCommand.Sequence == 0 ||
		StrictCommand.Sequence == StrictReportedSequence)
	{
		return;
	}

	const AShooterPlayerState* State = Pawn->GetPlayerState<AShooterPlayerState>();
	const bool bSubject = GetTestPlayerIdForTest() == StrictCommand.PlayerId;
	const ENetRole ExpectedRole = HasAuthority() ? ROLE_Authority
		: (bSubject ? ROLE_AutonomousProxy : ROLE_SimulatedProxy);
	if (!State || State->GetPlayerId() != StrictCommand.PlayerId || Pawn->GetLocalRole() != ExpectedRole)
	{
		return;
	}

	AShooterWeapon* Weapon = StrictCommand.Weapon.Get();
	const EShooterStrictStep Step = StrictCommand.Step;
	const bool bWeaponObserved = Weapon && Pawn->GetCurrentWeaponActor() == Weapon && Weapon->GetOwner() == Pawn;
	if (Step == EShooterStrictStep::Death)
	{
		if (!Pawn->IsDead())
		{
			return;
		}
		ClearStrictMontageObserver();
	}
	else
	{
		if (!Weapon)
		{
			return;
		}

		if (bSubject && Controller->GetPawn() != Pawn)
		{
			return;
		}
		if (bSubject && Step != EShooterStrictStep::Ready && StrictInputSequence != StrictCommand.Sequence)
		{
			if (Step == EShooterStrictStep::Switch && State->GetEquipAbilitySpecCount() == 0)
			{
				return;
			}
			if (Step == EShooterStrictStep::Fire)
			{
				// 公共装备复制可先于拥有端装备 Tag 清除。正向单发等真实本地就绪后只提交一次。
				const UAbilitySystemComponent* ASC = Pawn->GetAbilitySystemComponent();
				const bool bActionBlocked = !ASC || ASC->HasMatchingGameplayTag(ShooterGameplayTags::State_Equipping) ||
					ASC->HasMatchingGameplayTag(ShooterGameplayTags::State_Reloading) ||
					ASC->HasMatchingGameplayTag(ShooterGameplayTags::State_Dead);
				const bool bPoseReady = Pawn->IsRifleReadyForFire();
				if (!bWeaponObserved || Weapon->IsHidden() || bActionBlocked || !bPoseReady ||
					!ResolveOwnerFireSpecForSingleShot(State, Weapon))
				{
					if (StrictWaitLoggedSequence != StrictCommand.Sequence)
					{
						StrictWaitLoggedSequence = StrictCommand.Sequence;
						UE_LOG(
						LogShootGame,
						Display,
						TEXT("STRICT_FIRE_WAIT Seq=%d PlayerId=%d Blocked=%d PoseReady=%d"),
							StrictCommand.Sequence,
						StrictCommand.PlayerId,
						bActionBlocked,
						bPoseReady);
					}
					return;
				}
			}
			if (Step == EShooterStrictStep::Reload && (!bWeaponObserved || State->GetReloadAbilitySpecCount() == 0))
			{
				return;
			}

			StrictInputSequence = StrictCommand.Sequence;
			UE_LOG(
				LogShootGame,
				Display,
				TEXT("STRICT_WEAPON_INPUT Seq=%d Step=%s PlayerId=%d Pawn=%s Weapon=%s Time=%.3f"),
				StrictCommand.Sequence,
				DescribeStrictStep(Step),
				StrictCommand.PlayerId,
				*GetNameSafe(Pawn),
				*GetNameSafe(Weapon),
				GetWorld()->GetTimeSeconds());

			switch (Step)
			{
			case EShooterStrictStep::Switch:
				Pawn->DoSwitchWeapon();
				break;
			case EShooterStrictStep::Fire:
				Controller->SetControlRotation(FRotator(60.0f, 0.0f, 0.0f));
				Pawn->DoStartFiring();
				Pawn->DoStopFiring();
				break;
			case EShooterStrictStep::Reload:
				Pawn->DoReload();
				break;
			default:
				break;
			}
		}

		if (!bWeaponObserved)
		{
			return;
		}

		switch (Step)
		{
		case EShooterStrictStep::Switch:
			if (!CaptureStrictFireBaseline(Pawn, Weapon))
			{
				return;
			}
			break;
		case EShooterStrictStep::Fire:
		{
			const FShooterRemotePresentationDeltasForTest Sample = SampleStrictPresentation(Pawn, Weapon);
			const int32 Confirmed = Sample.Confirmed - StrictRemoteBaseline.Confirmed;
			const int32 Montage = Sample.Montage - StrictRemoteBaseline.Montage;
			const int32 Muzzle = Sample.Muzzle - StrictRemoteBaseline.Muzzle;
			const int32 Sound = Sample.Sound - StrictRemoteBaseline.Sound;
			const int32 OwnerFeedback = Weapon->GetPredictedOwnerFeedbackCountForAutomationTest()
				- StrictOwnerFeedbackBefore;
			const int32 OwnerConfirmation = Weapon->GetOwnerAuthorityConfirmationCountForAutomationTest()
				- StrictOwnerConfirmationBefore;
			const bool bValid = bSubject
				? OwnerFeedback == 1 && OwnerConfirmation == 1 && Montage == 0 && Confirmed == 0 &&
					Weapon->GetOwnerConfirmedReplayCountForAutomationTest() == 0 &&
					Weapon->GetLastOwnerFeedbackSequenceForAutomationTest() < Weapon->GetLastOwnerConfirmationSequenceForAutomationTest() &&
					Weapon->GetBulletCount() == StrictCommand.Magazine - 1
				: Confirmed == 1 && Montage == 1 && Muzzle == 1 && Sound == 1 && StrictMontageEnds == 1;
			if (!bValid)
			{
				return;
			}
			UE_LOG(LogShootGame, Display, TEXT(
					"STRICT_WEAPON_FIRE Seq=%d Observer=%d Target=%d Confirmed=%d Montage=%d End=%d Muzzle=%d "
					"Sound=%d Owner=%d/%d"),
				StrictCommand.Sequence, GetTestPlayerIdForTest(), StrictCommand.PlayerId, Confirmed, Montage,
				StrictMontageEnds, Muzzle, Sound, OwnerFeedback, OwnerConfirmation);
			ClearStrictMontageObserver();
			break;
		}
		case EShooterStrictStep::Reload:
			if (State->GetReloadId() == StrictCommand.ReloadId || (bSubject && Weapon->GetBulletCount() != StrictCommand.Magazine + 1))
			{
				return;
			}
			break;
		default:
			break;
		}
	}

	StrictReportedSequence = StrictCommand.Sequence;
	UE_LOG(LogShootGame, Display, TEXT(
			"STRICT_WEAPON_OBSERVED Seq=%d Step=%s Observer=%d Target=%d Pawn=%s Weapon=%s LocalRole=%d "
			"RemoteRole=%d NetMode=%d"),
		StrictCommand.Sequence,
		DescribeStrictStep(Step),
		GetTestPlayerIdForTest(),
		State->GetPlayerId(),
		*GetNameSafe(Pawn),
		*GetNameSafe(Weapon),
		Pawn->GetLocalRole(),
		Pawn->GetRemoteRole(),
		GetNetMode());
	ServerReportStrictStep(StrictCommand.Sequence, Pawn, Weapon);
}

void AShooterNetworkTestCoordinator::ServerReportStrictStep_Implementation(int32 Sequence,
	AShooterCharacter* ObservedPawn, AShooterWeapon* ObservedWeapon)
{
	if (Sequence != StrictCommand.Sequence || ObservedPawn != StrictCommand.Pawn ||
		ObservedWeapon != StrictCommand.Weapon || Sequence <= StrictAcknowledgedSequence)
	{
		return;
	}

	StrictAcknowledgedSequence = Sequence;
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("STRICT_WEAPON_ACK Seq=%d Step=%s Observer=%d Target=%d Weapon=%s Time=%.3f"),
		Sequence,
		DescribeStrictStep(StrictCommand.Step),
		GetTestPlayerIdForTest(),
		StrictCommand.PlayerId,
		*GetNameSafe(ObservedWeapon),
		GetWorld()->GetTimeSeconds());
}
