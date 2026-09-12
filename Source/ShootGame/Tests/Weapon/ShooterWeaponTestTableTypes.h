// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "UObject/Package.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/ShooterWeaponConfigRow.h"
#include "Weapons/ShooterWeaponRuntimeSubsystem.h"

/**
 * 测试用武器运行时配置助手。
 *
 * S3 起授予链为：注入瞬态表到 WeaponRuntimeSubsystem → 启动导入 →
 * Runtime.AcquireWeapon → Inventory.AddWeapon。
 * 测试因此显式构造一张瞬态武器表并注入子系统：
 * - 不修改任何磁盘资产；
 * - 不依赖生产表的行内容与行名；
 * - 行数据默认从测试武器类的当前配置导出，只在需要时覆盖具体字段。
 */

/** 判断给定表是否为测试助手创建的瞬态武器表。 */
inline bool IsTestWeaponTable(const UDataTable* Table)
{
	return Table &&
		Table->GetOutermost() == GetTransientPackage() &&
		Table->GetRowStruct() == FShooterWeaponConfigRow::StaticStruct();
}

/**
 * 返回测试 World 已注入的瞬态武器表；没有时创建一张、注入并执行启动导入。
 * 同一 World 内幂等（重复调用返回同一张表）。
 */
inline UDataTable* GetOrInjectRuntimeTestTable(UWorld* World)
{
	UShooterWeaponRuntimeSubsystem* Runtime = World
		? World->GetSubsystem<UShooterWeaponRuntimeSubsystem>()
		: nullptr;
	if (!Runtime)
	{
		return nullptr;
	}

	if (UDataTable* Existing = Runtime->GetWeaponTableOverride())
	{
		if (IsTestWeaponTable(Existing))
		{
			return Existing;
		}
	}

	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
	Table->RowStruct = FShooterWeaponConfigRow::StaticStruct();
	Runtime->SetWeaponTableOverride(Table);
	Runtime->InitializeWeaponRuntime();
	return Table;
}

/** 追加上表中的一行并返回行名；行名按表内顺序生成，保证同一测试内唯一。 */
inline FName AddTestWeaponRow(UDataTable* Table, const FShooterWeaponConfigRow& Row)
{
	if (!Table)
	{
		return NAME_None;
	}

	const FName RowName(*FString::Printf(TEXT("TestWeapon_%d"), Table->GetRowNames().Num()));
	Table->AddRow(RowName, Row);
	return RowName;
}

/** 从武器类当前配置构造测试行，并覆盖弹药声明与预热数量。 */
inline FShooterWeaponConfigRow MakeTestWeaponRow(
	TSubclassOf<AShooterWeapon> WeaponActorClass,
	int32 MagazineSize = 10,
	int32 InitialReserveAmmo = -1,
	int32 InitialPoolSize = 1)
{
	FShooterWeaponConfigRow Row;
	if (const AShooterWeapon* WeaponDefaults = WeaponActorClass
		? WeaponActorClass->GetDefaultObject<AShooterWeapon>()
		: nullptr)
	{
		Row = WeaponDefaults->CaptureWeaponConfigRow();
	}

	Row.WeaponActorClass = WeaponActorClass;
	Row.MagazineSize = MagazineSize;
	Row.InitialReserveAmmo = InitialReserveAmmo;
	Row.InitialPoolSize = InitialPoolSize;
	return Row;
}

/**
 * 测试授予：注入瞬态表（幂等）→ 追加一行 → 重建快照（保留已租出实体）→
 * Runtime.Acquire → Inventory.AddWeapon。
 * 返回授予成功的 WeaponActor；失败返回 nullptr，具体结果通过 OutResult 返回。
 * 成功后武器处于 Holstered 且已入背包；装备由调用方通过 Equipment.EquipWeapon(Weapon) 提交。
 */
inline AShooterWeapon* GrantTestWeapon(
	UWorld* World,
	UShooterInventoryComponent* Inventory,
	TSubclassOf<AShooterWeapon> WeaponActorClass,
	int32 MagazineSize = 10,
	int32 InitialReserveAmmo = -1,
	EShooterInventoryAddResult* OutResult = nullptr)
{
	if (OutResult)
	{
		*OutResult = EShooterInventoryAddResult::NotAuthoritative;
	}

	UShooterWeaponRuntimeSubsystem* Runtime = World
		? World->GetSubsystem<UShooterWeaponRuntimeSubsystem>()
		: nullptr;
	if (!World || !Inventory || !Runtime || !WeaponActorClass)
	{
		return nullptr;
	}

	UDataTable* Table = GetOrInjectRuntimeTestTable(World);
	if (!Table)
	{
		return nullptr;
	}

	const FName RowName = AddTestWeaponRow(
		Table,
		MakeTestWeaponRow(WeaponActorClass, MagazineSize, InitialReserveAmmo));
	// 追加行后重建快照；已授予（租出中）的武器跨重导入存活。
	Runtime->InitializeWeaponRuntimeForTest();

	AShooterWeapon* Weapon = Runtime->AcquireWeapon(RowName, Inventory->GetOwner(), nullptr);
	if (!Weapon)
	{
		if (OutResult)
		{
			*OutResult = EShooterInventoryAddResult::InvalidWeapon;
		}
		return nullptr;
	}

	const EShooterInventoryAddResult Result = Inventory->AddWeapon(Weapon);
	if (OutResult)
	{
		*OutResult = Result;
	}
	return Result == EShooterInventoryAddResult::Added ? Weapon : nullptr;
}
