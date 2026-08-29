// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Characters/Animation/ShooterAnimInstanceBase.h"
#include "Characters/Animation/ShooterFirstPersonAnimInstance.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Weapons/ShooterPickup.h"
#include "Weapons/ShooterWeapon.h"
#include "ShooterWeaponPresentationTestTypes.generated.h"

/**
 * 武器装备表现事件收束与动画切换解耦执行计划的 Editor Automation 测试类型。
 * 只存在于测试代码路径，不进入游戏玩法。
 */

/** 表现测试角色：具体化 abstract 的 AShooterCharacter。 */
UCLASS(Transient, NotBlueprintable)
class AShooterWeaponPresentationTestCharacter : public AShooterCharacter
{
	GENERATED_BODY()

public:
	virtual void OnWeaponDeactivated(AShooterWeapon* Weapon) override
	{
		++WeaponDeactivatedCount;
		Super::OnWeaponDeactivated(Weapon);
	}

	int32 WeaponDeactivatedCount = 0;
};

/** 表现测试主武器：配置第一/第三人称专用 AnimInstance。 */
UCLASS(Transient, NotBlueprintable)
class AShooterWeaponPresentationTestWeaponPrimary : public AShooterWeapon
{
	GENERATED_BODY()

public:
	AShooterWeaponPresentationTestWeaponPrimary()
	{
		MagazineSize = 10;
		FirstPersonAnimInstanceClass = UShooterFirstPersonAnimInstance::StaticClass();
		ThirdPersonAnimInstanceClass = UShooterThirdPersonAnimInstance::StaticClass();
	}

	bool HasWeaponOwnerForTest() const { return WeaponOwner != nullptr; }
	void SetFiringForTest(bool bInFiring) { bIsFiring = bInFiring; }
	bool IsFiringForTest() const { return bIsFiring; }
};

/** 表现测试副武器：配置与主武器不同的公共 AnimInstance，用于验证 AnimClass 切换。 */
UCLASS(Transient, NotBlueprintable)
class AShooterWeaponPresentationTestWeaponSecondary : public AShooterWeapon
{
	GENERATED_BODY()

public:
	AShooterWeaponPresentationTestWeaponSecondary()
	{
		MagazineSize = 10;
		FirstPersonAnimInstanceClass = UShooterAnimInstanceBase::StaticClass();
		ThirdPersonAnimInstanceClass = UShooterAnimInstanceBase::StaticClass();
	}
};

/** 删除顺序测试武器：记录 DeactivateWeapon 是否发生在 Actor Destroy 之前。 */
UCLASS(Transient, NotBlueprintable)
class AShooterInventoryOrderTestWeapon : public AShooterWeapon
{
	GENERATED_BODY()

public:
	AShooterInventoryOrderTestWeapon()
	{
		MagazineSize = 10;
	}

	void DeactivateWeapon()
	{
		bDeactivatedForTest = true;
		Super::DeactivateWeapon();
	}

	bool bDeactivatedForTest = false;
};

/** 测试 Pickup：暴露 OnOverlap 与 WeaponClass 写入，供自动化驱动 Pickup 事务。 */
UCLASS(Transient, NotBlueprintable)
class AShooterWeaponPresentationTestPickup : public AShooterPickup
{
	GENERATED_BODY()

public:
	void SetWeaponClassForTest(TSubclassOf<AShooterWeapon> InWeaponClass)
	{
		WeaponClass = InWeaponClass;
	}

	void TriggerOverlapForTest(AActor* OtherActor)
	{
		OnOverlap(nullptr, OtherActor, nullptr, 0, false, FHitResult());
	}
};

/** 逻辑装备变化事件监听器（OnEquippedWeaponChanged 的 Dynamic Delegate 订阅方）。 */
UCLASS(Transient, NotBlueprintable)
class UShooterEquipmentEventTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleEquippedWeaponChanged(AShooterWeapon* PreviousWeapon, AShooterWeapon* CurrentWeapon)
	{
		++EventCount;
		LastPreviousWeapon = PreviousWeapon;
		LastCurrentWeapon = CurrentWeapon;
	}

	int32 EventCount = 0;
	TObjectPtr<AShooterWeapon> LastPreviousWeapon = nullptr;
	TObjectPtr<AShooterWeapon> LastCurrentWeapon = nullptr;
};

/** 本机武器表现完成事件监听器（OnWeaponPresentationChanged 的 Dynamic Delegate 订阅方）。 */
UCLASS(Transient, NotBlueprintable)
class UShooterWeaponPresentationEventTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleWeaponPresentationChanged(AShooterWeapon* PreviousWeapon, AShooterWeapon* CurrentWeapon)
	{
		++EventCount;
		LastPreviousWeapon = PreviousWeapon;
		LastCurrentWeapon = CurrentWeapon;
	}

	int32 EventCount = 0;
	TObjectPtr<AShooterWeapon> LastPreviousWeapon = nullptr;
	TObjectPtr<AShooterWeapon> LastCurrentWeapon = nullptr;
};

/** 子弹 HUD 事件监听器：验证重复表现收敛不重复广播 HUD。 */
UCLASS(Transient, NotBlueprintable)
class UShooterBulletCountEventTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleBulletCountUpdated(int32 MagazineSize, int32 Bullets)
	{
		++EventCount;
	}

	int32 EventCount = 0;
};

/** Equipment 测试壳：暴露 OnRep / 提交字段写入，模拟复制到达顺序。 */
UCLASS(Transient, NotBlueprintable)
class UShooterEquipmentTestHarnessComponent : public UShooterEquipmentComponent
{
	GENERATED_BODY()

public:
	void SetCurrentWeaponActorForTest(AShooterWeapon* Weapon) { CurrentWeaponActor = Weapon; }
	void SetActiveWeaponInstanceIdForTest(const FGuid& InstanceId) { ActiveWeaponInstanceId = InstanceId; }

	void CallOnRepCurrentWeaponActorForTest(AShooterWeapon* PreviousWeapon)
	{
		OnRep_CurrentWeaponActor(PreviousWeapon);
	}

	void CallOnRepActiveWeaponInstanceIdForTest()
	{
		OnRep_ActiveWeaponInstanceId();
	}
};
