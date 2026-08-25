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

	FAnimNode_ShooterLeftHandIK();

	// FAnimNode_SkeletalControlBase interface
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface

private:
	FCompactPoseBoneIndex CachedUpperArmIndex = FCompactPoseBoneIndex(INDEX_NONE);
	FCompactPoseBoneIndex CachedLowerArmIndex = FCompactPoseBoneIndex(INDEX_NONE);
};
