// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "AnimNode_ShooterAimIK.generated.h"

/**
 * 第三人称程序化瞄准 IK 节点（Skeletal Control）。
 *
 * 阶段 C1：仅验证项目基础设施——Pose 完全透传，不修改骨骼，Alpha 可用。
 * 阶段 C2 起：实现 hand_r 单骨骼枪口校正（Muzzle Forward → Aim Direction）。
 * 见 Docs/执行计划/第三人称C++程序化瞄准IK实施计划.md。
 */
USTRUCT(BlueprintInternalUseOnly)
struct SHOOTGAME_API FAnimNode_ShooterAimIK : public FAnimNode_SkeletalControlBase
{
	GENERATED_USTRUCT_BODY()

	/** 受控手部骨骼（C2 起生效；C1 空壳阶段仅用于验证节点基础设施）。 */
	UPROPERTY(EditAnywhere, Category = "Shooter Aim IK")
	FBoneReference HandBone;

	/** 世界空间瞄准方向（C2 起生效）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooter Aim IK", meta = (PinShownByDefault, DisplayName = "Aim Direction"))
	FVector AimDirectionWorld = FVector::ForwardVector;

	/** Muzzle 相对 hand 的刚性 Transform（C2 起生效）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooter Aim IK", meta = (PinShownByDefault, DisplayName = "Hand To Muzzle"))
	FTransform HandToMuzzle = FTransform::Identity;

	/** 单帧最大校正角（度；C2 起生效）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooter Aim IK", meta = (PinHiddenByDefault, DisplayName = "Max Correction Angle", ClampMin = "0.0", ClampMax = "180.0"))
	float MaxCorrectionAngle = 30.0f;

public:
	FAnimNode_ShooterAimIK();

	// FAnimNode_SkeletalControlBase interface
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface
};
