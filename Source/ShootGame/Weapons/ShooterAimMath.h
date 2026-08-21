// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 第三人称瞄准表现的角度数学（纯函数：无状态、无网络字段、无副作用）。
 * B1 冻结基线时提供可测试的纯计算；B3 观察端平滑与 AimYaw/AimPitch
 * 局部角度计算继续使用同一套规则，避免两端出现不一致的环绕处理。
 */
namespace FShooterAimMath
{
	/**
	 * 将任意角度差值归一到 (-180, 180]（与 FRotator::NormalizeAxis 语义一致）。
	 * 例：359 -> -1，181 -> -179，-180 -> 180。
	 */
	float NormalizeAngleDelta(float AngleDegrees);

	/**
	 * 最短路径插值步进：从 CurrentDegrees 沿最短方向走向 TargetDegrees，
	 * 单步最多走 MaxDeltaDegrees，返回未归一的新角度。
	 * 例：(0, 350, 30) -> -10（等效 350，走最短的 -10°）。
	 */
	float ShortestAngleInterp(float CurrentDegrees, float TargetDegrees, float MaxDeltaDegrees);

	/**
	 * 世界方向 -> 相对参考变换的局部 AimYaw / AimPitch（度）。
	 * 局部方向 = ReferenceTransform 逆变换后的方向（与实施计划 B3 公式一致）。
	 * AimYaw   ：局部方向的水平角，范围 (-180, 180]。
	 * AimPitch ：局部方向的垂直角（上正），范围 [-90, 90]。
	 * 符号约定与 UE 5.6 FRotator 一致（FQuat(FRotator) 的 pitch 绕 -Y、yaw 绕 +Z）：
	 * 参考系 +Yaw 旋转后，世界前方在局部表现为 -Yaw。行为由 ShootGame.Aim.Math
	 * 自动化测试锁定，B3 消费端不得自行假设右手系直觉符号。
	 * WorldDirection 为零向量时输出 0 / 0。
	 */
	void WorldDirectionToLocalAngles(
		const FVector& WorldDirection,
		const FTransform& ReferenceTransform,
		float& OutAimYaw,
		float& OutAimPitch);
}
