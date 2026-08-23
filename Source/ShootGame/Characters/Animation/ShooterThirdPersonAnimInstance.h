// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "ShooterThirdPersonAnimInstance.generated.h"

class AShooterCharacter;
class AShooterWeapon;

/**
 * 第三人称 AnimBP 数据源（计划 C2.1）。
 *
 * 向 AnimGraph 提供：
 *   AimDirectionWorld —— 世界空间瞄准方向（本地拥有者：即时 GetBaseAimRotation；非本地角色暂回退 Actor Forward，C4 接网络方向）；
 *   HandToMuzzle      —— 当前武器 Muzzle 相对角色 Mesh hand socket 的刚性 Transform（武器切换时刷新并缓存）；
 *   bAimIKEnabled     —— 程序化 Aim IK 总开关（数据无效时自动关闭）。
 *
 * 只承载表现数据，不修改骨骼；骨骼修改由 FAnimNode_ShooterAimIK 完成。
 */
UCLASS(Blueprintable)
class SHOOTGAME_API UShooterThirdPersonAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** 世界空间瞄准方向（C2：本地拥有者即时 AimRotation；非本地角色回退 Actor Forward）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	FVector AimDirectionWorld = FVector::ForwardVector;

	/** 当前武器 Muzzle 相对角色 Mesh HandSocket 的刚性 Transform（武器变化时刷新缓存）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	FTransform HandToMuzzle = FTransform::Identity;

	/** 程序化 Aim IK 总开关；无有效角色 / 数据无效时为 false。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	bool bAimIKEnabled = true;

	/** 角色 Mesh 上的手部 socket 名（与 FAnimNode_ShooterAimIK.HandBone 保持一致）。 */
	UPROPERTY(EditAnywhere, Category = "Shooter Aim", meta = (DisplayName = "Hand Socket Name"))
	FName HandSocketName = TEXT("hand_r");

	/** 计算 Muzzle 相对 HandSocket 的刚性 Transform（纯数据路径，可被 Automation 验证）。 */
	UFUNCTION(BlueprintPure, Category = "Shooter Aim")
	static FTransform ComputeHandToMuzzleTransform(
		const FTransform& InHandWorld,
		const FTransform& InMuzzleWorld);

	/** 读取角色 Mesh 指定 socket 的世界变换；无角色 / 无 socket 时回退 Identity。 */
	UFUNCTION(BlueprintPure, Category = "Shooter Aim")
	static FTransform GetHandWorldTransform(const AShooterCharacter* InCharacter, FName InHandSocketName);

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	/** 已缓存武器引用；变化时刷新 HandToMuzzle。 */
	UPROPERTY(Transient)
	TObjectPtr<AShooterWeapon> CachedWeapon = nullptr;
};
