// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/Animation/AnimNodes/AnimNode_ShooterAimIK.h"
#include "Characters/Animation/AnimNodes/ShooterAimIKMath.h"
#include "Animation/AnimInstanceProxy.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ShootGame.h"

namespace
{
	TAtomic<uint64> GAimIKDiagnosticTraversalCount = 0;
	TAtomic<uint64> GAimIKDiagnosticEvaluationCount = 0;

	bool IsAimIKDiagnosticEnabled()
	{
		static const bool bEnabled = FParse::Param(
			FCommandLine::Get(),
			TEXT("ShootGameAimTurnCsvTest"));
		return bEnabled;
	}
}

FAnimNode_ShooterAimIK::FAnimNode_ShooterAimIK()
{
	HandBone.BoneName = TEXT("hand_r");
}

void FAnimNode_ShooterAimIK::UpdateComponentPose_AnyThread(const FAnimationUpdateContext& Context)
{
	if (IsAimIKDiagnosticEnabled())
	{
		const uint64 TraversalIndex = ++GAimIKDiagnosticTraversalCount;
		if (TraversalIndex <= 12 || TraversalIndex % 120 == 0)
		{
			UE_LOG(
				LogShootGame,
				Display,
				TEXT("AIM_IK_NODE_TRAVERSED index=%llu alpha_input=%d alpha=%.3f bool_enabled=%d lod_threshold=%d"),
				static_cast<unsigned long long>(TraversalIndex),
				static_cast<int32>(AlphaInputType),
				Alpha,
				bAlphaBoolEnabled ? 1 : 0,
				LODThreshold);
		}
	}

	FAnimNode_SkeletalControlBase::UpdateComponentPose_AnyThread(Context);
}

void FAnimNode_ShooterAimIK::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(EvaluateSkeletalControl_AnyThread)

	uint64 EvaluationIndex = 0;
	const bool bDiagnosticEnabled = IsAimIKDiagnosticEnabled();
	if (bDiagnosticEnabled)
	{
		EvaluationIndex = ++GAimIKDiagnosticEvaluationCount;
		if (EvaluationIndex <= 12 || EvaluationIndex % 120 == 0)
		{
			UE_LOG(
				LogShootGame,
				Display,
				TEXT("AIM_IK_NODE_EVAL_ENTER index=%llu actual_alpha=%.3f hand_bone=%s"),
				static_cast<unsigned long long>(EvaluationIndex),
				ActualAlpha,
				*HandBone.BoneName.ToString());
		}
	}

	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	const FCompactPoseBoneIndex HandIndex = HandBone.GetCompactPoseIndex(BoneContainer);
	if (HandIndex == INDEX_NONE)
	{
		if (bDiagnosticEnabled)
		{
			UE_LOG(LogShootGame, Warning, TEXT("AIM_IK_NODE_INVALID_HAND index=%llu"), static_cast<unsigned long long>(EvaluationIndex));
		}
		return;
	}

	FTransform HandTM = Output.Pose.GetComponentSpaceTransform(HandIndex);
	if (!HandTM.IsValid())
	{
		return;
	}

	const FTransform ComponentTransform = Output.AnimInstanceProxy->GetComponentTransform();
	FVector DesiredAimDirectionWorld = AimDirectionWorld;

	if (bUseAimTargetWorld &&
		FShooterAimIKMath::IsFinite(AimTargetWorld) &&
		FShooterAimIKMath::IsFinite(AimDirectionWorld) &&
		!AimDirectionWorld.IsNearlyZero())
	{
		// HandTM 来自进入本节点的 Component Space Pose，尚未应用本帧 Aim IK。
		// 因此这里还原出的枪口不会消费上一帧 IK 后的武器姿势，不会形成反馈环。
		const FTransform PreAimIKMuzzleComponent = HandToMuzzle * HandTM;
		const FTransform PreAimIKMuzzleWorld = PreAimIKMuzzleComponent * ComponentTransform;
		if (PreAimIKMuzzleWorld.IsValid())
		{
			const FVector BaseDirectionWorld = AimDirectionWorld.GetSafeNormal();
			FVector SafeTargetWorld = AimTargetWorld;
			const float TargetDepthFromMuzzle = FVector::DotProduct(
				SafeTargetWorld - PreAimIKMuzzleWorld.GetLocation(),
				BaseDirectionWorld);
			if (TargetDepthFromMuzzle < MinimumTargetDistanceFromMuzzle)
			{
				SafeTargetWorld += BaseDirectionWorld *
					(MinimumTargetDistanceFromMuzzle - TargetDepthFromMuzzle);
			}

			const FVector MuzzleToTarget = SafeTargetWorld - PreAimIKMuzzleWorld.GetLocation();
			if (FShooterAimIKMath::IsFinite(MuzzleToTarget) && !MuzzleToTarget.IsNearlyZero())
			{
				DesiredAimDirectionWorld = MuzzleToTarget.GetSafeNormal();
			}
		}
	}

	FQuat Correction;
	// Alpha 由基类 ActualAlpha 统一混合（LocalBlendCSBoneTransforms），此处传 1。
	const bool bSolved = FShooterAimIKMath::SolveHandCorrection(
			DesiredAimDirectionWorld,
			ComponentTransform,
			HandTM,
			HandToMuzzle,
			1.0f,
			MaxCorrectionAngle,
			Correction);

	if (bDiagnosticEnabled)
	{
		if (EvaluationIndex <= 12 || EvaluationIndex % 120 == 0)
		{
			UE_LOG(
				LogShootGame,
				Display,
					TEXT("AIM_IK_NODE_EVAL index=%llu solved=%d actual_alpha=%.3f correction_deg=%.3f target_mode=%d aim=%s"),
				static_cast<unsigned long long>(EvaluationIndex),
					bSolved ? 1 : 0,
					ActualAlpha,
					bSolved ? FMath::RadiansToDegrees(Correction.GetAngle()) : 0.0f,
					bUseAimTargetWorld ? 1 : 0,
					*DesiredAimDirectionWorld.ToCompactString());
		}
	}

	if (!bSolved)
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
