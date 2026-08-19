// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShooterInventoryTypes.h"
#include "ShooterInventoryComponent.generated.h"

class AShooterWeapon;

/** Inventory 新增武器结果。Pickup 与测试入口据此决定是否消费 Pickup。 */
UENUM(BlueprintType)
enum class EShooterInventoryAddResult : uint8
{
	Added,
	InvalidWeaponClass,
	NotAuthoritative,
	InvalidInstance,
	DuplicateInstance,
	DuplicateDefinition,
	SlotOccupied,
	SlotFull,
	SpawnFailed,
};

/**
 * 角色武器 Inventory 组件。
 *
 * 职责边界：
 * - 持有并复制 WeaponInstance FastArray（OwnerOnly）；
 * - 持有 ActiveWeaponInstanceId 数据字段（OwnerOnly）；
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

	/** 服务器权威：按 WeaponClass 创建 WeaponInstance 与 WeaponActor，并自动选择空 Slot。 */
	EShooterInventoryAddResult TryAddWeapon(
		TSubclassOf<AShooterWeapon> WeaponClass,
		FGuid& OutInstanceId);

	/** 服务器权威：新增一条武器实例数据。InstanceId 与 SlotIndex 均必须唯一。 */
	bool AddWeaponInstance(const FShooterWeaponInstanceData& InstanceData);

	/** 服务器权威：按 InstanceId 移除武器实例。移除当前 Active 实例时同步清空 ActiveWeaponInstanceId。 */
	bool RemoveWeaponInstance(const FGuid& InstanceId);

	/** 服务器权威：清空全部武器实例并清空 ActiveWeaponInstanceId。 */
	void ClearInventory();

	/** 按 InstanceId 查找武器实例；不存在时返回 nullptr。 */
	const FShooterWeaponInstanceData* FindWeaponInstance(const FGuid& InstanceId) const;

	/** 按 DefinitionId 查找武器实例；不存在时返回 nullptr。 */
	const FShooterWeaponInstanceData* FindWeaponInstanceByDefinitionId(
		const FPrimaryAssetId& DefinitionId) const;

	/** 按 SlotIndex 查找武器实例；不存在时返回 nullptr。 */
	const FShooterWeaponInstanceData* FindWeaponInstanceBySlot(int32 SlotIndex) const;

	/** 返回第一个空 SlotIndex；没有空位时返回 INDEX_NONE。 */
	int32 FindFreeSlotIndex() const;

	/** 返回 Slot 上限。 */
	int32 GetMaxWeaponSlots() const { return MaxWeaponSlots; }

	/** 调整 Slot 上限，供蓝图配置与测试使用。 */
	void SetMaxWeaponSlots(int32 NewMaxWeaponSlots) { MaxWeaponSlots = FMath::Max(0, NewMaxWeaponSlots); }

	/** 返回绑定到指定 InstanceId 的 WeaponActor；不存在时返回 nullptr。 */
	AShooterWeapon* FindWeaponActor(const FGuid& InstanceId) const;

	/** 返回当前 Active Instance 绑定的 WeaponActor；不存在时返回 nullptr。 */
	AShooterWeapon* GetActiveWeaponActor() const { return FindWeaponActor(ActiveWeaponInstanceId); }

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

	/** 当前逻辑武器实例 ID；无效 FGuid 表示未装备。 */
	FGuid GetActiveWeaponInstanceId() const { return ActiveWeaponInstanceId; }

	/** 服务器权威：更新 ActiveWeaponInstanceId。传入有效 ID 时要求该实例已存在。 */
	void SetActiveWeaponInstanceId(const FGuid& NewInstanceId);

	/** 查询指定实例当前弹匣弹药；实例不存在时返回 0。 */
	int32 GetMagazineAmmo(const FGuid& InstanceId) const;

	/** 查询指定实例当前备用弹药；实例不存在时返回 0。 */
	int32 GetReserveAmmo(const FGuid& InstanceId) const;

	/** 指定实例是否还有弹匣弹药。 */
	bool CanConsumeMagazineAmmo(const FGuid& InstanceId) const;

	/** 服务器权威：从指定实例弹匣扣除 Amount；失败时不产生任何变化。 */
	bool ConsumeMagazineAmmo(const FGuid& InstanceId, int32 Amount = 1);

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Owner Client FastArray 回调与服务器本地修改共用的表现刷新入口。 */
	void HandleInstanceChanged(const FShooterWeaponInstanceData& InstanceData);

	/** Owner Client FastArray 删除回调：同步解除对应 WeaponActor 绑定。 */
	void HandleInstanceRemoved(const FGuid& InstanceId);

	/** 第一版固定 Slot 上限。SlotFull 时 Pickup 必须明确 Reject。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Inventory")
	int32 MaxWeaponSlots = 3;

	/** 服务器维护的 InstanceId -> WeaponActor 绑定；客户端按需在 BoundInstanceId 到达时注册。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AShooterWeapon>> BoundWeaponActors;

	/** 完整 Inventory 只复制给 Owner；远端通过 Character.CurrentWeaponActor 获取公共表现。 */
	UPROPERTY(Replicated)
	FShooterWeaponInventoryList ReplicatedInventory;

	/** 逻辑当前武器身份；2A 只建立数据字段。 */
	UPROPERTY(ReplicatedUsing = OnRep_ActiveWeaponInstanceId)
	FGuid ActiveWeaponInstanceId;

	UFUNCTION()
	void OnRep_ActiveWeaponInstanceId();
};
