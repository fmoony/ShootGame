// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimNodes/AnimNode_ShooterAimIK.h"
#include "Animation/AnimInstanceProxy.h"

FAnimNode_ShooterAimIK::FAnimNode_ShooterAimIK()
{
	// C2 起使用 hand_r 作为受控骨骼；C1 空壳阶段 Evaluate 不修改 Pose。
	HandBone.BoneName = TEXT("hand_r");
}

void FAnimNode_ShooterAimIK::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(EvaluateSkeletalControl_AnyThread)

	// C1 空壳：不修改任何骨骼，Pose 完全透传（保持 OutBoneTransforms 为空）。
	// C2 将在此实现 hand_r 单骨骼枪口校正：
	//   CurrentMuzzleForwardCS -> Desired(AimDirection) 的最短 Rotation Delta，
	//   按 Alpha / MaxCorrectionAngle 限制后应用到 HandBone Rotation。
}

bool FAnimNode_ShooterAimIK::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	// C1：HandBone 无效时节点直接透传（基类行为），保证姿势与基线完全一致。
	return HandBone.IsValidToEvaluate(RequiredBones);
}

void FAnimNode_ShooterAimIK::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	HandBone.Initialize(RequiredBones);
}
