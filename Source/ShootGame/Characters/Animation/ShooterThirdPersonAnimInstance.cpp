// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Characters/Animation/AnimNodes/ShooterAimIKMath.h"
#include "Characters/Aim/ShooterAimPresentationComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Weapons/ShooterWeapon.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"
#include "ShootGame.h"

FTransform UShooterThirdPersonAnimInstance::ComputeHandToMuzzleTransform(
	const FTransform& InHandWorld,
	const FTransform& InMuzzleWorld)
{
	// 只保留真正的数学防御：NaN/Inf 输入不参与计算。
	// Identity 是合法 Transform，不再被当作“缺失数据”；是否可消费由 Binding.State == Ready 决定。
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

	// 握把相对 hand_r 的刚性 Transform（hand_r 坐标系中的握把位置/旋转）。
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

bool UShooterThirdPersonAnimInstance::IsMathematicallyValidBindingFrame(const FTransform& T)
{
	// 只查真正的非法数值 / 旋转 / Scale，不比较 Identity。
	// FTransform::IsValid() 在本机 UE 5.6 中已覆盖 NaN/Inf 与 Rotation 归一化；Scale 由这里补查。
	return T.IsValid() &&
		T.GetScale3D().X > 0.0f &&
		T.GetScale3D().Y > 0.0f &&
		T.GetScale3D().Z > 0.0f;
}

FAimIKBindingSignature UShooterThirdPersonAnimInstance::GatherAimSignature(
	AShooterCharacter* Character) const
{
	FAimIKBindingSignature Signature;
	if (!Character)
	{
		return Signature;
	}

	Signature.Character = Character;
	Signature.CharacterMesh = Character->GetMesh();
	Signature.CharacterMeshAsset =
		Signature.CharacterMesh.IsValid() ? Signature.CharacterMesh->GetSkeletalMeshAsset() : nullptr;

	Signature.Weapon = Character->GetCurrentWeapon();
	if (Signature.Weapon.IsValid())
	{
		// WeaponOwner / AttachParent / AttachSocketName 存在复制与时序：
		// CurrentWeaponActor 先到而 Owner 未到时暂不附着，故必须纳入签名（实施计划第 1 节）。
		Signature.WeaponOwner = Signature.Weapon->GetOwner();
		Signature.WeaponMesh = Signature.Weapon->GetThirdPersonMesh();
		Signature.WeaponMeshAsset =
			Signature.WeaponMesh.IsValid() ? Signature.WeaponMesh->GetSkeletalMeshAsset() : nullptr;
		Signature.WeaponMeshAttachParent =
			Signature.WeaponMesh.IsValid() ? Signature.WeaponMesh->GetAttachParent() : nullptr;
		Signature.WeaponMeshAttachSocketName =
			Signature.WeaponMesh.IsValid() ? Signature.WeaponMesh->GetAttachSocketName() : NAME_None;
		Signature.WeaponMuzzleSocketName = Signature.Weapon->GetMuzzleSocketName();
	}

	Signature.HandSocketName = HandSocketName;
	return Signature;
}

FLeftHandIKBindingSignature UShooterThirdPersonAnimInstance::GatherLeftHandSignature(
	AShooterCharacter* Character) const
{
	FLeftHandIKBindingSignature Signature;
	if (!Character)
	{
		return Signature;
	}

	Signature.Character = Character;
	Signature.CharacterMesh = Character->GetMesh();
	Signature.CharacterMeshAsset =
		Signature.CharacterMesh.IsValid() ? Signature.CharacterMesh->GetSkeletalMeshAsset() : nullptr;

	Signature.Weapon = Character->GetCurrentWeapon();
	if (Signature.Weapon.IsValid())
	{
		Signature.WeaponOwner = Signature.Weapon->GetOwner();
		Signature.WeaponMesh = Signature.Weapon->GetThirdPersonMesh();
		Signature.WeaponMeshAsset =
			Signature.WeaponMesh.IsValid() ? Signature.WeaponMesh->GetSkeletalMeshAsset() : nullptr;
		Signature.WeaponMeshAttachParent =
			Signature.WeaponMesh.IsValid() ? Signature.WeaponMesh->GetAttachParent() : nullptr;
		Signature.WeaponMeshAttachSocketName =
			Signature.WeaponMesh.IsValid() ? Signature.WeaponMesh->GetAttachSocketName() : NAME_None;
		Signature.WeaponLeftHandGripSocketName =
			Signature.Weapon->GetThirdPersonLeftHandGripSocketName();
	}

	Signature.HandSocketName = HandSocketName;
	Signature.LeftHandBoneName = LeftHandBoneName;
	Signature.HandGripSocketName = HandGripSocketName;
	return Signature;
}

void UShooterThirdPersonAnimInstance::UpdateAimBinding(
	const FAimIKBindingSignature& Signature,
	float DeltaSeconds)
{
	const EIKBindingState PreviousState = AimBinding.State;
	const bool bSignatureChanged = !(Signature == AimBinding.StoredSignature);

	if (bSignatureChanged)
	{
		AimBinding.StoredSignature = Signature;
		RebuildAimBinding(Signature);
	}
	else if (AimBinding.State == EIKBindingState::Pending)
	{
		// 唯一签名不变仍每帧重建的状态；Pending 超时后也继续每帧 Rebuild，不降频。
		RebuildAimBinding(Signature);
	}

	if (bSignatureChanged || AimBinding.State != PreviousState)
	{
		ResetBindingTimers(AimBinding);
	}

	TickBindingTimers(AimBinding, DeltaSeconds, WaitingForAttachWarningDelaySeconds);
}

void UShooterThirdPersonAnimInstance::UpdateLeftHandBinding(
	const FLeftHandIKBindingSignature& Signature,
	float DeltaSeconds)
{
	const EIKBindingState PreviousState = LeftHandBinding.State;
	const bool bSignatureChanged = !(Signature == LeftHandBinding.StoredSignature);

	if (bSignatureChanged)
	{
		LeftHandBinding.StoredSignature = Signature;
		RebuildLeftHandBinding(Signature);
	}
	else if (LeftHandBinding.State == EIKBindingState::Pending)
	{
		RebuildLeftHandBinding(Signature);
	}

	if (bSignatureChanged || LeftHandBinding.State != PreviousState)
	{
		ResetBindingTimers(LeftHandBinding);
	}

	TickBindingTimers(LeftHandBinding, DeltaSeconds, WaitingForAttachWarningDelaySeconds);
}

void UShooterThirdPersonAnimInstance::RebuildAimBinding(const FAimIKBindingSignature& Signature)
{
	const AShooterCharacter* Character = Signature.Character.Get();
	USkeletalMeshComponent* CharacterMesh = Signature.CharacterMesh.Get();
	AShooterWeapon* Weapon = Signature.Weapon.Get();
	USkeletalMeshComponent* WeaponMesh = Signature.WeaponMesh.Get();

	// 判定链每层只决定一次，命中即退出（实施计划 5.2）。
	if (!IsValid(Character) || !IsValid(CharacterMesh))
	{
		SetBindingState(AimBinding, EIKBindingState::Unbound, EIKBindingFailureReason::None, TEXT("Aim"));
		return;
	}

	if (CharacterMesh->GetSkeletalMeshAsset() == nullptr)
	{
		SetBindingState(AimBinding, EIKBindingState::Unsupported, EIKBindingFailureReason::MissingCharacterMeshAsset, TEXT("Aim"));
		return;
	}

	// 与 ShooterAnimInstanceBase 的 bHasEquippedWeapon 保持同一 Weapon 有效性判定。
	if (!IsValid(Weapon) || Signature.WeaponOwner.Get() != Character)
	{
		SetBindingState(AimBinding, EIKBindingState::Unbound, EIKBindingFailureReason::None, TEXT("Aim"));
		return;
	}

	if (WeaponMesh == nullptr)
	{
		SetBindingState(AimBinding, EIKBindingState::Unsupported, EIKBindingFailureReason::MissingWeaponMeshComponent, TEXT("Aim"));
		return;
	}

	if (WeaponMesh->GetSkeletalMeshAsset() == nullptr)
	{
		SetBindingState(AimBinding, EIKBindingState::Unsupported, EIKBindingFailureReason::MissingWeaponMeshAsset, TEXT("Aim"));
		return;
	}

	if (WeaponMesh->GetAttachParent() != CharacterMesh)
	{
		// Weapon 与 WeaponMesh 存在但尚未附着：等待，只走超时诊断，不进入 Rebuild。
		SetBindingState(AimBinding, EIKBindingState::WaitingForAttach, EIKBindingFailureReason::None, TEXT("Aim"));
		return;
	}

	const FName AttachSocketName = WeaponMesh->GetAttachSocketName();
	if (AttachSocketName == NAME_None || !CharacterMesh->DoesSocketExist(AttachSocketName))
	{
		SetBindingState(AimBinding, EIKBindingState::Unsupported, EIKBindingFailureReason::MissingWeaponAttachSocket, TEXT("Aim"));
		return;
	}

	if (!CharacterMesh->DoesSocketExist(Signature.HandSocketName))
	{
		SetBindingState(AimBinding, EIKBindingState::Unsupported, EIKBindingFailureReason::MissingRightHand, TEXT("Aim"));
		return;
	}

	if (!Weapon->HasThirdPersonMuzzleSocket())
	{
		SetBindingState(AimBinding, EIKBindingState::Unsupported, EIKBindingFailureReason::MissingMuzzle, TEXT("Aim"));
		return;
	}

	const FTransform HandWorld = CharacterMesh->GetSocketTransform(Signature.HandSocketName, RTS_World);
	const FTransform MuzzleWorld = Weapon->GetThirdPersonMuzzleWorldTransform();
	if (!IsMathematicallyValidBindingFrame(HandWorld) || !IsMathematicallyValidBindingFrame(MuzzleWorld))
	{
		SetBindingState(AimBinding, EIKBindingState::Pending, EIKBindingFailureReason::InvalidHandToMuzzle, TEXT("Aim"));
		return;
	}

	const FTransform ComputedHandToMuzzle = ComputeHandToMuzzleTransform(HandWorld, MuzzleWorld);
	if (!IsMathematicallyValidBindingFrame(ComputedHandToMuzzle))
	{
		SetBindingState(AimBinding, EIKBindingState::Pending, EIKBindingFailureReason::InvalidHandToMuzzle, TEXT("Aim"));
		return;
	}

	// Ready：即使 HandToMuzzle == Identity 也允许（Identity 是合法 Transform）。
	AimBinding.HandToMuzzle = ComputedHandToMuzzle;
	SetBindingState(AimBinding, EIKBindingState::Ready, EIKBindingFailureReason::None, TEXT("Aim"));
}

void UShooterThirdPersonAnimInstance::RebuildLeftHandBinding(const FLeftHandIKBindingSignature& Signature)
{
	const AShooterCharacter* Character = Signature.Character.Get();
	USkeletalMeshComponent* CharacterMesh = Signature.CharacterMesh.Get();
	AShooterWeapon* Weapon = Signature.Weapon.Get();
	USkeletalMeshComponent* WeaponMesh = Signature.WeaponMesh.Get();

	// 前七层与 Aim 完全相同（实施计划 5.3）。
	if (!IsValid(Character) || !IsValid(CharacterMesh))
	{
		SetBindingState(LeftHandBinding, EIKBindingState::Unbound, EIKBindingFailureReason::None, TEXT("LeftHand"));
		return;
	}

	if (CharacterMesh->GetSkeletalMeshAsset() == nullptr)
	{
		SetBindingState(LeftHandBinding, EIKBindingState::Unsupported, EIKBindingFailureReason::MissingCharacterMeshAsset, TEXT("LeftHand"));
		return;
	}

	if (!IsValid(Weapon) || Signature.WeaponOwner.Get() != Character)
	{
		SetBindingState(LeftHandBinding, EIKBindingState::Unbound, EIKBindingFailureReason::None, TEXT("LeftHand"));
		return;
	}

	if (WeaponMesh == nullptr)
	{
		SetBindingState(LeftHandBinding, EIKBindingState::Unsupported, EIKBindingFailureReason::MissingWeaponMeshComponent, TEXT("LeftHand"));
		return;
	}

	if (WeaponMesh->GetSkeletalMeshAsset() == nullptr)
	{
		SetBindingState(LeftHandBinding, EIKBindingState::Unsupported, EIKBindingFailureReason::MissingWeaponMeshAsset, TEXT("LeftHand"));
		return;
	}

	if (WeaponMesh->GetAttachParent() != CharacterMesh)
	{
		SetBindingState(LeftHandBinding, EIKBindingState::WaitingForAttach, EIKBindingFailureReason::None, TEXT("LeftHand"));
		return;
	}

	const FName AttachSocketName = WeaponMesh->GetAttachSocketName();
	if (AttachSocketName == NAME_None || !CharacterMesh->DoesSocketExist(AttachSocketName))
	{
		SetBindingState(LeftHandBinding, EIKBindingState::Unsupported, EIKBindingFailureReason::MissingWeaponAttachSocket, TEXT("LeftHand"));
		return;
	}

	if (!CharacterMesh->DoesSocketExist(Signature.HandSocketName))
	{
		SetBindingState(LeftHandBinding, EIKBindingState::Unsupported, EIKBindingFailureReason::MissingRightHand, TEXT("LeftHand"));
		return;
	}

	if (!CharacterMesh->DoesSocketExist(Signature.LeftHandBoneName))
	{
		SetBindingState(LeftHandBinding, EIKBindingState::Unsupported, EIKBindingFailureReason::MissingLeftHand, TEXT("LeftHand"));
		return;
	}

	if (!CharacterMesh->DoesSocketExist(Signature.HandGripSocketName))
	{
		SetBindingState(LeftHandBinding, EIKBindingState::Unsupported, EIKBindingFailureReason::MissingCharacterHandGrip, TEXT("LeftHand"));
		return;
	}

	if (Signature.WeaponLeftHandGripSocketName == NAME_None)
	{
		// Pistol 等未配置左手握把：预期能力缺失，不 Warning。
		SetBindingState(LeftHandBinding, EIKBindingState::Unsupported, EIKBindingFailureReason::WeaponLeftHandGripNotConfigured, TEXT("LeftHand"));
		return;
	}

	if (!Weapon->HasThirdPersonLeftHandGripSocket())
	{
		SetBindingState(LeftHandBinding, EIKBindingState::Unsupported, EIKBindingFailureReason::WeaponLeftHandGripSocketMissing, TEXT("LeftHand"));
		return;
	}

	const FTransform RightHandWorld = CharacterMesh->GetSocketTransform(Signature.HandSocketName, RTS_World);
	const FTransform GripWorld = Weapon->GetThirdPersonLeftHandGripWorldTransform();
	if (!IsMathematicallyValidBindingFrame(RightHandWorld) || !IsMathematicallyValidBindingFrame(GripWorld))
	{
		SetBindingState(LeftHandBinding, EIKBindingState::Pending, EIKBindingFailureReason::InvalidWeaponGripInRightHandSpace, TEXT("LeftHand"));
		return;
	}

	const FTransform ComputedGripInRightHandSpace =
		ComputeLeftHandGripInRightHandSpace(RightHandWorld, GripWorld);
	if (!IsMathematicallyValidBindingFrame(ComputedGripInRightHandSpace))
	{
		SetBindingState(LeftHandBinding, EIKBindingState::Pending, EIKBindingFailureReason::InvalidWeaponGripInRightHandSpace, TEXT("LeftHand"));
		return;
	}

	const FTransform LeftHandWorld = CharacterMesh->GetSocketTransform(Signature.LeftHandBoneName, RTS_World);
	const FTransform HandGripWorld = CharacterMesh->GetSocketTransform(Signature.HandGripSocketName, RTS_World);
	if (!IsMathematicallyValidBindingFrame(LeftHandWorld) || !IsMathematicallyValidBindingFrame(HandGripWorld))
	{
		SetBindingState(LeftHandBinding, EIKBindingState::Pending, EIKBindingFailureReason::InvalidHandGripInLeftHandSpace, TEXT("LeftHand"));
		return;
	}

	const FTransform ComputedHandGripInLeftHandSpace =
		ComputeHandGripInLeftHandSpace(LeftHandWorld, HandGripWorld);
	if (!IsMathematicallyValidBindingFrame(ComputedHandGripInLeftHandSpace))
	{
		SetBindingState(LeftHandBinding, EIKBindingState::Pending, EIKBindingFailureReason::InvalidHandGripInLeftHandSpace, TEXT("LeftHand"));
		return;
	}

	// Ready：即使任一结果为 Identity 也允许。
	LeftHandBinding.WeaponGripInRightHandSpace = ComputedGripInRightHandSpace;
	LeftHandBinding.HandGripInLeftHandSpace = ComputedHandGripInLeftHandSpace;
	SetBindingState(LeftHandBinding, EIKBindingState::Ready, EIKBindingFailureReason::None, TEXT("LeftHand"));
}

void UShooterThirdPersonAnimInstance::ResetBindingsAndOutputs()
{
	AimBinding = FAimIKBinding();
	LeftHandBinding = FLeftHandIKBinding();

	AimDirectionWorld = FVector::ZeroVector;
	AimTargetWorld = FVector::ZeroVector;
	bAimTargetWorldValid = false;
	AimPitchN = 0.0f;
	MoveDirection = 0.0f;
	bShouldMove = false;
	HandToMuzzle = FTransform::Identity;
	LeftHandGripInRightHandSpace = FTransform::Identity;
	HandGripInLeftHandSpace = FTransform::Identity;
	bAimIKEnabled = false;
	bLeftHandIKEnabled = false;

	AimBindingState = EIKBindingState::Unbound;
	AimBindingFailureReason = EIKBindingFailureReason::None;
	LeftHandBindingState = EIKBindingState::Unbound;
	LeftHandBindingFailureReason = EIKBindingFailureReason::None;
}

void UShooterThirdPersonAnimInstance::UpdateAimInputs(const AShooterCharacter* Character)
{
	// 注意：远端 AimDirection / AimTarget 计算不消费 MuzzleWorld；
	// 若未来引入 Muzzle 依赖，必须以 AimBinding.State == Ready 把关（实施计划 5.5）。
	const UShooterAimPresentationComponent* AimPresentationComponent =
		Character->GetAimPresentationComponent();
	const bool bShouldRunPresentationSmoothing =
		UShooterAimPresentationComponent::ShouldRunPresentationAimSmoothing(
			Character->GetLocalRole(),
			Character->GetNetMode(),
			Character->IsLocallyControlled());
	const FVector BaseAimDirection = Character->GetBaseAimRotation().Vector().GetSafeNormal();
	AimDirectionWorld = FVector::ZeroVector;
	AimTargetWorld = FVector::ZeroVector;
	bAimTargetWorldValid = false;

	if (Character->IsLocallyControlled())
	{
		AimDirectionWorld = BaseAimDirection;
	}
	else if (bShouldRunPresentationSmoothing &&
		AimPresentationComponent &&
		AimPresentationComponent->IsPresentationAimTargetValid() &&
		FShooterAimIKMath::IsFinite(BaseAimDirection) &&
		!BaseAimDirection.IsNearlyZero())
	{
		const FVector ViewWorldLocation = Character->GetPawnViewLocation();
		FVector SafeTargetWorld = AimPresentationComponent->GetSmoothedPresentationAimTarget();
		if (FShooterAimIKMath::IsFinite(ViewWorldLocation) &&
			FShooterAimIKMath::IsFinite(SafeTargetWorld))
		{
			const float TargetDepthFromView = FVector::DotProduct(
				SafeTargetWorld - ViewWorldLocation,
				BaseAimDirection);
			if (TargetDepthFromView < MinimumRemoteAimTargetDistanceFromView)
			{
				SafeTargetWorld += BaseAimDirection *
					(MinimumRemoteAimTargetDistanceFromView - TargetDepthFromView);
			}

			AimDirectionWorld = BaseAimDirection;
			AimTargetWorld = SafeTargetWorld;
			bAimTargetWorldValid = true;
		}
	}
}

void UShooterThirdPersonAnimInstance::RefreshIKEnabled(const AShooterCharacter* Character)
{
	// 调试副本与 Enabled 同步刷新；AnimBP 调试面板只读显示。
	AimBindingState = AimBinding.State;
	AimBindingFailureReason = AimBinding.FailureReason;
	LeftHandBindingState = LeftHandBinding.State;
	LeftHandBindingFailureReason = LeftHandBinding.FailureReason;

	// AimDirection == Zero、AimTarget 本帧失效、死亡、Montage、快速转身都不得改变 Binding 状态，
	// 只作为帧级输入追加在 Enabled 表达式尾部（实施计划 5.5）。
	bAimIKEnabled =
		AimBinding.State == EIKBindingState::Ready &&
		FShooterAimIKMath::IsFinite(AimDirectionWorld) &&
		!AimDirectionWorld.IsNearlyZero() &&
		(Character->IsLocallyControlled() || bAimTargetWorldValid);

	bLeftHandIKEnabled = LeftHandBinding.State == EIKBindingState::Ready;
}

void UShooterThirdPersonAnimInstance::ForceRebuildIKBindings()
{
	AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwningActor());
	if (!Character)
	{
		ResetBindingsAndOutputs();
		return;
	}

	// 清空两个 StoredSignature 并立即 Rebuild，唤醒 Unsupported / 重新评估依赖。
	AimBinding.StoredSignature = FAimIKBindingSignature();
	LeftHandBinding.StoredSignature = FLeftHandIKBindingSignature();

	UpdateAimBinding(GatherAimSignature(Character), 0.0f);
	UpdateLeftHandBinding(GatherLeftHandSignature(Character), 0.0f);
	RefreshIKEnabled(Character);
}

