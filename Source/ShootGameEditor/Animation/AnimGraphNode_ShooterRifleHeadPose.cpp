// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_ShooterRifleHeadPose.h"

FText UAnimGraphNode_ShooterRifleHeadPose::GetNodeTitle(ENodeTitleType::Type Type) const
{
	return FText::FromString(TEXT("Rifle Head Forward + Upright (Local / preserve AO pitch)"));
}
