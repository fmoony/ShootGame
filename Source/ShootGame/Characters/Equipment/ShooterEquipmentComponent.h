// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShooterEquipmentComponent.generated.h"

class AShooterCharacter;
class AShooterWeapon;
class UShooterInventoryComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FShooterEquippedWeaponChangedDelegate,
	AShooterWeapon*,
	PreviousWeapon,
	AShooterWeapon*,
	CurrentWeapon);

/**
 * 角色“当前正在使用哪把武器”的唯一装备权威。
 *
 * ActiveWeaponInstanceId 与 CurrentWeaponActor 是同一装备事务的
 * “稳定身份 + 世界实体”原子对：两者同时有效或同时无效。
 * CurrentWeaponActor 复制给所有观察者，完整身份只复制给 Owner。
 */
UCLASS(ClassGroup=(Equipment), meta=(BlueprintSpawnableComponent))
class SHOOTGAME_API UShooterEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShooterEquipmentComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 服务器权威：提交当前装备事务（Deactivate / Activate / Attach / AnimClass / Event / AimReset）。 */
	bool EquipWeapon(const FGuid& InstanceId);

	/** 幂等清空当前装备；Inventory Remove/Clear 或死亡路径调用。 */
	void ClearEquippedWeapon();

	/** 当前装备身份；无效 FGuid 表示未装备。 */
	FGuid GetActiveWeaponInstanceId() const { return ActiveWeaponInstanceId; }

	/** 当前装备 WeaponActor；没有装备时为 nullptr。 */
	AShooterWeapon* GetCurrentWeaponActor() const { return CurrentWeaponActor; }

	/** WeaponActor 的 Owner / BoundInstanceId 晚到时，由 Weapon 回调本组件补做幂等应用。 */
	void HandleWeaponActorReady(AShooterWeapon* Weapon);

	/** Inventory 移除指定 Instance 后通知装备组件；命中当前装备时清空。 */
	void NotifyWeaponInstanceRemoved(const FGuid& InstanceId);

	/** Inventory Clear 后通知装备组件；始终清空当前装备。 */
	void NotifyInventoryCleared();

	/** 逻辑装备变化事件：只在 CurrentWeaponActor 真实转移时广播 (Previous, Current)；Current == nullptr 表示 Unequip。 */
	UPROPERTY(BlueprintAssignable, Category = "Equipment")
	FShooterEquippedWeaponChangedDelegate OnEquippedWeaponChanged;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 当前装备 WeaponActor；所有客户端需要它显示第三人称持枪。 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeaponActor, VisibleAnywhere, BlueprintReadOnly, Category = "Equipment")
	TObjectPtr<AShooterWeapon> CurrentWeaponActor;

	/** 当前装备 Instance 身份；完整身份只复制给 Owner。 */
	UPROPERTY(ReplicatedUsing = OnRep_ActiveWeaponInstanceId, VisibleAnywhere, BlueprintReadOnly, Category = "Equipment")
	FGuid ActiveWeaponInstanceId;

	UFUNCTION()
	void OnRep_CurrentWeaponActor(AShooterWeapon* PreviousWeapon);

	UFUNCTION()
	void OnRep_ActiveWeaponInstanceId();

	/** E2 迁移期表现应用：只做附着 + Activate，不广播逻辑事件；E3 由 Character::EnsureWeaponPresentation 取代。 */
	void ApplyCurrentWeapon(AShooterWeapon* PreviousWeapon);

	/** 装备切换时重置表现瞄准平滑；清空时使用 Clear。 */
	void ResetAimPresentationForEquipChange();

private:
	AShooterCharacter* GetOwnerCharacter() const;
	UShooterInventoryComponent* GetOwnerInventory() const;

	/** 仅在 Previous != Current 时发布逻辑装备变化事件。 */
	void BroadcastEquippedWeaponChanged(AShooterWeapon* PreviousWeapon, AShooterWeapon* CurrentWeapon);
};
