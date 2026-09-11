// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShooterInventoryTypes.h"
#include "ShooterInventoryComponent.generated.h"

class AShooterWeapon;
class AShooterCharacter;
class UDataTable;
struct FShooterWeaponConfigRow;

DECLARE_MULTICAST_DELEGATE_OneParam(FShooterInventoryWeaponRemovedDelegate, const FGuid&);
DECLARE_MULTICAST_DELEGATE(FShooterInventoryClearedDelegate);

/** Inventory 新增武器结果。Pickup 与测试入口据此决定是否消费 Pickup。 */
UENUM(BlueprintType)
enum class EShooterInventoryAddResult : uint8
{
	Added,
	NotAuthoritative,
	InvalidInstance,
	DuplicateInstance,
	DuplicateWeaponRow,
	SlotOccupied,
	SlotFull,
	AcquireFailed,
	InvalidWeaponRow,
};

/**
 * 角色武器 Inventory 组件。
 *
 * 职责边界：
 * - 持有并复制 WeaponInstance FastArray（OwnerOnly）；
 * - 提供服务器权威的数据增删、查找与 WeaponActor 绑定入口；
 * - 2B 起承接 Pickup 的最终授予路径。
 */
UCLASS(ClassGroup=(Inventory), meta=(BlueprintSpawnableComponent))
class SHOOTGAME_API UShooterInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShooterInventoryComponent();

	virtual void InitializeComponent() override;

	/**
	 * 服务器权威唯一生产授予入口：按 DT_WeaponData 行名创建 WeaponInstance 与 WeaponActor，并自动选择空 Slot。
	 * WeaponActorClass、弹匣容量与初始备弹只由该行决定，不从 WeaponActor 默认值推导。
	 * 表缺失、行名为空、行缺失或行非法时 fail closed，调用方不得消费 Pickup。
	 */
	EShooterInventoryAddResult TryAddWeaponRow(
		FName WeaponRowName,
		FGuid& OutInstanceId);

	/** 服务器权威：新增一条武器实例数据。InstanceId 与 SlotIndex 均必须唯一。 */
	bool AddWeaponInstance(const FShooterWeaponInstanceData& InstanceData);

	/** 服务器权威：按 InstanceId 移除武器实例；移除事件由 Equipment 决定是否清空当前装备。 */
	bool RemoveWeaponInstance(const FGuid& InstanceId);

	/** 服务器权威：清空全部武器实例；清空事件由 Equipment 收敛当前装备。 */
	void ClearInventory();

	/** 按 InstanceId 查找武器实例；不存在时返回 nullptr。 */
	const FShooterWeaponInstanceData* FindWeaponInstance(const FGuid& InstanceId) const;

	/** 按武器模板行名查找武器实例；不存在时返回 nullptr。重复武器类型判定使用本入口。 */
	const FShooterWeaponInstanceData* FindWeaponInstanceByRowName(FName WeaponRowName) const;

	/** 按 SlotIndex 查找武器实例；不存在时返回 nullptr。 */
	const FShooterWeaponInstanceData* FindWeaponInstanceBySlot(int32 SlotIndex) const;

	/** 集中解析入口转发：RowName -> 武器模板行；未配置表、行名为空或行缺失时返回 nullptr。 */
	const FShooterWeaponConfigRow* ResolveWeaponRow(FName WeaponRowName) const;

	/** 返回本组件使用的武器模板表；未注入时使用固定的 DT_WeaponData。 */
	UDataTable* GetWeaponTable() const;

	/** 注入武器模板表；传 nullptr 恢复使用固定的 DT_WeaponData。 */
	void SetWeaponTable(UDataTable* InWeaponTable);

	/** 返回第一个空 SlotIndex；没有空位时返回 INDEX_NONE。 */
	int32 FindFreeSlotIndex() const;

	/** 返回 Slot 上限。 */
	int32 GetMaxWeaponSlots() const { return MaxWeaponSlots; }

	/** 调整 Slot 上限，供蓝图配置与测试使用。 */
	void SetMaxWeaponSlots(int32 NewMaxWeaponSlots) { MaxWeaponSlots = FMath::Max(0, NewMaxWeaponSlots); }

	/** 返回绑定到指定 InstanceId 的 WeaponActor；不存在时返回 nullptr。 */
	AShooterWeapon* FindWeaponActor(const FGuid& InstanceId) const;


	/** 按 Slot 顺序返回 CurrentId 之后的下一个 InstanceId；失败时返回 false。 */
	bool FindNextWeaponInstanceId(const FGuid& CurrentId, FGuid& OutNextId) const;

	/** WeaponActor 初始化或复制 BoundInstanceId 后注册到 Inventory。 */
	void RegisterWeaponActor(AShooterWeapon* Weapon);

	/** WeaponActor 解绑或销毁前从 Inventory 移除。 */
	void UnregisterWeaponActor(AShooterWeapon* Weapon);

	/** 返回 OwnerOnly FastArray 的只读入口。 */
	const TArray<FShooterWeaponInstanceEntry>& GetWeaponEntries() const { return ReplicatedInventory.Items; }

	/** 当前逻辑武器实例数量。 */
	int32 GetWeaponCount() const { return ReplicatedInventory.Items.Num(); }

	/** R4 迁移期只读转发：当前装备身份来自 Owner 的 EquipmentComponent。 */
	FGuid GetActiveWeaponInstanceId() const;

	/** R4 迁移期只读转发：当前装备 WeaponActor 来自 Owner 的 EquipmentComponent。 */
	AShooterWeapon* GetActiveWeaponActor() const;

	/** Equipment 订阅：指定 Instance 被移除（服务器与 Owner 客户端都会广播）。 */
	FShooterInventoryWeaponRemovedDelegate OnWeaponInstanceRemovedFromInventory;

	/** Equipment 订阅：Inventory 被整体清空（服务器与 Owner 客户端都会广播）。 */
	FShooterInventoryClearedDelegate OnInventoryCleared;

	/** 查询指定实例当前弹匣弹药；弹药权威在 WeaponActor，本入口只做转发。 */
	int32 GetMagazineAmmo(const FGuid& InstanceId) const;

	/** 查询指定实例当前备用弹药；弹药权威在 WeaponActor，本入口只做转发。 */
	int32 GetReserveAmmo(const FGuid& InstanceId) const;

	/** 指定实例是否还有弹匣弹药；转发 WeaponActor 权威判定。 */
	bool CanConsumeMagazineAmmo(const FGuid& InstanceId) const;

	/** 服务器权威：从指定实例弹匣扣除一发；转发 WeaponActor::ConsumeAmmo。 */
	bool ConsumeMagazineAmmo(const FGuid& InstanceId, int32 Amount = 1);

	/**
	 * 服务器权威换弹原子事务：转发 WeaponActor::ReloadFromReserve，
	 * 把 ReserveAmmo 一次性转进 MagazineAmmo 并推送 Owner HUD。
	 */
	bool ReloadMagazine(const FGuid& InstanceId, int32& OutTransferredAmmo);

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * 唯一授予入口的实例创建与 WeaponActor 获取事务；参数已由入口完成校验。
	 * 服务器权威：实例写入成功后从对象池 Acquire WeaponActor；获取失败必须回滚实例。
	 */
	EShooterInventoryAddResult TryAddWeaponInternal(
		FName WeaponRowName,
		const FShooterWeaponConfigRow& Row,
		FGuid& OutInstanceId);

	/**
	 * 归还 WeaponActor：池化武器走池 Release（Inventory Actor 映射由归还清理统一解除），
	 * 非池出生（NPC / 旧测试直接 Spawn）无法归还时回落销毁，保证不留悬挂引用。
	 */
	void ReleaseWeaponActor(AShooterWeapon* Weapon);

	/** Owner Client FastArray 删除回调：同步解除对应 WeaponActor 绑定。 */
	void HandleInstanceRemoved(const FGuid& InstanceId);

	/** 第一版固定 Slot 上限。SlotFull 时 Pickup 必须明确 Reject。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Inventory")
	int32 MaxWeaponSlots = 3;

	/**
	 * 可选武器模板表注入。留空表示使用 ShooterWeaponTable 的固定 DT_WeaponData；
	 * 用于蓝图配置覆盖与自动化测试隔离：测试注入的瞬态表由此保持强引用，不会被 GC 回收。
	 */
	UPROPERTY(EditDefaultsOnly, Category="Inventory")
	TObjectPtr<UDataTable> WeaponTable;

	/** 服务器维护的 InstanceId -> WeaponActor 绑定；客户端按需在 BoundInstanceId 到达时注册。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AShooterWeapon>> BoundWeaponActors;

	/** 完整 Inventory 只复制给 Owner；远端通过 Character.CurrentWeaponActor 获取公共表现。 */
	UPROPERTY(Replicated)
	FShooterWeaponInventoryList ReplicatedInventory;

};
