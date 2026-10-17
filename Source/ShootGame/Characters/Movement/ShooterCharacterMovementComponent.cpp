// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterCharacterMovementComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/ShooterGameplayTags.h"
#include "Characters/ShooterCharacter.h"
#include "GameFramework/PlayerState/ShooterPlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Weapons/ShooterWeapon.h"

namespace
{
	class FShooterClientPredictionData : public FNetworkPredictionData_Client_Character
	{
	public:
		explicit FShooterClientPredictionData(const UCharacterMovementComponent& Movement)
			: FNetworkPredictionData_Client_Character(Movement) {}
		virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FShooterSavedMove()); }
	};
}

UShooterCharacterMovementComponent::UShooterCharacterMovementComponent()
{
	SetIsReplicatedByDefault(true);
}

bool UShooterCharacterMovementComponent::SupportsLowReady(FName WeaponId)
{
	return WeaponId == TEXT("Rifle") || WeaponId == TEXT("AWP") || WeaponId == TEXT("GrenadeLauncher");
}

bool UShooterCharacterMovementComponent::CanSprint() const
{
	const AShooterCharacter* Character = Cast<AShooterCharacter>(CharacterOwner);
	if (!Character || !Character->GetController() || Character->IsDead() || !bSprintHeld || bFireHeld ||
		bFireReadyPending || !IsMovingOnGround() || IsCrouching() || Acceleration.IsNearlyZero())
	{
		return false;
	}
	const UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	if (HasBlockingAction() || (!Character->HasAuthority() && bSprintBlockedByAction) ||
		ASC->HasMatchingGameplayTag(ShooterGameplayTags::State_Firing))
	{
		return false;
	}
	// GA_Fire 的可靠激活可能先于 ServerMove 到达；权威输入镜像也要阻止恢复 Sprint。
	const AShooterPlayerState* State = Character->GetPlayerState<AShooterPlayerState>();
	const AShooterWeapon* Weapon = Character->GetCurrentWeaponActor();
	const FGameplayAbilitySpec* Spec = State && Weapon ? State->FindFireAbilitySpecForWeapon(Weapon) : nullptr;
	return !Character->HasAuthority() || !Spec || !Spec->InputPressed;
}

bool UShooterCharacterMovementComponent::HasBlockingAction() const
{
	const AShooterCharacter* Character = Cast<AShooterCharacter>(CharacterOwner);
	const UAbilitySystemComponent* ASC = Character ? Character->GetAbilitySystemComponent() : nullptr;
	return !ASC || ASC->HasMatchingGameplayTag(ShooterGameplayTags::State_Dead) ||
		ASC->HasMatchingGameplayTag(ShooterGameplayTags::State_Reloading) ||
		ASC->HasMatchingGameplayTag(ShooterGameplayTags::State_Equipping);
}

float UShooterCharacterMovementComponent::GetMaxSpeed() const
{
	if (IsMovingOnGround() && !IsCrouching())
	{
		const bool bSprint = CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy
			? bIsSprinting : CanSprint();
		return bSprint ? FMath::Max(WalkSpeed, SprintSpeed) : WalkSpeed;
	}
	return Super::GetMaxSpeed();
}

void UShooterCharacterMovementComponent::SetSprintHeld(bool bHeld)
{
	bSprintHeld = bHeld;
	RefreshSprintState();
}

void UShooterCharacterMovementComponent::SetFireHeld(bool bHeld)
{
	bFireHeld = bHeld;
	RefreshSprintState();
}

void UShooterCharacterMovementComponent::ResetMovementIntents()
{
	bSprintHeld = false;
	bFireHeld = false;
	bFireReadyPending = false;
	RefreshSprintState();
}

void UShooterCharacterMovementComponent::SetFireReadyPending(bool bPending)
{
	bFireReadyPending = bPending;
	RefreshSprintState();
}

