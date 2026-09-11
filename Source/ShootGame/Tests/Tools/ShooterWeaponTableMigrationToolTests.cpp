// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Engine/DataTable.h"
#include "Misc/PackageName.h"
#include "ShootGame.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Weapons/ShooterProjectile.h"
#include "Weapons/ShooterProjectileFireBehavior.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/ShooterWeaponConfigRow.h"
#include "Weapons/ShooterWeaponTable.h"

namespace ShooterWeaponTableMigration
{
	/**
	 * 一次性迁移清单：行名、武器蓝图类路径与 Pickup 预览网格。
	 *
	 * 行名与武器类来自纠偏前 DT_WeaponData 的四行；PickupMesh 的现值来自 C0 基线导出
	 * （Saved/Automation/WeaponConfigBaseline.json 的 rows[].staticMesh）。其余可表格化字段
	 * 由武器蓝图 CDO 现场导出，避免在源码里重复维护一份配置快照。
	 */
	struct FWeaponRowMigration
	{
		const TCHAR* RowName;
		const TCHAR* WeaponClassPath;
		const TCHAR* PickupMeshPath;
	};

	const FWeaponRowMigration WeaponRowMigrations[] = {
		{
			TEXT("Rifle"),
			TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C"),
			TEXT("/Game/Weapons/Rifle/Meshes/SM_Rifle.SM_Rifle"),
		},
		{
			TEXT("Pistol"),
			TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Pistol.BP_ShooterWeapon_Pistol_C"),
			TEXT("/Game/Weapons/Pistol/Meshes/SM_Pistol.SM_Pistol"),
		},
		{
			TEXT("AWP"),
			TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_AWP.BP_ShooterWeapon_AWP_C"),
			TEXT("/Game/Weapons/AWP/Meshes/AWP_Sniper_Rifle.AWP_Sniper_Rifle"),
		},
		{
			TEXT("GrenadeLauncher"),
			TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_GrenadeLauncher.BP_ShooterWeapon_GrenadeLauncher_C"),
			TEXT("/Game/Weapons/GrenadeLauncher/Meshes/SM_GrenadeLauncher.SM_GrenadeLauncher"),
		},
	};

	/** 保存资产包到磁盘的统一入口。 */
	bool SaveAssetPackage(FAutomationTestBase& Test, UPackage* Package, UObject* Asset)
	{
		const FString FileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		return Test.TestTrue(
			TEXT("Weapon DataTable package saved"),
			UPackage::SavePackage(Package, Asset, *FileName, SaveArgs));
	}

	/** 从武器蓝图 CDO 构造完整模板行；Pickup 预览网格由迁移清单提供。 */
	FShooterWeaponConfigRow BuildRowFromWeaponClass(
		FAutomationTestBase& Test,
		const FWeaponRowMigration& Migration,
		bool& bOutValid)
	{
		bOutValid = false;

		UClass* WeaponClass = LoadObject<UClass>(nullptr, Migration.WeaponClassPath);
		if (!Test.TestNotNull(
			FString::Printf(TEXT("Weapon class %s loaded"), Migration.RowName).GetCharArray().GetData(),
			WeaponClass))
		{
			return FShooterWeaponConfigRow();
		}

		const AShooterWeapon* WeaponDefaults = WeaponClass->GetDefaultObject<AShooterWeapon>();
		if (!Test.TestNotNull(
			FString::Printf(TEXT("Weapon CDO %s resolved"), Migration.RowName).GetCharArray().GetData(),
			WeaponDefaults))
		{
			return FShooterWeaponConfigRow();
		}

		// 复用 WeaponActor 的配置导出入口：迁移与测试使用同一份字段清单，避免漏字段。
		FShooterWeaponConfigRow Row = WeaponDefaults->CaptureWeaponConfigRow();
		Row.WeaponActorClass = WeaponClass;
		Row.PickupMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(Migration.PickupMeshPath));
		// 第一版唯一正式开火行为；行为实例无状态，弹丸类保存在同一行。
		Row.FireBehaviorClass = UShooterProjectileFireBehavior::StaticClass();

		bOutValid = true;
		return Row;
	}

	/** 判断表内四行是否已经是目标配置；用于幂等短路，避免重复写盘。 */
	bool RowsAlreadyMigrated(
		FAutomationTestBase& Test,
		const UDataTable* WeaponTable,
		TArray<FShooterWeaponConfigRow>& OutRows)
	{
		if (WeaponTable->GetRowStruct() != FShooterWeaponConfigRow::StaticStruct())
		{
			return false;
		}

		if (WeaponTable->GetRowNames().Num() != UE_ARRAY_COUNT(WeaponRowMigrations))
		{
			return false;
		}

		for (const FWeaponRowMigration& Migration : WeaponRowMigrations)
		{
			const FShooterWeaponConfigRow* Existing = ShooterWeaponTable::FindWeaponRow(
				WeaponTable,
				FName(Migration.RowName));
			if (!Existing ||
				!ShooterWeaponTable::IsRowValidForGrant(Existing) ||
				!Existing->FireBehaviorClass ||
				!Existing->ProjectileClass ||
				Existing->PickupMesh.ToSoftObjectPath() != FSoftObjectPath(Migration.PickupMeshPath) ||
				Existing->WeaponActorClass != LoadObject<UClass>(nullptr, Migration.WeaponClassPath))
			{
				return false;
			}
		}

		OutRows.Reset();
		return true;
	}
}

