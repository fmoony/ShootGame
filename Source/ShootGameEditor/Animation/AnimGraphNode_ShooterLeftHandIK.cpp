// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShootGameEditor/Animation/AnimGraphNode_ShooterLeftHandIK.h"

#define LOCTEXT_NAMESPACE "ShooterLeftHandIKNodes"

UAnimGraphNode_ShooterLeftHandIK::UAnimGraphNode_ShooterLeftHandIK(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FText UAnimGraphNode_ShooterLeftHandIK::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("ShooterLeftHandIK_Title", "Shooter Left Hand IK");
}

FText UAnimGraphNode_ShooterLeftHandIK::GetTooltipText() const
{
	return LOCTEXT(
		"ShooterLeftHandIK_Tooltip",
		"在最终 hand_r 姿势之后求解左臂，使角色 HandGrip_L 与武器握把的位置和旋转完整对齐。");
}

FString UAnimGraphNode_ShooterLeftHandIK::GetNodeCategory() const
{
	return TEXT("Animation|Skeletal Controls");
}

#undef LOCTEXT_NAMESPACE
