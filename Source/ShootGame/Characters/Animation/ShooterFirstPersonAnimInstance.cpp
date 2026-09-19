// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterFirstPersonAnimInstance.h"

#include "Characters/ShooterCharacter.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Weapons/ShooterWeapon.h"
#include "Animation/AnimInstanceProxy.h"
#include "Camera/CameraComponent.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

namespace ShooterFirstPersonPresentation
{
	static TAutoConsoleVariable<float> Clearance(TEXT("ShootGame.FirstPerson.Clearance"), 8.0f,
		TEXT("First-person forearm/hand and weapon minimum camera depth in cm; 0 disables."));

	/** 在现有 AnimGraph 求值后向视图前下方避让双侧前臂。保留握枪关系、原动画和头部相机。 */
	struct FClearanceProxy : FAnimInstanceProxy
	{
		enum EBone { Pelvis, Head, LeftShoulder, RightShoulder, LeftUpperArm, RightUpperArm,
			LeftElbow, LeftHand, RightElbow, RightHand,
			LeftThumb, LeftIndex, LeftMiddle, LeftRing, LeftPinky,
			RightThumb, RightIndex, RightMiddle, RightRing, RightPinky, BoneCount };

		explicit FClearanceProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

		virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
		{
			FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
			Margin = 0.0f;
			CompositionDepth = 0.0f;
			const AShooterCharacter* Character = Cast<AShooterCharacter>(Instance->GetOwningActor());
			if (!Character || !Character->IsLocallyControlled() || Character->IsDead() ||
				!IsValid(Character->GetCurrentWeapon()) || Instance->GetSkelMeshComponent() != Character->GetFirstPersonMesh())
			{
				return;
			}
			const AShooterWeapon* Weapon = Character->GetCurrentWeapon();
			const USkeletalMeshComponent* Mesh = Character->GetFirstPersonMesh();
			const UCameraComponent* Camera = Character->GetFirstPersonCameraComponent();
			if (!Camera || !Camera->IsActive())
			{
				return;
			}
			// UObject 读取仅在游戏线程；求值线程只消费下列数值快照。
			Margin = Clearance.GetValueOnGameThread();
			CompositionDepth = Weapon->GetFirstPersonCompositionDrop();
			// 有独立托枪握点的长枪才限制肘向；手枪保留原换弹前臂，避免腕部过度弯折。
			bConstrainLeftElbow = Weapon->HasThirdPersonLeftHandGripSocket();
			BodyForward = Mesh->GetComponentTransform().InverseTransformVectorNoScale(Character->GetActorForwardVector());
			Forward = Mesh->GetComponentTransform().InverseTransformVectorNoScale(Character->GetViewRotation().Vector());
			Up = Mesh->GetComponentTransform().InverseTransformVectorNoScale(
				FRotationMatrix(Character->GetViewRotation()).GetUnitAxis(EAxis::Z));
			CameraLocation = Mesh->GetComponentTransform().InverseTransformPosition(Camera->GetComponentLocation());
			CameraOffset = Camera->GetRelativeLocation();
			bCameraOnHead = Camera->GetAttachParent() == Mesh && Camera->GetAttachSocketName() == TEXT("head");
			const TCHAR* Names[] = {
				TEXT("pelvis"), TEXT("head"), TEXT("clavicle_l"), TEXT("clavicle_r"),
				TEXT("upperarm_l"), TEXT("upperarm_r"),
				TEXT("lowerarm_l"), TEXT("hand_l"), TEXT("lowerarm_r"), TEXT("hand_r"),
				TEXT("thumb_03_l"), TEXT("index_03_l"), TEXT("middle_03_l"), TEXT("ring_03_l"), TEXT("pinky_03_l"),
				TEXT("thumb_03_r"), TEXT("index_03_r"), TEXT("middle_03_r"), TEXT("ring_03_r"), TEXT("pinky_03_r") };
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
			{
				BoneIndices[Index] = Mesh->GetBoneIndex(Names[Index]);
			}
			const USkeletalMeshComponent* WeaponMesh = Weapon->GetFirstPersonMesh();
			WeaponBounds = WeaponMesh->GetSkeletalMeshAsset()
				? WeaponMesh->GetSkeletalMeshAsset()->GetBounds().GetBox() : FBox(ForceInit);
			WeaponToHand = WeaponMesh->GetComponentTransform().GetRelativeTransform(Mesh->GetSocketTransform(TEXT("hand_r")));
		}

