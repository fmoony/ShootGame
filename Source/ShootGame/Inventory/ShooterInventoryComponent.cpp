// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterInventoryComponent.h"

#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "ShootGame.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/ShooterWeaponRuntimeSubsystem.h"

UShooterInventoryComponent::UShooterInventoryComponent()
{
	// 组件随 Character 一起复制；数组属性本身使用 COND_OwnerOnly。
	SetIsReplicatedByDefault(true);

	// UE5.6 不会因子类重写 InitializeComponent 而自动置位该标志；
	// 不显式开启时 InitializeComponent 永不执行，Owner Client 的
	// FastArray Remove 通知（装备清空桥）不会被绑定。
	bWantsInitializeComponent = true;
}

void UShooterInventoryComponent::InitializeComponent()
{
	Super::InitializeComponent();

	// Owner Client 的 FastArray Remove 回调驱动本地装备镜像清空。
	ReplicatedInventory.OnWeaponEntryRemoved.AddUObject(
		this,
		&UShooterInventoryComponent::HandleWeaponEntryRemoved);
}

EShooterInventoryAddResult UShooterInventoryComponent::AddWeapon(AShooterWeapon* Weapon)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return EShooterInventoryAddResult::NotAuthoritative;
	}

	// 只接收已经由 WeaponRuntimeSubsystem Acquire 配置好的实体：
	// 必须有永久 WeaponId 身份、属于本角色且不处于池内。
	if (!IsValid(Weapon) ||
		Weapon->GetWeaponId().IsNone() ||
		Weapon->GetOwner() != GetOwner() ||
		Weapon->GetLifecycleState() == EShooterWeaponLifecycleState::InPool)
	{
		return EShooterInventoryAddResult::InvalidWeapon;
	}

	if (FindWeaponByWeaponId(Weapon->GetWeaponId()))
	{
		return EShooterInventoryAddResult::DuplicateWeapon;
	}

	const int32 FreeSlot = FindFreeSlotIndex();
	if (FreeSlot == INDEX_NONE)
	{
		return EShooterInventoryAddResult::SlotFull;
	}

	if (!ReplicatedInventory.AddItem(Weapon, FreeSlot))
	{
		return EShooterInventoryAddResult::SlotOccupied;
	}

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Inventory AddWeapon committed: Actor=%s WeaponId=%s Weapon=%s Slot=%d Count=%d"),
		*GetNameSafe(GetOwner()),
		*Weapon->GetWeaponId().ToString(),
		*GetNameSafe(Weapon),
		FreeSlot,
		GetWeaponCount());
	return EShooterInventoryAddResult::Added;
}

bool UShooterInventoryComponent::RemoveWeapon(AShooterWeapon* Weapon)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsValid(Weapon))
	{
		return false;
	}

	const bool bRemoved = ReplicatedInventory.RemoveItem(Weapon);
	if (bRemoved)
	{
		// E1 顺序：先广播移除，让 Equipment 在 WeaponActor 归还池之前清理当前装备。
		OnWeaponRemovedFromInventory.Broadcast(Weapon);
		ReleaseWeaponActor(Weapon);

		UE_LOG(
			LogShootGame,
			Display,
			TEXT("Inventory RemoveWeapon committed: Actor=%s WeaponId=%s Weapon=%s Count=%d"),
			*GetNameSafe(GetOwner()),
			*Weapon->GetWeaponId().ToString(),
			*GetNameSafe(Weapon),
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

	// E1 顺序：冻结待归还 WeaponActor，先清逻辑 Entries 并广播，
	// 让 Equipment 在归还池之前清理当前装备，最后统一归还。
	TArray<AShooterWeapon*> WeaponsToRelease;
	for (const FShooterInventoryWeaponEntry& Entry : ReplicatedInventory.Items)
	{
		if (IsValid(Entry.Weapon))
		{
			WeaponsToRelease.Add(Entry.Weapon);
		}
	}

	if (ReplicatedInventory.Items.Num() == 0 && WeaponsToRelease.Num() == 0)
	{
		// 重复 Clear / 空 Inventory 幂等返回，不重复广播错误状态。
		return;
	}

	ReplicatedInventory.ClearItems();
	OnInventoryCleared.Broadcast();

	for (AShooterWeapon* Weapon : WeaponsToRelease)
	{
		if (IsValid(Weapon))
		{
			ReleaseWeaponActor(Weapon);
		}
	}

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Inventory ClearInventory: Actor=%s Released=%d"),
		*GetNameSafe(GetOwner()),
		WeaponsToRelease.Num());
}

