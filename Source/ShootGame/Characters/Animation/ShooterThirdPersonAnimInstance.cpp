// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Characters/Animation/AnimNodes/ShooterAimIKMath.h"
#include "Characters/Aim/ShooterAimPresentationComponent.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Weapons/ShooterWeapon.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"

FTransform UShooterThirdPersonAnimInstance::ComputeHandToMuzzleTransform(
	const FTransform& InHandWorld,
	const FTransform& InMuzzleWorld)
{
	// Identity 是合法 Transform；是否可消费由 bAimIKBindingValid 决定。
	if (!InHandWorld.IsValid() || !InMuzzleWorld.IsValid())
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
	if (!InRightHandWorld.IsValid() || !InLeftHandGripWorld.IsValid())
	{
		return FTransform::Identity;
	}

	return InLeftHandGripWorld.GetRelativeTransform(InRightHandWorld);
}

FTransform UShooterThirdPersonAnimInstance::ComputeHandGripInLeftHandSpace(
	const FTransform& InLeftHandWorld,
	const FTransform& InHandGripWorld)
{
	if (!InLeftHandWorld.IsValid() || !InHandGripWorld.IsValid())
	{
		return FTransform::Identity;
	}

	return InHandGripWorld.GetRelativeTransform(InLeftHandWorld);
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
	float MinimumTargetDistanceFromView,
	float MinimumTargetDistanceFromMuzzle)
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

	// 观察端使用平滑表现目标；近点或枪口后方目标只投影到安全深度，保留横向偏移。
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
			const float MuzzleDepthOnViewRay = FVector::DotProduct(
				MuzzleWorldLocation - ViewWorldLocation,
				StableBaseDirection);
			const float SafeMinimumTargetDepth = FMath::Max3(
				0.0f,
				MinimumTargetDistanceFromView,
				MuzzleDepthOnViewRay + MinimumTargetDistanceFromMuzzle);

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

	// 无效目标 / 无武器 / 无 Muzzle / Dedicated Server：关闭 IK。
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

bool UShooterThirdPersonAnimInstance::IsMathematicallyValidBindingFrame(const FTransform& T)
{
	// FTransform::IsValid() 已覆盖 NaN/Inf 与 Rotation 归一化；Scale 各分量必须大于 0。
	return T.IsValid() &&
		T.GetScale3D().X > 0.0f &&
		T.GetScale3D().Y > 0.0f &&
		T.GetScale3D().Z > 0.0f;
}

void UShooterThirdPersonAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	AShooterCharacter* Character = GetCachedShooterCharacter();
	if (!Character)
	{
		ClearWeaponStaticBindings();
		return;
	}

	Character->OnWeaponPresentationChanged.AddDynamic(
		this,
		&UShooterThirdPersonAnimInstance::HandleWeaponPresentationChanged);

	// AnimClass 切换可能发生在事件之前：初始化后立即从 Equipment 回放一次当前状态。
	ReplayWeaponPresentationState(Character);
}

void UShooterThirdPersonAnimInstance::NativeUninitializeAnimation()
{
	AShooterCharacter* Character = GetCachedShooterCharacter();
	if (Character)
	{
		Character->OnWeaponPresentationChanged.RemoveDynamic(
			this,
			&UShooterThirdPersonAnimInstance::HandleWeaponPresentationChanged);
	}

	ClearWeaponStaticBindings();
	Super::NativeUninitializeAnimation();
}

void UShooterThirdPersonAnimInstance::HandleWeaponPresentationChanged(
	AShooterWeapon* PreviousWeapon,
	AShooterWeapon* CurrentWeapon)
{
	// 表现事件已由 Character 验证完成后才发布；这里只消费 (Previous, Current) 上下文。
	if (IsValid(CurrentWeapon) && CurrentWeapon->GetOwner() == GetCachedShooterCharacter())
	{
		CachedPresentationWeapon = CurrentWeapon;
		RebuildWeaponStaticBindings(CurrentWeapon);
	}
	else
	{
		// Unequip / 失效：清空静态 Binding 与 IK 开关，保留当前 AnimClass。
		ClearWeaponStaticBindings();
	}
}

void UShooterThirdPersonAnimInstance::ReplayWeaponPresentationState(AShooterCharacter* Character)
{
	if (!Character)
	{
		ClearWeaponStaticBindings();
		return;
	}

	AShooterWeapon* LogicalWeapon = Character->GetCurrentWeaponActor();
	if (IsValid(LogicalWeapon) && LogicalWeapon->GetOwner() == Character)
	{
		CachedPresentationWeapon = LogicalWeapon;
		RebuildWeaponStaticBindings(LogicalWeapon);
	}
	else
	{
		ClearWeaponStaticBindings();
	}
}

