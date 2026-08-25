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

	/** Transform 有限且不是用于表示“无数据”的 Identity。 */
	static bool IsUsableFrame(const FTransform& Transform);
};
