// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Components/SkeletalMeshComponent.h"
#include "AI/ShooterNPC.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Misc/PackageName.h"
#include "ShootGame.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/ShooterWeaponTable.h"

namespace ShooterWeaponBlueprintDefaults
{
	/**
	 * 需要从武器蓝图默认值中移除的“可表格化”字段。
	 * 这些字段在单表纠偏后只由 DT_WeaponData 行决定，蓝图默认值不再是配置来源。
	 */
	const TCHAR* const RowDrivenPropertyNames[] = {
		TEXT("ProjectileClass"),
		TEXT("MagazineSize"),
		TEXT("InitialReserveAmmo"),
		TEXT("FiringMontage"),
		TEXT("MuzzleFlash"),
		TEXT("FireSound"),
		TEXT("ReloadMagazineOutSound"),
		TEXT("ReloadMagazineInSound"),
		TEXT("ReloadCockingSound"),
		TEXT("FirstPersonAnimInstanceClass"),
		TEXT("ThirdPersonAnimInstanceClass"),
		TEXT("AimVariance"),
		TEXT("FiringRecoil"),
		TEXT("MuzzleSocketName"),
		TEXT("ThirdPersonLeftHandGripSocketName"),
		TEXT("MuzzleOffset"),
		TEXT("bFullAuto"),
		TEXT("RefireRate"),
		TEXT("ReloadDuration"),
		TEXT("EquipDuration"),
		TEXT("ShotLoudness"),
		TEXT("ShotNoiseRange"),
		TEXT("ShotNoiseTag"),
		TEXT("FirstPersonCompositionDrop"),
	};

	/** 需要清空网格资产的组件访问器。 */
	const TCHAR* const WeaponBlueprintPaths[] = {
		TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeaponBase.BP_ShooterWeaponBase"),
		TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle"),
		TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Pistol.BP_ShooterWeapon_Pistol"),
		TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_AWP.BP_ShooterWeapon_AWP"),
		TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_GrenadeLauncher.BP_ShooterWeapon_GrenadeLauncher"),
	};

	/** 把武器类某个命名属性的原生默认值写入目标 CDO。 */
	bool ResetPropertyToNativeDefault(
		FAutomationTestBase& Test,
		UClass* GeneratedClass,
		UObject* TargetCdo,
		const UObject* NativeCdo,
		const TCHAR* PropertyName)
	{
		const FProperty* TargetProperty = FindFProperty<FProperty>(GeneratedClass, PropertyName);
		const FProperty* NativeProperty = FindFProperty<FProperty>(
			AShooterWeapon::StaticClass(),
			PropertyName);
		if (!TargetProperty || !NativeProperty)
		{
			return Test.TestNotNull(
				FString::Printf(TEXT("Property %s exists on both weapon class and CDO"), PropertyName).GetCharArray().GetData(),
				TargetProperty);
		}

		if (TargetProperty->Identical_InContainer(TargetCdo, NativeCdo))
		{
			// 已经是原生默认值：无需写入，序列化时本就不会产生覆盖。
			return true;
		}

		TargetProperty->CopyCompleteValue_InContainer(TargetCdo, NativeCdo);
		return true;
	}
}

