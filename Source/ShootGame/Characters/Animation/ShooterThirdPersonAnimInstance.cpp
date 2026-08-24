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
	// Identity 是合法 Transform，但在这里只代表“缺失数据”或“尚未初始化”，
	// 不能作为 Hand 与 Muzzle 的真实相对关系继续参与 IK。
	if (!InHandWorld.IsValid() || !InMuzzleWorld.IsValid() ||
		InHandWorld.Equals(FTransform::Identity) || InMuzzleWorld.Equals(FTransform::Identity))
	{
		return FTransform::Identity;
	}
	// HandToMuzzle = Muzzle 相对 Hand 的刚性 Transform（Hand 坐标系中的 Muzzle）。
	return InMuzzleWorld.GetRelativeTransform(InHandWorld);
}

FVector UShooterThirdPersonAnimInstance::ComputeMuzzleToTargetDirection(
	const FVector& MuzzleWorldLocation,
	const FVector& TargetWorld)
{
	if (!FShooterAimIKMath::IsFinite(MuzzleWorldLocation) || !FShooterAimIKMath::IsFinite(TargetWorld))
	{
		return FVector::ZeroVector;
	}

	const FVector Delta = TargetWorld - MuzzleWorldLocation;
	if (!FShooterAimIKMath::IsFinite(Delta) || Delta.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}
	return Delta.GetSafeNormal();
}

FVector UShooterThirdPersonAnimInstance::ComputeAimDirectionWorldForState(
	bool bLocallyControlled,
	bool bShouldRunPresentationSmoothing,
	bool bPresentationTargetValid,
	const FVector& LocalAimDirection,
	const FVector& MuzzleWorldLocation,
	const FVector& SmoothedPresentationTarget,
	bool bHasThirdPersonMuzzle)
{
	// 本地拥有者：即时基础瞄准方向，永远不消费远端表现目标。
	if (bLocallyControlled)
	{
		return FShooterAimIKMath::IsFinite(LocalAimDirection) && !LocalAimDirection.IsNearlyZero()
			? LocalAimDirection
			: FVector::ZeroVector;
	}

	// SimulatedProxy 与 Listen Server 观察远端 Pawn：都使用同一份平滑表现目标；
	// 方向必须从 ThirdPerson Muzzle 指向该目标，不能用 ActorForward / ViewLocation 代替。
	if (bShouldRunPresentationSmoothing && bPresentationTargetValid && bHasThirdPersonMuzzle)
	{
		return ComputeMuzzleToTargetDirection(MuzzleWorldLocation, SmoothedPresentationTarget);
	}

	// 无效目标 / 无武器 / 无 Muzzle / Dedicated Server：关闭 IK，不长期回退 ActorForward。
	return FVector::ZeroVector;
}

FTransform UShooterThirdPersonAnimInstance::GetHandWorldTransform(
	const AShooterCharacter* InCharacter,
	FName InHandSocketName)
{
	if (!InCharacter || !InCharacter->GetMesh() ||
		!InCharacter->GetMesh()->DoesSocketExist(InHandSocketName))
	{
		return FTransform::Identity;
	}
	return InCharacter->GetMesh()->GetSocketTransform(InHandSocketName, RTS_World);
}

bool UShooterThirdPersonAnimInstance::IsAimIKEnabledForState(
	bool bHasCharacter,
	bool bHasThirdPersonMesh,
	bool bHasCurrentWeapon,
	bool bHasThirdPersonMuzzle,
	const FTransform& HandToMuzzle,
	const FVector& AimDirectionWorld)
{
	// FTransform::Identity 本身是 Valid Transform，不能作为“有 HandToMuzzle 数据”的证据；
	// 必须同时验证角色、第三人称 Mesh、CurrentWeapon 和真实的第三人称 Muzzle socket。
	if (!bHasCharacter || !bHasThirdPersonMesh || !bHasCurrentWeapon || !bHasThirdPersonMuzzle)
	{
		return false;
	}
	if (!HandToMuzzle.IsValid() || HandToMuzzle.Equals(FTransform::Identity))
	{
		return false;
	}
	if (!FShooterAimIKMath::IsFinite(AimDirectionWorld) || AimDirectionWorld.IsNearlyZero())
	{
		return false;
	}
	return true;
}

void UShooterThirdPersonAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwningActor());
	if (!Character)
	{
		AimDirectionWorld = FVector::ZeroVector;
		HandToMuzzle = FTransform::Identity;
		CachedWeapon = nullptr;
		bCachedWeaponThirdPersonMeshAttached = false;
		bAimIKEnabled = false;
		return;
	}

	AShooterWeapon* Weapon = Character->GetCurrentWeapon();
	const bool bHasThirdPersonMuzzle = Weapon != nullptr && Weapon->HasThirdPersonMuzzleSocket();
	const FTransform MuzzleWorld = bHasThirdPersonMuzzle
		? Weapon->GetThirdPersonMuzzleWorldTransform()
		: FTransform::Identity;

	// 武器第三人称 Mesh 必须已经附着到角色 Mesh。若 CurrentWeapon 先到、Attach 后发生，
	// 本轮按“未附着”处理并把缓存置为 Identity；附着完成后状态变化会触发重建。
	const USkeletalMeshComponent* WeaponThirdPersonMesh = Weapon ? Weapon->GetThirdPersonMesh() : nullptr;
	const bool bThirdPersonMeshAttached =
		WeaponThirdPersonMesh != nullptr &&
		WeaponThirdPersonMesh->GetAttachParent() == Character->GetMesh();

	// HandToMuzzle 缓存刷新条件：
	//  1. 武器引用变化（切枪）；
	//  2. 同一武器的第三人称 Mesh 附着状态变化（首次附着 / 重新挂接）；
	//  3. 已缓存结果仍是 Identity（上一轮未附着或 Pose 尚未就绪），避免错误结果被永久缓存。
	const bool bWeaponChanged = Weapon != CachedWeapon;
	const bool bAttachStateChanged =
		Weapon != nullptr &&
		Weapon == CachedWeapon &&
		bThirdPersonMeshAttached != bCachedWeaponThirdPersonMeshAttached;
	const bool bCachedHandToMuzzleInvalid =
		CachedWeapon != nullptr &&
		(!HandToMuzzle.IsValid() || HandToMuzzle.Equals(FTransform::Identity));

	if (bWeaponChanged || bAttachStateChanged || bCachedHandToMuzzleInvalid)
	{
		CachedWeapon = Weapon;
		bCachedWeaponThirdPersonMeshAttached = bThirdPersonMeshAttached;
		HandToMuzzle = FTransform::Identity;

		if (Weapon && bThirdPersonMeshAttached && bHasThirdPersonMuzzle)
		{
			const FTransform HandWorld = GetHandWorldTransform(Character, HandSocketName);
			HandToMuzzle = ComputeHandToMuzzleTransform(HandWorld, MuzzleWorld);
		}
	}

	// C4 消费角色边界：
	//  本地拥有者 → 即时 GetBaseAimRotation().Vector()；
	//  SimulatedProxy / Listen Server 远端 → Muzzle → SmoothedPresentationAimTarget；
	//  Dedicated Server / 无效数据 → 零向量并关闭 IK。
	const bool bShouldRunPresentationSmoothing = AShooterCharacter::ShouldRunPresentationAimSmoothing(
		Character->GetLocalRole(),
		Character->GetNetMode(),
		Character->IsLocallyControlled());
	AimDirectionWorld = ComputeAimDirectionWorldForState(
		Character->IsLocallyControlled(),
		bShouldRunPresentationSmoothing,
		Character->IsPresentationAimTargetValid(),
		Character->GetBaseAimRotation().Vector(),
		MuzzleWorld.GetLocation(),
		Character->GetSmoothedPresentationAimTarget(),
		bHasThirdPersonMuzzle);

	// C2.5 IK 开关：所有条件都真实有效才允许启用；无武器时 Identity 不会错误开启 IK。
	bAimIKEnabled = IsAimIKEnabledForState(
		Character != nullptr,
		Character->GetMesh() != nullptr,
		Weapon != nullptr,
		bHasThirdPersonMuzzle,
		HandToMuzzle,
		AimDirectionWorld);
}
