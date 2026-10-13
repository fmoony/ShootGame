// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_Base.h"
#include "ShootGame/Characters/Animation/AnimNodes/AnimNode_ShooterRifleReadyLayer.h"
#include "AnimGraphNode_ShooterRifleReadyLayer.generated.h"

/** 正式 Rifle Local 混合的编辑器外壳；运行期不依赖 Editor/Test 类型。 */
UCLASS(meta = (Keywords = "Shooter Rifle LowReady Local"))
class SHOOTGAMEEDITOR_API UAnimGraphNode_ShooterRifleReadyLayer : public UAnimGraphNode_Base
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	FAnimNode_ShooterRifleReadyLayer Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FString GetNodeCategory() const override { return TEXT("Shooter|Rifle"); }
	virtual void ValidateAnimNodeDuringCompilation(USkeleton* Skeleton, FCompilerResultsLog& MessageLog) override;
};
