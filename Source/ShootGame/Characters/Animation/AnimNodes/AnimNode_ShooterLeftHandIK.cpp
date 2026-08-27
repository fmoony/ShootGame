// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/Animation/AnimNodes/AnimNode_ShooterLeftHandIK.h"
#include "Characters/Animation/AnimNodes/ShooterLeftHandIKMath.h"
#include "TwoBoneIK.h"

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
	if (!FShooterLeftHandIKMath::CalculateDesiredLeftHandTransform(
			RightHandCS,
			WeaponGripInRightHandSpace,
			HandGripInLeftHandSpace,
			DesiredLeftHandCS))
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

	if (CachedPreferredPoleDirectionCS.IsNearlyZero())
	{
		FShooterLeftHandIKMath::CalculateSourcePoleDirection(
			UpperArmCS.GetLocation(),
			LowerArmCS.GetLocation(),
			LeftHandCS.GetLocation(),
			CachedPreferredPoleDirectionCS);
	}

	// 缓存持枪基准并用上一帧锁定半球；失败时保留旧的当前肘点行为。
	FVector JointTargetCS = LowerArmCS.GetLocation();
	FVector ResolvedPoleDirectionCS;
	if (FShooterLeftHandIKMath::CalculateLockedJointTarget(
		UpperArmCS.GetLocation(),
		LowerArmCS.GetLocation(),
		DesiredLeftHandCS.GetLocation(),
			CachedPreferredPoleDirectionCS,
			PreviousPoleDirectionCS,
			MinimumElbowPoleOffset,
			JointTargetCS,
		ResolvedPoleDirectionCS))
	{
		PreviousPoleDirectionCS = ResolvedPoleDirectionCS;
	}
	AnimationCore::SolveTwoBoneIK(
		UpperArmCS,
		LowerArmCS,
		LeftHandCS,
		JointTargetCS,
		DesiredLeftHandCS.GetLocation(),
		false,
		1.0f,
		1.0f);

	// SolveTwoBoneIK 只负责链条位置；末端旋转必须使用完整握把参考帧。
	LeftHandCS.SetRotation(DesiredLeftHandCS.GetRotation());
	LeftHandCS.NormalizeRotation();

	OutBoneTransforms.Add(FBoneTransform(CachedUpperArmIndex, UpperArmCS));
	OutBoneTransforms.Add(FBoneTransform(CachedLowerArmIndex, LowerArmCS));
	OutBoneTransforms.Add(FBoneTransform(LeftHandIndex, LeftHandCS));
}

bool FAnimNode_ShooterLeftHandIK::IsValidToEvaluate(
	const USkeleton* Skeleton,
	const FBoneContainer& RequiredBones)
{
	return LeftHandBone.IsValidToEvaluate(RequiredBones) &&
		RightHandBone.IsValidToEvaluate(RequiredBones) &&
		CachedUpperArmIndex != INDEX_NONE &&
		CachedLowerArmIndex != INDEX_NONE;
}

void FAnimNode_ShooterLeftHandIK::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(InitializeBoneReferences)

	LeftHandBone.Initialize(RequiredBones);
	RightHandBone.Initialize(RequiredBones);
	CachedUpperArmIndex = FCompactPoseBoneIndex(INDEX_NONE);
	CachedLowerArmIndex = FCompactPoseBoneIndex(INDEX_NONE);
	CachedPreferredPoleDirectionCS = FVector::ZeroVector;
	PreviousPoleDirectionCS = FVector::ZeroVector;

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
