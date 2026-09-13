// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNode_ShooterLeftHandIK.h"
#include "Characters/Animation/AnimNodes/ShooterLeftHandIKMath.h"
#include "Animation/AnimInstanceProxy.h"
#include "HAL/IConsoleManager.h"
#include "ShootGame.h"
#include "TwoBoneIK.h"

namespace
{
	TAutoConsoleVariable<int32> CVarShooterLeftHandIKDiag(TEXT("ShootGame.LeftHandIK.Diag"), 0,
		TEXT("输出左手 IK 可达范围、目标偏移和肘平面诊断。0=关闭，1=开启。"), ECVF_Default);
}

FAnimNode_ShooterLeftHandIK::FAnimNode_ShooterLeftHandIK()
{
	LeftHandBone.BoneName = TEXT("hand_l");
	RightHandBone.BoneName = TEXT("hand_r");
}

void FAnimNode_ShooterLeftHandIK::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(EvaluateSkeletalControl_AnyThread)

	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	const FCompactPoseBoneIndex LeftHandIndex = LeftHandBone.GetCompactPoseIndex(BoneContainer);
	const FCompactPoseBoneIndex RightHandIndex = RightHandBone.GetCompactPoseIndex(BoneContainer);
	if (LeftHandIndex == INDEX_NONE || RightHandIndex == INDEX_NONE ||
		CachedUpperArmIndex == INDEX_NONE || CachedLowerArmIndex == INDEX_NONE)
	{
		return;
	}

	const FTransform RightHandCS = Output.Pose.GetComponentSpaceTransform(RightHandIndex);
	FTransform DesiredLeftHandCS;
	if (!FShooterLeftHandIKMath::CalculateDesiredLeftHandTransform(RightHandCS, WeaponGripInRightHandSpace,
			HandGripInLeftHandSpace, DesiredLeftHandCS))
	{
		return;
	}

	FTransform UpperArmCS = Output.Pose.GetComponentSpaceTransform(CachedUpperArmIndex);
	FTransform LowerArmCS = Output.Pose.GetComponentSpaceTransform(CachedLowerArmIndex);
	FTransform LeftHandCS = Output.Pose.GetComponentSpaceTransform(LeftHandIndex);
	if (!UpperArmCS.IsValid() || !LowerArmCS.IsValid() || !LeftHandCS.IsValid())
	{
		return;
	}
	const FTransform SourceUpperArmCS = UpperArmCS;
	const FTransform SourceLowerArmCS = LowerArmCS;
	const FTransform SourceLeftHandCS = LeftHandCS;

	// 正常区域直接沿用输入动画的真实肘点；只有接近共线时才延续上一帧。
	FVector JointTargetCS = LowerArmCS.GetLocation();
	FVector ResolvedPoleDirectionCS;
	if (FShooterLeftHandIKMath::CalculateCurrentPoseJointTarget(UpperArmCS.GetLocation(), LowerArmCS.GetLocation(),
		DesiredLeftHandCS.GetLocation(), PreviousPoleDirectionCS, MinimumElbowPoleOffset, JointTargetCS,
		ResolvedPoleDirectionCS))
	{
		PreviousPoleDirectionCS = ResolvedPoleDirectionCS;
	}
	AnimationCore::SolveTwoBoneIK(UpperArmCS, LowerArmCS, LeftHandCS, JointTargetCS, DesiredLeftHandCS.GetLocation(),
		false, 1.0f, 1.0f);

	// SolveTwoBoneIK 只负责链条位置；末端旋转必须使用完整握把参考帧。
	LeftHandCS.SetRotation(DesiredLeftHandCS.GetRotation());
	LeftHandCS.NormalizeRotation();

	if (CVarShooterLeftHandIKDiag.GetValueOnAnyThread() != 0)
	{
		++DiagnosticEvaluationCounter;
		const float DesiredHandStep = bHasPreviousDiagnosticSample
			? FVector::Distance(PreviousDiagnosticDesiredHandLocationCS, DesiredLeftHandCS.GetLocation())
			: 0.0f;
		const float SourceHandStep = bHasPreviousDiagnosticSample
			? FVector::Distance(PreviousDiagnosticSourceHandLocationCS, SourceLeftHandCS.GetLocation())
			: 0.0f;
		const float RightHandRotationStep = bHasPreviousDiagnosticSample
			? FMath::RadiansToDegrees(PreviousDiagnosticRightHandRotationCS.AngularDistance(RightHandCS.GetRotation()))
			: 0.0f;

		const FVector RootLocation = SourceUpperArmCS.GetLocation();
		const FVector SourceJointLocation = SourceLowerArmCS.GetLocation();
		const FVector SourceHandLocation = SourceLeftHandCS.GetLocation();
		const FVector DesiredHandLocation = DesiredLeftHandCS.GetLocation();
		const float UpperArmLength = FVector::Distance(RootLocation, SourceJointLocation);
		const float LowerArmLength = FVector::Distance(SourceJointLocation, SourceHandLocation);
		const float MinimumReach = FMath::Abs(UpperArmLength - LowerArmLength);
		const float MaximumReach = UpperArmLength + LowerArmLength;
		const float DesiredReach = FVector::Distance(RootLocation, DesiredHandLocation);
		const float SourceHandError = FVector::Distance(SourceHandLocation, DesiredHandLocation);
		const FVector DesiredAxis = (DesiredHandLocation - RootLocation).GetSafeNormal();
		const float SourceElbowLateral = FVector::VectorPlaneProject(SourceJointLocation - RootLocation, DesiredAxis).Size();
		FVector CurrentSourcePole;
		const bool bHasCurrentSourcePole = FShooterLeftHandIKMath::CalculateSourcePoleDirection(RootLocation,
			SourceJointLocation, SourceHandLocation, CurrentSourcePole);
		const float ResolvedVsCurrentPole = bHasCurrentSourcePole && !ResolvedPoleDirectionCS.IsNearlyZero()
			? FVector::DotProduct(ResolvedPoleDirectionCS, CurrentSourcePole)
			: 0.0f;
		const float ElbowShift = FVector::Distance(SourceJointLocation, LowerArmCS.GetLocation());
		const float HandRotationDelta = FMath::RadiansToDegrees(
			SourceLeftHandCS.GetRotation().AngularDistance(DesiredLeftHandCS.GetRotation()));

		const bool bPeriodicSample = DiagnosticEvaluationCounter == 1 || DiagnosticEvaluationCounter % 30 == 0;
		const bool bTransientSample = SourceHandError >= 20.0f || DesiredHandStep >= 10.0f || SourceHandStep >= 10.0f ||
			RightHandRotationStep >= 10.0f || ElbowShift >= 8.0f || ResolvedVsCurrentPole < 0.5f || DesiredReach > MaximumReach;
		if (bPeriodicSample || bTransientSample)
		{
			const FString& ActorName = Output.AnimInstanceProxy->GetActorName();
			const FString& AnimInstanceName = Output.AnimInstanceProxy->GetAnimInstanceName();
			UE_LOG(
				LogShootGame,
				Display,
				TEXT("LEFT_HAND_IK_REACH actor=%s anim=%s instance=%p eval=%u alpha=%.3f reach=%.2f min=%.2f max=%.2f outside_min=%d outside_max=%d source_error=%.2f target_step=%.2f source_step=%.2f right_rot_step=%.2f elbow_lateral=%.2f resolved_vs_current=%.3f elbow_shift=%.2f hand_rot_delta=%.2f"),
				*ActorName,
				*AnimInstanceName,
				Output.AnimInstanceProxy->GetAnimInstanceObject(),
				DiagnosticEvaluationCounter,
				ActualAlpha,
				DesiredReach,
				MinimumReach,
				MaximumReach,
				DesiredReach < MinimumReach ? 1 : 0,
				DesiredReach > MaximumReach ? 1 : 0,
				SourceHandError,
				DesiredHandStep,
				SourceHandStep,
				RightHandRotationStep,
				SourceElbowLateral,
				ResolvedVsCurrentPole,
				ElbowShift,
				HandRotationDelta);
		}

		PreviousDiagnosticDesiredHandLocationCS = DesiredLeftHandCS.GetLocation();
		PreviousDiagnosticSourceHandLocationCS = SourceLeftHandCS.GetLocation();
		PreviousDiagnosticRightHandRotationCS = RightHandCS.GetRotation();
		bHasPreviousDiagnosticSample = true;
	}
	else
	{
		bHasPreviousDiagnosticSample = false;
	}

	OutBoneTransforms.Add(FBoneTransform(CachedUpperArmIndex, UpperArmCS));
	OutBoneTransforms.Add(FBoneTransform(CachedLowerArmIndex, LowerArmCS));
	OutBoneTransforms.Add(FBoneTransform(LeftHandIndex, LeftHandCS));
}

