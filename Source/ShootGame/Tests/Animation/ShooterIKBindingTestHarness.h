// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Characters/ShooterCharacter.h"
#include "Weapons/ShooterWeapon.h"
#include "ShooterIKBindingTestHarness.generated.h"

/**
 * 第三人称事件驱动 IK Binding 测试壳（仅 Editor Automation 使用）。
 *
 * 用真实对象（NewObject 的 Character / Weapon / SkeletalMeshComponent）驱动
 * RebuildWeaponStaticBindings / HandleWeaponPresentationChanged / ClearWeaponStaticBindings，
 * 覆盖表现事件到达、初始化回放、单次 Pending 重试与“永久缺失 Socket 不逐帧重试”。
 */

/** 测试角色：AShooterCharacter 是 UCLASS(abstract)，测试需要可 NewObject 的具体子类。 */
UCLASS(Transient, NotBlueprintable)
class AShooterIKBindingTestCharacter : public AShooterCharacter
{
	GENERATED_BODY()
};

/** 测试武器：具体化 abstract 的 AShooterWeapon。 */
UCLASS(Transient, NotBlueprintable)
class AShooterIKBindingTestWeapon : public AShooterWeapon
{
	GENERATED_BODY()

public:
	AShooterIKBindingTestWeapon()
	{
		MuzzleSocketName = TEXT("Muzzle");
		ThirdPersonLeftHandGripSocketName = TEXT("Grip_L");
	}

	void SetMuzzleSocketNameForTest(FName InName) { MuzzleSocketName = InName; }
	void SetLeftHandGripSocketNameForTest(FName InName) { ThirdPersonLeftHandGripSocketName = InName; }
};

/** 测试壳 AnimInstance：访问 protected 事件驱动 Binding 层。 */
UCLASS(Transient, NotBlueprintable)
class UShooterIKBindingTestHarness : public UShooterThirdPersonAnimInstance
{
	GENERATED_BODY()

public:
	void CallNativeInitializeAnimationForTest()
	{
		NativeInitializeAnimation();
	}

	void CallProductionPendingRetryForTest(AShooterWeapon* LogicalWeapon)
	{
		// 镜像生产 UpdateShooterAnimationData 中“身份一致 + 只消费一次 Pending”的规则。
		if (bStaticBindingRebuildPending &&
			LogicalWeapon != nullptr &&
			CachedPresentationWeapon.Get() == LogicalWeapon)
		{
			bStaticBindingRebuildPending = false;
			RebuildWeaponStaticBindings(LogicalWeapon);
		}
	}

	void CallHandleWeaponPresentationChanged(AShooterWeapon* PreviousWeapon, AShooterWeapon* CurrentWeapon)
	{
		HandleWeaponPresentationChanged(PreviousWeapon, CurrentWeapon);
	}

	void CallReplayWeaponPresentationState(AShooterCharacter* Character)
	{
		ReplayWeaponPresentationState(Character);
	}

	void CallRebuildWeaponStaticBindings(AShooterWeapon* Weapon)
	{
		RebuildWeaponStaticBindings(Weapon);
	}

	void CallClearWeaponStaticBindings()
	{
		ClearWeaponStaticBindings();
	}

	void CallRefreshIKEnabled(AShooterCharacter* Character)
	{
		RefreshIKEnabled(Character);
	}

	bool IsAimBindingValidForTest() const { return bAimIKBindingValid; }
	bool IsLeftHandBindingValidForTest() const { return bLeftHandIKBindingValid; }
	bool HasPendingRebuildForTest() const { return bStaticBindingRebuildPending; }
	AShooterWeapon* GetCachedPresentationWeaponForTest() const { return CachedPresentationWeapon.Get(); }
};