AShooterWeapon* UShooterInventoryComponent::FindWeaponByWeaponId(FName WeaponId) const
{
	const FShooterInventoryWeaponEntry* Entry = ReplicatedInventory.FindItemByWeaponId(WeaponId);
	return Entry ? Entry->Weapon.Get() : nullptr;
}

bool UShooterInventoryComponent::HasWeaponId(FName WeaponId) const
{
	return FindWeaponByWeaponId(WeaponId) != nullptr;
}

bool UShooterInventoryComponent::ContainsWeapon(const AShooterWeapon* Weapon) const
{
	return ReplicatedInventory.FindItem(Weapon) != nullptr;
}

AShooterWeapon* UShooterInventoryComponent::FindWeaponBySlot(int32 SlotIndex) const
{
	const FShooterInventoryWeaponEntry* Entry = ReplicatedInventory.FindItemBySlot(SlotIndex);
	return Entry ? Entry->Weapon.Get() : nullptr;
}

AShooterWeapon* UShooterInventoryComponent::FindNextWeapon(const AShooterWeapon* CurrentWeapon) const
{
	return ReplicatedInventory.FindNextWeapon(CurrentWeapon);
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

void UShooterInventoryComponent::HandleWeaponEntryRemoved(AShooterWeapon* Weapon)
{
	// Owner Client：背包 Entry 删除时，若它正是当前装备则清空本地装备镜像；
	// WeaponActor 本体由服务器归还池，通过复制（Owner 清空 / 隐藏）收敛表现。
	AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwner());
	UShooterEquipmentComponent* Equipment = Character
		? Character->GetEquipmentComponent()
		: nullptr;
	if (Equipment && Equipment->GetCurrentWeaponActor() == Weapon)
	{
		Equipment->ClearEquippedWeapon();
	}
}

void UShooterInventoryComponent::ReleaseWeaponActor(AShooterWeapon* Weapon)
{
	if (!IsValid(Weapon))
	{
		return;
	}

	// 唯一归还入口：运行时池租出的实体归还对应 WeaponId Bucket。
	UShooterWeaponRuntimeSubsystem* Runtime = GetWorld()
		? GetWorld()->GetSubsystem<UShooterWeaponRuntimeSubsystem>()
		: nullptr;
	if (Runtime && Runtime->ReleaseWeapon(Weapon))
	{
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("Inventory released WeaponActor to runtime pool: Actor=%s WeaponId=%s Weapon=%s"),
			*GetNameSafe(GetOwner()),
			*Weapon->GetWeaponId().ToString(),
			*GetNameSafe(Weapon));
		return;
	}

	// 兼容路径：非运行时池出生（NPC / 旧测试直接 Spawn）无法归还，只能销毁。
	Weapon->Destroy();
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Inventory destroyed non-pooled WeaponActor: Actor=%s WeaponId=%s Weapon=%s"),
		*GetNameSafe(GetOwner()),
		*Weapon->GetWeaponId().ToString(),
		*GetNameSafe(Weapon));
}

void UShooterInventoryComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 完整 Inventory 只复制给 Owner；远端只通过 Equipment.CurrentWeaponActor 表现当前持枪。
	DOREPLIFETIME_CONDITION(UShooterInventoryComponent, ReplicatedInventory, COND_OwnerOnly);
}
