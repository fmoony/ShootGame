// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_ShooterRifleReadyLayer.h"

#include "Kismet2/CompilerResultsLog.h"

FText UAnimGraphNode_ShooterRifleReadyLayer::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText::FromString(Node.bTransition ? TEXT("Shooter Rifle Transition (Local t/T)")
		: TEXT("Shooter Rifle LowReady (Local)"));
}

FText UAnimGraphNode_ShooterRifleReadyLayer::GetTooltipText() const
{
	return FText::FromString(TEXT("以 ArmedAimPose 为底姿态，Local 肩臂混合。")
		TEXT("单层更新权重固定为 1，姿态权重使用原 SequencePlayer 实际 t/T；LowReady 为 1。"));
}

void UAnimGraphNode_ShooterRifleReadyLayer::ValidateAnimNodeDuringCompilation(USkeleton* Skeleton, FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(Skeleton, MessageLog);
	const UEdGraphPin* Weight = FindPin(TEXT("BlendWeights_0"));
	if (Node.BlendPoses.Num() != 1 || Node.BlendWeights.Num() != 1 || Node.BlendMasks.Num() != 1 ||
		!Node.BlendMasks[0] || Node.BlendMode != ELayeredBoneBlendMode::BlendMask || Node.bMeshSpaceRotationBlend ||
		(Weight && (!Weight->LinkedTo.IsEmpty() || !FMath::IsNearlyEqual(FCString::Atof(*Weight->DefaultValue), 1.0f))))
	{
		MessageLog.Error(TEXT("@@ 必须使用单层 Local BlendMask，更新权重为未连接的常量 1。"), this);
	}
}
