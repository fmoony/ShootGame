// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "AnimNode_ShooterLeftHandIK.generated.h"

/**
 * 将角色 HandGrip_L 完整对齐到武器握把的第三人称左手 IK。
 * 节点应紧接 Shooter Aim IK，以消费已经校正后的 hand_r 组件空间姿势。
 */
USTRUCT(BlueprintInternalUseOnly)
struct SHOOTGAME_API FAnimNode_ShooterLeftHandIK : public FAnimNode_SkeletalControlBase
{
	GENERATED_USTRUCT_BODY()

	/** 左臂 IK 末端骨骼。 */
	UPROPERTY(EditAnywhere, Category = "Shooter Left Hand IK")
	FBoneReference LeftHandBone;

	/** 武器跟随的右手参考骨骼。 */
	UPROPERTY(EditAnywhere, Category = "Shooter Left Hand IK")
	FBoneReference RightHandBone;

	/** 武器握把相对 hand_r 的完整 Transform。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooter Left Hand IK", meta = (PinShownByDefault, DisplayName = "Weapon Grip In Right Hand Space"))
	FTransform WeaponGripInRightHandSpace = FTransform::Identity;

	/** 角色 HandGrip_L 相对 hand_l 的完整 Transform。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooter Left Hand IK", meta = (PinShownByDefault, DisplayName = "Hand Grip In Left Hand Space"))
	FTransform HandGripInLeftHandSpace = FTransform::Identity;

	/** Joint Target 与目标肩腕轴的最小横向距离；只防止接近共线，不改变握把目标。 */
	UPROPERTY(EditAnywhere, Category = "Shooter Left Hand IK", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumElbowPoleOffset = 5.0f;

	FAnimNode_ShooterLeftHandIK();

	// FAnimNode_SkeletalControlBase interface
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface

private:
	FCompactPoseBoneIndex CachedUpperArmIndex = FCompactPoseBoneIndex(INDEX_NONE);
	FCompactPoseBoneIndex CachedLowerArmIndex = FCompactPoseBoneIndex(INDEX_NONE);

	/** 上一帧已采用的 Pole 方向，只在当前动画肘点接近共线时兜底。 */
	FVector PreviousPoleDirectionCS = FVector::ZeroVector;

	/** 仅用于 ShootGame.LeftHandIK.Diag 的实例级日志节流。 */
	uint32 DiagnosticEvaluationCounter = 0;

	/** 上一次诊断求值的输入，用于区分目标跳变与基础姿势滞后；不参与 IK 求解。 */
	bool bHasPreviousDiagnosticSample = false;
	FVector PreviousDiagnosticDesiredHandLocationCS = FVector::ZeroVector;
	FVector PreviousDiagnosticSourceHandLocationCS = FVector::ZeroVector;
	FQuat PreviousDiagnosticRightHandRotationCS = FQuat::Identity;
};
