// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Characters/ShooterCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "ShooterInventoryReserveTestTypes.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/ShooterWeaponConfigRow.h"
#include "../Equipment/ShooterWeaponPresentationTestTypes.h"
#include "../Weapon/ShooterWeaponTestTableTypes.h"

namespace ShooterInventoryReserveDeclarationAutomationTests
{
	UWorld* CreateReserveDeclarationTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!World || !GEngine)
		{
			return World;
		}

		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		return World;
	}

	void DestroyReserveDeclarationTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	AShooterWeaponPresentationTestCharacter* SpawnReserveDeclarationTestCharacter(FAutomationTestBase& Test, UWorld* World)
	{
		AShooterWeaponPresentationTestCharacter* Character = World->SpawnActor<AShooterWeaponPresentationTestCharacter>(
				FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("Reserve declaration test character spawned"), Character))
		{
			return nullptr;
		}

		// 测试 World 没有 GameMode；直接驱动 WorldSettings 让组件订阅与 Actor BeginPlay 生效。
		if (AWorldSettings* WorldSettings = World->GetWorldSettings())
		{
			WorldSettings->NotifyBeginPlay();
		}
		return Character;
	}

	/**
	 * 按武器类自身声明的弹匣容量与初始备弹构造武器模板行并授予，
	 * 返回该实例入库后的初始 ReserveAmmo；授予失败时返回 INDEX_NONE。
	 */
	int32 GrantDeclaredReserveForTest(FAutomationTestBase& Test, UShooterInventoryComponent* Inventory,
		TSubclassOf<AShooterWeapon> WeaponClass, const TCHAR* GrantLabel)
	{
		const AShooterWeapon* WeaponDefaults = WeaponClass
			? WeaponClass->GetDefaultObject<AShooterWeapon>()
			: nullptr;
		if (!Test.TestNotNull(TEXT("Reserve test weapon class has defaults"), WeaponDefaults))
		{
			return INDEX_NONE;
		}

		EShooterInventoryAddResult AddResult = EShooterInventoryAddResult::NotAuthoritative;
		AShooterWeapon* Weapon = GrantTestWeapon(Inventory->GetWorld(), Inventory, WeaponClass,
			/*MagazineSize*/ WeaponDefaults->GetMagazineSize(),
			/*InitialReserveAmmo*/ WeaponDefaults->GetInitialReserveAmmo(),
			&AddResult);
		if (!Test.TestEqual(GrantLabel, static_cast<int32>(AddResult),
			static_cast<int32>(EShooterInventoryAddResult::Added)) ||
			!Test.TestNotNull(TEXT("Reserve test weapon granted"), Weapon))
		{
			return INDEX_NONE;
		}

		Test.TestEqual(TEXT("Granted weapon magazine comes from the runtime snapshot"), Weapon->GetBulletCount(),
			WeaponDefaults->GetMagazineSize());
		return Weapon->GetReserveAmmo();
	}
}

/**
 * 有限备弹声明的语义归档（单表武器配置纠偏起由 DT_WeaponData 武器模板行 FShooterWeaponConfigRow 承担）：
 * 显式 >=0 直接成为入库初始备弹；0 表示纯弹匣经济；-1 保持 MagazineSize×3 兼容基线。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterInventoryReserveDeclaredByWeaponRowTest,
	"ShootGame.Inventory.Reserve.DeclaredByWeaponRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterInventoryReserveDeclaredByWeaponRowTest::RunTest(const FString& Parameters)
{
	using namespace ShooterInventoryReserveDeclarationAutomationTests;

	// (a) 行语义：直接在本地构造的武器模板行上覆盖三个备弹声明区间。
	FShooterWeaponConfigRow DeclaredRow;
	DeclaredRow.MagazineSize = 4;
	DeclaredRow.InitialReserveAmmo = 7;
	TestEqual(TEXT("显式声明的备弹值被采用"), DeclaredRow.ResolveInitialReserveAmmo(), 7);

	FShooterWeaponConfigRow ZeroRow;
	ZeroRow.MagazineSize = 4;
	ZeroRow.InitialReserveAmmo = 0;
	TestEqual(TEXT("声明零备弹的武器模板行没有初始 ReserveAmmo"), ZeroRow.ResolveInitialReserveAmmo(), 0);

	FShooterWeaponConfigRow AutoRow;
	AutoRow.MagazineSize = 4;
	AutoRow.InitialReserveAmmo = -1;
	TestEqual(TEXT("默认 -1 保持 MagazineSize×3 兼容基线"), AutoRow.ResolveInitialReserveAmmo(), 12);

	// (b) 授予链路：三个备弹声明测试武器仍然按各自声明的武器模板行入库，
	// 并存出预期初始备弹。授予入口只接受行名，因此需要真实 UWorld 与注入的测试武器表。
	UWorld* World = CreateReserveDeclarationTestWorld();
	if (!TestNotNull(TEXT("Reserve declaration test world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnReserveDeclarationTestCharacter(*this, World);
	if (!Character)
	{
		DestroyReserveDeclarationTestWorld(World);
		return false;
	}

	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	if (!TestNotNull(TEXT("Character owns inventory"), Inventory))
	{
		DestroyReserveDeclarationTestWorld(World);
		return false;
	}

	// 自动基线：弹匣 4 + InitialReserveAmmo -1 -> 入库备弹 4×3 = 12。
	TestEqual(TEXT("自动 -1 的武器模板行入库后解析为 MagazineSize×3"), GrantDeclaredReserveForTest(*this, Inventory,
			AShooterInventoryAutoReserveTestWeapon::StaticClass(), TEXT("自动备弹测试武器按武器模板行授予成功")), 12);

	// 显式有限值：7 原样成为入库初始备弹。
	TestEqual(
		TEXT("显式 7 的武器模板行入库后原样采用"),
		GrantDeclaredReserveForTest(
			*this,
			Inventory,
			AShooterInventoryDeclaredReserveTestWeapon::StaticClass(),
			TEXT("显式备弹测试武器按武器模板行授予成功")),
		7);

	// 显式零备弹：纯弹匣经济。
	TestEqual(TEXT("显式 0 的武器模板行入库后备弹为零"), GrantDeclaredReserveForTest(*this, Inventory,
			AShooterInventoryZeroReserveTestWeapon::StaticClass(), TEXT("零备弹测试武器按武器模板行授予成功")), 0);
	TestEqual(TEXT("三个备弹声明武器各占一个 Slot"), Inventory->GetWeaponCount(), 3);

	DestroyReserveDeclarationTestWorld(World);

	// 未绑定的 WeaponActor 没有备弹概念，HUD 链路读取到的备弹必须是 0 而不是任意镜像值。
	TestEqual(TEXT("未绑定 Inventory 的武器备弹读取为 0"),
		GetDefault<AShooterInventoryAutoReserveTestWeapon>()->GetReserveAmmo(), 0);
	return true;
}

#endif
