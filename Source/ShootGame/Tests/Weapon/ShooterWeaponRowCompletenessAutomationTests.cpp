// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "ShooterProjectile.h"
#include "ShooterWeapon.h"
#include "ShooterWeaponConfigRow.h"
#include "ShooterWeaponFireBehavior.h"
#include "ShooterWeaponTable.h"

namespace ShooterWeaponRowCompleteness
{
	/** 四个正式武器行；行名就是配置键。 */
	const TCHAR* ProductionRowNames[] = {
		TEXT("Rifle"),
		TEXT("Pistol"),
		TEXT("AWP"),
		TEXT("GrenadeLauncher"),
	};

	/** 校验单个正式行是否完整且可解析。 */
	bool TestRow(FAutomationTestBase& Test, const TCHAR* RowName)
	{
		UDataTable* WeaponTable = ShooterWeaponTable::ResolveWeaponTable();
		if (!Test.TestNotNull(TEXT("Weapon table resolves as FShooterWeaponConfigRow"), WeaponTable))
		{
			return false;
		}

		const FShooterWeaponConfigRow* Row = ShooterWeaponTable::FindWeaponRow(WeaponTable, FName(RowName));
		if (!Test.TestNotNull(FString::Printf(TEXT("Row %s resolves"), RowName), Row))
		{
			return false;
		}

		bool bValid = true;
		bValid &= Test.TestTrue(FString::Printf(TEXT("Row %s is valid for grant"), RowName), ShooterWeaponTable::IsRowValidForGrant(Row));

		// 数值区间：非法值必须在数据层就被拒绝，而不是留到运行时。
		bValid &= Test.TestTrue(FString::Printf(TEXT("Row %s magazine size is positive"), RowName),
			Row->MagazineSize > 0 && Row->MagazineSize <= 100);
		bValid &= Test.TestTrue(FString::Printf(TEXT("Row %s reserve declaration is >= -1"), RowName),
			Row->InitialReserveAmmo >= -1);
		bValid &= Test.TestTrue(FString::Printf(TEXT("Row %s refire rate is within range"), RowName),
			Row->RefireRate > 0.0f && Row->RefireRate <= 5.0f);
		bValid &= Test.TestTrue(FString::Printf(TEXT("Row %s aim variance is within range"), RowName),
			Row->AimVariance >= 0.0f && Row->AimVariance <= 90.0f);
		bValid &= Test.TestTrue(FString::Printf(TEXT("Row %s muzzle offset is non-negative"), RowName),
			Row->MuzzleOffset >= 0.0f);
		bValid &= Test.TestTrue(FString::Printf(TEXT("Row %s reload duration is positive"), RowName),
			Row->ReloadDuration > 0.0f);
		bValid &= Test.TestTrue(FString::Printf(TEXT("Row %s equip duration is non-negative"), RowName),
			Row->EquipDuration >= 0.0f);
		// FiringRecoil 是俯仰输入增量，允许负值（负值表示下压）；这里只校验约束区间。
		bValid &= Test.TestTrue(
			FString::Printf(TEXT("Row %s firing recoil is within the declared clamp range"), RowName),
			Row->FiringRecoil >= -100.0f && Row->FiringRecoil <= 100.0f);
		bValid &= Test.TestTrue(FString::Printf(TEXT("Row %s declares a muzzle socket"), RowName),
			!Row->MuzzleSocketName.IsNone());

		// 资源引用：类引用必须是具体子类，网格/动画/表现资源必须真实存在。
		bValid &= Test.TestTrue(FString::Printf(TEXT("Row %s WeaponActorClass derives from AShooterWeapon"), RowName),
			Row->WeaponActorClass && Row->WeaponActorClass->IsChildOf<AShooterWeapon>());
		bValid &= Test.TestTrue(
			FString::Printf(TEXT("Row %s FireBehaviorClass derives from UShooterWeaponFireBehavior"), RowName),
			Row->FireBehaviorClass && Row->FireBehaviorClass->IsChildOf<UShooterWeaponFireBehavior>());
		bValid &= Test.TestTrue(
			FString::Printf(TEXT("Row %s ProjectileClass derives from AShooterProjectile"), RowName),
			Row->ProjectileClass && Row->ProjectileClass->IsChildOf<AShooterProjectile>());
		bValid &= Test.TestTrue(
			FString::Printf(TEXT("Row %s FirstPersonAnimInstanceClass derives from UAnimInstance"), RowName),
			Row->FirstPersonAnimInstanceClass && Row->FirstPersonAnimInstanceClass->IsChildOf<UAnimInstance>());
		bValid &= Test.TestTrue(
			FString::Printf(TEXT("Row %s ThirdPersonAnimInstanceClass derives from UAnimInstance"), RowName),
			Row->ThirdPersonAnimInstanceClass && Row->ThirdPersonAnimInstanceClass->IsChildOf<UAnimInstance>());

		bValid &= Test.TestNotNull(FString::Printf(TEXT("Row %s first-person mesh resolves"), RowName),
			Row->FirstPersonMesh.LoadSynchronous());
		bValid &= Test.TestNotNull(FString::Printf(TEXT("Row %s third-person mesh resolves"), RowName),
			Row->ThirdPersonMesh.LoadSynchronous());
		bValid &= Test.TestNotNull(FString::Printf(TEXT("Row %s pickup mesh resolves"), RowName),
			Row->PickupMesh.LoadSynchronous());
		bValid &= Test.TestNotNull(FString::Printf(TEXT("Row %s firing montage resolves"), RowName),
			Row->FiringMontage.Get());
		bValid &= Test.TestNotNull(FString::Printf(TEXT("Row %s fire sound resolves"), RowName), Row->FireSound.Get());
		bValid &= Test.TestNotNull(FString::Printf(TEXT("Row %s reload magazine-out sound resolves"), RowName),
			Row->ReloadMagazineOutSound.Get());
		bValid &= Test.TestNotNull(FString::Printf(TEXT("Row %s reload magazine-in sound resolves"), RowName),
			Row->ReloadMagazineInSound.Get());
		bValid &= Test.TestNotNull(FString::Printf(TEXT("Row %s reload cocking sound resolves"), RowName),
			Row->ReloadCockingSound.Get());

		// Socket 是 IK 与权威枪口的共同前提：行配置的 Muzzle / 握把必须真实存在于第三人称网格。
		const USkeletalMesh* ThirdPersonMesh = Row->ThirdPersonMesh.LoadSynchronous();
		bValid &= Test.TestTrue(
			FString::Printf(TEXT("Row %s third-person mesh owns the configured muzzle socket"), RowName),
			ThirdPersonMesh && ThirdPersonMesh->FindSocket(Row->MuzzleSocketName) != nullptr);
		if (!Row->LeftHandGripSocketName.IsNone())
		{
			bValid &= Test.TestTrue(
				FString::Printf(TEXT("Row %s third-person mesh owns the configured left hand grip"), RowName),
				ThirdPersonMesh && ThirdPersonMesh->FindSocket(Row->LeftHandGripSocketName) != nullptr);
		}

		return bValid;
	}
}

/**
 * 配置完整性数据测试（纠偏 C0）：
 * DT_WeaponData 的每一行正式武器都必须完整、可解析，并且所有引用资源真实存在。
 * 该测试是“单表武器配置”唯一权威来源的数据层守卫，新增武器行时必须同步满足。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponRowCompletenessTest, "ShootGame.Weapon.ProductionRowsResolvable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponRowCompletenessTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponRowCompleteness;

	if (!TestNotNull(TEXT("Weapon table asset resolves"), ShooterWeaponTable::ResolveWeaponTable()))
	{
		return false;
	}

	bool bSucceeded = true;
	for (const TCHAR* RowName : ProductionRowNames)
	{
		bSucceeded &= TestRow(*this, RowName);
	}

	// 行名之外不应存在“半配置”的正式行：正式行数量必须与清单一致。
	const UDataTable* WeaponTable = ShooterWeaponTable::ResolveWeaponTable();
	if (WeaponTable)
	{
		TestEqual(TEXT("Weapon table contains exactly the declared production rows"), WeaponTable->GetRowNames().Num(),
			static_cast<int32>(UE_ARRAY_COUNT(ProductionRowNames)));
	}

	return bSucceeded;
}

#endif // WITH_DEV_AUTOMATION_TESTS