		virtual bool Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* Root) override
		{
			EvaluateAnimationNode_WithRoot(Output, Root);
			if (Margin <= 0.0f || Root != GetRootNode())
			{
				return true;
			}
			const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
			TArray<FCompactPoseBoneIndex, TInlineAllocator<BoneCount>> Indices;
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(BoneIndices); ++Index)
			{
				Indices.Add(Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(BoneIndices[Index])));
				if (Indices[Index] == INDEX_NONE)
				{
					return true;
				}
			}
			FCSPose<FCompactPose> ComponentPose;
			ComponentPose.InitPose(Output.Pose);
			// 从本帧最终头部姿势计算视点，避免换弹 head 动画导致一帧滞后。
			const FVector Eye = bCameraOnHead
				? ComponentPose.GetComponentSpaceTransform(Indices[Head]).TransformPosition(CameraOffset) : CameraLocation;
			const FVector LeftForearm = ComponentPose.GetComponentSpaceTransform(Indices[LeftHand]).GetLocation()
				- ComponentPose.GetComponentSpaceTransform(Indices[LeftElbow]).GetLocation();
			FVector LeftForearmDirection = LeftForearm.GetSafeNormal();
			constexpr float MinimumForearmRise = 0.55f;
			if (bConstrainLeftElbow && FVector::DotProduct(LeftForearmDirection, Up) < MinimumForearmRise)
			{
				FVector Horizontal = FVector::VectorPlaneProject(LeftForearmDirection, Up).GetSafeNormal();
				if (Horizontal.IsNearlyZero())
				{
					Horizontal = Forward;
				}
				LeftForearmDirection = Horizontal * FMath::Sqrt(1.0f - FMath::Square(MinimumForearmRise))
					+ Up * MinimumForearmRise;
			}
			const FVector LeftElbowOffset = LeftForearm - LeftForearmDirection * LeftForearm.Size();
			float MinimumDepth = TNumericLimits<float>::Max();
			auto IncludePoint = [&](const FVector& Point)
			{
				MinimumDepth = FMath::Min(MinimumDepth, static_cast<float>(FVector::DotProduct(Point - Eye, Forward)));
			};
			// 手腕在近裁剪面前并不代表朝向镜头的手指也安全，指节同样参与最小深度计算。
			for (int32 Index = LeftElbow; Index < Indices.Num(); ++Index)
			{
				IncludePoint(ComponentPose.GetComponentSpaceTransform(Indices[Index]).GetLocation());
			}
			IncludePoint(ComponentPose.GetComponentSpaceTransform(Indices[LeftElbow]).GetLocation() + LeftElbowOffset);
			if (WeaponBounds.IsValid)
			{
				const FTransform WeaponTransform = WeaponToHand * ComponentPose.GetComponentSpaceTransform(Indices[RightHand]);
				for (int32 Corner = 0; Corner < 8; ++Corner)
				{
					IncludePoint(WeaponTransform.TransformPosition(FVector(
						Corner & 1 ? WeaponBounds.Max.X : WeaponBounds.Min.X,
						Corner & 2 ? WeaponBounds.Max.Y : WeaponBounds.Min.Y,
						Corner & 4 ? WeaponBounds.Max.Z : WeaponBounds.Min.Z)));
				}
			}

			const float ClearanceDistance = FMath::Max(0.0f, Margin - MinimumDepth);
			// 防裁切：双臂与武器向视图前下方避让。
			const FVector ClearanceOffset = (Forward - Up * 0.5f) * ClearanceDistance;
			// 构图修正：例如手枪额外下沉，降低屏幕占比。
			const FVector CompositionOffset = -Up * CompositionDepth;
			// 双手最终需要保持的整体位移。
			const FVector HandTargetOffset = ClearanceOffset + CompositionOffset;
			// 用于调整上臂朝向，保持肘部姿态。
			const FVector UpperArmDirectionBias = Up * ClearanceDistance;
			auto TranslateBone = [&](EBone Bone, const FVector& Offset)
			{
				const FCompactPoseBoneIndex Parent = Output.Pose.GetParentBoneIndex(Indices[Bone]);
				Output.Pose[Indices[Bone]].AddToTranslation(
					ComponentPose.GetComponentSpaceTransform(Parent).InverseTransformVector(Offset));
			};
			// 将本地身体与视点留出间距，头部反向补偿保持相机不动，手臂独立保持握枪位置。
			const FVector BodyOffset = -BodyForward * 12.0f;
			TranslateBone(Pelvis, BodyOffset);
			TranslateBone(Head, -BodyOffset);
			// 长枪左手作为固定端，肘部位于手腕下方；用关节旋转保持两段骨长，避免反折与拉伸。
			auto PlaceArm = [&](EBone Shoulder, EBone UpperArm, EBone Elbow, EBone Hand)
			{
				const FTransform UpperTransform = ComponentPose.GetComponentSpaceTransform(Indices[UpperArm]);
				const FTransform ElbowTransform = ComponentPose.GetComponentSpaceTransform(Indices[Elbow]);
				const FTransform HandTransform = ComponentPose.GetComponentSpaceTransform(Indices[Hand]);
				const FVector OriginalForearm = HandTransform.GetLocation() - ElbowTransform.GetLocation();
				const FVector ForearmDirection = Elbow == LeftElbow ? LeftForearmDirection : OriginalForearm.GetSafeNormal();
				const FVector ElbowOffset = OriginalForearm - ForearmDirection * OriginalForearm.Size();
				const FQuat ElbowRotation = FQuat::FindBetweenNormals(OriginalForearm.GetSafeNormal(), ForearmDirection)
					* ElbowTransform.GetRotation();
				const FVector OriginalArm = ElbowTransform.GetLocation() - UpperTransform.GetLocation();
				const FVector ArmDirection = (OriginalArm + UpperArmDirectionBias).GetSafeNormal();
				const FVector ShoulderOffset = HandTargetOffset + ElbowOffset + OriginalArm - ArmDirection * OriginalArm.Size() - BodyOffset;
				TranslateBone(Shoulder, ShoulderOffset);
				const FQuat UpperRotation = FQuat::FindBetweenNormals(OriginalArm.GetSafeNormal(), ArmDirection)
					* UpperTransform.GetRotation();
				const FCompactPoseBoneIndex Parent = Output.Pose.GetParentBoneIndex(Indices[UpperArm]);
				Output.Pose[Indices[UpperArm]].SetRotation(
					(ComponentPose.GetComponentSpaceTransform(Parent).GetRotation().Inverse() * UpperRotation).GetNormalized());
				Output.Pose[Indices[Elbow]].SetRotation((UpperRotation.Inverse() * ElbowRotation).GetNormalized());
				Output.Pose[Indices[Hand]].SetRotation((ElbowRotation.Inverse() * HandTransform.GetRotation()).GetNormalized());
			};
			PlaceArm(LeftShoulder, LeftUpperArm, LeftElbow, LeftHand);
			PlaceArm(RightShoulder, RightUpperArm, RightElbow, RightHand);
