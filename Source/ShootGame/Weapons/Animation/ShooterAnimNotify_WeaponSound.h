// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "ShooterAnimNotify_WeaponSound.generated.h"

/** 换弹音效阶段；对应 AShooterWeapon 上的三个换弹表现音效属性。 */
UENUM(BlueprintType)
enum class EShooterReloadSoundStage : uint8
{
	MagazineOut,
	MagazineIn,
	Cocking
};

/**
 * 换弹 Sequence 内的武器音效 Notify。
 *
 * 各端（本地玩家与远端客户端）由各自机器上同步播放的换弹动画本地触发，
 * 不经任何 RPC：声音与动画同源同时，天然声画同步；Dedicated 服务器在武器播放侧早退。
 * 只承载可丢失的纯表现，符合“AnimNotify 不得写入权威状态”的既有边界。
 */
UCLASS(meta = (DisplayName = "WeaponSound"))
class SHOOTGAME_API UShooterAnimNotify_WeaponSound : public UAnimNotify
{
	GENERATED_BODY()

public:
	/** 该 Notify 触发时要播放的换弹音效阶段。 */
	UPROPERTY(EditAnywhere, Category = "Sound")
	EShooterReloadSoundStage Stage = EShooterReloadSoundStage::MagazineOut;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;
};
