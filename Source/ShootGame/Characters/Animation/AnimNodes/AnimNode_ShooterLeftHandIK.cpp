// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/Animation/AnimNodes/AnimNode_ShooterLeftHandIK.h"
#include "Characters/Animation/AnimNodes/ShooterLeftHandIKMath.h"
#include "HAL/IConsoleManager.h"
#include "ShootGame.h"
#include "TwoBoneIK.h"

namespace ShooterLeftHandIKDebug
{
	TAutoConsoleVariable<int32> CVarDiagnostics(
		TEXT("ShootGame.LeftHandIK.Diag"),
		0,
		TEXT("左手 IK 数值诊断。0=关闭，1=输出 IK 前后位置/旋转差、Twist/Swing、肘部翻转与水平基准修正日志（LEFT_HAND_IK_DIAG）。水平基准在首次诊断求值时捕获，建议在水平持枪姿势下开启。"),
		ECVF_Cheat);
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

	// [诊断] 仅在 CVar 开启时保留 IK 前快照，并在首次有效求值捕获水平基准手腕姿态。
	const bool bDiagnosticsEnabled = ShooterLeftHandIKDebug::CVarDiagnostics.GetValueOnAnyThread() > 0;
	FTransform SourceHandCS = FTransform::Identity;
	if (bDiagnosticsEnabled)
	{
		SourceHandCS = LeftHandCS;
		if (!bDiagnosticsBaselineValid)
		{
			if (FShooterLeftHandIKMath::ComputeHandRelativeToForearm(
					SourceHandCS,
					LowerArmCS,
					CachedBaselineWristRotationRelativeToForearmCS))
			{
				bDiagnosticsBaselineValid = true;
				UE_LOG(
					LogShootGame,
					Display,
					TEXT("LEFT_HAND_IK_DIAG_BASELINE captured horizontal wrist attitude relative to forearm"));
			}
		}
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

	if (bDiagnosticsEnabled)
	{
		EmitLeftHandDiagnostics(
			SourceHandCS,
			DesiredLeftHandCS,
			UpperArmCS,
			LowerArmCS,
			LeftHandCS);
	}

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

void FAnimNode_ShooterLeftHandIK::EmitLeftHandDiagnostics(
	const FTransform& SourceHandCS,
	const FTransform& DesiredLeftHandCS,
	const FTransform& SolvedUpperArmCS,
	const FTransform& SolvedLowerArmCS,
	const FTransform& SolvedHandCS)
{
	FShooterLeftHandWristDiagnostics Diagnostics;
	if (!FShooterLeftHandIKMath::ComputeWristDiagnostics(
			SourceHandCS,
			DesiredLeftHandCS,
			SolvedUpperArmCS,
			SolvedLowerArmCS,
			SolvedHandCS,
			CachedPreferredPoleDirectionCS,
			CachedBaselineWristRotationRelativeToForearmCS,
			bDiagnosticsBaselineValid,
			Diagnostics))
	{
		UE_LOG(LogShootGame, Warning, TEXT("LEFT_HAND_IK_DIAG_INVALID invalid diagnostic frame"));
		return;
	}

	// 常规帧节流输出；肘部翻转作为罕见事件立即记录。
	const uint64 EvaluationIndex = ++DiagnosticsEvaluationCount;
	if (EvaluationIndex <= 6 || EvaluationIndex % 30 == 0 || Diagnostics.bElbowHemisphereFlipped)
	{
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("LEFT_HAND_IK_DIAG index=%llu pos_delta_cm=%.3f pos_residual_cm=%.3f rot_delta_deg=%.2f twist_deg=%.2f swing_deg=%.2f elbow_valid=%d elbow_align=%.3f elbow_flipped=%d base_valid=%d base_corr_deg=%.2f base_corr_twist_deg=%.2f base_corr_swing_deg=%.2f"),
			static_cast<unsigned long long>(EvaluationIndex),
			Diagnostics.TargetPositionDelta,
			Diagnostics.PostSolvePositionResidual,
			Diagnostics.TotalRotationDeltaDegrees,
			Diagnostics.TwistDegrees,
			Diagnostics.SwingDegrees,
			Diagnostics.bElbowMeasured ? 1 : 0,
			Diagnostics.ElbowPoleAlignment,
			Diagnostics.bElbowHemisphereFlipped ? 1 : 0,
			Diagnostics.bBaselineCorrectionValid ? 1 : 0,
			Diagnostics.BaselineCorrectionDegrees,
			Diagnostics.BaselineCorrectionTwistDegrees,
			Diagnostics.BaselineCorrectionSwingDegrees);
	}
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
	CachedBaselineWristRotationRelativeToForearmCS = FQuat::Identity;
	bDiagnosticsBaselineValid = false;
	DiagnosticsEvaluationCount = 0;

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