void UShooterThirdPersonAnimInstance::ResetBindingTimers(FAimIKBinding& Binding)
{
	Binding.WaitingForAttachElapsedSeconds = 0.0f;
	Binding.bWaitingForAttachWarningReported = false;
	Binding.PendingElapsedSeconds = 0.0f;
	Binding.bPendingTimeoutReported = false;
}

void UShooterThirdPersonAnimInstance::ResetBindingTimers(FLeftHandIKBinding& Binding)
{
	Binding.WaitingForAttachElapsedSeconds = 0.0f;
	Binding.bWaitingForAttachWarningReported = false;
	Binding.PendingElapsedSeconds = 0.0f;
	Binding.bPendingTimeoutReported = false;
}

void UShooterThirdPersonAnimInstance::TickBindingTimers(
	FAimIKBinding& Binding,
	float DeltaSeconds,
	float WaitingForAttachDelay)
{
	// 计时只累加 Max(DeltaSeconds, 0.0f)；同签名、同状态持续 Pending 时继续累加。
	const float SafeDelta = FMath::Max(DeltaSeconds, 0.0f);

	if (Binding.State == EIKBindingState::Pending)
	{
		Binding.PendingElapsedSeconds += SafeDelta;
		if (!Binding.bPendingTimeoutReported && Binding.PendingElapsedSeconds >= PendingTimeoutSeconds)
		{
			Binding.bPendingTimeoutReported = true;
			UE_LOG(
				LogShootGame,
				Error,
				TEXT("[IKBinding][Aim] Pending exceeded %.2fs\n")
				TEXT("    Reason=%s Elapsed=%.3fs\n")
				TEXT("    Weapon=%s HandSocket=%s MuzzleSocket=%s"),
				PendingTimeoutSeconds,
				GetFailureReasonName(Binding.FailureReason),
				Binding.PendingElapsedSeconds,
				*GetNameSafe(Binding.StoredSignature.Weapon.Get()),
				*Binding.StoredSignature.HandSocketName.ToString(),
				*Binding.StoredSignature.WeaponMuzzleSocketName.ToString());
		}
	}
	else if (Binding.State == EIKBindingState::WaitingForAttach)
	{
		Binding.WaitingForAttachElapsedSeconds += SafeDelta;
		if (!Binding.bWaitingForAttachWarningReported &&
			Binding.WaitingForAttachElapsedSeconds >= WaitingForAttachDelay)
		{
			Binding.bWaitingForAttachWarningReported = true;
#if !UE_BUILD_SHIPPING
			UE_LOG(
				LogShootGame,
				Warning,
				TEXT("[IKBinding][Aim] WaitingForAttach exceeded %.2fs\n")
				TEXT("    Character=%s CharacterMesh=%s Weapon=%s WeaponMesh=%s\n")
				TEXT("    AttachParent=%s AttachSocketName=%s"),
				WaitingForAttachDelay,
				*GetNameSafe(Binding.StoredSignature.Character.Get()),
				*GetNameSafe(Binding.StoredSignature.CharacterMesh.Get()),
				*GetNameSafe(Binding.StoredSignature.Weapon.Get()),
				*GetNameSafe(Binding.StoredSignature.WeaponMesh.Get()),
				*GetNameSafe(Binding.StoredSignature.WeaponMeshAttachParent.Get()),
				*Binding.StoredSignature.WeaponMeshAttachSocketName.ToString());
#endif
		}
	}
}

