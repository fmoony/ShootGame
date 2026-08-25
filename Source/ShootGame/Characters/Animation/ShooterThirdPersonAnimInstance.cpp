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

FTransform UShooterThirdPersonAnimInstance::ComputeLeftHandGripInRightHandSpace(
	const FTransform& InRightHandWorld,
	const FTransform& InLeftHandGripWorld)
{
	// Identity 是合法 Transform，但在这里只代表“缺失数据”或“尚未初始化”，
	// 不能作为右手与左手握把的真实相对关系继续参与 IK。
	if (!InRightHandWorld.IsValid() || !InLeftHandGripWorld.IsValid() ||
		InRightHandWorld.Equals(FTransform::Identity) || InLeftHandGripWorld.Equals(FTransform::Identity))
	{
		return FTransform::Identity;
	}

	// 握把相对 hand_r 的刚性 Transform（hand_r 坐标系中的握把位置/旋转）。
	return InLeftHandGripWorld.GetRelativeTransform(InRightHandWorld);
}

FTransform UShooterThirdPersonAnimInstance::ComputeHandGripInLeftHandSpace(
	const FTransform& InLeftHandWorld,
	const FTransform& InHandGripWorld)
{
	if (!InLeftHandWorld.IsValid() || !InHandGripWorld.IsValid() ||
		InLeftHandWorld.Equals(FTransform::Identity) || InHandGripWorld.Equals(FTransform::Identity))
	{
		return FTransform::Identity;
	}

	return InHandGripWorld.GetRelativeTransform(InLeftHandWorld);
}

bool UShooterThirdPersonAnimInstance::IsLeftHandIKEnabledForState(
	bool bHasCharacter,
	bool bHasThirdPersonMesh,
	bool bHasCurrentWeapon,
	bool bWeaponThirdPersonMeshAttached,
	bool bHasRightHandBone,
	bool bHasLeftHandBone,
	bool bHasHandGripSocket,
	bool bHasThirdPersonLeftHandGripSocket,
	const FTransform& LeftHandGripInRightHandSpace,
	const FTransform& HandGripInLeftHandSpace)
{
	// 左手 IK 与 Aim IK 相互独立，但必须同时具备角色手掌参考帧和武器握把参考帧。
	if (!bHasCharacter || !bHasThirdPersonMesh || !bHasCurrentWeapon ||
		!bWeaponThirdPersonMeshAttached || !bHasRightHandBone ||
		!bHasLeftHandBone || !bHasHandGripSocket ||
		!bHasThirdPersonLeftHandGripSocket)
	{
		return false;
	}
	if (!LeftHandGripInRightHandSpace.IsValid() ||
		LeftHandGripInRightHandSpace.Equals(FTransform::Identity) ||
		!HandGripInLeftHandSpace.IsValid() ||
		HandGripInLeftHandSpace.Equals(FTransform::Identity))
	{
		return false;
	}
	return true;
}

