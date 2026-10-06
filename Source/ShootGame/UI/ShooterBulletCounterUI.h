// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterBulletCounterUI.generated.h"

/**
 *  Simple bullet counter UI widget for a first person shooter game
 */
UCLASS(abstract)
class SHOOTGAME_API UShooterBulletCounterUI : public UUserWidget
{
	GENERATED_BODY()

public:

	/** HUD 弹药入口：把弹匣与备弹数据统一转发给蓝图子弹条表现。 */
	void UpdateBulletCounter(int32 MagazineSize, int32 BulletCount, int32 ReserveAmmo);

#if WITH_DEV_AUTOMATION_TESTS
	int32 GetDisplayedMagazineForTest() const { return DisplayedMagazineForTest; }
	int32 GetDisplayedReserveForTest() const { return DisplayedReserveForTest; }
	int32 GetPredictedUpdateCountForTest() const { return PredictedUpdateCountForTest; }

private:
	int32 DisplayedMagazineForTest = INDEX_NONE;
	int32 DisplayedReserveForTest = INDEX_NONE;
	int32 PredictedUpdateCountForTest = 0;

public:
#endif

	/** Allows Blueprint to update sub-widgets with the new bullet count */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta=(DisplayName = "UpdateBulletCounter"))
	void BP_UpdateBulletCounter(int32 MagazineSize, int32 BulletCount, int32 ReserveAmmo);

	/** Allows Blueprint to update sub-widgets with the new life total and play a damage effect on the HUD */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta=(DisplayName = "Damaged"))
	void BP_Damaged(float LifePercent);
};