void UShooterThirdPersonAnimInstance::TickBindingTimers(
	FLeftHandIKBinding& Binding,
	float DeltaSeconds,
	float WaitingForAttachDelay)
{
	const float SafeDelta = FMath::Max(DeltaSeconds, 0.0f);

	if (Binding.State == EIKBindingState::Pending)
	{
		Binding.PendingElapsedSeconds += SafeDelta;
		if (!Binding.bPendingTimeoutReported && Binding.PendingElapsedSeconds >= PendingTimeoutSeconds)
		{
			Binding.bPendingTimeoutReported = true;
			UE_LOG(
				LogShootGame,
				Error,
				TEXT("[IKBinding][LeftHand] Pending exceeded %.2fs\n")
				TEXT("    Reason=%s Elapsed=%.3fs\n")
				TEXT("    Weapon=%s GripSocket=%s"),
				PendingTimeoutSeconds,
				GetFailureReasonName(Binding.FailureReason),
				Binding.PendingElapsedSeconds,
				*GetNameSafe(Binding.StoredSignature.Weapon.Get()),
				*Binding.StoredSignature.WeaponLeftHandGripSocketName.ToString());
		}
	}
	else if (Binding.State == EIKBindingState::WaitingForAttach)
	{
		Binding.WaitingForAttachElapsedSeconds += SafeDelta;
		if (!Binding.bWaitingForAttachWarningReported &&
			Binding.WaitingForAttachElapsedSeconds >= WaitingForAttachDelay)
		{
			Binding.bWaitingForAttachWarningReported = true;
#if !UE_BUILD_SHIPPING
			UE_LOG(
				LogShootGame,
				Warning,
				TEXT("[IKBinding][LeftHand] WaitingForAttach exceeded %.2fs\n")
				TEXT("    Character=%s CharacterMesh=%s Weapon=%s WeaponMesh=%s\n")
				TEXT("    AttachParent=%s AttachSocketName=%s"),
				WaitingForAttachDelay,
				*GetNameSafe(Binding.StoredSignature.Character.Get()),
				*GetNameSafe(Binding.StoredSignature.CharacterMesh.Get()),
				*GetNameSafe(Binding.StoredSignature.Weapon.Get()),
				*GetNameSafe(Binding.StoredSignature.WeaponMesh.Get()),
				*GetNameSafe(Binding.StoredSignature.WeaponMeshAttachParent.Get()),
				*Binding.StoredSignature.WeaponMeshAttachSocketName.ToString());
#endif
		}
	}
}

