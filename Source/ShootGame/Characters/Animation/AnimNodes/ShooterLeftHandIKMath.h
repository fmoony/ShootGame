// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Shooter 左手握持节点使用的纯 Transform 计算。 */
class SHOOTGAME_API FShooterLeftHandIKMath
{
public:
	/**
	 * 计算 hand_l 在组件空间中的目标 Transform，使角色 HandGrip_L 与武器握把完整重合。
	 *
	 * WeaponGripInRightHandSpace * RightHandCS = 武器握把目标（组件空间）
	 * HandGripInLeftHandSpace * DesiredLeftHandCS = 武器握把目标（组件空间）
	 */
	static bool CalculateDesiredLeftHandTransform(
		const FTransform& RightHandCS,
		const FTransform& WeaponGripInRightHandSpace,
		const FTransform& HandGripInLeftHandSpace,
		FTransform& OutDesiredLeftHandCS);

	/** 从首次有效输入动画提取组件空间弯肘方向，供节点缓存为稳定基准。 */
	static bool CalculateSourcePoleDirection(
		const FVector& RootLocation,
		const FVector& JointLocation,
		const FVector& EndLocation,
		FVector& OutPoleDirection);

	/**
	 * 将缓存 Pole 投影到当前目标肩腕平面，并按上一帧方向锁定同一半球。
	 * 这样快速横扫经过共线区时，Joint Target 不会突然换到手臂另一侧。
	 */
	static bool CalculateLockedJointTarget(
		const FVector& RootLocation,
		const FVector& JointLocation,
		const FVector& EffectorLocation,
		const FVector& PreferredPoleDirection,
		const FVector& PreviousPoleDirection,
		float MinimumPoleOffset,
		FVector& OutJointTarget,
		FVector& OutPoleDirection);

	/** Transform 有限且不是用于表示“无数据”的 Identity。 */
	static bool IsUsableFrame(const FTransform& Transform);
};
