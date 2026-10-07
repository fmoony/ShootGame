// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "ShooterInventoryTypes.generated.h"

/**
 * 只前置声明 AShooterWeapon：本头文件仅持有 TObjectPtr 引用与槽位数据，
 * 任何需要武器完整类型的实现都在 Inventory/ShooterInventoryTypes.cpp 中。
 */
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

	/** Owner Client Remove 回调：需要武器完整类型，实现位于 ShooterInventoryTypes.cpp。 */
	void PreReplicatedRemove(const FShooterWeaponInventoryList& InArraySerializer);
	void PostReplicatedAdd(const FShooterWeaponInventoryList& InArraySerializer);
	void PostReplicatedChange(const FShooterWeaponInventoryList& InArraySerializer);

	/** 调试字符串，供 LogNetFastTArray 使用。 */
	FString GetDebugString() const;
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
	bool AddItem(AShooterWeapon* Weapon, int32 SlotIndex);

	/** 服务器移除入口：按 Actor 身份删除条目并标记数组脏。 */
	bool RemoveItem(AShooterWeapon* Weapon);

	void ClearItems();

	/** 按武器种类身份查找；不存在时返回 nullptr。重复 WeaponId 判定使用本入口。 */
	const FShooterInventoryWeaponEntry* FindItemByWeaponId(FName WeaponId) const;

	const FShooterInventoryWeaponEntry* FindItemBySlot(int32 SlotIndex) const;

	/** 按 Actor 身份查找条目；该 Actor 不在背包时返回 nullptr。 */
	const FShooterInventoryWeaponEntry* FindItem(const AShooterWeapon* Weapon) const;

	/** 按 Slot 顺序和 Direction（+1 升序、-1 降序）返回相邻武器，并在边界回绕。 */
	AShooterWeapon* FindAdjacentWeapon(const AShooterWeapon* CurrentWeapon, int32 Direction) const;
};

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
