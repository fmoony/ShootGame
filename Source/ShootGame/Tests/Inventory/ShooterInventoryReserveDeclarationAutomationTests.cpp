// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ShooterInventoryComponent.h"
#include "ShooterInventoryReserveTestTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterInventoryReserveDeclaredByWeaponTest,
	"ShootGame.Inventory.Reserve.DeclaredByWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryReserveDeclaredByWeaponTest::RunTest(const FString& Parameters)
{
	// 有限备弹声明：显式配置 >=0 的 InitialReserveAmmo 直接成为入库初始备弹。
	TestEqual(
		TEXT("显式声明的备弹值被采用"),
		ShooterInventory::GetInitialReserveAmmoForWeaponClass(
			AShooterInventoryDeclaredReserveTestWeapon::StaticClass()),
		7);
	TestEqual(
		TEXT("声明零备弹的武器没有初始 ReserveAmmo"),
		ShooterInventory::GetInitialReserveAmmoForWeaponClass(
			AShooterInventoryZeroReserveTestWeapon::StaticClass()),
		0);
	// 默认 -1 保持既有资产的 MagazineSize×3 兼容基线（4 × 3 = 12）。
	TestEqual(
		TEXT("默认 -1 保持 MagazineSize×3 兼容基线"),
		ShooterInventory::GetInitialReserveAmmoForWeaponClass(
			AShooterInventoryAutoReserveTestWeapon::StaticClass()),
		12);

	// 未绑定的 WeaponActor 没有备弹概念，HUD 链路读取到的备弹必须是 0 而不是任意镜像值。
	TestEqual(
		TEXT("未绑定 Inventory 的武器备弹读取为 0"),
		GetDefault<AShooterInventoryAutoReserveTestWeapon>()->GetReserveAmmo(),
		0);
	return true;
}

#endif
