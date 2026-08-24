// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "ShooterThirdPersonAnimInstance.generated.h"

class AShooterCharacter;
class AShooterWeapon;

/**
 * 第三人称 AnimBP 数据源（计划 C2.1 / C4）。
 *
 * 向 AnimGraph 提供：
 *   AimDirectionWorld —— 世界空间瞄准方向（本地拥有者：即时 GetBaseAimRotation；
 *                        SimulatedProxy / Listen Server 远端观察：Muzzle → 平滑表现目标，C4）；
 *   HandToMuzzle      —— 当前武器 Muzzle 相对角色 Mesh hand socket 的刚性 Transform（附着状态变化时刷新并缓存）；
 *   bAimIKEnabled     —— 程序化 Aim IK 总开关（数据无效时自动关闭）。
 *   LeftHandGripInRightHandSpace —— 当前武器左手握把 Socket 相对 hand_r 的刚性 Transform（武器/附着事件重建并缓存）；
 *   bLeftHandIKEnabled     —— 左手 Two Bone IK 总开关（无有效握把数据时自动关闭）。
 *
 * 只承载表现数据，不修改骨骼；骨骼修改由 FAnimNode_ShooterAimIK 完成。
 */
UCLASS(Blueprintable)
class SHOOTGAME_API UShooterThirdPersonAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** 世界空间瞄准方向（本地拥有者即时 AimRotation；观察端为 Muzzle → 平滑表现目标；无效状态为零向量）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	FVector AimDirectionWorld = FVector::ZeroVector;

	/** 当前武器 Muzzle 相对角色 Mesh HandSocket 的刚性 Transform（武器/附着状态变化时刷新缓存）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	FTransform HandToMuzzle = FTransform::Identity;

	/** 程序化 Aim IK 总开关；无有效角色 / 武器 / Muzzle / HandToMuzzle / AimDirection 时为 false。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	bool bAimIKEnabled = false;

	/** 当前武器第三人称左手握把 Socket 相对角色 Mesh HandSocket 的刚性 Transform（只缓存事件重建，不逐帧追逐世界点）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Left Hand IK")
	FTransform LeftHandGripInRightHandSpace = FTransform::Identity;

	/** 左手 Two Bone IK 总开关；无有效角色 / 第三人称 Mesh / 武器 / 握把 Socket / 有效握把缓存时为 false。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Left Hand IK")
	bool bLeftHandIKEnabled = false;


	/** 角色 Mesh 上的手部 socket 名（与 FAnimNode_ShooterAimIK.HandBone 保持一致）。 */
	UPROPERTY(EditAnywhere, Category = "Shooter Aim", meta = (DisplayName = "Hand Socket Name"))
	FName HandSocketName = TEXT("hand_r");

	/** 计算 Muzzle 相对 HandSocket 的刚性 Transform（纯数据路径，可被 Automation 验证）。
	 *  任一输入为 Identity / 无效时返回 Identity，避免把“原点恒等”误当成有效相对关系。 */
	UFUNCTION(BlueprintPure, Category = "Shooter Aim")
	static FTransform ComputeHandToMuzzleTransform(
		const FTransform& InHandWorld,
		const FTransform& InMuzzleWorld);

	/** 从第三人称 Muzzle 世界位置指向表现目标的世界方向；无效输入返回零向量（C4 纯计算）。 */
	UFUNCTION(BlueprintPure, Category = "Shooter Aim")
	static FVector ComputeMuzzleToTargetDirection(
		const FVector& MuzzleWorldLocation,
		const FVector& TargetWorld);

	/** C4 角色矩阵：按本地控制 / 消费角色 / 有效性决定 AimDirectionWorld 来源（纯计算，可被 Automation 验证）。 */
	static FVector ComputeAimDirectionWorldForState(
		bool bLocallyControlled,
		bool bShouldRunPresentationSmoothing,
		bool bPresentationTargetValid,
		const FVector& LocalAimDirection,
		const FVector& MuzzleWorldLocation,
		const FVector& SmoothedPresentationTarget,
		bool bHasThirdPersonMuzzle);

	/** 纯判定：给定表现数据是否允许开启 Aim IK（可被 Automation 验证）。
	 *  至少要求 Character、第三人称 Mesh、CurrentWeapon、第三人称 Muzzle socket、
	 *  非 Identity 的 HandToMuzzle 与有限非零 AimDirection。 */
	static bool IsAimIKEnabledForState(
		bool bHasCharacter,
		bool bHasThirdPersonMesh,
		bool bHasCurrentWeapon,
		bool bHasThirdPersonMuzzle,
		const FTransform& HandToMuzzle,
		const FVector& AimDirectionWorld);

	/** 读取角色 Mesh 指定 socket 的世界变换；无角色 / 无 socket 时回退 Identity。 */
	UFUNCTION(BlueprintPure, Category = "Shooter Aim")
	static FTransform GetHandWorldTransform(const AShooterCharacter* InCharacter, FName InHandSocketName);

	/** 计算左手握把相对右手 HandSocket 的刚性 Transform（纯数据路径，可被 Automation 验证）。
	 *  任一输入为 Identity / 无效时返回 Identity，避免把“原点恒等”误当成有效握把关系。 */
	UFUNCTION(BlueprintPure, Category = "Shooter Left Hand IK")
	static FTransform ComputeLeftHandGripInRightHandSpace(
		const FTransform& InRightHandWorld,
		const FTransform& InLeftHandGripWorld);

	/** 纯判定：左手 IK 是否允许开启（可被 Automation 验证）。
	 *  要求 Character、第三人称 Mesh、CurrentWeapon、第三人称武器 Mesh 已附着、
	 *  HandSocket 与武器左手握把 Socket 真实存在，且握把缓存为有限非 Identity 的刚性 Transform。 */
	static bool IsLeftHandIKEnabledForState(
		bool bHasCharacter,
		bool bHasThirdPersonMesh,
		bool bHasCurrentWeapon,
		bool bWeaponThirdPersonMeshAttached,
		bool bHasThirdPersonHandSocket,
		bool bHasThirdPersonLeftHandGripSocket,
		const FTransform& LeftHandGripInRightHandSpace);

	/** 纯判定：左手握把缓存是否需要重建（可被 Automation 验证）。
	 *  缓存未建立、武器变化、同一武器第三人称 Mesh 附着状态变化或已缓存结果无效时返回 true；
	 *  缺失 Socket 的稳定 Identity 状态不返回 true，避免每帧追逐世界点。 */
	static bool ShouldRefreshLeftHandGripCache(
		bool bCacheDirty,
		bool bWeaponChanged,
		bool bAttachStateChanged,
		bool bCacheInvalid);


protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	/** 已缓存武器引用；变化时刷新 HandToMuzzle。 */
	UPROPERTY(Transient)
	TObjectPtr<AShooterWeapon> CachedWeapon = nullptr;

	/** 已缓存武器第三人称 Mesh 是否附着到角色 Mesh；附着状态变化时刷新 HandToMuzzle。 */
	bool bCachedWeaponThirdPersonMeshAttached = false;

	/** 已缓存左手握把所属武器；武器变化时重建左手握把缓存。 */
	UPROPERTY(Transient)
	TObjectPtr<AShooterWeapon> CachedLeftHandGripWeapon = nullptr;

	/** 左手握把缓存时的武器第三人称 Mesh 附着状态；附着状态变化时重建缓存。 */
	bool bCachedLeftHandGripThirdPersonMeshAttached = false;

	/** 左手握把缓存尚未建立；角色变化或武器清空后重新置脏。 */
	bool bLeftHandGripCacheDirty = true;

	/** 左手握把缓存重建时记录的 HandSocket 存在状态，避免动画更新每帧查询 Socket。 */
	bool bCachedThirdPersonHandSocketExists = false;

};
