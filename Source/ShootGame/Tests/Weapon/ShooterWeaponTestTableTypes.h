// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "UObject/Package.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/ShooterWeaponConfigRow.h"

/**
 * 测试用武器模板表助手。
 *
 * 单表武器配置纠偏后，武器配置只来自 DT_WeaponData，授予入口只接受行名。
 * 测试因此显式构造一张瞬态武器模板表并注入被测 Inventory：
 * - 不修改任何磁盘资产；
 * - 不依赖生产表的行内容与行名；
 * - 行数据默认从测试武器类的当前配置导出，只在需要时覆盖具体字段，
 *   保持既有测试对 AnimClass / Socket / 时序 / 网格的期望不变。
 */

/** 判断给定表是否为测试助手创建的瞬态武器模板表。 */
inline bool IsTestWeaponTable(const UDataTable* Table)
{
	return Table &&
		Table->GetOutermost() == GetTransientPackage() &&
		Table->GetRowStruct() == FShooterWeaponConfigRow::StaticStruct();
}

/** 返回该 Inventory 已注入的测试武器表；没有时创建一张并注入。 */
inline UDataTable* GetOrCreateTestWeaponTable(UShooterInventoryComponent* Inventory)
{
	if (!Inventory)
	{
		return nullptr;
	}

	UDataTable* Existing = Inventory->GetWeaponTable();
	if (IsTestWeaponTable(Existing))
	{
		return Existing;
	}

	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
	Table->RowStruct = FShooterWeaponConfigRow::StaticStruct();
	Inventory->SetWeaponTable(Table);
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

/** 从武器类当前配置构造测试行，并覆盖弹药声明。 */
inline FShooterWeaponConfigRow MakeTestWeaponRow(
	TSubclassOf<AShooterWeapon> WeaponActorClass,
	int32 MagazineSize = 10,
	int32 InitialReserveAmmo = -1)
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
	return Row;
}

/**
 * 测试授予：向 Inventory 的测试表中追加一行并授予。
 * 返回该行行名；非 Added 结果返回 NAME_None，具体结果通过 OutResult 返回。
 */
inline FName GrantTestWeaponRow(
	UShooterInventoryComponent* Inventory,
	TSubclassOf<AShooterWeapon> WeaponActorClass,
	FGuid& OutInstanceId,
	int32 MagazineSize = 10,
	int32 InitialReserveAmmo = -1,
	EShooterInventoryAddResult* OutResult = nullptr)
{
	OutInstanceId = FGuid();
	UDataTable* Table = GetOrCreateTestWeaponTable(Inventory);
	if (!Table)
	{
		if (OutResult)
		{
			*OutResult = EShooterInventoryAddResult::NotAuthoritative;
		}
		return NAME_None;
	}

	const FName RowName = AddTestWeaponRow(
		Table,
		MakeTestWeaponRow(WeaponActorClass, MagazineSize, InitialReserveAmmo));
	const EShooterInventoryAddResult Result = Inventory->TryAddWeaponRow(RowName, OutInstanceId);
	if (OutResult)
	{
		*OutResult = Result;
	}

	return Result == EShooterInventoryAddResult::Added ? RowName : NAME_None;
}
