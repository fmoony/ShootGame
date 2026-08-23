// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * FAnimNode_ShooterAimIK 的纯数学求解器。
 * 不依赖任何引擎动画对象，可在 Automation 中直接验证（见 Tests/ShooterAimIKMathAutomationTests.cpp）。
 *
 * 几何定义：
 *   MuzzleForwardInHand = HandToMuzzle.Rotation * +X      （Muzzle 局部 +X 在 hand 坐标系中的方向）
 *   CurrentMuzzleForwardCS = HandRotation * MuzzleForwardInHand
 *   Desired = AimDirection 转换到 Component Space
 *   Correction = CurrentMuzzleForwardCS → Desired 的最短旋转（按 MaxCorrectionAngle 限制）
 */
class SHOOTGAME_API FShooterAimIKMath
{
public:
	/**
	 * 计算施加给 HandBone 的最短校正旋转（Component Space）。
	 *
	 * @param InAimDirectionWorld   世界空间瞄准方向（任意非零向量）
	 * @param InComponentTransform  骨骼网格组件世界变换（世界 → Component Space）
	 * @param InHandTM              当前 hand 骨骼的 Component Space Transform
	 * @param InHandToMuzzle        Muzzle 相对 hand 的刚性 Transform
	 * @param InAlpha               校正强度 [0,1]（节点侧已由基类 ActualAlpha 混合时传 1）
	 * @param InMaxCorrectionDegrees 单帧最大校正角（度；<=0 表示不限制）
	 * @param OutCorrectionDelta    输出：应用于 hand Rotation 的最短旋转
	 * @return false 表示输入无效（fail-soft：调用方不得修改 Pose）
	 */
	static bool SolveHandCorrection(
		const FVector& InAimDirectionWorld,
		const FTransform& InComponentTransform,
		const FTransform& InHandTM,
		const FTransform& InHandToMuzzle,
		float InAlpha,
		float InMaxCorrectionDegrees,
		FQuat& OutCorrectionDelta);

	/** Muzzle Forward（+X）在 hand 局部坐标系中的方向。 */
	static FVector GetMuzzleForwardInHand(const FTransform& InHandToMuzzle);

	/** 向量全分量有限（非 NaN / 非 Infinity）。 */
	static bool IsFinite(const FVector& InV);
};
