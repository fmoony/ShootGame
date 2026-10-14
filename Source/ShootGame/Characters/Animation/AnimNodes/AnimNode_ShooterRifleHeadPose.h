// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "AnimNode_ShooterRifleHeadPose.generated.h"

/** RifleReady 的头颈表现：沿真实播放器进度解除 ADS 偏头，保留 AO 仰角。 */
USTRUCT(BlueprintInternalUseOnly)
struct SHOOTGAME_API FAnimNode_ShooterRifleHeadPose : public FAnimNode_Base
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "Rifle Head Pose")
	FPoseLink Source;
	UPROPERTY(EditAnywhere, Category = "Rifle Head Pose")
	bool bTransition = true;
	UPROPERTY(EditAnywhere, Category = "Rifle Head Pose")
	bool bUpright = true;
	float ActualAlpha = 0.0f;
	float AppliedSwingDegrees = 0.0f;
	virtual bool HasPreUpdate() const override { return true; }
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
private:
	// 角色方向只在游戏线程采集；求值使用组件空间缓存，不读取 Actor Transform。
	FVector BodyForward = FVector(0, 1, 0);
	FVector BodyUp = FVector::UpVector;
};
