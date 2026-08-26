// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/Animation/ShooterAnimInstanceBase.h"

#include "AbilitySystemComponent.h"
#include "Characters/ShooterCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ShooterGameplayTags.h"
#include "Weapons/ShooterWeapon.h"

void UShooterAnimInstanceBase::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	CachedShooterCharacter = Cast<AShooterCharacter>(GetOwningActor());
	if (!CachedShooterCharacter)
	{
		ClearCommonAnimationData();
	}
}

void UShooterAnimInstanceBase::NativeUninitializeAnimation()
{
	ClearCommonAnimationData();
	CachedShooterCharacter = nullptr;

	Super::NativeUninitializeAnimation();
}

void UShooterAnimInstanceBase::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	RefreshCommonAnimationData();
	UpdateShooterAnimationData(DeltaSeconds);
}

void UShooterAnimInstanceBase::UpdateShooterAnimationData(float DeltaSeconds)
{
}

void UShooterAnimInstanceBase::RefreshCommonAnimationData()
{
	AShooterCharacter* Character = GetCachedShooterCharacter();
	if (!Character)
	{
		ClearCommonAnimationData();
		return;
	}

	// 移动基础值：只读采集，不产生权威结果。
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	Velocity = Character->GetVelocity();
	LocomotionGroundSpeed = Velocity.Size2D();
	bIsInAir = Movement != nullptr && Movement->IsFalling();

	// 当前武器来自 Equipment 唯一权威。
	CurrentWeaponActor = Character->GetCurrentWeaponActor();
	bHasEquippedWeapon =
		IsValid(CurrentWeaponActor) &&
		CurrentWeaponActor->GetOwner() == Character;

	// 表现状态只读 ASC Tag；Tag 不存在时保持 false。
	if (const UAbilitySystemComponent* AbilitySystemComponent =
		Character->GetAbilitySystemComponent())
	{
		bIsFiring = AbilitySystemComponent->HasMatchingGameplayTag(
			ShooterGameplayTags::State_Firing);
		bIsReloading = AbilitySystemComponent->HasMatchingGameplayTag(
			ShooterGameplayTags::State_Reloading);
		bIsEquipping = AbilitySystemComponent->HasMatchingGameplayTag(
			ShooterGameplayTags::State_Equipping);
		bIsDead = AbilitySystemComponent->HasMatchingGameplayTag(
			ShooterGameplayTags::State_Dead);
	}
	else
	{
		bIsFiring = false;
		bIsReloading = false;
		bIsEquipping = false;
		bIsDead = Character->IsDead();
	}
}

void UShooterAnimInstanceBase::ClearCommonAnimationData()
{
	LocomotionGroundSpeed = 0.0f;
	Velocity = FVector::ZeroVector;
	bIsInAir = false;
	bHasEquippedWeapon = false;
	bIsFiring = false;
	bIsReloading = false;
	bIsEquipping = false;
	bIsDead = false;
	CurrentWeaponActor = nullptr;
}
