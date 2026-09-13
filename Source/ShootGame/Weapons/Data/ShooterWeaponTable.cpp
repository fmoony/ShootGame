// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterWeaponTable.h"

#include "Engine/DataTable.h"
#include "ShootGame.h"
#include "ShooterWeapon.h"
#include "ShooterWeaponConfigRow.h"

namespace ShooterWeaponTable
{
	const TCHAR* GetWeaponTablePath()
	{
		// 唯一武器模板库；仓库内所有武器配置都从这里解析，不提供第二处路径常量。
		return TEXT("/Game/Shooter/Data/DT_WeaponData");
	}

	UDataTable* ResolveWeaponTable()
	{
		const FString TablePath = GetWeaponTablePath();
		UDataTable* WeaponTable = LoadObject<UDataTable>(nullptr, *TablePath);
		if (!WeaponTable)
		{
			UE_LOG(LogShootGame, Warning, TEXT("Weapon table resolve failed: %s is missing"), *TablePath);
			return nullptr;
		}

		// 表结构不符时同样 fail closed，避免用错误的行结构解释武器配置。
		if (WeaponTable->GetRowStruct() != FShooterWeaponConfigRow::StaticStruct())
		{
			UE_LOG(LogShootGame, Warning, TEXT("Weapon table resolve failed: %s does not use FShooterWeaponConfigRow"), *TablePath);
			return nullptr;
		}

		return WeaponTable;
	}

	const FShooterWeaponConfigRow* FindWeaponRow(const UDataTable* WeaponTable, FName WeaponId)
	{
		if (!WeaponTable || WeaponId.IsNone())
		{
			return nullptr;
		}

		return WeaponTable->FindRow<FShooterWeaponConfigRow>(WeaponId, TEXT("ShooterWeaponTable"),
			// 缺失行由调用方决定拒绝语义，这里不重复刷屏。
			/*bWarnIfRowMissing=*/false);
	}

	bool IsRowValidForGrant(const FShooterWeaponConfigRow* Row)
	{
		if (!Row)
		{
			return false;
		}

		if (!Row->WeaponActorClass || !Row->WeaponActorClass->IsChildOf<AShooterWeapon>())
		{
			return false;
		}

		// 非法弹匣容量必须 fail closed，不能回落到 WeaponActor 默认值。
		if (Row->MagazineSize <= 0)
		{
			return false;
		}

		if (Row->InitialReserveAmmo < -1)
		{
			return false;
		}

		return true;
	}
}
