// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "ShooterBulletCounterUI.h"
#include "Components/TextBlock.h"

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
 * 原生备弹文本挂接到根面板，且 UpdateBulletCounter 把 ReserveAmmo 写成数字。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterBulletCounterReserveTextTest,
	"ShootGame.UI.BulletCounter.ReserveText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterBulletCounterReserveTextTest::RunTest(const FString& Parameters)
{
	using namespace ShooterBulletCounterReserveAutomationTests;

	UWorld* World = CreateReserveTestWorld();
	if (!TestNotNull(TEXT("Reserve test world created"), World))
	{
		return false;
	}

	UClass* WidgetClass = LoadClass<UShooterBulletCounterUI>(
		nullptr,
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

	// CreateWidget 不会立即构建 Slate；TakeWidget 触发 OnWidgetRebuilt → NativePreConstruct。
	Widget->TakeWidget();

	UTextBlock* ReserveText = Widget->GetReserveTextBlock();
	if (!TestNotNull(TEXT("原生备弹文本已创建"), ReserveText))
	{
		DestroyReserveTestWorld(World);
		return false;
	}
	TestNotNull(TEXT("备弹文本已挂接到根面板"), ReserveText->GetParent());

	Widget->UpdateBulletCounter(10, 3, 47);
	TestEqual(
		TEXT("备弹数字写入文本"),
		Widget->GetReserveTextBlock()->GetText().ToString(),
		FString(TEXT("47")));

	Widget->UpdateBulletCounter(10, 0, 0);
	TestEqual(
		TEXT("备弹耗尽归零"),
		Widget->GetReserveTextBlock()->GetText().ToString(),
		FString(TEXT("0")));

	DestroyReserveTestWorld(World);
	return true;
}

#endif