bool FAnimNode_ShooterLeftHandIK::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	return LeftHandBone.IsValidToEvaluate(RequiredBones) && RightHandBone.IsValidToEvaluate(RequiredBones) &&
		CachedUpperArmIndex != INDEX_NONE && CachedLowerArmIndex != INDEX_NONE;
}

void FAnimNode_ShooterLeftHandIK::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(InitializeBoneReferences)

	LeftHandBone.Initialize(RequiredBones);
	RightHandBone.Initialize(RequiredBones);
	CachedUpperArmIndex = FCompactPoseBoneIndex(INDEX_NONE);
	CachedLowerArmIndex = FCompactPoseBoneIndex(INDEX_NONE);
	PreviousPoleDirectionCS = FVector::ZeroVector;
	DiagnosticEvaluationCounter = 0;
	bHasPreviousDiagnosticSample = false;
	PreviousDiagnosticDesiredHandLocationCS = FVector::ZeroVector;
	PreviousDiagnosticSourceHandLocationCS = FVector::ZeroVector;
	PreviousDiagnosticRightHandRotationCS = FQuat::Identity;

	const FCompactPoseBoneIndex LeftHandIndex = LeftHandBone.GetCompactPoseIndex(RequiredBones);
	if (LeftHandIndex != INDEX_NONE)
	{
		CachedLowerArmIndex = RequiredBones.GetParentBoneIndex(LeftHandIndex);
		if (CachedLowerArmIndex != INDEX_NONE)
		{
			CachedUpperArmIndex = RequiredBones.GetParentBoneIndex(CachedLowerArmIndex);
		}
	}
}
