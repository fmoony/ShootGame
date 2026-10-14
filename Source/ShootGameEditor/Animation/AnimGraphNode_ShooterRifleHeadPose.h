// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_Base.h"
#include "ShootGame/Characters/Animation/AnimNodes/AnimNode_ShooterRifleHeadPose.h"
#include "AnimGraphNode_ShooterRifleHeadPose.generated.h"

UCLASS()
class UAnimGraphNode_ShooterRifleHeadPose : public UAnimGraphNode_Base
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	FAnimNode_ShooterRifleHeadPose Node;
	virtual FText GetNodeTitle(ENodeTitleType::Type Type) const override;
};
