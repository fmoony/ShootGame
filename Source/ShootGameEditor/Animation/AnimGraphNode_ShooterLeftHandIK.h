// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "ShootGame/Characters/Animation/AnimNodes/AnimNode_ShooterLeftHandIK.h"
#include "AnimGraphNode_ShooterLeftHandIK.generated.h"

/** Shooter Left Hand IK 的 AnimBP 编辑器节点外壳。 */
UCLASS(meta = (Keywords = "Left Hand Grip IK Shooter Weapon"))
class SHOOTGAMEEDITOR_API UAnimGraphNode_ShooterLeftHandIK : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, Category = "Shooter Left Hand IK")
	FAnimNode_ShooterLeftHandIK Node;

public:
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FString GetNodeCategory() const override;
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
};
