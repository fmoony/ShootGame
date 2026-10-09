// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "ShooterFPPlayerMigrationCommandlet.generated.h"

/**
 * 方案 E 限定迁移：从初始第一人称主图派生固定的 ABP_FP_Player。
 *
 * - 复制 ABP_FP_Copy，继承它的 CopyPose 设置与 Skeleton；
 * - 重定位到 UShooterFirstPersonAnimInstance，使 C++ Clearance 生效；
 * - 只保留 Output Pose ← CopyPose 这一条有效链，不迁移断开的旧节点与 Control Rig；
 * - 把角色蓝图的 PlayerFirstPersonAnimInstanceClass 指向新主图；
 * - 不修改 FP Mesh 的默认 AnimClass（出生仍使用 ABP_FP_Copy 及其 Warp）。
 *
 * 只有显式 -Apply 才会创建并保存资产。
 */
UCLASS()
class UShooterFPPlayerMigrationCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UShooterFPPlayerMigrationCommandlet();
	virtual int32 Main(const FString& Params) override;
};
