// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "ShooterRifleAnimDeriveCommandlet.generated.h"

/**
 * Rifle 动画派生：从 Mannequins 原始导入资源建立四份规范副本。
 *
 * 背景（见 Docs/Rifle新动画与WeaponAction预审查_20261010.md）：
 * 原始 RifleIdle / RifleAimToDown / PutBackRifleBehindShoulder 位于
 * /Game/Characters/Mannequins/Anims/Rifle，是外部导入资产，本工具不改动它们。
 * 阶段 A 需要在 Shooter 正式资产根下建立可命名引用的派生副本。
 *
 * - 原始资产只读：运行前后对源包做文件哈希与数据模型指纹比对；
 * - 裁切走 UE5.6 原生 IAnimationDataController::SetNumberOfFrames，是截断而非重采样；
 * - MM_Rifle_Draw 按时间镜像重写全部骨骼轨道，使 Drawing 状态可以正向播放抬枪动作；
 * - 目标已存在时拒绝覆盖，不使用任何删除或替换接口；
 * - 只有显式 -Apply 才会创建并保存资产，否则只做只读校验与计划输出。
 */
UCLASS()
class UShooterRifleAnimDeriveCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UShooterRifleAnimDeriveCommandlet();
	virtual int32 Main(const FString& Params) override;
};
