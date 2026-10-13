// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNodes/AnimNode_LayeredBoneBlend.h"
#include "AnimNode_ShooterRifleReadyLayer.generated.h"

/** Rifle 单层 Local 混合：更新保持相关，姿态权重从原播放器实际 t/T 读取。 */
USTRUCT(BlueprintInternalUseOnly)
struct SHOOTGAME_API FAnimNode_ShooterRifleReadyLayer : public FAnimNode_LayeredBoneBlend
{
	GENERATED_BODY()

	FAnimNode_ShooterRifleReadyLayer();

	/** AimTransition 使用真实累计时间；LowReady 使用完整肩臂权重。 */
	UPROPERTY(EditAnywhere, Category = "Shooter Rifle Ready")
	bool bTransition = true;

	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
};
