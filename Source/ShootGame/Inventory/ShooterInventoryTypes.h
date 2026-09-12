// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Weapons/ShooterWeapon.h"
#include "ShooterInventoryTypes.generated.h"

class AShooterWeapon;

DECLARE_MULTICAST_DELEGATE_OneParam(FShooterInventoryWeaponRemovedDelegate, AShooterWeapon*);

/**
 * Inventory 的单件武器 Entry：只保存 WeaponActor 引用与背包槽位（重构方案 4.4）。
 *
 * - 武器种类身份在 WeaponActor.WeaponId 上（创建后不变）；
 * - 弹药权威在 WeaponActor（MagazineAmmo / ReserveAmmo）；
 * - InstanceId / RowName 不再进入背包数据。
 * Actor 引用与 WeaponId 的复制到达顺序不作假设，消费端表现入口必须幂等。
 */
USTRUCT()
struct FShooterInventoryWeaponEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** 本槽位上的池化武器实体。 */
	UPROPERTY()
	TObjectPtr<AShooterWeapon> Weapon = nullptr;

	/** 背包槽位；同一 Inventory 内唯一。 */
	UPROPERTY()
	int32 SlotIndex = INDEX_NONE;

	void PreReplicatedRemove(const FShooterWeaponInventoryList& InArraySerializer);
	void PostReplicatedAdd(const FShooterWeaponInventoryList& InArraySerializer);
	void PostReplicatedChange(const FShooterWeaponInventoryList& InArraySerializer);

	/** 调试字符串，供 LogNetFastTArray 使用。 */
	FString GetDebugString() const
	{
		return FString::Printf(TEXT("Weapon=%s WeaponId=%s Slot=%d"), *GetNameSafe(Weapon),
			Weapon ? *Weapon->GetWeaponId().ToString() : TEXT("None"), SlotIndex);
	}
};

/**
 * Inventory 的 FastArray 容器。
 * 增删必须通过本结构体的 AddItem / RemoveItem / ClearItems，确保 MarkItemDirty / MarkArrayDirty 正确。
 */
USTRUCT()
struct FShooterWeaponInventoryList : public FFastArraySerializer
{
	GENERATED_BODY()

	/** FastArray 要求的 Items 数组。 */
	UPROPERTY()
	TArray<FShooterInventoryWeaponEntry> Items;

	/** Owner Client 收到 Remove 后的解绑桥接；由 InventoryComponent 绑定。 */
	FShooterInventoryWeaponRemovedDelegate OnWeaponEntryRemoved;

	void NotifyWeaponEntryRemoved(AShooterWeapon* Weapon) const
	{
		OnWeaponEntryRemoved.Broadcast(Weapon);
	}

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<
			FShooterInventoryWeaponEntry,
			FShooterWeaponInventoryList>(Items, DeltaParms, *this);
	}

	/** 服务器写入入口：校验武器有效、Actor 唯一、Slot 唯一后加入。 */
	bool AddItem(AShooterWeapon* Weapon, int32 SlotIndex)
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

	bool RemoveItem(AShooterWeapon* Weapon)
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

	void ClearItems()
	{
		if (Items.Num() > 0)
		{
			Items.Empty();
			MarkArrayDirty();
		}
	}

	/** 按武器种类身份查找；不存在时返回 nullptr。重复 WeaponId 判定使用本入口。 */
	const FShooterInventoryWeaponEntry* FindItemByWeaponId(FName WeaponId) const
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

	const FShooterInventoryWeaponEntry* FindItemBySlot(int32 SlotIndex) const
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

	const FShooterInventoryWeaponEntry* FindItem(const AShooterWeapon* Weapon) const
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

	/** 按 Slot 顺序返回 Current 之后的下一把武器；到达末尾时回绕到最小 Slot。 */
	AShooterWeapon* FindNextWeapon(const AShooterWeapon* CurrentWeapon) const
	{
		if (Items.Num() < 2 || !CurrentWeapon)
		{
			return nullptr;
		}

		const FShooterInventoryWeaponEntry* Current = FindItem(CurrentWeapon);
		if (!Current)
		{
			return nullptr;
		}

		const FShooterInventoryWeaponEntry* WrapCandidate = nullptr;
		const FShooterInventoryWeaponEntry* NextCandidate = nullptr;
		for (const FShooterInventoryWeaponEntry& Candidate : Items)
		{
			if (!Candidate.Weapon)
			{
				continue;
			}

			if (!WrapCandidate || Candidate.SlotIndex < WrapCandidate->SlotIndex)
			{
				WrapCandidate = &Candidate;
			}

			if (Candidate.SlotIndex > Current->SlotIndex && (!NextCandidate || Candidate.SlotIndex < NextCandidate->SlotIndex))
			{
				NextCandidate = &Candidate;
			}
		}

		const FShooterInventoryWeaponEntry* Target = NextCandidate ? NextCandidate : WrapCandidate;
		return Target ? Target->Weapon.Get() : nullptr;
	}
};

FORCEINLINE void FShooterInventoryWeaponEntry::PreReplicatedRemove(const FShooterWeaponInventoryList& InArraySerializer)
{
	// Owner Client Remove 回调：InventoryComponent 统一处理解绑与表现收敛。
	if (Weapon)
	{
		InArraySerializer.NotifyWeaponEntryRemoved(Weapon);
	}
}

FORCEINLINE void FShooterInventoryWeaponEntry::PostReplicatedAdd(const FShooterWeaponInventoryList& InArraySerializer)
{
}

FORCEINLINE void FShooterInventoryWeaponEntry::PostReplicatedChange(const FShooterWeaponInventoryList& InArraySerializer)
{
}

template<>
struct TStructOpsTypeTraits<FShooterWeaponInventoryList> : public TStructOpsTypeTraitsBase2<FShooterWeaponInventoryList>
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};
