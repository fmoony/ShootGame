// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/Animation/AnimNodes/AnimNode_ShooterAimIK.h"
#include "Characters/Animation/AnimNodes/ShooterAimIKMath.h"
#include "Animation/AnimInstanceProxy.h"

FAnimNode_ShooterAimIK::FAnimNode_ShooterAimIK()
{
	HandBone.BoneName = TEXT("hand_r");
}

void FAnimNode_ShooterAimIK::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(EvaluateSkeletalControl_AnyThread)

	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	const FCompactPoseBoneIndex HandIndex = HandBone.GetCompactPoseIndex(BoneContainer);
	if (HandIndex == INDEX_NONE)
	{
		return;
	}

	FTransform HandTM = Output.Pose.GetComponentSpaceTransform(HandIndex);
	if (!HandTM.IsValid())
	{
		return;
	}

	const FTransform ComponentTransform = Output.AnimInstanceProxy->GetComponentTransform();

	FQuat Correction;
	// Alpha 由基类 ActualAlpha 统一混合（LocalBlendCSBoneTransforms），此处传 1。
	if (!FShooterAimIKMath::SolveHandCorrection(
			AimDirectionWorld,
			ComponentTransform,
			HandTM,
			HandToMuzzle,
			1.0f,
			MaxCorrectionAngle,
			Correction))
	{
		// fail-soft：无效输入不修改 Pose。
		return;
	}

	if (!Correction.IsIdentity(KINDA_SMALL_NUMBER))
	{
		HandTM.SetRotation(Correction * HandTM.GetRotation());
		OutBoneTransforms.Add(FBoneTransform(HandIndex, HandTM));
	}
}

bool FAnimNode_ShooterAimIK::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	return HandBone.IsValidToEvaluate(RequiredBones);
}

void FAnimNode_ShooterAimIK::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	HandBone.Initialize(RequiredBones);
}
