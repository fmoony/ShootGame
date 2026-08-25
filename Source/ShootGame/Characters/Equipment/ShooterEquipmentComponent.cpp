// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/Equipment/ShooterEquipmentComponent.h"

#include "Characters/ShooterCharacter.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "ShootGame.h"
#include "Weapons/ShooterWeapon.h"

UShooterEquipmentComponent::UShooterEquipmentComponent()
{
	// R4 将启用组件复制；R3 facade 阶段不复制任何字段，保持现有复制结果完全不变。
	SetIsReplicatedByDefault(false);
}

bool UShooterEquipmentComponent::EquipWeapon(const FGuid& InstanceId)
{
	AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwner());
	if (!Character || !Character->HasAuthority())
	{
		return false;
	}

	const bool bCommitted = Character->CommitActiveWeapon(InstanceId);
	if (bCommitted)
	{
		BroadcastEquippedWeaponChanged();
	}

	return bCommitted;
}

FGuid UShooterEquipmentComponent::GetActiveWeaponInstanceId() const
{
	const AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwner());
	const UShooterInventoryComponent* Inventory = Character
		? Character->GetInventoryComponent()
		: nullptr;
	return Inventory ? Inventory->GetActiveWeaponInstanceId() : FGuid();
}

AShooterWeapon* UShooterEquipmentComponent::GetCurrentWeaponActor() const
{
	const AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwner());
	return Character ? Character->GetCurrentWeaponActor() : nullptr;
}

void UShooterEquipmentComponent::BroadcastEquippedWeaponChanged()
{
	const AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwner());
	AShooterWeapon* Weapon = Character ? Character->GetCurrentWeaponActor() : nullptr;
	const FGuid InstanceId = GetActiveWeaponInstanceId();

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Equipment facade transaction completed: Actor=%s InstanceId=%s Weapon=%s"),
		*GetNameSafe(Character),
		*InstanceId.ToString(),
		*GetNameSafe(Weapon));

	OnEquippedWeaponChanged.Broadcast(InstanceId, Weapon);
}