/**
 * 一次性收口工具：移除武器蓝图默认值中已由 DT_WeaponData 行驱动的重复字段。
 *
 * 约束（单表武器配置纠偏小计划 C3）：
 * - 只通过 Unreal Editor 资产系统修改并保存蓝图包，不直接写 .uasset；
 * - 写入原生默认值后，这些字段在蓝图 CDO 序列化时不再产生覆盖；
 * - 第一/第三人称网格组件模板同样清空，网格改由行在绑定时同步加载；
 * - 脚本写盘后立即重新加载资产并回读校验，确认覆盖确实已经消失。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponBlueprintDefaultsTool,
	"ShootGame.Tools.WeaponConfig.ClearBlueprintDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponBlueprintDefaultsTool::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponBlueprintDefaults;

	const AShooterWeapon* NativeDefaults =
		AShooterWeapon::StaticClass()->GetDefaultObject<AShooterWeapon>();
	if (!TestNotNull(TEXT("Native AShooterWeapon defaults resolved"), NativeDefaults))
	{
		return false;
	}

	for (const TCHAR* BlueprintPath : WeaponBlueprintPaths)
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, BlueprintPath);
		if (!TestNotNull(
			FString::Printf(TEXT("Weapon blueprint %s loaded"), BlueprintPath).GetCharArray().GetData(),
			Blueprint))
		{
			return false;
		}

		UClass* GeneratedClass = Blueprint->GeneratedClass;
		if (!TestNotNull(
			FString::Printf(TEXT("Weapon blueprint %s has a generated class"), BlueprintPath).GetCharArray().GetData(),
			GeneratedClass))
		{
			return false;
		}

		AShooterWeapon* WeaponCdo = GeneratedClass->GetDefaultObject<AShooterWeapon>();
		if (!TestNotNull(
			FString::Printf(TEXT("Weapon blueprint %s CDO resolved"), BlueprintPath).GetCharArray().GetData(),
			WeaponCdo))
		{
			return false;
		}

		WeaponCdo->Modify();
		for (const TCHAR* PropertyName : RowDrivenPropertyNames)
		{
			if (!ResetPropertyToNativeDefault(*this, GeneratedClass, WeaponCdo, NativeDefaults, PropertyName))
			{
				return false;
			}
		}

		// 网格组件模板同样由行提供：清空后绑定行配置时再同步加载。
		USkeletalMeshComponent* FirstPersonMesh = WeaponCdo->GetFirstPersonMesh();
		USkeletalMeshComponent* ThirdPersonMesh = WeaponCdo->GetThirdPersonMesh();
		if (!TestNotNull(
			FString::Printf(TEXT("Weapon blueprint %s exposes both mesh components"), BlueprintPath).GetCharArray().GetData(),
			FirstPersonMesh && ThirdPersonMesh ? FirstPersonMesh : nullptr))
		{
			return false;
		}
		FirstPersonMesh->Modify();
		ThirdPersonMesh->Modify();
		FirstPersonMesh->SetSkeletalMeshAsset(
			NativeDefaults->GetFirstPersonMesh()->GetSkeletalMeshAsset());
		ThirdPersonMesh->SetSkeletalMeshAsset(
			NativeDefaults->GetThirdPersonMesh()->GetSkeletalMeshAsset());

		UPackage* Package = Blueprint->GetOutermost();
		Package->MarkPackageDirty();

		const FString FileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!TestTrue(
			FString::Printf(TEXT("Weapon blueprint %s saved"), BlueprintPath).GetCharArray().GetData(),
			UPackage::SavePackage(Package, Blueprint, *FileName, SaveArgs)))
		{
			return false;
		}

		// 重新加载并回读：确认蓝图 CDO 上不再保留行驱动字段的覆盖值。
		ResetLoaders(Package);
		UBlueprint* ReloadedBlueprint = LoadObject<UBlueprint>(nullptr, BlueprintPath);
		const AShooterWeapon* ReloadedCdo = ReloadedBlueprint && ReloadedBlueprint->GeneratedClass
			? ReloadedBlueprint->GeneratedClass->GetDefaultObject<AShooterWeapon>()
			: nullptr;
		if (!TestNotNull(
			FString::Printf(TEXT("Weapon blueprint %s reloads"), BlueprintPath).GetCharArray().GetData(),
			ReloadedCdo))
		{
			return false;
		}

		for (const TCHAR* PropertyName : RowDrivenPropertyNames)
		{
			const FProperty* Property = FindFProperty<FProperty>(
				ReloadedCdo->GetClass(),
				PropertyName);
			TestTrue(
				FString::Printf(
					TEXT("Weapon blueprint %s no longer overrides %s"),
					BlueprintPath,
					PropertyName).GetCharArray().GetData(),
				Property && Property->Identical_InContainer(ReloadedCdo, NativeDefaults));
		}

		TestTrue(
			FString::Printf(
				TEXT("Weapon blueprint %s no longer overrides the first-person mesh"),
				BlueprintPath).GetCharArray().GetData(),
			ReloadedCdo->GetFirstPersonMesh() &&
			ReloadedCdo->GetFirstPersonMesh()->GetSkeletalMeshAsset() ==
				NativeDefaults->GetFirstPersonMesh()->GetSkeletalMeshAsset());
		TestTrue(
			FString::Printf(
				TEXT("Weapon blueprint %s no longer overrides the third-person mesh"),
				BlueprintPath).GetCharArray().GetData(),
			ReloadedCdo->GetThirdPersonMesh() &&
			ReloadedCdo->GetThirdPersonMesh()->GetSkeletalMeshAsset() ==
				NativeDefaults->GetThirdPersonMesh()->GetSkeletalMeshAsset());

		UE_LOG(
			LogShootGame,
			Display,
			TEXT("WEAPONDEFAULTS|cleared|%s"),
			BlueprintPath);
	}

	return true;
}

/**
 * 一次性收口工具：把 NPC 蓝图切换到武器模板行选择。
 *
 * 单表纠偏后武器蓝图默认值不再承载可表格化配置，NPC 若继续只用 WeaponClass
 * 会拿到空配置；因此把 BP_ShooterNPC 改为选择 DT_WeaponData 的 Rifle 行，
 * 并清空 WeaponClass（行已经决定 WeaponActorClass，不再需要第二处来源）。
 * 自动化测试用的 NPC 类仍可在运行时自行写入 WeaponClass，本工具不修改 C++ 默认值。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterNpcWeaponRowTool,
	"ShootGame.Tools.WeaponConfig.SetNpcWeaponRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterNpcWeaponRowTool::RunTest(const FString& Parameters)
{
	const TCHAR* NpcBlueprintPath = TEXT("/Game/Shooter/Blueprints/AI/BP_ShooterNPC.BP_ShooterNPC");
	const FName NpcWeaponRowName(TEXT("Rifle"));

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, NpcBlueprintPath);
	if (!TestNotNull(TEXT("NPC blueprint loaded"), Blueprint))
	{
		return false;
	}

	UClass* GeneratedClass = Blueprint->GeneratedClass;
	if (!TestNotNull(TEXT("NPC blueprint has a generated class"), GeneratedClass))
	{
		return false;
	}

	AShooterNPC* NpcCdo = GeneratedClass->GetDefaultObject<AShooterNPC>();
	if (!TestNotNull(TEXT("NPC CDO resolved"), NpcCdo))
	{
		return false;
	}

	NpcCdo->Modify();
	NpcCdo->SetWeaponRowName(NpcWeaponRowName);
	NpcCdo->SetWeaponClass(nullptr);

	UPackage* Package = Blueprint->GetOutermost();
	Package->MarkPackageDirty();

	const FString FileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	if (!TestTrue(
		TEXT("NPC blueprint saved"),
		UPackage::SavePackage(Package, Blueprint, *FileName, SaveArgs)))
	{
		return false;
	}

	// 重新加载并回读：确认 NPC 蓝图确实只通过武器模板行选择武器。
	ResetLoaders(Package);
	UBlueprint* ReloadedBlueprint = LoadObject<UBlueprint>(nullptr, NpcBlueprintPath);
	const AShooterNPC* ReloadedCdo = ReloadedBlueprint && ReloadedBlueprint->GeneratedClass
		? ReloadedBlueprint->GeneratedClass->GetDefaultObject<AShooterNPC>()
		: nullptr;
	if (!TestNotNull(TEXT("NPC blueprint reloads"), ReloadedCdo))
	{
		return false;
	}

	TestEqual(TEXT("NPC blueprint selects the weapon row"), ReloadedCdo->GetWeaponRowName(), NpcWeaponRowName);
	TestNull(TEXT("NPC blueprint no longer sets a weapon class"), ReloadedCdo->GetWeaponClass().Get());
	TestTrue(
		TEXT("NPC weapon row resolves to a valid production row"),
		ShooterWeaponTable::IsRowValidForGrant(
			ShooterWeaponTable::FindWeaponRow(
				ShooterWeaponTable::ResolveWeaponTable(),
				ReloadedCdo->GetWeaponRowName())));

	UE_LOG(LogShootGame, Display, TEXT("WEAPONDEFAULTS|npc|%s|Row=%s"), NpcBlueprintPath, *NpcWeaponRowName.ToString());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
