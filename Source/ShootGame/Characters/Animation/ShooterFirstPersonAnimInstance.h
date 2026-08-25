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

protected:
	virtual void UpdateShooterAnimationData(float DeltaSeconds) override;

private:
	/** 无本地控制 / 无 Mesh / 死亡 / 无武器时清空专用快照。 */
	void RefreshFirstPersonAnimationData();

	/** 清空第一人称专用值，保留公共快照的采集规则。 */
	void ClearFirstPersonAnimationData();
};
