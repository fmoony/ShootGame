// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterAnimNotify_WeaponSound.h"
#include "Characters/Animation/ShooterAnimInstanceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "ShooterCharacter.h"
#include "ShooterWeapon.h"

void UShooterAnimNotify_WeaponSound::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	// 声音只由第三人称 Mesh 承担：本地玩家的 TP Mesh 动画一直在跑（仅 OwnerNoSee 不渲染），
	// 若同一序列同时驱动 FP/TP 两个 Mesh（如 Pistol 共享序列），这里保证每台机器恰好播放一次。
	AShooterCharacter* ShooterCharacter = MeshComp ? Cast<AShooterCharacter>(MeshComp->GetOwner()) : nullptr;
	if (!ShooterCharacter || MeshComp != ShooterCharacter->GetMesh())
	{
		return;
	}

	// Notify 可能在低权重混合或取消后的残余帧到达；只接受仍处于权威 Reload 表现期的事件，
	// 与 BeginReloadPresentationRecovery 的守卫口径一致。
	const UShooterAnimInstanceBase* AnimInstance =
		Cast<UShooterAnimInstanceBase>(MeshComp->GetAnimInstance());
	if (!AnimInstance || !AnimInstance->bIsReloading)
	{
		return;
	}

	if (AShooterWeapon* Weapon = ShooterCharacter->GetCurrentWeaponActor())
	{
		Weapon->PlayReloadSoundStage(Stage);
	}
}

FString UShooterAnimNotify_WeaponSound::GetNotifyName_Implementation() const
{
	const UEnum* StageEnum = StaticEnum<EShooterReloadSoundStage>();
	return FString::Printf(
		TEXT("WeaponSound:%s"),
		StageEnum ? *StageEnum->GetNameStringByValue(static_cast<int64>(Stage)) : TEXT("Invalid"));
}
