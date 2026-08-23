// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Characters/Animation/AnimNodes/ShooterAimIKMath.h"
#include "Characters/ShooterCharacter.h"
#include "Weapons/ShooterWeapon.h"
#include "Components/SkeletalMeshComponent.h"

FTransform UShooterThirdPersonAnimInstance::ComputeHandToMuzzleTransform(
	const FTransform& InHandWorld,
	const FTransform& InMuzzleWorld)
{
	if (!InHandWorld.IsValid() || !InMuzzleWorld.IsValid())
	{
		return FTransform::Identity;
	}
	// HandToMuzzle = Muzzle 相对 Hand 的刚性 Transform（Hand 坐标系中的 Muzzle）。
	return InMuzzleWorld.GetRelativeTransform(InHandWorld);
}

FTransform UShooterThirdPersonAnimInstance::GetHandWorldTransform(
	const AShooterCharacter* InCharacter,
	FName InHandSocketName)
{
	if (!InCharacter || !InCharacter->GetMesh())
	{
		return FTransform::Identity;
	}
	return InCharacter->GetMesh()->GetSocketTransform(InHandSocketName, RTS_World);
}

void UShooterThirdPersonAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwningActor());
	if (!Character)
	{
		bAimIKEnabled = false;
		return;
	}

	// C2：本地拥有者使用即时 AimRotation（快速转向时方向立即变化，不追逐旧世界目标）。
	// 非本地角色暂回退 Actor Forward（C4 接入网络 AimDirection）。
	if (Character->IsLocallyControlled())
	{
		AimDirectionWorld = Character->GetBaseAimRotation().Vector();
	}
	else
	{
		AimDirectionWorld = Character->GetActorForwardVector();
	}

	if (!FShooterAimIKMath::IsFinite(AimDirectionWorld) || AimDirectionWorld.IsNearlyZero())
	{
		AimDirectionWorld = Character->GetActorForwardVector();
	}

	// HandToMuzzle：武器引用变化时刷新一次并缓存；同一把已附着武器不每帧重算。
	AShooterWeapon* Weapon = Character->GetCurrentWeapon();
	if (Weapon != CachedWeapon)
	{
		CachedWeapon = Weapon;
		HandToMuzzle = FTransform::Identity;
		if (Weapon)
		{
			const FTransform HandWorld = GetHandWorldTransform(Character, HandSocketName);
			const FTransform MuzzleWorld = Weapon->GetThirdPersonMuzzleWorldTransform();
			HandToMuzzle = ComputeHandToMuzzleTransform(HandWorld, MuzzleWorld);
		}
	}

	bAimIKEnabled = Character->GetMesh() != nullptr && HandToMuzzle.IsValid();
}
