// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "ShooterInventoryTypes.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(
	FShooterWeaponInstanceChangedDelegate,
	const FShooterWeaponInstanceData&);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FShooterWeaponInstanceRemovedDelegate,
	const FGuid&);

/**
 * 单把武器实例的权威逻辑数据。
 *
 * WeaponInstanceData 是逻辑数据 / 权威状态；
 * WeaponActor 是世界实体 / 表现实体；
 * InstanceId 是稳定逻辑身份。
 */
USTRUCT(BlueprintType)
struct FShooterWeaponInstanceData
{
	GENERATED_BODY()

	/** 稳定逻辑身份，服务器生成且不可变。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Inventory")
	FGuid InstanceId;

	/**
	 * 武器模板表（DT_WeaponData）行名：武器类型身份与唯一只读配置来源。
	 * FName 是 UHT 反射类型，直接进入默认属性网络复制，不再需要手写 NetSerialize。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Inventory")
	FName WeaponRowName;

	/** 当前弹匣弹药，权威位置在本结构体。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Inventory")
	int32 MagazineAmmo = 0;

	/** 当前备用弹药，权威位置在本结构体。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Inventory")
	int32 ReserveAmmo = 0;

	/** 固定槽位，第一版要求槽位唯一。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Inventory")
	int32 SlotIndex = INDEX_NONE;

	/** 返回该实例是否具备可写入 Inventory 的最小合法数据。 */
	bool IsValid() const
	{
		return InstanceId.IsValid() &&
			!WeaponRowName.IsNone() &&
			MagazineAmmo >= 0 &&
			ReserveAmmo >= 0 &&
			SlotIndex >= 0;
	}
};

struct FShooterWeaponInventoryList;

/**
 * FastArray 单项：复制层身份 + WeaponInstanceData。
 * InstanceData 只含 UHT 反射字段（FGuid / FName / int32），因此恢复 UE 默认的
 * Struct Delta 序列化：服务器只发送真正变化的字段，不再手工维护整包 Payload。
 */
USTRUCT()
struct FShooterWeaponInstanceEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** 逻辑武器数据。 */
	UPROPERTY()
	FShooterWeaponInstanceData InstanceData;

	void PreReplicatedRemove(const FShooterWeaponInventoryList& InArraySerializer);
	void PostReplicatedAdd(const FShooterWeaponInventoryList& InArraySerializer);
	void PostReplicatedChange(const FShooterWeaponInventoryList& InArraySerializer);

	/** 调试字符串，供 LogNetFastTArray 使用。 */
	FString GetDebugString() const
	{
		return FString::Printf(
			TEXT("InstanceId=%s Row=%s Slot=%d Mag=%d Reserve=%d"),
			*InstanceData.InstanceId.ToString(),
			*InstanceData.WeaponRowName.ToString(),
			InstanceData.SlotIndex,
			InstanceData.MagazineAmmo,
			InstanceData.ReserveAmmo);
	}
};

/**
 * Inventory 的 FastArray 容器。
 * 增删改必须通过本结构体的 AddItem / RemoveItem / ClearItems，确保 MarkItemDirty / MarkArrayDirty 正确。
 */
USTRUCT()
struct FShooterWeaponInventoryList : public FFastArraySerializer
{
	GENERATED_BODY()

	/** FastArray 要求的 Items 数组。 */
	UPROPERTY()
	TArray<FShooterWeaponInstanceEntry> Items;

	/** Owner Client 收到 Add/Change/Remove 后的表现桥接；由 InventoryComponent 绑定。 */
	FShooterWeaponInstanceChangedDelegate OnInstanceChanged;
	FShooterWeaponInstanceRemovedDelegate OnInstanceRemoved;

	void NotifyInstanceChanged(const FShooterWeaponInstanceData& InstanceData) const
	{
		OnInstanceChanged.Broadcast(InstanceData);
	}

	void NotifyInstanceRemoved(const FGuid& InstanceId) const
	{
		OnInstanceRemoved.Broadcast(InstanceId);
	}

	/** 服务器权威扣减：当前弹匣不足时返回 false。 */
	bool ConsumeMagazineAmmo(const FGuid& InstanceId, int32 Amount = 1)
	{
		if (!InstanceId.IsValid() || Amount <= 0)
		{
			return false;
		}

		FShooterWeaponInstanceEntry* Entry = FindItem(InstanceId);
		if (!Entry || Entry->InstanceData.MagazineAmmo < Amount)
		{
			return false;
		}

		Entry->InstanceData.MagazineAmmo -= Amount;
		MarkItemDirty(*Entry);
		return true;
	}