#if WITH_DEV_AUTOMATION_TESTS
			static const bool bValidateCapture = FParse::Param(FCommandLine::Get(), TEXT("ShootGameFirstPersonCapture"));
			if (bValidateCapture)
			{
				FCSPose<FCompactPose> FinalPose;
				FinalPose.InitPose(Output.Pose);
				auto Position = [&](EBone Bone, bool bFinal)
				{
					return (bFinal ? FinalPose : ComponentPose).GetComponentSpaceTransform(Indices[Bone]).GetLocation();
				};
				bool bValid = Position(Head, true).Equals(Position(Head, false), 0.05f);
				for (EBone Hand : { LeftHand, RightHand })
				{
					const EBone Elbow = Hand == LeftHand ? LeftElbow : RightElbow;
					const EBone UpperArm = Hand == LeftHand ? LeftUpperArm : RightUpperArm;
					bValid &= Position(Hand, true).Equals(Position(Hand, false) + HandTargetOffset, 0.05f);
					for (EBone Joint : { UpperArm, Hand })
					{
						bValid &= FMath::IsNearlyEqual(FVector::Distance(Position(Joint, true), Position(Elbow, true)),
							FVector::Distance(Position(Joint, false), Position(Elbow, false)), 0.05);
					}
				}
				bValid &= !bConstrainLeftElbow
						|| FVector::DotProduct((Position(LeftHand, true) - Position(LeftElbow, true)).GetSafeNormal(), Up)
							>= MinimumForearmRise - 0.001f;
				if (!bValid)
				{
					UE_LOG(LogTemp, Error, TEXT("FIRST_PERSON_POSE_FAILURE: bone length, hand target, head or elbow direction changed"));
				}
			}
