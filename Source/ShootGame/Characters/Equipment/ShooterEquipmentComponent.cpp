// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/Equipment/ShooterEquipmentComponent.h"

#include "Characters/Aim/ShooterAimPresentationComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "ShootGame.h"
#include "Weapons/ShooterWeapon.h"

UShooterEquipmentComponent::UShooterEquipmentComponent()
{
	SetIsReplicatedByDefault(true);
}

void UShooterEquipmentComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UShooterInventoryComponent* Inventory = GetOwnerInventory())
	{
		Inventory->OnWeaponInstanceRemovedFromInventory.AddUObject(
			this,
			&UShooterEquipmentComponent::NotifyWeaponInstanceRemoved);
		Inventory->OnInventoryCleared.AddUObject(
			this,
			&UShooterEquipmentComponent::NotifyInventoryCleared);
	}

	// 复制属性可能先于 BeginPlay 到达；补做一次幂等应用，避免漏掉初始装备。
	ApplyCurrentWeapon(nullptr);
}

void UShooterEquipmentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UShooterInventoryComponent* Inventory = GetOwnerInventory())
	{
		Inventory->OnWeaponInstanceRemovedFromInventory.RemoveAll(this);
		Inventory->OnInventoryCleared.RemoveAll(this);
	}

	Super::EndPlay(EndPlayReason);
}

bool UShooterEquipmentComponent::EquipWeapon(const FGuid& InstanceId)
{
	AShooterCharacter* Character = GetOwnerCharacter();
	UShooterInventoryComponent* Inventory = GetOwnerInventory();
	if (!Character || !Character->HasAuthority() || !Inventory || !InstanceId.IsValid())
	{
		return false;
	}

	// 事务目标必须真实存在于 Inventory，并且是绑定到本角色的 WeaponActor。
	AShooterWeapon* TargetWeapon = Inventory->FindWeaponActor(InstanceId);
	if (!IsValid(TargetWeapon) ||
		TargetWeapon->GetOwner() != Character ||
		TargetWeapon->IsActorBeingDestroyed())
	{
		return false;
	}

	AShooterWeapon* PreviousWeapon = CurrentWeaponActor;
	if (IsValid(PreviousWeapon) && PreviousWeapon != TargetWeapon)
	{
		PreviousWeapon->DeactivateWeapon();
	}

	// 身份与实体必须作为同一事务原子提交。
	ActiveWeaponInstanceId = InstanceId;
	CurrentWeaponActor = TargetWeapon;
	ApplyCurrentWeapon(nullptr);
	ResetAimPresentationForEquipChange();
	Character->ForceNetUpdate();

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Equipment EquipWeapon committed: Actor=%s InstanceId=%s Weapon=%s"),
		*GetNameSafe(Character),
		*InstanceId.ToString(),
		*GetNameSafe(TargetWeapon));
	return true;
}

void UShooterEquipmentComponent::ClearEquippedWeapon()
{
	if (IsValid(CurrentWeaponActor))
	{
		CurrentWeaponActor->DeactivateWeapon();
	}

	CurrentWeaponActor = nullptr;
	ActiveWeaponInstanceId = FGuid();

	if (AShooterCharacter* Character = GetOwnerCharacter())
	{
		// 清空是死亡 / Inventory Clear 生命周期，Aim 表现必须显式失效，不复用旧值。
		if (UShooterAimPresentationComponent* AimPresentation =
			Character->GetAimPresentationComponent())
		{
			AimPresentation->ClearPresentationAimSmoothing();
		}

		Character->ForceNetUpdate();
	}

	BroadcastEquippedWeaponChanged();

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Equipment ClearEquippedWeapon: Actor=%s"),
		*GetNameSafe(GetOwner()));
}

void UShooterEquipmentComponent::HandleWeaponActorReady(AShooterWeapon* Weapon)
{
	if (IsValid(Weapon) && Weapon == CurrentWeaponActor)
	{
		// WeaponActor 的 Owner / BoundInstanceId 晚到时补做幂等表现应用。
		ApplyCurrentWeapon(nullptr);
	}
}

void UShooterEquipmentComponent::NotifyWeaponInstanceRemoved(const FGuid& InstanceId)
{
	if (ActiveWeaponInstanceId == InstanceId)
	{
		ClearEquippedWeapon();
	}
}

void UShooterEquipmentComponent::NotifyInventoryCleared()
{
	ClearEquippedWeapon();
}

void UShooterEquipmentComponent::ApplyCurrentWeapon(AShooterWeapon* PreviousWeapon)
{
	if (IsValid(PreviousWeapon) && PreviousWeapon != CurrentWeaponActor)
	{
		PreviousWeapon->DeactivateWeapon();
	}

	AShooterCharacter* Character = GetOwnerCharacter();
	if (!Character || !IsValid(CurrentWeaponActor))
	{
		return;
	}

	// 幂等：WeaponActor 的所有权信息尚未到达时暂不附着，待 HandleWeaponActorReady 补做。
	if (CurrentWeaponActor->GetOwner() != Character)
	{
		return;
	}

	Character->AttachWeaponMeshes(CurrentWeaponActor);
	CurrentWeaponActor->ActivateWeapon();
	BroadcastEquippedWeaponChanged();
}

void UShooterEquipmentComponent::ResetAimPresentationForEquipChange()
{
	if (AShooterCharacter* Character = GetOwnerCharacter())
	{
		if (UShooterAimPresentationComponent* AimPresentation =
			Character->GetAimPresentationComponent())
		{
			AimPresentation->ResetPresentationAimSmoothing();
		}
	}
}

AShooterCharacter* UShooterEquipmentComponent::GetOwnerCharacter() const
{
	return Cast<AShooterCharacter>(GetOwner());
}

UShooterInventoryComponent* UShooterEquipmentComponent::GetOwnerInventory() const
{
	const AShooterCharacter* Character = GetOwnerCharacter();
	return Character ? Character->GetInventoryComponent() : nullptr;
}

void UShooterEquipmentComponent::BroadcastEquippedWeaponChanged()
{
	OnEquippedWeaponChanged.Broadcast(ActiveWeaponInstanceId, CurrentWeaponActor);
}

void UShooterEquipmentComponent::OnRep_CurrentWeaponActor(AShooterWeapon* PreviousWeapon)
{
	ApplyCurrentWeapon(PreviousWeapon);
	ResetAimPresentationForEquipChange();

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Equipment CurrentWeaponActor replicated: Actor=%s Previous=%s Current=%s"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(PreviousWeapon),
		*GetNameSafe(CurrentWeaponActor));
}

void UShooterEquipmentComponent::OnRep_ActiveWeaponInstanceId()
{
	// Active 身份可能先于或晚于 WeaponActor 到达；两者都到达后由 Apply / HandleReady 幂等补做。
	if (IsValid(CurrentWeaponActor))
	{
		ApplyCurrentWeapon(nullptr);
	}

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Equipment ActiveWeaponInstanceId replicated: Actor=%s InstanceId=%s"),
		*GetNameSafe(GetOwner()),
		*ActiveWeaponInstanceId.ToString());
}

void UShooterEquipmentComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 观察者只需要 CurrentWeaponActor；完整装备身份只发给拥有者。
	DOREPLIFETIME(UShooterEquipmentComponent, CurrentWeaponActor);
	DOREPLIFETIME_CONDITION(UShooterEquipmentComponent, ActiveWeaponInstanceId, COND_OwnerOnly);
}