const TCHAR* UShooterThirdPersonAnimInstance::GetFailureReasonName(EIKBindingFailureReason Reason)
{
	switch (Reason)
	{
	case EIKBindingFailureReason::None: return TEXT("None");
	case EIKBindingFailureReason::MissingCharacterMeshAsset: return TEXT("MissingCharacterMeshAsset");
	case EIKBindingFailureReason::MissingWeaponMeshComponent: return TEXT("MissingWeaponMeshComponent");
	case EIKBindingFailureReason::MissingWeaponMeshAsset: return TEXT("MissingWeaponMeshAsset");
	case EIKBindingFailureReason::MissingWeaponAttachSocket: return TEXT("MissingWeaponAttachSocket");
	case EIKBindingFailureReason::MissingRightHand: return TEXT("MissingRightHand");
	case EIKBindingFailureReason::MissingLeftHand: return TEXT("MissingLeftHand");
	case EIKBindingFailureReason::MissingMuzzle: return TEXT("MissingMuzzle");
	case EIKBindingFailureReason::MissingCharacterHandGrip: return TEXT("MissingCharacterHandGrip");
	case EIKBindingFailureReason::WeaponLeftHandGripNotConfigured: return TEXT("WeaponLeftHandGripNotConfigured");
	case EIKBindingFailureReason::WeaponLeftHandGripSocketMissing: return TEXT("WeaponLeftHandGripSocketMissing");
	case EIKBindingFailureReason::InvalidHandToMuzzle: return TEXT("InvalidHandToMuzzle");
	case EIKBindingFailureReason::InvalidWeaponGripInRightHandSpace: return TEXT("InvalidWeaponGripInRightHandSpace");
	case EIKBindingFailureReason::InvalidHandGripInLeftHandSpace: return TEXT("InvalidHandGripInLeftHandSpace");
	default: return TEXT("Unknown");
	}
}