#endif
			return true;
		}

		float Margin = 0.0f;
		float CompositionDepth = 0.0f;
		FVector Forward = FVector::ForwardVector;
		FVector BodyForward = FVector::ForwardVector;
		FVector Up = FVector::UpVector;
		FVector CameraLocation = FVector::ZeroVector;
		FVector CameraOffset = FVector::ZeroVector;
		bool bCameraOnHead = false;
		bool bConstrainLeftElbow = false;
		int32 BoneIndices[BoneCount] = {};
		FBox WeaponBounds = FBox(ForceInit);
		FTransform WeaponToHand;
	};
}

FAnimInstanceProxy* UShooterFirstPersonAnimInstance::CreateAnimInstanceProxy()
{
	return new ShooterFirstPersonPresentation::FClearanceProxy(this);
}

void UShooterFirstPersonAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}

void UShooterFirstPersonAnimInstance::UpdateShooterAnimationData(float DeltaSeconds)
{
	RefreshFirstPersonAnimationData(DeltaSeconds);
}

void UShooterFirstPersonAnimInstance::RefreshFirstPersonAnimationData(float DeltaSeconds)
{
	AShooterCharacter* Character = GetCachedShooterCharacter();
	if (!Character || !Character->IsLocallyControlled() || !Character->GetFirstPersonMesh() || Character->IsDead() ||
		!IsValid(CurrentWeaponActor) || CurrentWeaponActor->GetOwner() != Character)
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

void UShooterFirstPersonAnimInstance::UpdateAimRigPresentationAlpha(const AShooterCharacter* Character, float DeltaSeconds)
{
	bool bReloadPresentationRecovering = false;
	if (Character)
	{
		const USkeletalMeshComponent* ThirdPersonMesh = Character->GetMesh();
		const UShooterThirdPersonAnimInstance* ThirdPersonAnimInstance = ThirdPersonMesh
			? Cast<UShooterThirdPersonAnimInstance>(ThirdPersonMesh->GetAnimInstance())
			: nullptr;
		bReloadPresentationRecovering = ThirdPersonAnimInstance && ThirdPersonAnimInstance->bReloadPresentationRecovering;
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
	AimRigPresentationAlpha = FMath::Lerp(AimRigBlendSourceAlpha, AimRigBlendTargetAlpha, EasedAlpha);
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
