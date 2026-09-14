// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ShooterWeaponFireBehavior.generated.h"

class AShooterProjectile;
class AShooterWeapon;
class APawn;

/**
 * 单次开火的执行上下文。
 * 由 WeaponActor 在服务器上计算并填充；Behavior 只消费，不反向推导。
 */
USTRUCT(BlueprintType)
struct FShooterWeaponFireContext
{
	GENERATED_BODY()

	/** 开火中的 WeaponActor；行为只经它访问 World 与 Owner，不直接改写表现。 */
	UPROPERTY(BlueprintReadOnly, Category="Fire")
	TObjectPtr<AShooterWeapon> WeaponActor = nullptr;

	/** 弹丸 Instigator（持有者 Pawn）。 */
	UPROPERTY(BlueprintReadOnly, Category="Fire")
	TObjectPtr<APawn> Instigator = nullptr;

	/** 服务器权威计算的弹丸生成变换（含枪口偏移与散布）。 */
	UPROPERTY(BlueprintReadOnly, Category="Fire")
	FTransform MuzzleTransform = FTransform::Identity;

	/** 服务器权威瞄准点；供需要射线语义的未来行为使用。 */
	UPROPERTY(BlueprintReadOnly, Category="Fire")
	FVector TargetLocation = FVector::ZeroVector;

	/**
	 * 本次开火使用的弹丸类；由 WeaponActor 从行配置镜像（WeaponActor.ProjectileClass）填入。
	 * 上下文只携带这一条武器派生参数，不再携带整行配置快照。
	 */
	UPROPERTY(BlueprintReadOnly, Category="Fire")
	TSubclassOf<AShooterProjectile> ProjectileClass;

	/** 本次开火的武器种类身份；与 WeaponActor.WeaponId 一致。 */
	UPROPERTY(BlueprintReadOnly, Category="Fire")
	FName WeaponId;
};

/**
 * 开火行为边界：只回答"这一枪如何产生攻击结果"。
 *
 * 约束（武器与 Inventory 正式架构实施计划 4.3）：
 * - 无复制、无持久可变状态；武器派生参数只来自 FShooterWeaponFireContext::ProjectileClass；
 * - GA_Fire 管理 Ability 生命周期与激活条件；
 * - WeaponActor 管理弹药消耗与弹药权威；
 * - WeaponActor 负责 Muzzle、Mesh、Attach、表现入口与调用行为（当前未接入行为，见下）；
 * - Projectile、Damage 与 Ammo 只由服务器产生。
 *
 * 接入现状：本定义当前处于休眠状态 —— WeaponActor 直接生成自己的 ProjectileClass，
 * 不再实例化或调用行为；重新接入时由 WeaponActor 在表现入口之前填上下文并调用 ExecuteFire。
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced)
class SHOOTGAME_API UShooterWeaponFireBehavior : public UObject
{
	GENERATED_BODY()

public:
	/** 执行一次开火。实现必须自行校验服务器权威并 fail closed。 */
	virtual void ExecuteFire(const FShooterWeaponFireContext& Context)
	{
	}
};
