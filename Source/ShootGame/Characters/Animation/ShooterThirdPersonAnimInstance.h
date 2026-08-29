// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Characters/Animation/ShooterAnimInstanceBase.h"
#include "ShooterThirdPersonAnimInstance.generated.h"

class AShooterCharacter;
class AShooterWeapon;
class USkeletalMeshComponent;

/**
 * 第三人称 AnimBP 数据源。
 *
 * Aim / LeftHand IK 的静态 Binding（Weapon、Mesh、Attach、Socket 与缓存 Transform）
 * 只在两个时机重建：
 *   1. 本机武器表现完成事件 OnWeaponPresentationChanged 到达；
 *   2. NativeInitializeAnimation 后从 Equipment.CurrentWeaponActor 主动回放。
 * NativeUpdateAnimation 不再逐帧扫描静态附着签名；只保留移动、Aim、Tag 动态采集，
 * 以及表现身份变化的廉价校验和一次性的 bStaticBindingRebuildPending 重试。
 */
UCLASS(Blueprintable)
class SHOOTGAME_API UShooterThirdPersonAnimInstance : public UShooterAnimInstanceBase
{
	GENERATED_BODY()

public:
	/** 世界空间基础瞄准方向；本地拥有者直接使用，观察端用于目标安全投影与 IK 求解参考。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	FVector AimDirectionWorld = FVector::ZeroVector;

	/** 观察端平滑并经过视点安全距离处理后的世界目标点。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	FVector AimTargetWorld = FVector::ZeroVector;

	/** AimTargetWorld 是否可供 Aim IK 节点使用；本地拥有者保持 false。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	bool bAimTargetWorldValid = false;

	/** 观察端表现目标距视点的最小安全深度（详见旧实现注释）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumRemoteAimTargetDistanceFromView = 150.0f;

	/** 观察端安全姿势目标至少位于第三人称枪口前方该距离。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumRemoteAimTargetDistanceFromMuzzle = 50.0f;

	/** 第三人称 AimOffset 俯仰输入。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	float AimPitchN = 0.0f;

	/** 第三人称移动方向角。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter|Locomotion")
	float MoveDirection = 0.0f;

	/** 第三人称移动阈值开关。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter|Locomotion")
	bool bShouldMove = false;

	/** 当前武器 Muzzle 相对角色 Mesh HandSocket 的刚性 Transform（事件重建，不逐帧追逐）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	FTransform HandToMuzzle = FTransform::Identity;

	/** 程序化 Aim IK 总开关；静态 Binding 有效且帧级 Aim 输入有效时为 true。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	bool bAimIKEnabled = false;

	/** 当前武器第三人称左手握把 Socket 相对角色 Mesh HandSocket 的刚性 Transform。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Left Hand IK")
	FTransform LeftHandGripInRightHandSpace = FTransform::Identity;

	/** 角色 HandGrip_L 相对 hand_l 的固定 Transform。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Left Hand IK")
	FTransform HandGripInLeftHandSpace = FTransform::Identity;

	/** 左手 Two Bone IK 总开关；仅 LeftHand 静态 Binding 有效时为 true。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Left Hand IK")
	bool bLeftHandIKEnabled = false;

	/** 角色 Mesh 上的手部 socket 名（与 FAnimNode_ShooterAimIK.HandBone 保持一致）。 */
	UPROPERTY(EditAnywhere, Category = "Shooter Aim", meta = (DisplayName = "Hand Socket Name"))
	FName HandSocketName = TEXT("hand_r");

	/** 左手 IK 末端骨骼名。 */
	UPROPERTY(EditAnywhere, Category = "Shooter Left Hand IK")
	FName LeftHandBoneName = TEXT("hand_l");

	/** 角色手掌上的握持参考 Socket。 */
	UPROPERTY(EditAnywhere, Category = "Shooter Left Hand IK")
	FName HandGripSocketName = TEXT("HandGrip_L");

	/** 计算 Muzzle 相对 HandSocket 的刚性 Transform（纯几何计算）。 */
	UFUNCTION(BlueprintPure, Category = "Shooter Aim")
	static FTransform ComputeHandToMuzzleTransform(
		const FTransform& InHandWorld,
		const FTransform& InMuzzleWorld);

