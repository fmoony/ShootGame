// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "UObject/PrimaryAssetId.h"
#include "ShooterInventoryTypes.generated.h"

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
	 * 武器定义资产主 ID。
	 * UE5.6 的 FPrimaryAssetId 不是 UHT 反射类型，不能声明为 UPROPERTY；
	 * 它支持普通 FArchive << 序列化，但该能力不等于默认属性网络复制。
	 * 当前由 FShooterWeaponInstanceEntry::NetSerialize 显式序列化本字段。
	 */
	FPrimaryAssetId DefinitionId;

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
			DefinitionId.IsValid() &&
			MagazineAmmo >= 0 &&
			ReserveAmmo >= 0 &&
			SlotIndex >= 0;
	}

	FPrimaryAssetId GetDefinitionId() const { return DefinitionId; }
	void SetDefinitionId(const FPrimaryAssetId& InDefinitionId) { DefinitionId = InDefinitionId; }
};

struct FShooterWeaponInventoryList;

/**
 * FastArray 单项：复制层身份 + WeaponInstanceData。
 * 保留 FastArray 的 Item Add/Change/Remove Delta；关闭的是 Item 内部 Struct Delta，
 * 保证非反射字段 DefinitionId 也被完整复制。
 */
USTRUCT()
struct FShooterWeaponInstanceEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** 逻辑武器数据。 */
	UPROPERTY()
	FShooterWeaponInstanceData InstanceData;

	/**
	 * IMPORTANT：当前手动维护完整的 WeaponInstance 网络 Payload。
	 *
	 * 根因：UE5.6 的 FPrimaryAssetId 不是 UHT 反射类型，因此
	 * FShooterWeaponInstanceData::DefinitionId 不能声明为 UPROPERTY，
	 * 默认反射序列化看不到该字段。
	 *
	 * 维护约束：
	 * 1. 任何新增到 FShooterWeaponInstanceData 且需要复制给 Owner Client 的字段，
	 *    必须同步写入本函数；
	 * 2. 同时更新 Inventory / 网络复制自动化测试；
	 * 3. 不能因为字段已标记 UPROPERTY 就认为它已进入当前网络协议。
	 *
	 * 未来若 DefinitionId 改为项目可反射包装类型，再评估删除 WithNetSerializer
	 * 与恢复默认 Struct Delta；当前不执行该重构。
	 */
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		Ar << InstanceData.InstanceId;
		Ar << InstanceData.DefinitionId;
		Ar << InstanceData.MagazineAmmo;
		Ar << InstanceData.ReserveAmmo;
		Ar << InstanceData.SlotIndex;

		bOutSuccess = !Ar.IsError();
		return true;
	}

	void PreReplicatedRemove(const FShooterWeaponInventoryList& InArraySerializer) {}
	void PostReplicatedAdd(const FShooterWeaponInventoryList& InArraySerializer) {}
	void PostReplicatedChange(const FShooterWeaponInventoryList& InArraySerializer) {}

	/** 调试字符串，供 LogNetFastTArray 使用。 */
	FString GetDebugString() const
	{
		return FString::Printf(
			TEXT("InstanceId=%s Definition=%s Slot=%d Mag=%d Reserve=%d"),
			*InstanceData.InstanceId.ToString(),
			*InstanceData.DefinitionId.ToString(),
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

	FShooterWeaponInventoryList()
	{
		// 保留 FastArray 的 Item Add/Change/Remove Delta；这里关闭的只是 Item 内部 Struct Delta。
		// Struct Delta 只比较 UPROPERTY 反射字段，而 DefinitionId 是非反射字段；
		// 为可靠复制 DefinitionId-only 变更，强制每个 dirty Item 走完整 NetSerialize Payload。
		SetDeltaSerializationEnabled(false);
	}

	/** FastArray 要求的 Items 数组。 */
	UPROPERTY()
	TArray<FShooterWeaponInstanceEntry> Items;

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

	const FShooterWeaponInstanceEntry* FindItemByDefinitionId(const FPrimaryAssetId& DefinitionId) const
	{
		for (const FShooterWeaponInstanceEntry& Entry : Items)
		{
			if (Entry.InstanceData.DefinitionId == DefinitionId)
			{
				return &Entry;
			}
		}

		return nullptr;
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

/** WithNetSerializer：DefinitionId 是非反射字段，必须由 NetSerialize 手工复制；当前正确性依赖此配置。 */
template<>
struct TStructOpsTypeTraits<FShooterWeaponInstanceEntry> : public TStructOpsTypeTraitsBase2<FShooterWeaponInstanceEntry>
{
	enum
	{
		WithNetSerializer = true,
	};
};

template<>
struct TStructOpsTypeTraits<FShooterWeaponInventoryList> : public TStructOpsTypeTraitsBase2<FShooterWeaponInventoryList>
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};
