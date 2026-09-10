// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterBulletCounterUI.generated.h"

class UTextBlock;

/**
 *  Simple bullet counter UI widget for a first person shooter game
 */
UCLASS(abstract)
class SHOOTGAME_API UShooterBulletCounterUI : public UUserWidget
{
	GENERATED_BODY()

public:

	/** HUD 弹药入口：原生 TextBlock 显示有限备弹数字后，把弹匣数据转发给蓝图子弹条表现。 */
	void UpdateBulletCounter(int32 MagazineSize, int32 BulletCount, int32 ReserveAmmo);

	/** Allows Blueprint to update sub-widgets with the new bullet count */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta=(DisplayName = "UpdateBulletCounter"))
	void BP_UpdateBulletCounter(int32 MagazineSize, int32 BulletCount, int32 ReserveAmmo);

	/** Allows Blueprint to update sub-widgets with the new life total and play a damage effect on the HUD */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta=(DisplayName = "Damaged"))
	void BP_Damaged(float LifePercent);

	/** 测试与调试用：返回原生备弹文本控件；未构造或挂接失败时为空。 */
	UTextBlock* GetReserveTextBlock() const { return ReserveTextBlock; }

protected:

	virtual void NativePreConstruct() override;

	/** 备弹数字显示；蓝图设计器树中没有文本控件，由原生代码创建并挂到根面板。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ReserveTextBlock;
};
