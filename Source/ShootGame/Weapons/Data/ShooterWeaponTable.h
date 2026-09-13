// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDataTable;
struct FShooterWeaponConfigRow;

/**
 * 武器模板表的集中解析入口。
 *
 * 约束（单表武器配置纠偏小计划 4 / 5）：
 * - DT_WeaponData 是唯一武器模板数据源，不得再引入第二套 Registry 或 Primary Asset 映射；
 * - 本命名空间是 RowName → ConfigRow 的唯一解析实现，生产代码不得自行 GetRow；
 * - 表路径固定；运行时只在 WeaponRuntimeSubsystem 启动导入时查表，Pickup / NPC 只保存
 *   WeaponId，Inventory / Equipment / WeaponActor / GAS 不再访问 DataTable。
 */
namespace ShooterWeaponTable
{
	/** 唯一武器模板表资产路径。 */
	SHOOTGAME_API const TCHAR* GetWeaponTablePath();

	/** 解析默认武器模板表；资产缺失或类型不符时返回 nullptr。 */
	SHOOTGAME_API UDataTable* ResolveWeaponTable();

	/**
	 * 集中解析入口：RowName → ConfigRow。
	 * 表为空、行名为空或行缺失统一返回 nullptr，调用方必须 fail closed。
	 */
	SHOOTGAME_API const FShooterWeaponConfigRow* FindWeaponRow(const UDataTable* WeaponTable, FName WeaponId);

	/** 授予前的最小合法配置校验：ActorClass 是具体 AShooterWeapon 子类且弹匣容量合法。 */
	SHOOTGAME_API bool IsRowValidForGrant(const FShooterWeaponConfigRow* Row);
}
