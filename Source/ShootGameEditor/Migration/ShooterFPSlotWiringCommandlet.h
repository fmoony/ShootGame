// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "ShooterFPSlotWiringCommandlet.generated.h"

/**
 * 第一人称主图 Slot 接线：ABP_FP_Player 从 CopyPose → Output
 * 改为 CopyPose → Slot('Arms') → Output。
 *
 * 背景：Owner 本地预测已经对 FP AnimInstance 播放 Fire Montage，但 FP 图里没有
 * 任何 Slot 节点，Montage 实例完整跑完却对姿态零贡献，第一人称火线动作只能等
 * 服务器确认后的 TP Montage 经 CopyPose 进来。
 *
 * - 只新增一个名为 Arms 的 Slot 节点，不改父类、不改 CopyPose 设置、不改其他图；
 * - 幂等：已存在同名 Slot 时只校验并修复连线，不重复添加节点；
 * - 只有显式 -Apply 才会写入并保存资产。
 */
UCLASS()
class UShooterFPSlotWiringCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UShooterFPSlotWiringCommandlet();
	virtual int32 Main(const FString& Params) override;
};
