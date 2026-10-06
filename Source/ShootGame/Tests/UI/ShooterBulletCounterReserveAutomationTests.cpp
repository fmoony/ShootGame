// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "UI/ShooterBulletCounterUI.h"
#include "Weapons/ShooterAmmoDisplayState.h"

namespace ShooterBulletCounterReserveAutomationTests
{
	UWorld* CreateReserveTestWorld()
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

	void DestroyReserveTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
}

/**
 * 有限备弹 HUD：真实 UI_ShooterBulletCounter 资产加载后，
 * 蓝图设计器中的备弹文本存在，且 UpdateBulletCounter 把 ReserveAmmo 写成数字。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterBulletCounterReserveTextTest, "ShootGame.UI.BulletCounter.ReserveText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterBulletCounterReserveTextTest::RunTest(const FString& Parameters)
{
	using namespace ShooterBulletCounterReserveAutomationTests;

	UWorld* World = CreateReserveTestWorld();
	if (!TestNotNull(TEXT("Reserve test world created"), World))
	{
		return false;
	}

	UClass* WidgetClass = LoadClass<UShooterBulletCounterUI>(nullptr,
		TEXT("/Game/Shooter/Blueprints/UI/UI_ShooterBulletCounter.UI_ShooterBulletCounter_C"));
	if (!TestNotNull(TEXT("UI_ShooterBulletCounter 资产可加载"), WidgetClass))
	{
		DestroyReserveTestWorld(World);
		return false;
	}

	UShooterBulletCounterUI* Widget = CreateWidget<UShooterBulletCounterUI>(World, WidgetClass);
	if (!TestNotNull(TEXT("BulletCounter widget created"), Widget))
	{
		DestroyReserveTestWorld(World);
		return false;
	}

	// CreateWidget 不会立即构建 Slate；TakeWidget 触发蓝图 WidgetTree 实例化。
	Widget->TakeWidget();

	UTextBlock* ReserveText = Widget->WidgetTree
		? Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("ReserveAmmoTextBlock")))
		: nullptr;
	if (!TestNotNull(TEXT("蓝图备弹文本存在"), ReserveText))
	{
		DestroyReserveTestWorld(World);
		return false;
	}
	TestNotNull(TEXT("蓝图备弹文本已挂接到设计器树"), ReserveText->GetParent());

	Widget->UpdateBulletCounter(10, 3, 47);
	TestEqual(TEXT("蓝图备弹数字写入文本"), ReserveText->GetText().ToString(), FString(TEXT("47")));

	Widget->UpdateBulletCounter(10, 0, 0);
	TestEqual(TEXT("蓝图备弹耗尽归零"), ReserveText->GetText().ToString(), FString(TEXT("0")));

	DestroyReserveTestWorld(World);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAmmoDisplayOrderingTest, "ShootGame.UI.AmmoPrediction.SnapshotOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAmmoDisplayOrderingTest::RunTest(const FString& Parameters)
{
	FShooterAmmoDisplaySnapshot Initial;
	Initial.Revision = 1;
	Initial.Magazine = 10;
	Initial.Reserve = 30;
	FShooterAmmoDisplaySnapshot AfterShot = Initial;
	AfterShot.Revision = 2;
	AfterShot.Magazine = 9;
	for (int32 Order = 0; Order < 2; ++Order)
	{
		FShooterAmmoDisplayState State;
		State.ReceiveSnapshot(Initial);
		State.BeginActivation(11, Initial);
		State.PredictShot(11);
		TestEqual(TEXT("prediction immediately displays one less"), State.GetMagazine(10), 9);
		TestEqual(TEXT("prediction does not alter reserve"), State.GetReserve(30), 30);
		TestEqual(TEXT("authority snapshot is not mutated"), Initial.Magazine, 10);
		if (Order == 0)
		{
			State.ReceiveSnapshot(AfterShot);
			TestEqual(TEXT("property before final does not double deduct"), State.GetMagazine(9), 9);
		}
		TestTrue(TEXT("final result matches a display activation"), State.SettleActivation(11, AfterShot));
		TestEqual(TEXT("final before property does not restore old ammo"), State.GetMagazine(10), 9);
		State.ReceiveSnapshot(Initial);
		State.ReceiveSnapshot(AfterShot);
		TestEqual(TEXT("old or repeated properties do not overwrite final"), State.GetMagazine(10), 9);
		TestFalse(TEXT("duplicate final is ignored"), State.SettleActivation(11, Initial));
		TestEqual(TEXT("duplicate final cannot change display"), State.GetMagazine(10), 9);
		TestEqual(TEXT("display bookkeeping is resolved"), State.GetUnsettledCount(), 0);
	}

	// 两轮并发时第一轮结果不能清掉第二轮显示消费；新属性可以覆盖多轮最终结果。
	FShooterAmmoDisplayState Concurrent;
	Concurrent.ReceiveSnapshot(Initial);
	Concurrent.BeginActivation(21, Initial);
	Concurrent.PredictShot(21);
	Concurrent.BeginActivation(22, Initial);
	Concurrent.PredictShot(22);
	TestEqual(TEXT("two unresolved predictions display eight"), Concurrent.GetMagazine(10), 8);
	Concurrent.SettleActivation(21, AfterShot);
	TestEqual(TEXT("first final keeps second deduction"), Concurrent.GetMagazine(10), 8);
	FShooterAmmoDisplaySnapshot AfterReload = AfterShot;
	AfterReload.Revision = 4;
	AfterReload.Magazine = 10;
	AfterReload.Reserve = 28;
	Concurrent.ReceiveSnapshot(AfterReload);
	TestEqual(TEXT("reload property waits for unresolved final"), Concurrent.GetMagazine(10), 8);
	TestEqual(TEXT("reserve transfer waits for coherent baseline"), Concurrent.GetReserve(28), 30);
	FShooterAmmoDisplaySnapshot AfterSecond = AfterShot;
	AfterSecond.Revision = 3;
	AfterSecond.Magazine = 8;
	Concurrent.SettleActivation(22, AfterSecond);
	TestEqual(TEXT("last final adopts newer reload snapshot"), Concurrent.GetMagazine(8), 10);
	TestEqual(TEXT("new reserve arrives with magazine"), Concurrent.GetReserve(30), 28);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAmmoDisplayLifetimeTest, "ShootGame.UI.AmmoPrediction.RejectionLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAmmoDisplayLifetimeTest::RunTest(const FString& Parameters)
{
	FShooterAmmoDisplaySnapshot Initial;
	Initial.Revision = 10;
	Initial.Magazine = 1;
	Initial.Reserve = 5;
	FShooterAmmoDisplayState State;
	State.BeginActivation(31, Initial);
	State.PredictShot(31);
	TestEqual(TEXT("last predicted bullet displays zero"), State.GetMagazine(1), 0);
	State.RejectActivation(31);
	TestEqual(TEXT("reject restores displayed bullet"), State.GetMagazine(1), 1);
	State.RejectActivation(31);
	TestEqual(TEXT("duplicate reject does not add another bullet"), State.GetMagazine(1), 1);
	State.BeginActivation(32, Initial);
	State.PredictShot(32);
	FShooterAmmoDisplaySnapshot SameValue = Initial;
	SameValue.Revision = 12;
	SameValue.Reserve = 4;
	State.SettleActivation(32, SameValue);
	TestEqual(TEXT("same magazine after real replenishment is accepted"), State.GetMagazine(1), 1);
	TestEqual(TEXT("same-value final still updates reserve"), State.GetReserve(5), 4);
	State.BeginActivation(33, SameValue);
	State.PredictShot(33);
	State.PredictShot(33);
	TestEqual(TEXT("display never goes negative"), State.GetMagazine(1), 0);
	State.Reset();
	FShooterAmmoDisplaySnapshot NewLease = Initial;
	NewLease.Revision = 20;
	NewLease.Magazine = 7;
	State.ReceiveSnapshot(NewLease);
	TestFalse(TEXT("old final cannot resolve new lease"), State.SettleActivation(33, SameValue));
	TestEqual(TEXT("old final cannot overwrite new baseline"), State.GetMagazine(7), 7);
	TestEqual(TEXT("reset clears unresolved display state"), State.GetUnsettledCount(), 0);
	State.BeginActivation(33, NewLease);
	State.PredictShot(33);
	TestFalse(TEXT("old revision cannot settle a reused key"), State.SettleActivation(33, SameValue));
	TestEqual(TEXT("old final keeps new prediction deduction"), State.GetMagazine(7), 6);
	TestEqual(TEXT("new activation still waits for its own final"), State.GetUnsettledCount(), 1);
	NewLease.Revision = 21;
	NewLease.Magazine = 6;
	TestTrue(TEXT("new final settles the reused key"), State.SettleActivation(33, NewLease));
	TestEqual(TEXT("new final preserves accepted display"), State.GetMagazine(7), 6);
	return true;
}

#endif
