// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "ShootGame/Animation/AnimNodes/AnimNode_ShooterAimIK.h"
#include "AnimGraphNode_ShooterAimIK.generated.h"

/**
 * Shooter Aim IK 的 AnimBP 编辑器节点外壳。
 * 只负责 AnimBP 中的节点呈现与资产接线；IK 数学在 FAnimNode_ShooterAimIK（Runtime）。
 * 见 Docs/执行计划/第三人称C++程序化瞄准IK实施计划.md。
 */
UCLASS(meta = (Keywords = "Aim IK Shooter Muzzle Gun"))
class SHOOTGAMEEDITOR_API UAnimGraphNode_ShooterAimIK : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, Category = "Shooter Aim IK")
	FAnimNode_ShooterAimIK Node;

public:
	// UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	// End of UEdGraphNode interface

	// UAnimGraphNode_Base interface
	virtual FString GetNodeCategory() const override;
	// End of UAnimGraphNode_Base interface

	// UAnimGraphNode_SkeletalControlBase interface
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
	// End of UAnimGraphNode_SkeletalControlBase interface
};
