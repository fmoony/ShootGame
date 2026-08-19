// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterInventoryComponent.h"

#include "Net/UnrealNetwork.h"
#include "ShootGame.h"

UShooterInventoryComponent::UShooterInventoryComponent()
{
	// 组件随 Character 一起复制；数组属性本身使用 COND_OwnerOnly。
	SetIsReplicatedByDefault(true);
}

bool UShooterInventoryComponent::AddWeaponInstance(const FShooterWeaponInstanceData& InstanceData)
{
	if (!InstanceData.IsValid())
	{
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("Inventory AddWeaponInstance rejected invalid data: InstanceId=%s Definition=%s Slot=%d Mag=%d Reserve=%d"),
			*InstanceData.InstanceId.ToString(),
			*InstanceData.DefinitionId.ToString(),
			InstanceData.SlotIndex,
			InstanceData.MagazineAmmo,
			InstanceData.ReserveAmmo);
		return false;
	}

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	const bool bAdded = ReplicatedInventory.AddItem(InstanceData);
	if (bAdded)
	{
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("Inventory AddWeaponInstance succeeded: Actor=%s InstanceId=%s Slot=%d Count=%d"),
			*GetNameSafe(GetOwner()),
			*InstanceData.InstanceId.ToString(),
			InstanceData.SlotIndex,
			GetWeaponCount());
	}
	return bAdded;
}

bool UShooterInventoryComponent::RemoveWeaponInstance(const FGuid& InstanceId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !InstanceId.IsValid())
	{
		return false;
	}

	const bool bRemoved = ReplicatedInventory.RemoveItem(InstanceId);
	if (bRemoved)
	{
		if (ActiveWeaponInstanceId == InstanceId)
		{
			SetActiveWeaponInstanceId(FGuid());
		}

		UE_LOG(
			LogShootGame,
			Display,
			TEXT("Inventory RemoveWeaponInstance succeeded: Actor=%s InstanceId=%s Count=%d"),
			*GetNameSafe(GetOwner()),
			*InstanceId.ToString(),
			GetWeaponCount());
	}
	return bRemoved;
}

void UShooterInventoryComponent::ClearInventory()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	ReplicatedInventory.ClearItems();
	SetActiveWeaponInstanceId(FGuid());

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Inventory ClearInventory: Actor=%s Count=%d"),
		*GetNameSafe(GetOwner()),
		GetWeaponCount());
}

const FShooterWeaponInstanceData* UShooterInventoryComponent::FindWeaponInstance(
	const FGuid& InstanceId) const
{
	if (!InstanceId.IsValid())
	{
		return nullptr;
	}

	const FShooterWeaponInstanceEntry* Entry = ReplicatedInventory.FindItem(InstanceId);
	return Entry ? &Entry->InstanceData : nullptr;
}

void UShooterInventoryComponent::SetActiveWeaponInstanceId(const FGuid& NewInstanceId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || ActiveWeaponInstanceId == NewInstanceId)
	{
		return;
	}

	if (NewInstanceId.IsValid() && !ReplicatedInventory.FindItem(NewInstanceId))
	{
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("Inventory SetActiveWeaponInstanceId rejected missing instance: Actor=%s InstanceId=%s"),
			*GetNameSafe(GetOwner()),
			*NewInstanceId.ToString());
		return;
	}

	ActiveWeaponInstanceId = NewInstanceId;
	GetOwner()->ForceNetUpdate();

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Inventory ActiveWeaponInstanceId changed: Actor=%s InstanceId=%s"),
		*GetNameSafe(GetOwner()),
		*ActiveWeaponInstanceId.ToString());
}

void UShooterInventoryComponent::OnRep_ActiveWeaponInstanceId()
{
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Inventory ActiveWeaponInstanceId replicated: Actor=%s InstanceId=%s"),
		*GetNameSafe(GetOwner()),
		*ActiveWeaponInstanceId.ToString());
}

void UShooterInventoryComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 完整 Inventory 与逻辑 Active ID 都是 OwnerOnly：
	// 远端客户端只通过 Character.CurrentWeaponActor 表现当前持枪。
	DOREPLIFETIME_CONDITION(UShooterInventoryComponent, ReplicatedInventory, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UShooterInventoryComponent, ActiveWeaponInstanceId, COND_OwnerOnly);
}