	/** 从第三人称 Muzzle 世界位置指向表现目标的世界方向；无效输入返回零向量。 */
	UFUNCTION(BlueprintPure, Category = "Shooter Aim")
	static FVector ComputeMuzzleToTargetDirection(
		const FVector& MuzzleWorldLocation,
		const FVector& TargetWorld);

	/** 按本地控制 / 观察端状态决定 AimDirectionWorld 来源（纯计算）。 */
	static FVector ComputeAimDirectionWorldForState(
		bool bLocallyControlled,
		bool bShouldRunPresentationSmoothing,
		bool bPresentationTargetValid,
		const FVector& LocalAimDirection,
		const FVector& ViewWorldLocation,
		const FVector& MuzzleWorldLocation,
		const FVector& SmoothedPresentationTarget,
		bool bHasThirdPersonMuzzle,
		float MinimumTargetDistanceFromView = 150.0f,
		float MinimumTargetDistanceFromMuzzle = 50.0f);

	/** 读取角色 Mesh 指定 socket 的世界变换；无角色 / 无 socket 时回退 Identity。 */
	UFUNCTION(BlueprintPure, Category = "Shooter Aim")
	static FTransform GetHandWorldTransform(const AShooterCharacter* InCharacter, FName InHandSocketName);

	/** 计算左手握把相对右手 HandSocket 的刚性 Transform（纯几何计算）。 */
	UFUNCTION(BlueprintPure, Category = "Shooter Left Hand IK")
	static FTransform ComputeLeftHandGripInRightHandSpace(
		const FTransform& InRightHandWorld,
		const FTransform& InLeftHandGripWorld);

	/** 计算角色 HandGrip_L 相对 hand_l 的固定 Transform。 */
	UFUNCTION(BlueprintPure, Category = "Shooter Left Hand IK")
	static FTransform ComputeHandGripInLeftHandSpace(
		const FTransform& InLeftHandWorld,
		const FTransform& InHandGripWorld);

	/** 只查真正的非法数值 / 旋转 / Scale，不比较 Identity。 */
	static bool IsMathematicallyValidBindingFrame(const FTransform& T);

protected:
	/** 初始化：订阅 Character 表现完成事件并回放 Equipment 当前状态。 */
	virtual void NativeInitializeAnimation() override;

	/** 反初始化：解除订阅并清空静态 Binding 与弱引用。 */
	virtual void NativeUninitializeAnimation() override;

	/** 基类完成公共快照后采集第三人称专用数据。 */
	virtual void UpdateShooterAnimationData(float DeltaSeconds) override;

	/** OnWeaponPresentationChanged 订阅处理：表现完成时重建一次静态 Binding。 */
	UFUNCTION()
	void HandleWeaponPresentationChanged(AShooterWeapon* PreviousWeapon, AShooterWeapon* CurrentWeapon);

	/** 初始化后的主动回放：不依赖“以后一定会来一个事件”。 */
	void ReplayWeaponPresentationState(AShooterCharacter* Character);

	/** 事件 / 回放 / 单次 Pending 重试共用的静态 Binding 重建。 */
	void RebuildWeaponStaticBindings(AShooterWeapon* Weapon);

	/** 清空 Weapon 弱引用、静态 Binding、IK 开关与帧级 Aim 输出。 */
	void ClearWeaponStaticBindings();

	/** 采集 AimDirectionWorld / AimTargetWorld / bAimTargetWorldValid（本地即时 / 远端平滑投影）。 */
	void UpdateAimInputs(const AShooterCharacter* Character);

	/** 由静态 Binding 有效性与帧级 Aim 输入计算两个 IK 总开关。 */
	void RefreshIKEnabled(const AShooterCharacter* Character);

	/** 当前表现身份弱引用；只用于身份比较与事件重放，不逐帧做结构扫描。 */
	TWeakObjectPtr<AShooterWeapon> CachedPresentationWeapon;

	/** 只表示下一次普通动画更新再尝试一次静态 Binding；不承载 Weapon 身份。 */
	bool bStaticBindingRebuildPending = false;

	/** Aim IK 静态能力判定结果（事件重建后保持，直到下一次表现事件）。 */
	bool bAimIKBindingValid = false;

	/** LeftHand IK 静态能力判定结果。 */
	bool bLeftHandIKBindingValid = false;

private:
	/** Character OnWeaponPresentationChanged 的订阅句柄。 */
	FDelegateHandle WeaponPresentationChangedHandle;
};