/**
 * 一次性迁移工具：把四个正式武器在蓝图 CDO 中的可表格化配置写入 DT_WeaponData 完整行，
 * 并把 DataTable 的 RowStruct 切换到 FShooterWeaponConfigRow。
 *
 * 约束（单表武器配置纠偏小计划 C1 / C3）：
 * - 只通过 Unreal Editor 资产系统修改并保存 DataTable，不直接写 .uasset；
 * - EmptyTable 必须先于 RowStruct 赋值执行，否则会按新结构释放旧结构分配的行内存；
 * - AddRow 使用带类型的重载，避免 RowStruct 尚未设置时的 checkf；
 * - 本工具是一次性迁移入口，纠偏收口后随旧 Definition 迁移工具一并删除。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponTableMigrateRowsTool,
	"ShootGame.Tools.WeaponConfig.MigrateTableRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponTableMigrateRowsTool::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponTableMigration;

	UDataTable* WeaponTable = LoadObject<UDataTable>(
		nullptr,
		ShooterWeaponTable::GetWeaponTablePath());
	if (!TestNotNull(TEXT("Weapon DataTable loaded"), WeaponTable))
	{
		return false;
	}

	// 幂等短路：磁盘内容已是目标配置时直接通过；运行中的编辑器可能持有该文件句柄。
	TArray<FShooterWeaponConfigRow> UnusedRows;
	if (RowsAlreadyMigrated(*this, WeaponTable, UnusedRows))
	{
		UE_LOG(LogShootGame, Display, TEXT("Weapon table migration skipped: rows already migrated"));
		return true;
	}

	// 迁移动手前先确认现有行名都在迁移清单内，避免静默丢掉未知行。
	const TArray<FName> ExistingRowNames = WeaponTable->GetRowNames();
	for (const FName& ExistingRowName : ExistingRowNames)
	{
		bool bKnownRow = false;
		for (const FWeaponRowMigration& Migration : WeaponRowMigrations)
		{
			bKnownRow |= ExistingRowName == FName(Migration.RowName);
		}

		if (!bKnownRow)
		{
			AddError(FString::Printf(
				TEXT("Weapon DataTable contains unmigrated row %s; extend the migration list first"),
				*ExistingRowName.ToString()));
			return false;
		}
	}

	TArray<FShooterWeaponConfigRow> NewRows;
	TArray<FName> NewRowNames;
	for (const FWeaponRowMigration& Migration : WeaponRowMigrations)
	{
		bool bRowValid = false;
		FShooterWeaponConfigRow Row = BuildRowFromWeaponClass(*this, Migration, bRowValid);
		if (!bRowValid)
		{
			return false;
		}

		if (!TestTrue(
			FString::Printf(TEXT("Migrated row %s is valid for grant"), Migration.RowName).GetCharArray().GetData(),
			ShooterWeaponTable::IsRowValidForGrant(&Row)))
		{
			return false;
		}

		NewRowNames.Add(FName(Migration.RowName));
		NewRows.Add(MoveTemp(Row));
	}

	// 行结构切换：EmptyTable 使用当前 RowStruct 释放旧行，必须在赋值之前执行。
	WeaponTable->Modify();
	WeaponTable->EmptyTable();
	WeaponTable->RowStruct = FShooterWeaponConfigRow::StaticStruct();
	for (int32 RowIndex = 0; RowIndex < NewRows.Num(); ++RowIndex)
	{
		WeaponTable->AddRow(NewRowNames[RowIndex], NewRows[RowIndex]);
	}

	if (!TestTrue(
		TEXT("Weapon DataTable uses FShooterWeaponConfigRow"),
		WeaponTable->GetRowStruct() == FShooterWeaponConfigRow::StaticStruct()))
	{
		return false;
	}

	if (!TestEqual(
		TEXT("Weapon DataTable row count matches the migration list"),
		WeaponTable->GetRowNames().Num(),
		static_cast<int32>(UE_ARRAY_COUNT(WeaponRowMigrations))))
	{
		return false;
	}

	// 逐行读回校验：写入结果必须能被集中解析入口重新解析。
	for (const FWeaponRowMigration& Migration : WeaponRowMigrations)
	{
		const FShooterWeaponConfigRow* Written = ShooterWeaponTable::FindWeaponRow(
			WeaponTable,
			FName(Migration.RowName));
		if (!TestNotNull(
			FString::Printf(TEXT("Migrated row %s resolves after write"), Migration.RowName).GetCharArray().GetData(),
			Written))
		{
			return false;
		}

		TestEqual(
			FString::Printf(TEXT("Migrated row %s keeps WeaponActorClass"), Migration.RowName).GetCharArray().GetData(),
			Written->WeaponActorClass.Get(),
			LoadObject<UClass>(nullptr, Migration.WeaponClassPath));
		TestEqual(
			FString::Printf(TEXT("Migrated row %s keeps PickupMesh"), Migration.RowName).GetCharArray().GetData(),
			Written->PickupMesh.ToSoftObjectPath(),
			FSoftObjectPath(Migration.PickupMeshPath));
		TestNotNull(
			FString::Printf(TEXT("Migrated row %s configures a fire behavior class"), Migration.RowName).GetCharArray().GetData(),
			Written->FireBehaviorClass.Get());
		TestNotNull(
			FString::Printf(TEXT("Migrated row %s configures a projectile class"), Migration.RowName).GetCharArray().GetData(),
			Written->ProjectileClass.Get());
	}

	WeaponTable->MarkPackageDirty();
	return SaveAssetPackage(*this, WeaponTable->GetOutermost(), WeaponTable);
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
