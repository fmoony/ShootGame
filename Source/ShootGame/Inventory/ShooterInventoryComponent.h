// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShooterInventoryTypes.h"
#include "ShooterInventoryComponent.generated.h"

/**
 * 角色武器 Inventory 组件。
 *
 * 2A 职责边界：
 * - 持有并复制 WeaponInstance FastArray（OwnerOnly）；
 * - 持有 ActiveWeaponInstanceId 数据字段（OwnerOnly）；
 * - 提供服务器权威的数据增删与查找入口。
 *
 * 本阶段不接 Pickup、不创建 WeaponActor、不执行切换表现。
 */
UCLASS(ClassGroup=(Inventory), meta=(BlueprintSpawnableComponent))
class SHOOTGAME_API UShooterInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShooterInventoryComponent();

	/** 服务器权威：新增一条武器实例数据。InstanceId 与 SlotIndex 均必须唯一。 */
	bool AddWeaponInstance(const FShooterWeaponInstanceData& InstanceData);

	/** 服务器权威：按 InstanceId 移除武器实例。移除当前 Active 实例时同步清空 ActiveWeaponInstanceId。 */
	bool RemoveWeaponInstance(const FGuid& InstanceId);

	/** 服务器权威：清空全部武器实例并清空 ActiveWeaponInstanceId。 */
	void ClearInventory();

	/** 按 InstanceId 查找武器实例；不存在时返回 nullptr。 */
	const FShooterWeaponInstanceData* FindWeaponInstance(const FGuid& InstanceId) const;

	/** 返回 OwnerOnly FastArray 的只读入口。 */
	const TArray<FShooterWeaponInstanceEntry>& GetWeaponEntries() const { return ReplicatedInventory.Items; }

	/** 当前逻辑武器实例数量。 */
	int32 GetWeaponCount() const { return ReplicatedInventory.Items.Num(); }

	/** 当前逻辑武器实例 ID；无效 FGuid 表示未装备。 */
	FGuid GetActiveWeaponInstanceId() const { return ActiveWeaponInstanceId; }

	/** 服务器权威：更新 ActiveWeaponInstanceId。传入有效 ID 时要求该实例已存在；2A 不执行表现切换。 */
	void SetActiveWeaponInstanceId(const FGuid& NewInstanceId);

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 完整 Inventory 只复制给 Owner；远端通过 Character.CurrentWeaponActor 获取公共表现。 */
	UPROPERTY(Replicated)
	FShooterWeaponInventoryList ReplicatedInventory;

	/** 逻辑当前武器身份；2A 只建立数据字段。 */
	UPROPERTY(ReplicatedUsing = OnRep_ActiveWeaponInstanceId)
	FGuid ActiveWeaponInstanceId;

	UFUNCTION()
	void OnRep_ActiveWeaponInstanceId();
};
