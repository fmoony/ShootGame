// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/Animation/ShooterFirstPersonAnimInstance.h"

#include "Characters/ShooterCharacter.h"
#include "Weapons/ShooterWeapon.h"

void UShooterFirstPersonAnimInstance::UpdateShooterAnimationData(float DeltaSeconds)
{
	RefreshFirstPersonAnimationData();
}

void UShooterFirstPersonAnimInstance::RefreshFirstPersonAnimationData()
{
	AShooterCharacter* Character = GetCachedShooterCharacter();
	if (!Character ||
		!Character->IsLocallyControlled() ||
		!Character->GetFirstPersonMesh() ||
		Character->IsDead() ||
		!IsValid(CurrentWeaponActor) ||
		CurrentWeaponActor->GetOwner() != Character)
	{
		ClearFirstPersonAnimationData();
		return;
	}

	bFirstPersonDataValid = true;
}

void UShooterFirstPersonAnimInstance::ClearFirstPersonAnimationData()
{
	bFirstPersonDataValid = false;
}