void UShooterCharacterMovementComponent::RefreshSprintState()
{
	if (!CharacterOwner || CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy) { return; }
	if (CharacterOwner->HasAuthority() && bSprintBlockedByAction != HasBlockingAction())
	{
		bSprintBlockedByAction = HasBlockingAction();
		CharacterOwner->ForceNetUpdate();
	}
	const bool bNewSprint = CanSprint() && Velocity.SizeSquared2D() > FMath::Square(3.0f);
	if (bIsSprinting != bNewSprint)
	{
		bIsSprinting = bNewSprint;
		if (CharacterOwner->HasAuthority()) { CharacterOwner->ForceNetUpdate(); }
	}
}

void UShooterCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
	RefreshSprintState();
}

void UShooterCharacterMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);
	RefreshSprintState();
}

void UShooterCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bSprintHeld = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	bFireHeld = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
	bFireReadyPending = (Flags & FSavedMove_Character::FLAG_Custom_2) != 0;
}

FNetworkPredictionData_Client* UShooterCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UShooterCharacterMovementComponent* MutableThis = const_cast<UShooterCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FShooterClientPredictionData(*this);
	}
	return ClientPredictionData;
}

bool UShooterCharacterMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
	// 原生只恢复 Jump / Crouch；重放后还必须恢复当前帧真实的 Shift / Fire，避免旧 Move 吞松开沿。
	const bool bCurrentSprint = bSprintHeld;
	const bool bCurrentFire = bFireHeld;
	const bool bCurrentPending = bFireReadyPending;
#if WITH_DEV_AUTOMATION_TESTS
	const bool bCorrectionPending = HasPredictionData_Client() && GetPredictionData_Client_Character()->bUpdatePosition;
	if (bCorrectionPending) { ++CorrectionCount; }
#endif
	const bool bReplayed = Super::ClientUpdatePositionAfterServerUpdate();
#if WITH_DEV_AUTOMATION_TESTS
	if (bReplayed) { ++CorrectionReplayCount; }
#endif
	bSprintHeld = bCurrentSprint;
	bFireHeld = bCurrentFire;
	bFireReadyPending = bCurrentPending;
	RefreshSprintState();
	return bReplayed;
}

void UShooterCharacterMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UShooterCharacterMovementComponent, bIsSprinting, COND_SimulatedOnly);
	DOREPLIFETIME_CONDITION(UShooterCharacterMovementComponent, bSprintBlockedByAction, COND_OwnerOnly);
}

void FShooterSavedMove::Clear()
{
	Super::Clear();
	bSprintHeld = false;
	bFireHeld = false;
	bFireReadyPending = false;
}

uint8 FShooterSavedMove::GetCompressedFlags() const
{
	return Super::GetCompressedFlags() | (bSprintHeld ? FLAG_Custom_0 : 0) |
		(bFireHeld ? FLAG_Custom_1 : 0) | (bFireReadyPending ? FLAG_Custom_2 : 0);
}

bool FShooterSavedMove::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const
{
	const FShooterSavedMove* Other = static_cast<const FShooterSavedMove*>(NewMove.Get());
	return bSprintHeld == Other->bSprintHeld && bFireHeld == Other->bFireHeld && bFireReadyPending == Other->bFireReadyPending &&
		Super::CanCombineWith(NewMove, Character, MaxDelta);
}

void FShooterSavedMove::SetMoveFor(ACharacter* Character, float DeltaSeconds, const FVector& NewAccel,
	FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(Character, DeltaSeconds, NewAccel, ClientData);
	const UShooterCharacterMovementComponent* Movement = CastChecked<UShooterCharacterMovementComponent>(Character->GetCharacterMovement());
	bSprintHeld = Movement->IsSprintHeld();
	bFireHeld = Movement->IsFireHeld();
	bFireReadyPending = Movement->IsFireReadyPending();
}

void FShooterSavedMove::PrepMoveFor(ACharacter* Character)
{
	Super::PrepMoveFor(Character);
	UShooterCharacterMovementComponent* Movement = CastChecked<UShooterCharacterMovementComponent>(Character->GetCharacterMovement());
	Movement->SetSprintHeld(bSprintHeld);
	Movement->SetFireHeld(bFireHeld);
	Movement->SetFireReadyPending(bFireReadyPending);
}
