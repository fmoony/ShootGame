// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/Animation/ShooterFirstPersonAnimInstance.h"

#include "Characters/ShooterCharacter.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Weapons/ShooterWeapon.h"

void UShooterFirstPersonAnimInstance::UpdateShooterAnimationData(float DeltaSeconds)
{
	RefreshFirstPersonAnimationData(DeltaSeconds);
}

void UShooterFirstPersonAnimInstance::RefreshFirstPersonAnimationData(float DeltaSeconds)
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

	const bool bWasFirstPersonDataValid = bFirstPersonDataValid;
	bFirstPersonDataValid = true;
	AimPitchN = Character->GetAimPitchN();

	// 新建或重新启用第一人称 AnimInstance 时直接建立正确基线，避免首次显示从错误姿势淡入。
	if (!bWasFirstPersonDataValid)
	{
		const float InitialAlpha = bIsReloading ? 0.0f : 1.0f;
		AimRigPresentationAlpha = InitialAlpha;
		AimRigBlendSourceAlpha = InitialAlpha;
		AimRigBlendTargetAlpha = InitialAlpha;
		AimRigBlendElapsedTime = 0.0f;
	}

	UpdateAimRigPresentationAlpha(Character, DeltaSeconds);
}

void UShooterFirstPersonAnimInstance::UpdateAimRigPresentationAlpha(
	const AShooterCharacter* Character,
	float DeltaSeconds)
{
	bool bReloadPresentationRecovering = false;
	if (Character)
	{
		const USkeletalMeshComponent* ThirdPersonMesh = Character->GetMesh();
		const UShooterThirdPersonAnimInstance* ThirdPersonAnimInstance = ThirdPersonMesh
			? Cast<UShooterThirdPersonAnimInstance>(ThirdPersonMesh->GetAnimInstance())
			: nullptr;
		bReloadPresentationRecovering =
			ThirdPersonAnimInstance && ThirdPersonAnimInstance->bReloadPresentationRecovering;
	}

	// ReloadRecovery 在权威 Reload 结束前到达，使恢复平滑发生在既有时序窗口内。
	const float TargetAlpha = bIsReloading && !bReloadPresentationRecovering ? 0.0f : 1.0f;
	if (!FMath::IsNearlyEqual(TargetAlpha, AimRigBlendTargetAlpha))
	{
		AimRigBlendSourceAlpha = AimRigPresentationAlpha;
		AimRigBlendTargetAlpha = TargetAlpha;
		AimRigBlendElapsedTime = 0.0f;
	}

	const float BlendTime = AimRigBlendTargetAlpha > AimRigBlendSourceAlpha
		? AimRigBlendInTime
		: AimRigBlendOutTime;

	if (BlendTime <= UE_KINDA_SMALL_NUMBER || DeltaSeconds <= 0.0f)
	{
		AimRigPresentationAlpha = AimRigBlendTargetAlpha;
		return;
	}

	AimRigBlendElapsedTime = FMath::Min(AimRigBlendElapsedTime + DeltaSeconds, BlendTime);
	const float LinearAlpha = AimRigBlendElapsedTime / BlendTime;
	const float EasedAlpha = FMath::SmoothStep(0.0f, 1.0f, LinearAlpha);
	AimRigPresentationAlpha = FMath::Lerp(
		AimRigBlendSourceAlpha,
		AimRigBlendTargetAlpha,
		EasedAlpha);
}

void UShooterFirstPersonAnimInstance::ClearFirstPersonAnimationData()
{
	bFirstPersonDataValid = false;
	AimRigPresentationAlpha = 0.0f;
	AimRigBlendSourceAlpha = 0.0f;
	AimRigBlendTargetAlpha = 0.0f;
	AimRigBlendElapsedTime = 0.0f;
	AimPitchN = 0.0f;
}
