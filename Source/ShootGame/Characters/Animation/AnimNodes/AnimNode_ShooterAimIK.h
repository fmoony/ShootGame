// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "AnimNode_ShooterAimIK.generated.h"

/**
 * 第三人称程序化瞄准 IK 节点（Skeletal Control）。
 */
USTRUCT(BlueprintInternalUseOnly)
struct SHOOTGAME_API FAnimNode_ShooterAimIK : public FAnimNode_SkeletalControlBase
{
	GENERATED_USTRUCT_BODY()

	/** 受控手部骨骼 */
	UPROPERTY(EditAnywhere, Category = "Shooter Aim IK")
	FBoneReference HandBone;

	/** 世界空间瞄准方向 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooter Aim IK", meta = (PinShownByDefault, DisplayName = "Aim Direction"))
	FVector AimDirectionWorld = FVector::ForwardVector;

	/**
	 * 观察端平滑后的世界空间瞄准点。
	 * 节点会从输入 Pose 的 hand_r 还原“本节点修改前”的枪口，再从该枪口指向目标，避免姿势反馈环。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooter Aim IK", meta = (PinShownByDefault, DisplayName = "Aim Target World"))
	FVector AimTargetWorld = FVector::ZeroVector;

	/** 远端目标有效时使用 Aim Target World；本地拥有者或无效目标继续使用 Aim Direction。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooter Aim IK", meta = (PinShownByDefault, DisplayName = "Use Aim Target"))
	bool bUseAimTargetWorld = false;

	/** Muzzle 相对 hand 的刚性 Transform */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooter Aim IK", meta = (PinShownByDefault, DisplayName = "Hand To Muzzle"))
	FTransform HandToMuzzle = FTransform::Identity;

	/** 单帧最大校正角（度） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooter Aim IK", meta = (PinHiddenByDefault, DisplayName = "Max Correction Angle", ClampMin = "0.0", ClampMax = "180.0"))
	float MaxCorrectionAngle = 12.0f;

	/** 目标至少位于预 IK 枪口沿基础视线前方该距离，避免目标进入枪口后方。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooter Aim IK", meta = (PinHiddenByDefault, DisplayName = "Minimum Target Distance From Muzzle", ClampMin = "0.0", Units = "cm"))
	float MinimumTargetDistanceFromMuzzle = 50.0f;

public:
	FAnimNode_ShooterAimIK();

	// FAnimNode_SkeletalControlBase interface
	virtual void UpdateComponentPose_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface
};
