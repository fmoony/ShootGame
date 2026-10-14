// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNode_ShooterRifleHeadPose.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

namespace RifleHeadPose
{
	FQuat ComponentRotation(const FCompactPose& Pose, FCompactPoseBoneIndex Bone)
	{
		FQuat Q = Pose[Bone].GetRotation();
		const auto& Bones = Pose.GetBoneContainer();
		for (auto Parent = Bones.GetParentBoneIndex(Bone); Parent.IsValid(); Parent = Bones.GetParentBoneIndex(Parent))
		{
			Q = Pose[Parent].GetRotation() * Q;
		}
		return Q.GetNormalized();
	}
}

void FAnimNode_ShooterRifleHeadPose::PreUpdate(const UAnimInstance* InAnimInstance)
{
	BodyForward = FVector(0, 1, 0);
	BodyUp = FVector::UpVector;
	const auto* Mesh = InAnimInstance ? InAnimInstance->GetSkelMeshComponent() : nullptr;
	const auto* Actor = InAnimInstance ? InAnimInstance->GetOwningActor() : nullptr;
	if (Mesh && Actor)
	{
		const FTransform& Transform = Mesh->GetComponentTransform();
		BodyForward = Transform.InverseTransformVectorNoScale(Actor->GetActorForwardVector());
		BodyUp = Transform.InverseTransformVectorNoScale(Actor->GetActorUpVector());
	}
}

void FAnimNode_ShooterRifleHeadPose::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	Source.Initialize(Context);
}

void FAnimNode_ShooterRifleHeadPose::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Source.CacheBones(Context);
}

void FAnimNode_ShooterRifleHeadPose::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	Source.Update(Context);
}

void FAnimNode_ShooterRifleHeadPose::Evaluate_AnyThread(FPoseContext& Output)
{
	Source.Evaluate(Output);
	const auto* Anim = Cast<UShooterThirdPersonAnimInstance>(Output.AnimInstanceProxy->GetAnimInstanceObject());
	const auto* Player = Anim ? Anim->GetRifleTransitionPlayer() : nullptr;
	const float T = bTransition && Player && Player->GetSequence()
		? FMath::Clamp(Player->GetAccumulatedTime() / Player->GetSequence()->GetPlayLength(), 0.0f, 1.0f) : 1.0f;
	ActualAlpha = Anim ? T * T * (3 - 2 * T) : 0;
	AppliedSwingDegrees = 0;
	if (!Anim || !FAnimWeight::IsRelevant(ActualAlpha)) { return; }
	const auto& Bones = Output.Pose.GetBoneContainer();
	const auto& Ref = Bones.GetReferenceSkeleton();
	const FName Names[] = {TEXT("neck_01"), TEXT("neck_02"), TEXT("head")};
	FCompactPoseBoneIndex Chain[] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE),
		FCompactPoseBoneIndex(INDEX_NONE)};
	for (int32 I = 0; I < 3; ++I)
	{
		Chain[I] = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Ref.FindBoneIndex(Names[I])));
		if (!Chain[I].IsValid()) { return; }
	}
	TArray<FTransform> Rest;
	FAnimationRuntime::FillUpComponentSpaceTransforms(Ref, Ref.GetRefBonePose(), Rest);
	const FVector Axis = Rest[Ref.FindBoneIndex(TEXT("head"))].GetRotation().Inverse().RotateVector(FVector(0, 1, 0));
	const FQuat HeadRotation = RifleHeadPose::ComponentRotation(Output.Pose, Chain[2]);
	const FVector Forward = HeadRotation.RotateVector(Axis).GetSafeNormal();
	const FVector Up = BodyUp.GetSafeNormal();
	const double Elevation = FMath::Clamp(Forward | Up, -1.0, 1.0);
	const FVector Flat = (Forward - Up * Elevation).GetSafeNormal();
	const FVector Goal = (BodyForward - Up * (BodyForward | Up)).GetSafeNormal();
	if (Flat.IsNearlyZero() || Goal.IsNearlyZero()) { return; }
	const double Yaw = FMath::Atan2(Up | FVector::CrossProduct(Flat, Goal), Flat | Goal);
	// 在相同仰角上渐进解除偏头；不是世界水平固定，也不是 Roll 消减。
	const FVector Target = FQuat(Up, Yaw * ActualAlpha).RotateVector(Flat) * FMath::Sqrt(1 - Elevation * Elevation)
		+ Up * Elevation;
	FQuat Swing = FQuat::FindBetweenNormals(Forward, Target.GetSafeNormal());
	const double Angle = Swing.GetAngle();
	if (Angle > FMath::DegreesToRadians(28.0))
	{
		Swing = FQuat::Slerp(FQuat::Identity, Swing, FMath::DegreesToRadians(28.0) / Angle);
	}
	AppliedSwingDegrees = FMath::RadiansToDegrees(Swing.GetAngle());
	FPoseContext Delta(Output, true);
	Delta.Pose.ResetToAdditiveIdentity();
	const float Shares[] = {0.2f, 0.3f, 0.5f};
	for (int32 I = 0; I < 3; ++I)
	{
		const auto Parent = Bones.GetParentBoneIndex(Chain[I]);
		const FQuat ParentQ = RifleHeadPose::ComponentRotation(Output.Pose, Parent);
		const FQuat Part = FQuat::Slerp(FQuat::Identity, Swing, Shares[I]);
		Delta.Pose[Chain[I]].SetRotation(ParentQ.Inverse() * Part * ParentQ);
	}
	// 进度已包含在目标中，使用完整 Local Delta，避免二次 Alpha 改变中段仰角。
	FAnimationPoseData BaseData(Output);
	const FAnimationPoseData DeltaData(Delta);
	FAnimationRuntime::AccumulateAdditivePose(BaseData, DeltaData, 1.0f, AAT_LocalSpaceBase);
	Output.Pose.NormalizeRotations();
	if (bUpright)
	{
		const FQuat Q = RifleHeadPose::ComponentRotation(Output.Pose, Chain[2]);
		const FVector F = Q.RotateVector(Axis).GetSafeNormal();
		const FQuat RefQ = Rest[Ref.FindBoneIndex(TEXT("head"))].GetRotation();
		const FVector HeadUp = Q.RotateVector(RefQ.Inverse().RotateVector(FVector::UpVector)).GetSafeNormal();
		const FVector Vertical = (Up - F * (Up | F)).GetSafeNormal();
		if (Vertical.IsNearlyZero()) { return; }
		const double Roll = FMath::Atan2(F | FVector::CrossProduct(HeadUp, Vertical), HeadUp | Vertical);
		FPoseContext RollDelta(Output, true);
		RollDelta.Pose.ResetToAdditiveIdentity();
		for (int32 I = 0; I < 3; ++I)
		{
			const FQuat ParentQ = RifleHeadPose::ComponentRotation(Output.Pose, Bones.GetParentBoneIndex(Chain[I]));
			const FQuat Part(F, Roll * ActualAlpha * Shares[I]);
			RollDelta.Pose[Chain[I]].SetRotation(ParentQ.Inverse() * Part * ParentQ);
		}
		// 轴就是当前面部前向，三段同轴相加；不改变面部方向或已有 AO 仰角。
		const FAnimationPoseData RollData(RollDelta);
		FAnimationRuntime::AccumulateAdditivePose(BaseData, RollData, 1.0f, AAT_LocalSpaceBase);
		Output.Pose.NormalizeRotations();
	}
}
