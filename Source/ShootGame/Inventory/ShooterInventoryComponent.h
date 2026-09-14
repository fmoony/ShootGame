// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Inventory/ShooterInventoryTypes.h"
#include "ShooterInventoryComponent.generated.h"

class AShooterWeapon;
class AShooterCharacter;

DECLARE_MULTICAST_DELEGATE_OneParam(FShooterInventoryWeaponRemovedDelegate, AShooterWeapon*);
DECLARE_MULTICAST_DELEGATE(FShooterInventoryClearedDelegate);

/** Inventory 新增武器结果。Pickup 与测试入口据此决定是否消费与归还。 */
UENUM(BlueprintType)
enum class EShooterInventoryAddResult : uint8
{
	Added,
	NotAuthoritative,
	InvalidWeapon,
	DuplicateWeapon,
	SlotOccupied,
	SlotFull,
};

/**
 * 角色武器 Inventory 组件（重构方案 4.4）。
 *
 * 职责边界：
 * - 持有并复制最小 Actor Entry FastArray（WeaponActor + SlotIndex，OwnerOnly）；
 * - 接收已经由 WeaponRuntimeSubsystem Acquire 配置好的 WeaponActor；
 * - 按 WeaponActor.WeaponId 判重，按 Slot 查询与遍历；
 * - Add / Remove / Clear 时维护 Actor 引用；
 * - Remove / Clear 先广播让 Equipment 清空当前装备，再把 Actor 归还运行时池。
 *
 * 不再：读取 DataTable、生成 GUID、保存弹药或维护任何实例身份映射。
 */
UCLASS(ClassGroup=(Inventory), meta=(BlueprintSpawnableComponent))
class SHOOTGAME_API UShooterInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShooterInventoryComponent();

	virtual void InitializeComponent() override;

	/**
	 * 服务器权威：向 Inventory 提交一把已经 Acquire 配置好的 WeaponActor，并自动选择空 Slot。
	 * 调用方（Pickup / 测试）必须先从 WeaponRuntimeSubsystem Acquire；提交失败时调用方负责
	 * 把 Actor Release 回池。判重按 WeaponActor.GetWeaponId()。
	 */
	EShooterInventoryAddResult AddWeapon(AShooterWeapon* Weapon);

	/** 服务器权威：移除一把武器；先广播移除（Equipment 清空当前装备），再把 Actor 归还运行时池。 */
	bool RemoveWeapon(AShooterWeapon* Weapon);

	/** 服务器权威：清空全部武器；先清 Entries 并广播（Equipment 收敛装备），最后统一归还。 */
	void ClearInventory();

	/** 按武器种类身份查找所持 WeaponActor；不存在时返回 nullptr。重复类型判定使用本入口。 */
	AShooterWeapon* FindWeaponByWeaponId(FName WeaponId) const;

	/** 是否已持有该武器种类。 */
	bool HasWeaponId(FName WeaponId) const;

	/** 是否持有指定的 WeaponActor（GAS 提交前校验目标仍在背包）。 */
	bool ContainsWeapon(const AShooterWeapon* Weapon) const;

	/** 按 SlotIndex 查找 WeaponActor；不存在时返回 nullptr。 */
	AShooterWeapon* FindWeaponBySlot(int32 SlotIndex) const;

	/** 按 Slot 顺序和 Direction（+1 升序、-1 降序）返回相邻武器；单武器或未持有时返回 nullptr。 */
	AShooterWeapon* FindAdjacentWeapon(const AShooterWeapon* CurrentWeapon, int32 Direction) const;

	/** 返回第一个空 SlotIndex；没有空位时返回 INDEX_NONE。 */
	int32 FindFreeSlotIndex() const;

	/** 返回 Slot 上限。 */
	int32 GetMaxWeaponSlots() const { return MaxWeaponSlots; }

	/** 调整 Slot 上限，供蓝图配置与测试使用。 */
	void SetMaxWeaponSlots(int32 NewMaxWeaponSlots) { MaxWeaponSlots = FMath::Max(0, NewMaxWeaponSlots); }

	/** 返回 OwnerOnly FastArray 的只读入口。 */
	const TArray<FShooterInventoryWeaponEntry>& GetWeaponEntries() const { return ReplicatedInventory.Items; }

	/** 当前武器数量。 */
	int32 GetWeaponCount() const { return ReplicatedInventory.Items.Num(); }

	/** Equipment 订阅：指定 WeaponActor 被移除（服务器与 Owner 客户端都会广播）。 */
	FShooterInventoryWeaponRemovedDelegate OnWeaponRemovedFromInventory;

	/** Equipment 订阅：Inventory 被整体清空（服务器与 Owner 客户端都会广播）。 */
	FShooterInventoryClearedDelegate OnInventoryCleared;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 归还 WeaponActor 到 WeaponRuntimeSubsystem；非运行时池管理的武器回落销毁。 */
	void ReleaseWeaponActor(AShooterWeapon* Weapon);

	/** Owner Client FastArray 删除回调：同步清空本地装备镜像。 */
	void HandleWeaponEntryRemoved(AShooterWeapon* Weapon);

	/** 第一版固定 Slot 上限。SlotFull 时 Pickup 必须明确 Reject。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Inventory")
	int32 MaxWeaponSlots = 3;

	/** 完整 Inventory 只复制给 Owner；远端通过 Character.CurrentWeaponActor 获取公共表现。 */
	UPROPERTY(Replicated)
	FShooterWeaponInventoryList ReplicatedInventory;
};
