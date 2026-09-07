// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Characters/Animation/ShooterAnimInstanceBase.h"
#include "ShooterFirstPersonAnimInstance.generated.h"

/**
 * 第一人称 AnimBP 适配层。
 *
 * 仅服务本地拥有者的第一人称 Mesh；从 Character / Equipment / ASC Tag
 * 采集表现值，不复制任何第一人称专用相机或手臂状态。
 * 无本地控制、无第一人称 Mesh、死亡或无当前武器时清空专用快照。
 */
UCLASS(Blueprintable)
class SHOOTGAME_API UShooterFirstPersonAnimInstance : public UShooterAnimInstanceBase
{
	GENERATED_BODY()

public:
	/** 第一人称专用快照是否有效；AnimBP 可据此关闭专用姿势分支。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter|FirstPerson")
	bool bFirstPersonDataValid = false;

	/** 第一人称换弹姿势使用的归一化俯仰输入。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	float AimPitchN = 0.0f;

	/** 第一人称瞄准 Control Rig 的当前表现权重；换弹时平滑释放，结束后平滑恢复。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shooter|FirstPerson|Reload")
	float AimRigPresentationAlpha = 1.0f;

	/** 进入换弹时把第一人称瞄准 Control Rig 从当前权重释放到 0 的近似时长。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shooter|FirstPerson|Reload",
		meta = (ClampMin = "0.0", Units = "s"))
	float AimRigBlendOutTime = 0.15f;

	/** 换弹结束后把第一人称瞄准 Control Rig 恢复到 1 的近似时长。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shooter|FirstPerson|Reload",
		meta = (ClampMin = "0.0", Units = "s"))
	float AimRigBlendInTime = 0.20f;

protected:
	virtual void UpdateShooterAnimationData(float DeltaSeconds) override;

private:
	/** 无本地控制 / 无 Mesh / 死亡 / 无武器时清空专用快照。 */
	void RefreshFirstPersonAnimationData(float DeltaSeconds);

	/** 根据 Reload 表现状态平滑更新第一人称瞄准 Control Rig 权重。 */
	void UpdateAimRigPresentationAlpha(const AShooterCharacter* Character, float DeltaSeconds);

	/** 清空第一人称专用值，保留公共快照的采集规则。 */
	void ClearFirstPersonAnimationData();

	/** 当前平滑段的起始权重。 */
	float AimRigBlendSourceAlpha = 1.0f;

	/** 当前平滑段的目标权重。 */
	float AimRigBlendTargetAlpha = 1.0f;

	/** 当前平滑段已经推进的时间。 */
	float AimRigBlendElapsedTime = 0.0f;
};
