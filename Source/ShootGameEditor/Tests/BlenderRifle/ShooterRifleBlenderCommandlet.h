// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShootGameEditor/Tests/RifleTransition/ShooterRifleTransitionFixture.h"
#include "ShooterRifleBlenderCommandlet.generated.h"

/** 新实验场景显式选择 AnimBP，复用既有按钮与真实 Rifle 夹具。 */
UCLASS(Blueprintable)
class SHOOTGAMEEDITOR_API AShooterRifleBlenderPreview : public AShooterRifleTransitionPreview
{
	GENERATED_BODY()

public:
	/** 仅新实验夹具使用的动画类；不替换既有场景或正式 AnimBP。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rifle Test")
	TSubclassOf<UAnimInstance> ExperimentAnimClass;

	/** 直接设置实验意图，避免编辑属性通知重建夹具并重置状态机。 */
	UFUNCTION(BlueprintCallable, Category = "Rifle Test")
	void SetRiflePreviewGoal(bool bLowReady, float Pitch, float Strength);

	/** 显式推进隔离视口动画，并返回状态、动画时间和速率供截图取证。 */
	UFUNCTION(BlueprintCallable, Category = "Rifle Test")
	FString AdvanceRiflePreview(float DeltaSeconds);

	virtual void OnConstruction(const FTransform& Transform) override;
};
