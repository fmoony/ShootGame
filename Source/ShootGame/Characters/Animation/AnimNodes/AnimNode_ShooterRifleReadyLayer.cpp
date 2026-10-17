// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNode_ShooterRifleReadyLayer.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"

FAnimNode_ShooterRifleReadyLayer::FAnimNode_ShooterRifleReadyLayer()
{
	BlendMode = ELayeredBoneBlendMode::BlendMask;
	AddPose();
}

void FAnimNode_ShooterRifleReadyLayer::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	// 与已验证的 Local 原型一致：权重 1 只供原生更新登记播放器，不接管 TickRecord 或时钟。
	BlendWeights[0] = 1.0f;
	FAnimNode_LayeredBoneBlend::Update_AnyThread(Context);
}

void FAnimNode_ShooterRifleReadyLayer::Evaluate_AnyThread(FPoseContext& Output)
{
	const UShooterThirdPersonAnimInstance* Instance =
		Cast<UShooterThirdPersonAnimInstance>(Output.AnimInstanceProxy->GetAnimInstanceObject());
	const FAnimNode_SequencePlayer* Player = Instance ? Instance->GetRifleTransitionPlayer() : nullptr;
	const float Length = Player && Player->GetSequence() ? Player->GetSequence()->GetPlayLength() : 0.0f;
	const float Alpha = bTransition && Length > UE_SMALL_NUMBER
		? FMath::Clamp(Player->GetAccumulatedTime() / Length, 0.0f, 1.0f) : 1.0f;
	BlendWeights[0] = Alpha;
	FAnimationRuntime::UpdateDesiredBoneWeight(DesiredBoneBlendWeights, CurrentBoneBlendWeights, BlendWeights);
	bHasRelevantPoses = FAnimWeight::IsRelevant(Alpha);
	FAnimNode_LayeredBoneBlend::Evaluate_AnyThread(Output);
}
