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

	/** 首次有效 Rifle 姿势在角色组件空间的弯肘方向。 */
	FVector CachedPreferredPoleDirectionCS = FVector::ZeroVector;

	/** 上一帧已采用的 Pole 方向，用于跨越共线区时保持同一半球。 */
	FVector PreviousPoleDirectionCS = FVector::ZeroVector;

	/** [诊断] 水平基准：首次诊断求值捕获的源动画手腕相对前臂姿态（PIE 生成后水平持枪即为水平基准）。 */
	FQuat CachedBaselineWristRotationRelativeToForearmCS = FQuat::Identity;

	/** [诊断] 水平基准是否已捕获；由 ShootGame.LeftHandIK.Diag 控制是否启用诊断。 */
	bool bDiagnosticsBaselineValid = false;

	/** [诊断] 诊断日志节流用的求值计数。 */
	uint64 DiagnosticsEvaluationCount = 0;

	/** [诊断] 输出一次 IK 前后的数值诊断日志（LEFT_HAND_IK_DIAG）。 */
	void EmitLeftHandDiagnostics(
		const FTransform& SourceHandCS,
		const FTransform& DesiredLeftHandCS,
		const FTransform& SolvedUpperArmCS,
		const FTransform& SolvedLowerArmCS,
		const FTransform& SolvedHandCS);
};