bool UShooterThirdPersonAnimInstance::ShouldRefreshLeftHandGripCache(
	bool bCacheDirty,
	bool bWeaponChanged,
	bool bAttachStateChanged,
	bool bCacheInvalid)
{
	// 只在这些状态事件或“数据应该存在但缓存仍无效”时重建；
	// 已确认缺失 Socket 的稳定 Identity 状态不得触发逐帧重建。
	return bCacheDirty || bWeaponChanged || bAttachStateChanged || bCacheInvalid;
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
	const FVector& ViewWorldLocation,
	const FVector& MuzzleWorldLocation,
	const FVector& SmoothedPresentationTarget,
	bool bHasThirdPersonMuzzle,
	float MinimumTargetDistanceFromView)
{
	const FVector StableBaseDirection =
		FShooterAimIKMath::IsFinite(LocalAimDirection) && !LocalAimDirection.IsNearlyZero()
			? LocalAimDirection.GetSafeNormal()
			: FVector::ZeroVector;

	// 本地拥有者：即时基础瞄准方向，永远不消费远端表现目标。
	if (bLocallyControlled)
	{
		return StableBaseDirection;
	}

	// SimulatedProxy 与 Listen Server 观察远端 Pawn：都使用同一份平滑表现目标。
	// 近点或枪口后方目标会放大相机与枪口的视差，因此只把“姿势目标”沿基础视线向前投影到安全平面；
	// 横向偏移仍保留，使枪口继续向相机瞄准射线汇聚，而不是突然退化为完全平行的基础方向。
	if (bShouldRunPresentationSmoothing && bPresentationTargetValid && bHasThirdPersonMuzzle)
	{
		if (!FShooterAimIKMath::IsFinite(ViewWorldLocation) ||
			!FShooterAimIKMath::IsFinite(MuzzleWorldLocation) ||
			!FShooterAimIKMath::IsFinite(SmoothedPresentationTarget))
		{
			return StableBaseDirection;
		}

		if (!StableBaseDirection.IsNearlyZero())
		{
			const FVector ViewToTarget = SmoothedPresentationTarget - ViewWorldLocation;
			const float TargetDepthOnViewRay = FVector::DotProduct(ViewToTarget, StableBaseDirection);
			const float SafeMinimumTargetDepth = FMath::Max(
				0.0f,
				MinimumTargetDistanceFromView);

			if (TargetDepthOnViewRay < SafeMinimumTargetDepth)
			{
				const FVector SafePresentationTarget =
					SmoothedPresentationTarget +
					StableBaseDirection * (SafeMinimumTargetDepth - TargetDepthOnViewRay);
				return ComputeMuzzleToTargetDirection(MuzzleWorldLocation, SafePresentationTarget);
			}
		}

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
		LeftHandGripInRightHandSpace = FTransform::Identity;
		HandGripInLeftHandSpace = FTransform::Identity;

		CachedWeapon = nullptr;
		CachedLeftHandGripWeapon = nullptr;
		bCachedLeftHandGripThirdPersonMeshAttached = false;
		bLeftHandGripCacheDirty = true;
		bCachedThirdPersonHandSocketExists = false;
		bCachedLeftHandBoneExists = false;
		bCachedHandGripSocketExists = false;

		bCachedWeaponThirdPersonMeshAttached = false;
		bAimIKEnabled = false;
		bLeftHandIKEnabled = false;

		return;
	}

	AShooterWeapon* Weapon = Character->GetCurrentWeapon();
	const bool bHasThirdPersonMuzzle = Weapon != nullptr && Weapon->HasThirdPersonMuzzleSocket();
	const FTransform MuzzleWorld = bHasThirdPersonMuzzle
		? Weapon->GetThirdPersonMuzzleWorldTransform()
		: FTransform::Identity;

	// 左手握把只依赖武器自身配置和真实 Socket 存在性；Pistol 未配置时这里为 false。
	const bool bHasThirdPersonLeftHandGrip = Weapon != nullptr && Weapon->HasThirdPersonLeftHandGripSocket();


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

	// LeftHandGripInRightHandSpace 缓存刷新条件：
	//  1. 缓存尚未建立（首次进入 / 无角色复位）；
	//  2. 武器引用变化（切枪，Rifle 与 Pistol 握把配置彼此独立）；
	//  3. 同一武器的第三人称 Mesh 附着状态变化（首次附着 / 重新挂接）；
	//  4. 数据应该存在但缓存仍是 Identity（上一轮未附着或握把尚未就绪）。
	// 已确认缺失握把 Socket 的稳定 Identity 状态不会触发逐帧重建，也不追逐世界点。
	const bool bLeftHandGripWeaponChanged = Weapon != CachedLeftHandGripWeapon;
	const bool bLeftHandGripAttachStateChanged =
		Weapon != nullptr &&
		Weapon == CachedLeftHandGripWeapon &&
		bThirdPersonMeshAttached != bCachedLeftHandGripThirdPersonMeshAttached;
	const bool bLeftHandGripCacheInvalid =
		Weapon != nullptr &&
		bThirdPersonMeshAttached &&
		bHasThirdPersonLeftHandGrip &&
		bCachedThirdPersonHandSocketExists &&
		bCachedLeftHandBoneExists &&
		bCachedHandGripSocketExists &&
		(!LeftHandGripInRightHandSpace.IsValid() ||
			LeftHandGripInRightHandSpace.Equals(FTransform::Identity) ||
			!HandGripInLeftHandSpace.IsValid() ||
			HandGripInLeftHandSpace.Equals(FTransform::Identity));

	if (ShouldRefreshLeftHandGripCache(
		bLeftHandGripCacheDirty,
		bLeftHandGripWeaponChanged,
		bLeftHandGripAttachStateChanged,
		bLeftHandGripCacheInvalid))
	{
		CachedLeftHandGripWeapon = Weapon;
		bCachedLeftHandGripThirdPersonMeshAttached = bThirdPersonMeshAttached;
		bCachedThirdPersonHandSocketExists =
			Character->GetMesh() != nullptr &&
			Character->GetMesh()->DoesSocketExist(HandSocketName);
		bCachedLeftHandBoneExists =
			Character->GetMesh() != nullptr &&
			Character->GetMesh()->DoesSocketExist(LeftHandBoneName);
		bCachedHandGripSocketExists =
			Character->GetMesh() != nullptr &&
			Character->GetMesh()->DoesSocketExist(HandGripSocketName);
		LeftHandGripInRightHandSpace = FTransform::Identity;
		HandGripInLeftHandSpace = FTransform::Identity;
		bLeftHandGripCacheDirty = false;

		if (Weapon && bThirdPersonMeshAttached &&
			bCachedThirdPersonHandSocketExists &&
			bCachedLeftHandBoneExists &&
			bCachedHandGripSocketExists &&
			bHasThirdPersonLeftHandGrip)
		{
			const FTransform RightHandWorld = GetHandWorldTransform(Character, HandSocketName);
			const FTransform LeftHandWorld = GetHandWorldTransform(Character, LeftHandBoneName);
			const FTransform HandGripWorld = GetHandWorldTransform(Character, HandGripSocketName);
			const FTransform GripWorld = Weapon->GetThirdPersonLeftHandGripWorldTransform();
			LeftHandGripInRightHandSpace = ComputeLeftHandGripInRightHandSpace(RightHandWorld, GripWorld);
			HandGripInLeftHandSpace = ComputeHandGripInLeftHandSpace(LeftHandWorld, HandGripWorld);
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
		Character->GetPawnViewLocation(),
		MuzzleWorld.GetLocation(),
		Character->GetSmoothedPresentationAimTarget(),
		bHasThirdPersonMuzzle,
		MinimumRemoteAimTargetDistanceFromView);

	// C2.5 IK 开关：所有条件都真实有效才允许启用；无武器时 Identity 不会错误开启 IK。
	bAimIKEnabled = IsAimIKEnabledForState(
		Character != nullptr,
		Character->GetMesh() != nullptr,
		Weapon != nullptr,
		bHasThirdPersonMuzzle,
		HandToMuzzle,
		AimDirectionWorld);

	// 左手 IK 独立于 Aim IK：Rifle 配置握把 Socket 后启用，Pistol / 无武器 / 无握把时保持关闭。
	bLeftHandIKEnabled = IsLeftHandIKEnabledForState(
		Character != nullptr,
		Character->GetMesh() != nullptr,
		Weapon != nullptr,
		bThirdPersonMeshAttached,
		bCachedThirdPersonHandSocketExists,
		bCachedLeftHandBoneExists,
		bCachedHandGripSocketExists,
		bHasThirdPersonLeftHandGrip,
		LeftHandGripInRightHandSpace,
		HandGripInLeftHandSpace);

}
