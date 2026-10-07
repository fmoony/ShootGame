// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterInventoryTypes.h"

#include "Weapons/ShooterWeapon.h"

/**
 * 需要 AShooterWeapon 完整类型的实现集中在本文件：
 * 成员访问（WeaponId / 生命周期）、TObjectPtr 与裸指针的比较与赋值。
 */

FString FShooterInventoryWeaponEntry::GetDebugString() const
{
	return FString::Printf(TEXT("Weapon=%s WeaponId=%s Slot=%d"), *GetNameSafe(Weapon),
		Weapon ? *Weapon->GetWeaponId().ToString() : TEXT("None"), SlotIndex);
}

void FShooterInventoryWeaponEntry::PreReplicatedRemove(const FShooterWeaponInventoryList& InArraySerializer)
{
	// Owner Client Remove 回调：InventoryComponent 统一处理解绑与表现收敛。
	if (Weapon)
	{
		InArraySerializer.NotifyWeaponEntryRemoved(Weapon);
	}
}

bool FShooterWeaponInventoryList::AddItem(AShooterWeapon* Weapon, int32 SlotIndex)
{
	if (!Weapon || SlotIndex < 0)
	{
		return false;
	}

	for (const FShooterInventoryWeaponEntry& Entry : Items)
	{
		if (Entry.Weapon == Weapon || Entry.SlotIndex == SlotIndex)
		{
			return false;
		}
	}

	FShooterInventoryWeaponEntry& NewEntry = Items.AddDefaulted_GetRef();
	NewEntry.Weapon = Weapon;
	NewEntry.SlotIndex = SlotIndex;
	MarkItemDirty(NewEntry);
	return true;
}

bool FShooterWeaponInventoryList::RemoveItem(AShooterWeapon* Weapon)
{
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		if (Items[Index].Weapon == Weapon)
		{
			Items.RemoveAt(Index);
			MarkArrayDirty();
			return true;
		}
	}

	return false;
}

void FShooterWeaponInventoryList::ClearItems()
{
	if (Items.Num() > 0)
	{
		Items.Empty();
		MarkArrayDirty();
	}
}

const FShooterInventoryWeaponEntry* FShooterWeaponInventoryList::FindItemByWeaponId(FName WeaponId) const
{
	if (WeaponId.IsNone())
	{
		return nullptr;
	}

	for (const FShooterInventoryWeaponEntry& Entry : Items)
	{
		if (Entry.Weapon && Entry.Weapon->GetWeaponId() == WeaponId)
		{
			return &Entry;
		}
	}

	return nullptr;
}

const FShooterInventoryWeaponEntry* FShooterWeaponInventoryList::FindItemBySlot(int32 SlotIndex) const
{
	for (const FShooterInventoryWeaponEntry& Entry : Items)
	{
		if (Entry.SlotIndex == SlotIndex)
		{
			return &Entry;
		}
	}

	return nullptr;
}

const FShooterInventoryWeaponEntry* FShooterWeaponInventoryList::FindItem(const AShooterWeapon* Weapon) const
{
	for (const FShooterInventoryWeaponEntry& Entry : Items)
	{
		if (Entry.Weapon == Weapon)
		{
			return &Entry;
		}
	}

	return nullptr;
}

AShooterWeapon* FShooterWeaponInventoryList::FindAdjacentWeapon(const AShooterWeapon* CurrentWeapon, int32 Direction) const
{
	if (Items.Num() < 2 || !CurrentWeapon || Direction == 0)
	{
		return nullptr;
	}

	const FShooterInventoryWeaponEntry* Current = FindItem(CurrentWeapon);
	if (!Current)
	{
		return nullptr;
	}

	const bool bForward = Direction > 0;
	const FShooterInventoryWeaponEntry* WrapCandidate = nullptr;
	const FShooterInventoryWeaponEntry* AdjacentCandidate = nullptr;
	for (const FShooterInventoryWeaponEntry& Candidate : Items)
	{
		if (!Candidate.Weapon)
		{
			continue;
		}

		if (!WrapCandidate || (bForward && Candidate.SlotIndex < WrapCandidate->SlotIndex) ||
			(!bForward && Candidate.SlotIndex > WrapCandidate->SlotIndex))
		{
			WrapCandidate = &Candidate;
		}

		const bool bIsInDirection = bForward
			? Candidate.SlotIndex > Current->SlotIndex
			: Candidate.SlotIndex < Current->SlotIndex;
		const bool bIsCloser = !AdjacentCandidate || (bForward && Candidate.SlotIndex < AdjacentCandidate->SlotIndex) ||
			(!bForward && Candidate.SlotIndex > AdjacentCandidate->SlotIndex);
		if (bIsInDirection && bIsCloser)
		{
			AdjacentCandidate = &Candidate;
		}
	}

	const FShooterInventoryWeaponEntry* Target = AdjacentCandidate ? AdjacentCandidate : WrapCandidate;
	return Target ? Target->Weapon.Get() : nullptr;
}
