// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterInventoryComponent.h"

#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "ShootGame.h"
#include "ShooterWeapon.h"

namespace ShooterInventory
{
	FPrimaryAssetId MakeDefinitionIdForWeaponClass(TSubclassOf<AShooterWeapon> WeaponClass)
	{
		// WeaponDefinition 尚未正式 DataAsset 化；先以 WeaponClass 名建立稳定 DefinitionId 兼容桥接。
		return FPrimaryAssetId(
			FPrimaryAssetType(TEXT("ShooterWeapon")),
			WeaponClass->GetFName());
	}

	int32 GetInitialReserveAmmoForWeaponClass(TSubclassOf<AShooterWeapon> WeaponClass)
	{
		// 兼容桥接：Definition 未落地前，备用弹药先按弹匣容量估算；后续由 WeaponDefinition 取代。
		const AShooterWeapon* WeaponDefaults = WeaponClass->GetDefaultObject<AShooterWeapon>();
		return WeaponDefaults ? FMath::Max(0, WeaponDefaults->GetMagazineSize() * 3) : 0;
	}
}

UShooterInventoryComponent::UShooterInventoryComponent()
{
	// 组件随 Character 一起复制；数组属性本身使用 COND_OwnerOnly。
	SetIsReplicatedByDefault(true);
}

EShooterInventoryAddResult UShooterInventoryComponent::TryAddWeapon(
	TSubclassOf<AShooterWeapon> WeaponClass,
	FGuid& OutInstanceId)
{
	OutInstanceId = FGuid();
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return EShooterInventoryAddResult::NotAuthoritative;
	}

	if (!WeaponClass)
	{
		return EShooterInventoryAddResult::InvalidWeaponClass;
	}

	const AShooterWeapon* WeaponDefaults = WeaponClass->GetDefaultObject<AShooterWeapon>();
	if (!WeaponDefaults || WeaponDefaults->GetMagazineSize() <= 0)
	{
		return EShooterInventoryAddResult::InvalidWeaponClass;
	}

	const FPrimaryAssetId DefinitionId = ShooterInventory::MakeDefinitionIdForWeaponClass(WeaponClass);
	if (FindWeaponInstanceByDefinitionId(DefinitionId))
	{
		return EShooterInventoryAddResult::DuplicateDefinition;
	}

	const int32 FreeSlot = FindFreeSlotIndex();
	if (FreeSlot == INDEX_NONE)
	{
		return EShooterInventoryAddResult::SlotFull;
	}

	FShooterWeaponInstanceData InstanceData;
	InstanceData.InstanceId = FGuid::NewGuid();
	InstanceData.DefinitionId = DefinitionId;
	InstanceData.MagazineAmmo = WeaponDefaults->GetMagazineSize();
	InstanceData.ReserveAmmo = ShooterInventory::GetInitialReserveAmmoForWeaponClass(WeaponClass);
	InstanceData.SlotIndex = FreeSlot;

	if (!AddWeaponInstance(InstanceData))
	{
		return EShooterInventoryAddResult::SlotOccupied;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = GetOwner();
	SpawnParameters.Instigator = Cast<APawn>(GetOwner());
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot;

	AShooterWeapon* Weapon = GetWorld()->SpawnActor<AShooterWeapon>(
		WeaponClass,
		GetOwner()->GetActorTransform(),
		SpawnParameters);
	if (!Weapon)
	{
		ReplicatedInventory.RemoveItem(InstanceData.InstanceId);
		return EShooterInventoryAddResult::SpawnFailed;
	}

	Weapon->SetBoundInstanceId(InstanceData.InstanceId);
	Weapon->SetActorHiddenInGame(true);
	RegisterWeaponActor(Weapon);

	OutInstanceId = InstanceData.InstanceId;
	return EShooterInventoryAddResult::Added;
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

const FShooterWeaponInstanceData* UShooterInventoryComponent::FindWeaponInstanceByDefinitionId(
	const FPrimaryAssetId& DefinitionId) const
{
	if (!DefinitionId.IsValid())
	{
		return nullptr;
	}

	const FShooterWeaponInstanceEntry* Entry =
		ReplicatedInventory.FindItemByDefinitionId(DefinitionId);
	return Entry ? &Entry->InstanceData : nullptr;
}

const FShooterWeaponInstanceData* UShooterInventoryComponent::FindWeaponInstanceBySlot(
	int32 SlotIndex) const
{
	const FShooterWeaponInstanceEntry* Entry = ReplicatedInventory.FindItemBySlot(SlotIndex);
	return Entry ? &Entry->InstanceData : nullptr;
}

int32 UShooterInventoryComponent::FindFreeSlotIndex() const
{
	for (int32 SlotIndex = 0; SlotIndex < MaxWeaponSlots; ++SlotIndex)
	{
		if (!ReplicatedInventory.FindItemBySlot(SlotIndex))
		{
			return SlotIndex;
		}
	}

	return INDEX_NONE;
}

bool UShooterInventoryComponent::FindNextWeaponInstanceId(
	const FGuid& CurrentId,
	FGuid& OutNextId) const
{
	OutNextId = FGuid();
	const FShooterWeaponInstanceData* CurrentInstance = FindWeaponInstance(CurrentId);
	if (!CurrentInstance || GetWeaponCount() < 2)
	{
		return false;
	}

	const FShooterWeaponInstanceData* WrapCandidate = nullptr;
	const FShooterWeaponInstanceData* NextCandidate = nullptr;
	for (const FShooterWeaponInstanceEntry& Entry : GetWeaponEntries())
	{
		const FShooterWeaponInstanceData& Candidate = Entry.InstanceData;
		if (!WrapCandidate || Candidate.SlotIndex < WrapCandidate->SlotIndex)
		{
			WrapCandidate = &Candidate;
		}

		if (Candidate.SlotIndex > CurrentInstance->SlotIndex &&
			(!NextCandidate || Candidate.SlotIndex < NextCandidate->SlotIndex))
		{
			NextCandidate = &Candidate;
		}
	}

	const FShooterWeaponInstanceData* Target = NextCandidate ? NextCandidate : WrapCandidate;
	OutNextId = Target ? Target->InstanceId : FGuid();
	return OutNextId.IsValid();
}

AShooterWeapon* UShooterInventoryComponent::FindWeaponActor(const FGuid& InstanceId) const
{
	if (!InstanceId.IsValid())
	{
		return nullptr;
	}

	for (AShooterWeapon* Weapon : BoundWeaponActors)
	{
		if (IsValid(Weapon) && Weapon->GetBoundInstanceId() == InstanceId)
		{
			return Weapon;
		}
	}

	return nullptr;
}

void UShooterInventoryComponent::RegisterWeaponActor(AShooterWeapon* Weapon)
{
	if (!IsValid(Weapon))
	{
		return;
	}

	BoundWeaponActors.AddUnique(Weapon);
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Inventory registered WeaponActor: Actor=%s InstanceId=%s Weapon=%s"),
		*GetNameSafe(GetOwner()),
		*Weapon->GetBoundInstanceId().ToString(),
		*GetNameSafe(Weapon));
}

void UShooterInventoryComponent::UnregisterWeaponActor(AShooterWeapon* Weapon)
{
	if (!IsValid(Weapon))
	{
		return;
	}

	BoundWeaponActors.Remove(Weapon);
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Inventory unregistered WeaponActor: Actor=%s InstanceId=%s Weapon=%s"),
		*GetNameSafe(GetOwner()),
		*Weapon->GetBoundInstanceId().ToString(),
		*GetNameSafe(Weapon));
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
