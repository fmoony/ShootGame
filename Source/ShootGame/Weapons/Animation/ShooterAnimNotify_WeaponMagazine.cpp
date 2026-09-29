// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterAnimNotify_WeaponMagazine.h"

#include "Characters/Animation/ShooterAnimInstanceBase.h"
#include "Characters/ShooterCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Weapons/ShooterWeapon.h"

void UShooterAnimNotify_WeaponMagazine::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp || !MeshComp->GetWorld() || MeshComp->GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(MeshComp->GetOwner());
	if (!ShooterCharacter || MeshComp != ShooterCharacter->GetMesh())
	{
		return;
	}

	const UShooterAnimInstanceBase* AnimInstance = Cast<UShooterAnimInstanceBase>(MeshComp->GetAnimInstance());
	if (!AnimInstance || !AnimInstance->bIsReloading)
	{
		return;
	}

	AShooterWeapon* Weapon = ShooterCharacter->GetCurrentWeaponActor();
	if (!Weapon || Weapon->GetOwner() != ShooterCharacter)
	{
		return;
	}

	const APlayerController* PlayerController = Cast<APlayerController>(ShooterCharacter->GetController());
	const bool bIsLocalHumanPlayerView = ShooterCharacter->IsLocallyControlled() &&
		PlayerController &&	PlayerController->IsLocalController();
	if (bIsLocalHumanPlayerView)
	{
		if (Stage == EShooterMagazinePresentationStage::Detach)
		{
			Weapon->DetachFirstPersonMagazineProxy(ShooterCharacter->GetFirstPersonMesh());
		}
		else
		{
			Weapon->InsertFirstPersonMagazineProxy();
		}
	}

	if (Stage == EShooterMagazinePresentationStage::Detach)
	{
		Weapon->DetachThirdPersonMagazineProxy(MeshComp);
	}
	else
	{
		Weapon->InsertThirdPersonMagazineProxy();
	}
}

FString UShooterAnimNotify_WeaponMagazine::GetNotifyName_Implementation() const
{
	const UEnum* StageEnum = StaticEnum<EShooterMagazinePresentationStage>();
	return FString::Printf(TEXT("WeaponMagazine:%s"),
		StageEnum ? *StageEnum->GetNameStringByValue(static_cast<int64>(Stage)) : TEXT("Invalid"));
}
