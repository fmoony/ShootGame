// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ShooterInventoryReserveTestTypes.h"
#include "Weapons/Definitions/ShooterWeaponDefinition.h"

/**
 * 有限备弹声明的语义归档（A4 起由 Definition.AmmoConfig 承担）：
 * 显式 >=0 直接成为入库初始备弹；0 表示纯弹匣经济；-1 保持 MagazineSize×3 兼容基线。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryReserveDeclaredByWeaponTest,
	"ShootGame.Inventory.Reserve.DeclaredByDefinition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryReserveDeclaredByWeaponTest::RunTest(const FString& Parameters)
{
	FShooterWeaponAmmoConfig DeclaredConfig;
	DeclaredConfig.MagazineSize = 4;
	DeclaredConfig.InitialReserveAmmo = 7;
	TestEqual(
		TEXT("显式声明的备弹值被采用"),
		DeclaredConfig.ResolveInitialReserveAmmo(),
		7);

	FShooterWeaponAmmoConfig ZeroConfig;
	ZeroConfig.MagazineSize = 4;
	ZeroConfig.InitialReserveAmmo = 0;
	TestEqual(
		TEXT("声明零备弹的定义没有初始 ReserveAmmo"),
		ZeroConfig.ResolveInitialReserveAmmo(),
		0);

	FShooterWeaponAmmoConfig AutoConfig;
	AutoConfig.MagazineSize = 4;
	AutoConfig.InitialReserveAmmo = -1;
	TestEqual(
		TEXT("默认 -1 保持 MagazineSize×3 兼容基线"),
		AutoConfig.ResolveInitialReserveAmmo(),
		12);

	// 生产 Definition 资产的备弹声明必须落在合法区间并可解析。
	for (const TCHAR* DefinitionPath :
		{ TEXT("/Game/Shooter/Weapons/Definitions/WD_Rifle"),
		  TEXT("/Game/Shooter/Weapons/Definitions/WD_Pistol"),
		  TEXT("/Game/Shooter/Weapons/Definitions/WD_AWP"),
		  TEXT("/Game/Shooter/Weapons/Definitions/WD_GrenadeLauncher") })
	{
		const UShooterWeaponDefinition* Definition = LoadObject<UShooterWeaponDefinition>(
			nullptr,
			DefinitionPath);
		if (!TestNotNull(
			FString::Printf(TEXT("Production definition %s loads"), DefinitionPath).GetCharArray().GetData(),
			Definition))
		{
			continue;
		}

		TestTrue(
			FString::Printf(TEXT("Definition %s is valid for grant"), DefinitionPath).GetCharArray().GetData(),
			Definition->IsValidForGrant());
		TestTrue(
			FString::Printf(TEXT("Definition %s resolves a non-negative reserve"), DefinitionPath).GetCharArray().GetData(),
			Definition->AmmoConfig.ResolveInitialReserveAmmo() >= 0);
	}

	// 未绑定的 WeaponActor 没有备弹概念，HUD 链路读取到的备弹必须是 0 而不是任意镜像值。
	TestEqual(
		TEXT("未绑定 Inventory 的武器备弹读取为 0"),
		GetDefault<AShooterInventoryAutoReserveTestWeapon>()->GetReserveAmmo(),
		0);
	return true;
}

#endif
