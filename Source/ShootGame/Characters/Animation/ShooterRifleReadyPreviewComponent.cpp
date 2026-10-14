// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterRifleReadyPreviewComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/ShooterGameplayTags.h"
#include "Characters/ShooterCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "Weapons/ShooterWeapon.h"

static TAutoConsoleVariable<int32> CVarRifleNetworkPreview(TEXT("ShootGame.RifleReady.NetPreview"), 1,
	TEXT("临时 Rifle K 网络姿态预览；0 恢复原本地入口，1 使用服务器目标。"));

UShooterRifleReadyPreviewComponent::UShooterRifleReadyPreviewComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

bool UShooterRifleReadyPreviewComponent::SupportsWeapon(FName WeaponId)
{
	return WeaponId == TEXT("Rifle") || WeaponId == TEXT("AWP") || WeaponId == TEXT("GrenadeLauncher");
}

bool UShooterRifleReadyPreviewComponent::IsNetworkPreviewEnabled() const
{
	return GetWorld() && GetWorld()->GetNetMode() != NM_Standalone && CVarRifleNetworkPreview.GetValueOnGameThread() != 0;
}

bool UShooterRifleReadyPreviewComponent::CanPreview() const
{
	const auto* Character = Cast<AShooterCharacter>(GetOwner());
	const auto* Weapon = Character ? Character->GetCurrentWeaponActor() : nullptr;
	const auto* ASC = Character ? Character->GetAbilitySystemComponent() : nullptr;
	const auto* Anim = Character ? Character->GetMesh()->GetAnimInstance() : nullptr;
	return Character && Cast<APlayerController>(Character->GetController()) && Weapon &&
		Weapon->GetOwner() == Character && SupportsWeapon(Weapon->GetWeaponId()) && ASC &&
		!ASC->HasMatchingGameplayTag(ShooterGameplayTags::State_Firing) &&
		!ASC->HasMatchingGameplayTag(ShooterGameplayTags::State_Reloading) &&
		!ASC->HasMatchingGameplayTag(ShooterGameplayTags::State_Equipping) &&
		!ASC->HasMatchingGameplayTag(ShooterGameplayTags::State_Dead) && (!Anim || !Anim->IsAnyMontagePlaying());
}

void UShooterRifleReadyPreviewComponent::ToggleNetworkPreview()
{
	const auto* Character = Cast<AShooterCharacter>(GetOwner());
	if (IsNetworkPreviewEnabled() && Character && Character->IsLocallyControlled() && !bCancelPending)
	{
		ServerTogglePreview();
	}
}

void UShooterRifleReadyPreviewComponent::CancelNetworkPreview()
{
	const auto* Character = Cast<AShooterCharacter>(GetOwner());
	if (IsNetworkPreviewEnabled() && Character && Character->IsLocallyControlled() && bLowReadyRequested && !bCancelPending)
	{
		bCancelPending = true;
		ServerCancelPreview();
	}
}

void UShooterRifleReadyPreviewComponent::ServerTogglePreview_Implementation()
{
	if (IsNetworkPreviewEnabled() && CanPreview()) { SetServerGoal(!bLowReadyRequested); }
}

void UShooterRifleReadyPreviewComponent::ServerCancelPreview_Implementation()
{
	SetServerGoal(false);
}

void UShooterRifleReadyPreviewComponent::SetServerGoal(bool bLowReady)
{
	check(GetOwner()->HasAuthority());
	bLowReadyRequested = bLowReady;
	if (!bLowReady) { bCancelPending = false; }
	SetComponentTickEnabled(bLowReady);
	GetOwner()->ForceNetUpdate();
}

void UShooterRifleReadyPreviewComponent::OnRep_LowReadyRequested()
{
	if (!bLowReadyRequested) { bCancelPending = false; }
}

void UShooterRifleReadyPreviewComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	if (GetOwner()->HasAuthority() && bLowReadyRequested && (!IsNetworkPreviewEnabled() || !CanPreview()))
	{
		SetServerGoal(false);
	}
}

void UShooterRifleReadyPreviewComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UShooterRifleReadyPreviewComponent, bLowReadyRequested);
}
