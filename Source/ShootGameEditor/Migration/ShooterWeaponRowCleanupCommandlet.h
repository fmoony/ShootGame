// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "ShooterWeaponRowCleanupCommandlet.generated.h"

/**
 * 限定清理：在删除 FShooterWeaponConfigRow::ThirdPersonAnimInstanceClass 之后，
 * 用反射导出 DT_WeaponData 全部行的全部列，做前后一致性对照并重存资产。
 *
 * 只有显式模式才动作：
 * - `-Dump -Out=<file>`：字段仍然存在时导出迁移基线。
 * - `-Apply -Baseline=<file> -Out=<file>`：逐列对照后重存 DataTable。
 * - `-Verify -Baseline=<file> -Out=<file>`：在全新进程里从磁盘重新加载后对照，不保存。
 *
 * 对照失败时不保存任何资产；不使用 FUnknownPropertyTree 相关的编辑器状态跟踪。
 */
UCLASS()
class UShooterWeaponRowCleanupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UShooterWeaponRowCleanupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
