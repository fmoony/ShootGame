// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "ShooterWeaponPresentationTestTypes.h"

/**
 * 备弹 HUD 刷新时序复现：拾取立即装备、换弹提交、开火扣弹三个服务器权威事务
 * 是否都会广播 OnBulletCountUpdated（含最新 MagazineAmmo / ReserveAmmo）。
 * 用于定位“拾取/换弹后 HUD 不刷新、开火才刷新”的问题层。
 */
namespace ShooterAmmoHudRefreshAutomationTests
{
	UWorld* CreateHudRefreshTestWorld()
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

	void DestroyHudRefreshTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAmmoHudRefreshPickupReloadFireTest,
	"ShootGame.Inventory.AmmoHudRefresh.PickupReloadFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAmmoHudRefreshPickupReloadFireTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAmmoHudRefreshAutomationTests;

	UWorld* World = CreateHudRefreshTestWorld();
	if (!TestNotNull(TEXT("HUD refresh test world created"), World))
	{
		return false;
	}

	AShooterCharacter* Character = World->SpawnActor<AShooterWeaponPresentationTestCharacter>(
		FVector::ZeroVector,
		FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("HUD refresh test character spawned"), Character))
	{
		DestroyHudRefreshTestWorld(World);
		return false;
	}
	Character->DispatchBeginPlay();

	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	if (!TestNotNull(TEXT("Inventory exists"), Inventory) ||
		!TestNotNull(TEXT("Equipment exists"), Equipment))
	{
		DestroyHudRefreshTestWorld(World);
		return false;
	}

	UShooterBulletCountEventTestListener* Listener = NewObject<UShooterBulletCountEventTestListener>();
	Character->OnBulletCountUpdated.AddDynamic(
		Listener,
		&UShooterBulletCountEventTestListener::HandleBulletCountUpdated);

	const int32 BaseEventCount = Listener->EventCount;

	// --- 场景 1：拾取 = TryAddWeapon + 立即 EquipWeapon（与 AShooterPickup::OnOverlap 相同顺序） ---
	FGuid InstanceId;
	const EShooterInventoryAddResult AddResult = Inventory->TryAddWeapon(
		AShooterWeaponPresentationTestWeaponPrimary::StaticClass(),
		InstanceId);
	TestEqual(
		TEXT("拾取授予成功"),
		static_cast<int32>(AddResult),
		static_cast<int32>(EShooterInventoryAddResult::Added));
	if (AddResult != EShooterInventoryAddResult::Added)
	{
		DestroyHudRefreshTestWorld(World);
		return false;
	}

	TestTrue(
		TEXT("场景1a 拾取入库阶段（武器仍隐藏）不推 HUD"),
		Listener->EventCount == BaseEventCount);

	// 与幂等测试相同的前置：这个世界没有 BeginPlay，武器 Owner 绑定依赖 BeginPlay。
	if (AShooterWeapon* GrantedWeapon = Inventory->FindWeaponActor(InstanceId))
	{
		if (!GrantedWeapon->HasActorBegunPlay())
		{
			GrantedWeapon->DispatchBeginPlay();
		}
	}

	TestTrue(TEXT("拾取后立即装备"), Equipment->EquipWeapon(InstanceId));
	TestEqual(
		TEXT("场景1b 拾取装备后 HUD 立即广播一次"),
		Listener->EventCount,
		BaseEventCount + 1);
	TestEqual(TEXT("场景1b 拾取后弹匣满"), Listener->LastBullets, 10);
	TestEqual(TEXT("场景1b 拾取后备弹正确"), Listener->LastReserveAmmo, 30);

	// --- 场景 2：换弹提交（先扣一发让弹匣不满） ---
	TestTrue(TEXT("开火扣弹前置成功"), Inventory->ConsumeMagazineAmmo(InstanceId, 1));
	TestEqual(
		TEXT("场景2a 开火扣弹立即广播"),
		Listener->EventCount,
		BaseEventCount + 2);
	TestEqual(TEXT("场景2a 扣弹后弹匣 9"), Listener->LastBullets, 9);

	int32 Transferred = 0;
	TestTrue(TEXT("换弹事务提交"), Inventory->ReloadMagazine(InstanceId, Transferred));
	TestEqual(TEXT("场景2b 换弹提交后 HUD 立即广播"), Listener->EventCount, BaseEventCount + 3);
	TestEqual(TEXT("场景2b 换弹后弹匣回满"), Listener->LastBullets, 10);
	TestEqual(TEXT("场景2b 换弹后备弹 29"), Listener->LastReserveAmmo, 29);

	DestroyHudRefreshTestWorld(World);
	return true;
}

#endif
