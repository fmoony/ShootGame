// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShootGameEditor/Animation/AnimGraphNode_ShooterAimIK.h"

#define LOCTEXT_NAMESPACE "ShooterAimIKNodes"

UAnimGraphNode_ShooterAimIK::UAnimGraphNode_ShooterAimIK(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FText UAnimGraphNode_ShooterAimIK::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("ShooterAimIK_Title", "Shooter Aim IK");
}

FText UAnimGraphNode_ShooterAimIK::GetTooltipText() const
{
	return LOCTEXT("ShooterAimIK_Tooltip",
		"程序化第三人称枪口瞄准校正（C++ Skeletal Control）。"
		"当前为 C1 空壳：Pose 完全透传，不修改骨骼。"
		"C2 起将把 Muzzle Forward 校正到 Aim Direction。");
}

FString UAnimGraphNode_ShooterAimIK::GetNodeCategory() const
{
	return TEXT("Animation|Skeletal Controls");
}

#undef LOCTEXT_NAMESPACE
