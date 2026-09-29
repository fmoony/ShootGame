// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "ShooterAnimNotify_WeaponMagazine.generated.h"

/** 换弹弹匣表现阶段；只驱动当前视角对应的纯表现代理。 */
UENUM(BlueprintType)
enum class EShooterMagazinePresentationStage : uint8
{
	Detach,
	Insert
};

/**
 * Character Reload 动画中的弹匣换手 Notify。
 *
 * Notify 只读取当前装备与 Reload 表现上下文，不修改弹药、Gameplay 状态或网络属性。
 * Notify 只挂在第三人称 Reload Sequence 上；本机人类玩家同时驱动 FP/TP，
 * 远端观察者只驱动 TP，Dedicated Server 直接忽略。
 */
UCLASS(meta = (DisplayName = "WeaponMagazine"))
class SHOOTGAME_API UShooterAnimNotify_WeaponMagazine : public UAnimNotify
{
	GENERATED_BODY()

public:
	/** 该 Notify 触发时执行弹匣拆出或插回。 */
	UPROPERTY(EditAnywhere, Category = "Magazine")
	EShooterMagazinePresentationStage Stage = EShooterMagazinePresentationStage::Detach;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;
};
