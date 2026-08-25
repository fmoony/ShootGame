// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShooterEquipmentComponent.generated.h"

class AShooterCharacter;
class AShooterWeapon;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FShooterEquippedWeaponChangedDelegate,
	const FGuid&,
	InstanceId,
	AShooterWeapon*,
	Weapon);

/**
 * 角色“当前正在使用哪把武器”的装备组件。
 *
 * R3 阶段只建立 facade：EquipWeapon 仍转发现有的 Character.CommitActiveWeapon 旧事务，
 * 不移动字段、不改变复制结果；装备变化事件由旧事务完成后发布。
 * R4 会把 ActiveWeaponInstanceId / CurrentWeaponActor 及其完整事务迁入本组件。
 */
UCLASS(ClassGroup=(Equipment), meta=(BlueprintSpawnableComponent))
class SHOOTGAME_API UShooterEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShooterEquipmentComponent();

	/** 服务器权威装备事务入口（R3 facade：转发 Character.CommitActiveWeapon）。 */
	bool EquipWeapon(const FGuid& InstanceId);

	/** 当前装备身份；R3 从 Inventory 读取旧 Active 值。 */
	FGuid GetActiveWeaponInstanceId() const;

	/** 当前装备 WeaponActor；R3 从 Character 读取旧 CurrentWeapon。 */
	AShooterWeapon* GetCurrentWeaponActor() const;

	/** 装备变化事件；HUD 在迁移期可通过 Character 转发，最终由本地 Controller/UI 绑定。 */
	UPROPERTY(BlueprintAssignable, Category = "Equipment")
	FShooterEquippedWeaponChangedDelegate OnEquippedWeaponChanged;

private:
	/** R3 旧事务完成后的唯一事件发布入口。 */
	void BroadcastEquippedWeaponChanged();
};