	/**
	 * 服务器权威换弹原子事务：在同一次写入中把 ReserveAmmo 转移进 MagazineAmmo。
	 * Transfer = Min(MagazineCapacity - MagazineAmmo, ReserveAmmo)；
	 * Transfer 不大于 0（弹匣已满或无备用弹药）时返回 false 且不产生任何变化。
	 */
	bool ReloadMagazine(
		const FGuid& InstanceId,
		int32 MagazineCapacity,
		int32& OutTransferredAmmo)
	{
		OutTransferredAmmo = 0;
		if (!InstanceId.IsValid() || MagazineCapacity <= 0)
		{
			return false;
		}

		FShooterWeaponInstanceEntry* Entry = FindItem(InstanceId);
		if (!Entry)
		{
			return false;
		}

		const int32 Need = FMath::Max(
			0,
			MagazineCapacity - Entry->InstanceData.MagazineAmmo);
		const int32 Transfer = FMath::Min(Need, Entry->InstanceData.ReserveAmmo);
		if (Transfer <= 0)
		{
			return false;
		}

		Entry->InstanceData.MagazineAmmo += Transfer;
		Entry->InstanceData.ReserveAmmo -= Transfer;
		MarkItemDirty(*Entry);
		OutTransferredAmmo = Transfer;
		return true;
	}

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<
			FShooterWeaponInstanceEntry,
			FShooterWeaponInventoryList>(Items, DeltaParms, *this);
	}

	/** 服务器写入入口：校验数据合法、InstanceId 唯一、SlotIndex 唯一后加入。 */
	bool AddItem(const FShooterWeaponInstanceData& InstanceData)
	{
		if (!InstanceData.IsValid() || FindItem(InstanceData.InstanceId))
		{
			return false;
		}

		for (const FShooterWeaponInstanceEntry& Entry : Items)
		{
			if (Entry.InstanceData.SlotIndex == InstanceData.SlotIndex)
			{
				return false;
			}
		}

		const int32 NewIndex = Items.AddDefaulted();
		FShooterWeaponInstanceEntry& NewEntry = Items[NewIndex];
		NewEntry.InstanceData = InstanceData;
		MarkItemDirty(NewEntry);
		return true;
	}

	bool RemoveItem(const FGuid& InstanceId)
	{
		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			if (Items[Index].InstanceData.InstanceId == InstanceId)
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

	const FShooterWeaponInstanceEntry* FindItem(const FGuid& InstanceId) const
	{
		for (const FShooterWeaponInstanceEntry& Entry : Items)
		{
			if (Entry.InstanceData.InstanceId == InstanceId)
			{
				return &Entry;
			}
		}

		return nullptr;
	}

	const FShooterWeaponInstanceEntry* FindItemBySlot(int32 SlotIndex) const
	{
		for (const FShooterWeaponInstanceEntry& Entry : Items)
		{
			if (Entry.InstanceData.SlotIndex == SlotIndex)
			{
				return &Entry;
			}
		}

		return nullptr;
	}

	/** 按武器模板行名查找实例；不存在时返回 nullptr。重复类型判定也使用本入口。 */
	const FShooterWeaponInstanceEntry* FindItemByRowName(FName WeaponRowName) const
	{
		if (WeaponRowName.IsNone())
		{
			return nullptr;
		}

		for (const FShooterWeaponInstanceEntry& Entry : Items)
		{
			if (Entry.InstanceData.WeaponRowName == WeaponRowName)
			{
				return &Entry;
			}
		}

		return nullptr;
	}

	/** 按 Slot 顺序返回 CurrentId 之后的下一个实例；到达末尾时回绕到最小 Slot。 */
	bool FindNextItemId(const FGuid& CurrentId, FGuid& OutNextId) const
	{
		OutNextId = FGuid();
		const FShooterWeaponInstanceEntry* Current = FindItem(CurrentId);
		if (!Current || Items.Num() < 2)
		{
			return false;
		}

		const FShooterWeaponInstanceEntry* WrapCandidate = nullptr;
		const FShooterWeaponInstanceEntry* NextCandidate = nullptr;
		for (const FShooterWeaponInstanceEntry& Candidate : Items)
		{
			if (!WrapCandidate ||
				Candidate.InstanceData.SlotIndex < WrapCandidate->InstanceData.SlotIndex)
			{
				WrapCandidate = &Candidate;
			}

			if (Candidate.InstanceData.SlotIndex > Current->InstanceData.SlotIndex &&
				(!NextCandidate ||
					Candidate.InstanceData.SlotIndex < NextCandidate->InstanceData.SlotIndex))
			{
				NextCandidate = &Candidate;
			}
		}

		const FShooterWeaponInstanceEntry* Target = NextCandidate
			? NextCandidate
			: WrapCandidate;
		OutNextId = Target ? Target->InstanceData.InstanceId : FGuid();
		return OutNextId.IsValid();
	}

	FShooterWeaponInstanceEntry* FindItem(const FGuid& InstanceId)
	{
		for (FShooterWeaponInstanceEntry& Entry : Items)
		{
			if (Entry.InstanceData.InstanceId == InstanceId)
			{
				return &Entry;
			}
		}

		return nullptr;
	}
};

FORCEINLINE void FShooterWeaponInstanceEntry::PreReplicatedRemove(
	const FShooterWeaponInventoryList& InArraySerializer)
{
	InArraySerializer.NotifyInstanceRemoved(InstanceData.InstanceId);
}

FORCEINLINE void FShooterWeaponInstanceEntry::PostReplicatedAdd(
	const FShooterWeaponInventoryList& InArraySerializer)
{
	InArraySerializer.NotifyInstanceChanged(InstanceData);
}

FORCEINLINE void FShooterWeaponInstanceEntry::PostReplicatedChange(
	const FShooterWeaponInventoryList& InArraySerializer)
{
	InArraySerializer.NotifyInstanceChanged(InstanceData);
}

template<>
struct TStructOpsTypeTraits<FShooterWeaponInventoryList> : public TStructOpsTypeTraitsBase2<FShooterWeaponInventoryList>
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};
