// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "ShooterAnimInstanceBase.generated.h"

class AShooterCharacter;
class AShooterWeapon;

/**
 * 第一/第三人称 AnimInstance 的薄公共基类。
 *
 * 只承载两个视角真正共享的只读表现快照：
 * GroundSpeed / bIsInAir / bHasEquippedWeapon / CurrentWeaponActor /
 * State.Firing / State.Reloading / State.Equipping / State.Dead。
 * 不含摄像机、贴墙、Aim IK、HandToMuzzle、RPC 或任何权威写入。
 */
UCLASS(Blueprintable)
class SHOOTGAME_API UShooterAnimInstanceBase : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** 水平地面速度（cm/s）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter|Base")
	float GroundSpeed = 0.0f;

	/** 是否处于空中。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter|Base")
	bool bIsInAir = false;

	/** 当前是否装备有效 WeaponActor。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter|Base")
	bool bHasEquippedWeapon = false;

	/** ASC 表现状态：State.Firing。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter|Base")
	bool bIsFiring = false;

	/** ASC 表现状态：State.Reloading。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter|Base")
	bool bIsReloading = false;

	/** ASC 表现状态：State.Equipping。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter|Base")
	bool bIsEquipping = false;

	/** ASC 表现状态：State.Dead。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter|Base")
	bool bIsDead = false;

	/** 当前装备 WeaponActor 的只读缓存；无效时清空。 */
	UPROPERTY(BlueprintReadOnly, Category = "Shooter|Base")
	TObjectPtr<AShooterWeapon> CurrentWeaponActor;

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUninitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
	/** 子类专用数据更新钩子；基类先完成公共快照采集。 */
	virtual void UpdateShooterAnimationData(float DeltaSeconds);

	/** 清空公共快照；无 Owner / 无 Character / 死亡 / 无武器时保持零值状态。 */
	void ClearCommonAnimationData();

	/** 缓存 Owning Character；无效时返回 nullptr。 */
	AShooterCharacter* GetCachedShooterCharacter() const { return CachedShooterCharacter; }

private:
	/** 采集移动、装备与 ASC Tag 表现值。 */
	void RefreshCommonAnimationData();

	UPROPERTY(Transient)
	TObjectPtr<AShooterCharacter> CachedShooterCharacter;
};
