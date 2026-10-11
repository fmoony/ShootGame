// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "ShootGameEditor/Tests/BlenderRifle/ShooterRifleBlenderCommandlet.h"
#include "ShooterRifleSegmentedCommandlet.generated.h"

class UAnimSequence;

/** 隔离分段方案；只在离开稳定状态时选片，活动过渡及中途反向不换片。 */
UCLASS(Blueprintable)
class SHOOTGAMEEDITOR_API UShooterRifleSegmentedAnimInstance : public UShooterRifleTransitionTestAnimInstance
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rifle Segment")
	TObjectPtr<UAnimSequence> OriginalSequence;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rifle Segment")
	TObjectPtr<UAnimSequence> Down30Sequence;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rifle Segment")
	TObjectPtr<UAnimSequence> Down60Sequence;

	/** 新副本可调整选片阈值；默认保留旧负六十度方案。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rifle Segment")
	float Down60Threshold = -60.0f;

	/** 入场时的选择角度，仅诊断；不影响实时 ArmedAim 或 AO。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rifle Segment")
	float LatchedPitch = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rifle Segment")
	int32 SelectionCount = 0;

	/** 仅瞬态测量副本使用，不保存为资产。 */
	bool bProductionGraphTest = false;

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	UAnimSequence* ResolveSequence(float Pitch) const;
};

/** 分段方案关卡按钮，增加负六十度以下输入，不修改旧夹具。 */
UCLASS(Blueprintable)
class SHOOTGAMEEDITOR_API AShooterRifleSegmentedPreview : public AShooterRifleBlenderPreview
{
	GENERATED_BODY()

public:
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Rifle Segment")
	void CycleSegmentPitch();

	UFUNCTION(BlueprintPure, Category = "Rifle Segment")
	FString GetSegmentStatus() const;
};

/** 实际图测量与限定生产蓝图接入，验证分段、边界、反向和改角度。 */
UCLASS()
class UShooterRifleSegmentedCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UShooterRifleSegmentedCommandlet();
	virtual int32 Main(const FString& Params) override;
};
