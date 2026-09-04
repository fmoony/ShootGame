// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** 左手 IK 单次求值的数值诊断快照（IK 前后对比；角度单位为度）。 */
struct FShooterLeftHandWristDiagnostics
{
	/** 握把目标位置与 IK 前 hand_l 位置的距离（cm，IK 需要移动的量）。 */
	float TargetPositionDelta = 0.0f;

	/** IK 求解后 hand_l 实际位置与握把目标的残差（cm；>0 表示目标不完全可达）。 */
	float PostSolvePositionResidual = 0.0f;

	/** 握把目标旋转与 IK 前手腕旋转的总角度差。 */
	float TotalRotationDeltaDegrees = 0.0f;

	/** 旋转差相对求解后前臂轴（肘 → 腕）的 Twist 分量（带符号，符号随轴方向）。 */
	float TwistDegrees = 0.0f;

	/** 旋转差相对前臂轴的 Swing 分量（>=0）。 */
	float SwingDegrees = 0.0f;

	/** 肘部侧向是否已测量（缓存 Pole 有效且解算后肘点不与肩腕轴共线）。 */
	bool bElbowMeasured = false;

	/** 解算后肘点横向偏移与缓存 Pole 的同侧程度（[-1,1]；<0 表示肘部翻转到另一侧）。 */
	float ElbowPoleAlignment = 0.0f;

	/** 肘部翻转：解算后肘点换到缓存 Pole 的另一侧。 */
	bool bElbowHemisphereFlipped = false;

	/** 水平基准修正是否有效（基准已捕获且分解成功）。 */
	bool bBaselineCorrectionValid = false;

	/** 水平基准修正角度：保持当前握把位置、让手腕姿态回到基准所需的旋转。 */
	float BaselineCorrectionDegrees = 0.0f;

	/** 水平基准修正相对前臂轴的 Twist 分量（带符号）。 */
	float BaselineCorrectionTwistDegrees = 0.0f;

	/** 水平基准修正的 Swing 分量（>=0）。 */
	float BaselineCorrectionSwingDegrees = 0.0f;
};

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

	/** Transform 数学合法：有限（IsValid）且 Scale 各分量大于 0；Identity 是合法 Transform，不再当作“无数据”。 */
	static bool IsUsableFrame(const FTransform& Transform);

	/**
	 * 把单位旋转按 TwistAxis 分解（Rotation = Swing * Twist，与 FQuat::ToSwingTwist 一致）。
	 * 输出带符号 Twist 角度与非负 Swing 角度（度）。
	 * 输入必须已归一化且轴非零；否则 fail-soft 返回 false。
	 */
	static bool ComputeSwingTwistDegrees(
		const FQuat& Rotation,
		const FVector& TwistAxis,
		float& OutTwistDegrees,
		float& OutSwingDegrees);

	/** 源动画手腕姿态：hand_l 旋转相对前臂骨骼旋转（Hand.Rot * LowerArm.Rot^-1）。 */
	static bool ComputeHandRelativeToForearm(
		const FTransform& HandCS,
		const FTransform& LowerArmCS,
		FQuat& OutHandRelativeToForearm);

	/**
	 * 汇总一次左手 IK 前后的数值诊断。
	 * Twist / Swing 与水平基准修正都相对求解后前臂轴（肘 → 腕）分解。
	 * 水平基准修正 = BaselineHandRelativeToForearm * SolvedLowerArm.Rot * SolvedHand.Rot^-1，
	 * 即“保持当前握把目标位置、只把手腕姿态装回基准”所需的纯旋转修正。
	 * PreferredPoleDirection 近零或肘点与肩腕轴共线时跳过肘部测量；
	 * bBaselineValid 为 false 时跳过基准修正。
	 */
	static bool ComputeWristDiagnostics(
		const FTransform& SourceHandCS,
		const FTransform& DesiredLeftHandCS,
		const FTransform& SolvedUpperArmCS,
		const FTransform& SolvedLowerArmCS,
		const FTransform& SolvedHandCS,
		const FVector& PreferredPoleDirection,
		const FQuat& BaselineHandRelativeToForearm,
		bool bBaselineValid,
		FShooterLeftHandWristDiagnostics& OutDiagnostics);
};
