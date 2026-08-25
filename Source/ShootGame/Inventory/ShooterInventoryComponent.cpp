// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterInventoryComponent.h"

#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
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

void UShooterInventoryComponent::InitializeComponent()
{
	Super::InitializeComponent();

	// Owner Client 的 FastArray Add/Change/Remove 回调驱动本地 WeaponActor 弹药镜像与 HUD。
	ReplicatedInventory.OnInstanceChanged.AddUObject(
		this,
		&UShooterInventoryComponent::HandleInstanceChanged);
	ReplicatedInventory.OnInstanceRemoved.AddUObject(
		this,
		&UShooterInventoryComponent::HandleInstanceRemoved);
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
		if (AShooterWeapon* Weapon = FindWeaponActor(InstanceId))
		{
			UnregisterWeaponActor(Weapon);
			Weapon->Destroy();
		}

		OnWeaponInstanceRemovedFromInventory.Broadcast(InstanceId);

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

	for (AShooterWeapon* Weapon : BoundWeaponActors)
	{
		if (IsValid(Weapon))
		{
			Weapon->Destroy();
		}
	}
	BoundWeaponActors.Empty();
	ReplicatedInventory.ClearItems();
	OnInventoryCleared.Broadcast();

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
	return ReplicatedInventory.FindNextItemId(CurrentId, OutNextId);
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

int32 UShooterInventoryComponent::GetMagazineAmmo(const FGuid& InstanceId) const
{
	const FShooterWeaponInstanceData* Instance = FindWeaponInstance(InstanceId);
	return Instance ? Instance->MagazineAmmo : 0;
}

int32 UShooterInventoryComponent::GetReserveAmmo(const FGuid& InstanceId) const
{
	const FShooterWeaponInstanceData* Instance = FindWeaponInstance(InstanceId);
	return Instance ? Instance->ReserveAmmo : 0;
}

bool UShooterInventoryComponent::CanConsumeMagazineAmmo(const FGuid& InstanceId) const
{
	return GetMagazineAmmo(InstanceId) > 0;
}

bool UShooterInventoryComponent::ConsumeMagazineAmmo(const FGuid& InstanceId, int32 Amount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	if (!ReplicatedInventory.ConsumeMagazineAmmo(InstanceId, Amount))
	{
		return false;
	}

	if (const FShooterWeaponInstanceData* Instance = FindWeaponInstance(InstanceId))
	{
		HandleInstanceChanged(*Instance);
	}
	return true;
}

bool UShooterInventoryComponent::ReloadMagazine(
	const FGuid& InstanceId,
	int32& OutTransferredAmmo)
{
	OutTransferredAmmo = 0;
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	// 事务容量只来自绑定 WeaponActor 的权威配置；没有 Actor 时不能执行表现不可收敛的换弹。
	AShooterWeapon* Weapon = FindWeaponActor(InstanceId);
	if (!IsValid(Weapon))
	{
		return false;
	}

	if (!ReplicatedInventory.ReloadMagazine(
		InstanceId,
		Weapon->GetMagazineSize(),
		OutTransferredAmmo))
	{
		return false;
	}

	// 服务器本地立即刷新 WeaponActor 镜像与 Owner HUD；Owner 客户端由 FastArray Change 回调刷新。
	if (const FShooterWeaponInstanceData* Instance = FindWeaponInstance(InstanceId))
	{
		HandleInstanceChanged(*Instance);
	}

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Inventory ReloadMagazine committed: Actor=%s InstanceId=%s Transfer=%d Mag=%d Reserve=%d"),
		*GetNameSafe(GetOwner()),
		*InstanceId.ToString(),
		OutTransferredAmmo,
		GetMagazineAmmo(InstanceId),
		GetReserveAmmo(InstanceId));
	return true;
}

void UShooterInventoryComponent::HandleInstanceChanged(
	const FShooterWeaponInstanceData& InstanceData)
{
	if (AShooterWeapon* Weapon = FindWeaponActor(InstanceData.InstanceId))
	{
		Weapon->RefreshAmmoMirror();
	}
}

void UShooterInventoryComponent::HandleInstanceRemoved(const FGuid& InstanceId)
{
	if (AShooterWeapon* Weapon = FindWeaponActor(InstanceId))
	{
		UnregisterWeaponActor(Weapon);
		Weapon->SetBoundInstanceId(FGuid());
	}
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

FGuid UShooterInventoryComponent::GetActiveWeaponInstanceId() const
{
	const AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwner());
	const UShooterEquipmentComponent* Equipment = Character
		? Character->GetEquipmentComponent()
		: nullptr;
	return Equipment ? Equipment->GetActiveWeaponInstanceId() : FGuid();
}

AShooterWeapon* UShooterInventoryComponent::GetActiveWeaponActor() const
{
	const AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwner());
	const UShooterEquipmentComponent* Equipment = Character
		? Character->GetEquipmentComponent()
		: nullptr;
	return Equipment ? Equipment->GetCurrentWeaponActor() : nullptr;
}

void UShooterInventoryComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 完整 Inventory 只复制给 Owner；远端只通过 Equipment.CurrentWeaponActor 表现当前持枪。
	DOREPLIFETIME_CONDITION(UShooterInventoryComponent, ReplicatedInventory, COND_OwnerOnly);
}