void UShooterThirdPersonAnimInstance::WarnOnUnexpectedUnsupported(
	EIKBindingFailureReason Reason,
	const TCHAR* Tag,
	const AShooterWeapon* Weapon)
{
	// 异常类 Unsupported 在进入该状态时 Warning 一次；
	// WeaponLeftHandGripNotConfigured 属于预期能力缺失，不 Warning。
	if (Reason == EIKBindingFailureReason::WeaponLeftHandGripNotConfigured)
	{
		return;
	}

	UE_LOG(
		LogShootGame,
		Warning,
		TEXT("[IKBinding][%s] Unsupported: Reason=%s\n")
		TEXT("    Weapon=%s"),
		Tag,
		GetFailureReasonName(Reason),
		*GetNameSafe(Weapon));
}

void UShooterThirdPersonAnimInstance::UpdateShooterAnimationData(float DeltaSeconds)
{
	// 公共 GroundSpeed / 空中 / 装备 / ASC 表现 Tag 已由 UShooterAnimInstanceBase 采集。
	AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwningActor());
	if (!Character)
	{
		ResetBindingsAndOutputs();
		return;
	}

	// R6.3：接管旧 EventGraph 的 ShouldMove / Direction / PitchN 重复计算。
	// 来源与旧图逐项一致：Speed > 0.01；CalculateDirection 仅在非 OrientRotation 时夹取 ±45°；
	// PitchN 直接读取 Character 的 GetAimPitchN（与旧图调用同一函数）。
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	bShouldMove = LocomotionGroundSpeed > 0.01f;
	MoveDirection = UKismetAnimationLibrary::CalculateDirection(
		Velocity,
		Character->GetActorRotation());
	//if (Movement != nullptr && !Movement->bOrientRotationToMovement)
	//{
	//	MoveDirection = FMath::Clamp(MoveDirection, -45.0f, 45.0f);
	//}
	AimPitchN = Character->GetAimPitchN();

	// 先 Binding，后 Aim 输入（实施计划 5.1）。
	const FAimIKBindingSignature AimSignature = GatherAimSignature(Character);
	const FLeftHandIKBindingSignature LeftHandSignature = GatherLeftHandSignature(Character);

	UpdateAimBinding(AimSignature, DeltaSeconds);
	UpdateLeftHandBinding(LeftHandSignature, DeltaSeconds);

	UpdateAimInputs(Character);
	RefreshIKEnabled(Character);
}