void UShooterThirdPersonAnimInstance::RebuildWeaponStaticBindings(AShooterWeapon* Weapon)
{
	// 先清空上一把武器的全部缓存输出，避免重建失败时残留旧 Transform。
	bAimIKBindingValid = false;
	bLeftHandIKBindingValid = false;
	HandToMuzzle = FTransform::Identity;
	LeftHandGripInRightHandSpace = FTransform::Identity;
	HandGripInLeftHandSpace = FTransform::Identity;
	bAimIKEnabled = false;
	bLeftHandIKEnabled = false;
	AimDirectionWorld = FVector::ZeroVector;
	AimTargetWorld = FVector::ZeroVector;
	bAimTargetWorldValid = false;

	AShooterCharacter* Character = GetCachedShooterCharacter();
	USkeletalMeshComponent* CharacterMesh = Character ? Character->GetMesh() : nullptr;
	if (!Character || !IsValid(CharacterMesh) || !CharacterMesh->GetSkeletalMeshAsset() ||
		!IsValid(Weapon) || Weapon->IsActorBeingDestroyed() || Weapon->GetOwner() != Character)
	{
		return;
	}

	USkeletalMeshComponent* WeaponMesh = Weapon->GetThirdPersonMesh();
	if (!WeaponMesh || !WeaponMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	// Character 表现完成事件保证 Attach 已完成；这里不再逐帧等待 Attach。
	if (WeaponMesh->GetAttachParent() != CharacterMesh)
	{
		return;
	}

	const FName AttachSocketName = WeaponMesh->GetAttachSocketName();
	if (AttachSocketName == NAME_None || !CharacterMesh->DoesSocketExist(AttachSocketName))
	{
		return;
	}

	bool bHasTransientMathFailure = false;

	// ---- Aim IK 静态 Binding：永久缺失 Socket 只关闭 Aim IK，不影响 LeftHand。 ----
	if (CharacterMesh->DoesSocketExist(HandSocketName) && Weapon->HasThirdPersonMuzzleSocket())
	{
		const FTransform HandWorld = CharacterMesh->GetSocketTransform(HandSocketName, RTS_World);
		const FTransform MuzzleWorld = Weapon->GetThirdPersonMuzzleWorldTransform();
		if (IsMathematicallyValidBindingFrame(HandWorld) && IsMathematicallyValidBindingFrame(MuzzleWorld))
		{
			const FTransform ComputedHandToMuzzle = ComputeHandToMuzzleTransform(HandWorld, MuzzleWorld);
			if (IsMathematicallyValidBindingFrame(ComputedHandToMuzzle))
			{
				HandToMuzzle = ComputedHandToMuzzle;
				bAimIKBindingValid = true;
			}
			else
			{
				bHasTransientMathFailure = true;
			}
		}
		else
		{
			bHasTransientMathFailure = true;
		}
	}

	// ---- LeftHand IK 静态 Binding：未配置握把是预期能力缺失；永久缺失 Socket 关闭。 ----
	if (Weapon->GetThirdPersonLeftHandGripSocketName() != NAME_None)
	{
		if (CharacterMesh->DoesSocketExist(HandSocketName) &&
			CharacterMesh->DoesSocketExist(LeftHandBoneName) &&
			CharacterMesh->DoesSocketExist(HandGripSocketName) &&
			Weapon->HasThirdPersonLeftHandGripSocket())
		{
			const FTransform RightHandWorld = CharacterMesh->GetSocketTransform(HandSocketName, RTS_World);
			const FTransform GripWorld = Weapon->GetThirdPersonLeftHandGripWorldTransform();
			const FTransform LeftHandWorld = CharacterMesh->GetSocketTransform(LeftHandBoneName, RTS_World);
			const FTransform HandGripWorld = CharacterMesh->GetSocketTransform(HandGripSocketName, RTS_World);
			if (IsMathematicallyValidBindingFrame(RightHandWorld) &&
				IsMathematicallyValidBindingFrame(GripWorld) &&
				IsMathematicallyValidBindingFrame(LeftHandWorld) &&
				IsMathematicallyValidBindingFrame(HandGripWorld))
			{
				const FTransform ComputedGripInRightHandSpace =
					ComputeLeftHandGripInRightHandSpace(RightHandWorld, GripWorld);
				const FTransform ComputedHandGripInLeftHandSpace =
					ComputeHandGripInLeftHandSpace(LeftHandWorld, HandGripWorld);
				if (IsMathematicallyValidBindingFrame(ComputedGripInRightHandSpace) &&
					IsMathematicallyValidBindingFrame(ComputedHandGripInLeftHandSpace))
				{
					LeftHandGripInRightHandSpace = ComputedGripInRightHandSpace;
					HandGripInLeftHandSpace = ComputedHandGripInLeftHandSpace;
					bLeftHandIKBindingValid = true;
				}
				else
				{
					bHasTransientMathFailure = true;
				}
			}
			else
			{
				bHasTransientMathFailure = true;
			}
		}
	}

	// 骨骼世界 Transform 本帧不可用：下一次普通动画更新只重试一次。
	if (bHasTransientMathFailure)
	{
		bStaticBindingRebuildPending = true;
	}
}

void UShooterThirdPersonAnimInstance::ClearWeaponStaticBindings()
{
	CachedPresentationWeapon.Reset();
	bStaticBindingRebuildPending = false;
	bAimIKBindingValid = false;
	bLeftHandIKBindingValid = false;

	HandToMuzzle = FTransform::Identity;
	LeftHandGripInRightHandSpace = FTransform::Identity;
	HandGripInLeftHandSpace = FTransform::Identity;
	bAimIKEnabled = false;
	bLeftHandIKEnabled = false;

	AimDirectionWorld = FVector::ZeroVector;
	AimTargetWorld = FVector::ZeroVector;
	bAimTargetWorldValid = false;
}

void UShooterThirdPersonAnimInstance::UpdateAimInputs(const AShooterCharacter* Character)
{
	AimDirectionWorld = FVector::ZeroVector;
	AimTargetWorld = FVector::ZeroVector;
	bAimTargetWorldValid = false;

	// 网络角色、本地/远端数据源选择、目标有效性与视点安全深度全部收口到 Component。
	const UShooterAimPresentationComponent* AimPresentationComponent =
		Character->GetAimPresentationComponent();
	if (AimPresentationComponent)
	{
		AimPresentationComponent->ResolveAimPresentationInput(
			AimDirectionWorld,
			AimTargetWorld,
			bAimTargetWorldValid,
			MinimumRemoteAimTargetDistanceFromView);
	}
}

void UShooterThirdPersonAnimInstance::RefreshIKEnabled()
{
	// 只组合静态 Binding 与动态输入：ResolveAimPresentationInput 保证无效远端目标输出零方向，
	// 因此“有限且非零的 AimDirectionWorld”就是本帧动态瞄准输入有效的统一表达。
	bAimIKEnabled =
		bAimIKBindingValid &&
		FShooterAimIKMath::IsFinite(AimDirectionWorld) &&
		!AimDirectionWorld.IsNearlyZero();

	bLeftHandIKEnabled = bLeftHandIKBindingValid;
}

void UShooterThirdPersonAnimInstance::UpdateShooterAnimationData(float DeltaSeconds)
{
	AShooterCharacter* Character = GetCachedShooterCharacter();
	if (!Character)
	{
		ClearWeaponStaticBindings();
		return;
	}

	// 动态移动 / Aim 输入保持每帧采集。
	bShouldMove = LocomotionGroundSpeed > 0.01f;
	MoveDirection = UKismetAnimationLibrary::CalculateDirection(
		Velocity,
		Character->GetActorRotation());
	// AimPitchN 与最终世界输入都从 AimPresentationComponent 的统一口径获取。
	if (const UShooterAimPresentationComponent* AimPresentationComponent =
		Character->GetAimPresentationComponent())
	{
		AimPitchN = AimPresentationComponent->GetAimPitchN();
	}
	else
	{
		AimPitchN = 0.0f;
	}

	// AnimInstance 不主动要求 Character 修复表现：
	// 装备变化、Unequip 和武器销毁统一由 Equipment OnRep / 生命周期入口发布表现完成事件。
	AShooterWeapon* LogicalWeapon = Character->GetCurrentWeaponActor();

	// 事件重建时骨骼 Transform 暂时不可用：只允许下一次更新重试一次。
	if (bStaticBindingRebuildPending)
	{
		bStaticBindingRebuildPending = false;
		if (LogicalWeapon != nullptr && CachedPresentationWeapon.Get() == LogicalWeapon)
		{
			RebuildWeaponStaticBindings(LogicalWeapon);
		}
	}

	UpdateAimInputs(Character);
	RefreshIKEnabled();
}
