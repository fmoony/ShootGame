// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterBulletCounterUI.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "ShootGame.h"

void UShooterBulletCounterUI::NativePreConstruct()
{
	Super::NativePreConstruct();

	if (ReserveTextBlock || !WidgetTree)
	{
		return;
	}

	ReserveTextBlock = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(),
		TEXT("ReserveAmmoText"));
	ReserveTextBlock->SetJustification(ETextJustify::Right);
	ReserveTextBlock->SetShadowOffset(FVector2D(1.f, 1.f));
	ReserveTextBlock->SetShadowColorAndOpacity(FLinearColor::Black.CopyWithNewOpacity(0.8f));

	UPanelWidget* RootPanel = GetRootWidget() ? Cast<UPanelWidget>(GetRootWidget()) : nullptr;
	UPanelSlot* AttachedSlot = RootPanel ? RootPanel->AddChild(ReserveTextBlock) : nullptr;
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(AttachedSlot))
	{
		// 右上角锚点、向左对齐：数字显示在子弹条行尾下方，不与弹匣图标重叠。
		CanvasSlot->SetAnchors(FAnchors(1.f, 0.f));
		CanvasSlot->SetAlignment(FVector2D(1.f, 0.f));
		CanvasSlot->SetPosition(FVector2D(0.f, 24.f));
		CanvasSlot->SetSize(FVector2D(96.f, 28.f));
	}
	else if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(AttachedSlot))
	{
		OverlaySlot->SetPadding(4.f);
		OverlaySlot->SetHorizontalAlignment(HAlign_Right);
		OverlaySlot->SetVerticalAlignment(VAlign_Bottom);
	}
	else if (ReserveTextBlock)
	{
		UE_LOG(LogShootGame, Warning,
			TEXT("ShooterBulletCounterUI 根面板不是 Canvas/Overlay，备弹数字无法挂接：%s"),
			*GetNameSafe(GetRootWidget()));
	}
}

void UShooterBulletCounterUI::UpdateBulletCounter(int32 MagazineSize, int32 BulletCount, int32 ReserveAmmo)
{
	if (ReserveTextBlock)
	{
		ReserveTextBlock->SetText(FText::AsNumber(ReserveAmmo));
	}

	BP_UpdateBulletCounter(MagazineSize, BulletCount, ReserveAmmo);
}
