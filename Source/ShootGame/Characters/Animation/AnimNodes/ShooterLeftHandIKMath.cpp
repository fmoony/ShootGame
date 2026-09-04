// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/Animation/AnimNodes/ShooterLeftHandIKMath.h"

namespace
{
	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) &&
			FMath::IsFinite(Value.Y) &&
			FMath::IsFinite(Value.Z);
	}
}

bool FShooterLeftHandIKMath::CalculateDesiredLeftHandTransform(
	const FTransform& RightHandCS,
	const FTransform& WeaponGripInRightHandSpace,
	const FTransform& HandGripInLeftHandSpace,
	FTransform& OutDesiredLeftHandCS)
{
	OutDesiredLeftHandCS = FTransform::Identity;
	if (!RightHandCS.IsValid() ||
		!IsUsableFrame(WeaponGripInRightHandSpace) ||
		!IsUsableFrame(HandGripInLeftHandSpace))
	{
		return false;
	}

	const FTransform TargetGripCS = WeaponGripInRightHandSpace * RightHandCS;
	OutDesiredLeftHandCS = HandGripInLeftHandSpace.Inverse() * TargetGripCS;
	OutDesiredLeftHandCS.NormalizeRotation();

	return OutDesiredLeftHandCS.IsValid();
}

bool FShooterLeftHandIKMath::CalculateSourcePoleDirection(
	const FVector& RootLocation,
	const FVector& JointLocation,
	const FVector& EndLocation,
	FVector& OutPoleDirection)
{
	OutPoleDirection = FVector::ZeroVector;
	if (!IsFiniteVector(RootLocation) ||
		!IsFiniteVector(JointLocation) ||
		!IsFiniteVector(EndLocation))
	{
		return false;
	}

	const FVector SourceEndDirection = (EndLocation - RootLocation).GetSafeNormal();
	if (SourceEndDirection.IsNearlyZero())
	{
		return false;
	}

	OutPoleDirection = FVector::VectorPlaneProject(
		JointLocation - RootLocation,
		SourceEndDirection);
	return OutPoleDirection.Normalize();
}

bool FShooterLeftHandIKMath::CalculateLockedJointTarget(
	const FVector& RootLocation,
	const FVector& JointLocation,
	const FVector& EffectorLocation,
	const FVector& PreferredPoleDirection,
	const FVector& PreviousPoleDirection,
	float MinimumPoleOffset,
	FVector& OutJointTarget,
	FVector& OutPoleDirection)
{
	OutJointTarget = JointLocation;
	OutPoleDirection = FVector::ZeroVector;
	if (!IsFiniteVector(RootLocation) ||
		!IsFiniteVector(JointLocation) ||
		!IsFiniteVector(EffectorLocation) ||
		!IsFiniteVector(PreferredPoleDirection) ||
		!IsFiniteVector(PreviousPoleDirection))
	{
		return false;
	}

	const FVector DesiredDirection = (EffectorLocation - RootLocation).GetSafeNormal();
	if (DesiredDirection.IsNearlyZero())
	{
		return false;
	}

	FVector PreviousOnPlane = FVector::VectorPlaneProject(
		PreviousPoleDirection,
		DesiredDirection);
	PreviousOnPlane.Normalize();

	FVector DesiredBend = FVector::VectorPlaneProject(
		PreferredPoleDirection,
		DesiredDirection);
	if (!DesiredBend.Normalize())
	{
		// 共线区内优先延续上一帧 Pole；没有历史时才 fail-soft。
		if (PreviousOnPlane.IsNearlyZero())
		{
			return false;
		}
		DesiredBend = PreviousOnPlane;
	}
	else if (!PreviousOnPlane.IsNearlyZero() &&
		FVector::DotProduct(DesiredBend, PreviousOnPlane) < 0.0f)
	{
		// 投影越过奇异点后会自然反号；锁定上一帧半球阻止肘部换边。
		DesiredBend *= -1.0f;
	}

	const FVector SourceJointDelta = JointLocation - RootLocation;
	const float CurrentLateralOffset = FVector::VectorPlaneProject(
		SourceJointDelta,
		DesiredDirection).Size();
	const float PoleOffset = FMath::Max(
		CurrentLateralOffset,
		FMath::Max(MinimumPoleOffset, 0.01f));
	const float JointAxisDistance = FVector::DotProduct(SourceJointDelta, DesiredDirection);
	OutJointTarget = RootLocation +
		DesiredDirection * JointAxisDistance +
		DesiredBend * PoleOffset;
	OutPoleDirection = DesiredBend;
	return IsFiniteVector(OutJointTarget) && IsFiniteVector(OutPoleDirection);
}

bool FShooterLeftHandIKMath::IsUsableFrame(const FTransform& Transform)
{
	// 只查真正的非法数值 / 旋转 / Scale，不比较 Identity；
	// Identity 是否可消费由上层 Binding.State == Ready 决定。
	return Transform.IsValid() &&
		Transform.GetScale3D().X > 0.0f &&
		Transform.GetScale3D().Y > 0.0f &&
		Transform.GetScale3D().Z > 0.0f;
}

bool FShooterLeftHandIKMath::ComputeSwingTwistDegrees(
	const FQuat& Rotation,
	const FVector& TwistAxis,
	float& OutTwistDegrees,
	float& OutSwingDegrees)
{
	OutTwistDegrees = 0.0f;
	OutSwingDegrees = 0.0f;
	const FVector NormalizedAxis = TwistAxis.GetSafeNormal();
	if (!Rotation.IsNormalized() || NormalizedAxis.IsNearlyZero() || Rotation.ContainsNaN())
	{
		return false;
	}

	OutTwistDegrees = FMath::RadiansToDegrees(Rotation.GetTwistAngle(NormalizedAxis));
	FQuat Swing;
	FQuat Twist;
	Rotation.ToSwingTwist(NormalizedAxis, Swing, Twist);
	OutSwingDegrees = FMath::RadiansToDegrees(Swing.GetAngle());
	return FMath::IsFinite(OutTwistDegrees) && FMath::IsFinite(OutSwingDegrees);
}

bool FShooterLeftHandIKMath::ComputeHandRelativeToForearm(
	const FTransform& HandCS,
	const FTransform& LowerArmCS,
	FQuat& OutHandRelativeToForearm)
{
	OutHandRelativeToForearm = FQuat::Identity;
	if (!HandCS.IsValid() || !LowerArmCS.IsValid())
	{
		return false;
	}

	OutHandRelativeToForearm = HandCS.GetRotation() * LowerArmCS.GetRotation().Inverse();
	OutHandRelativeToForearm.Normalize();
	return OutHandRelativeToForearm.IsNormalized() && !OutHandRelativeToForearm.ContainsNaN();
}

bool FShooterLeftHandIKMath::ComputeWristDiagnostics(
	const FTransform& SourceHandCS,
	const FTransform& DesiredLeftHandCS,
	const FTransform& SolvedUpperArmCS,
	const FTransform& SolvedLowerArmCS,
	const FTransform& SolvedHandCS,
	const FVector& PreferredPoleDirection,
	const FQuat& BaselineHandRelativeToForearm,
	bool bBaselineValid,
	FShooterLeftHandWristDiagnostics& OutDiagnostics)
{
	OutDiagnostics = FShooterLeftHandWristDiagnostics();
	if (!SourceHandCS.IsValid() || !DesiredLeftHandCS.IsValid() ||
		!SolvedUpperArmCS.IsValid() || !SolvedLowerArmCS.IsValid() || !SolvedHandCS.IsValid())
	{
		return false;
	}

	OutDiagnostics.TargetPositionDelta = FVector::Dist(
		DesiredLeftHandCS.GetLocation(),
		SourceHandCS.GetLocation());
	OutDiagnostics.PostSolvePositionResidual = FVector::Dist(
		SolvedHandCS.GetLocation(),
		DesiredLeftHandCS.GetLocation());

	const FQuat RotationDelta = DesiredLeftHandCS.GetRotation() * SourceHandCS.GetRotation().Inverse();
	OutDiagnostics.TotalRotationDeltaDegrees = FMath::RadiansToDegrees(RotationDelta.GetAngle());

	// 前臂轴取求解后链条（肘 → 腕）方向；旋转差与基准修正共用同一根轴。
	const FVector ForearmAxisCS = (
		SolvedHandCS.GetLocation() - SolvedLowerArmCS.GetLocation()).GetSafeNormal();
	if (ForearmAxisCS.IsNearlyZero() ||
		!ComputeSwingTwistDegrees(
			RotationDelta,
			ForearmAxisCS,
			OutDiagnostics.TwistDegrees,
			OutDiagnostics.SwingDegrees))
	{
		return false;
	}

	// 肘部翻转：解算后肘点在肩腕轴横向的偏移是否仍与缓存 Pole 同侧。
	const FVector ShoulderWristAxisCS = (
		SolvedHandCS.GetLocation() - SolvedUpperArmCS.GetLocation()).GetSafeNormal();
	if (!PreferredPoleDirection.IsNearlyZero() && !ShoulderWristAxisCS.IsNearlyZero())
	{
		FVector PreferredPoleOnPlane = FVector::VectorPlaneProject(
			PreferredPoleDirection,
			ShoulderWristAxisCS);
		const FVector ElbowLateralCS = FVector::VectorPlaneProject(
			SolvedLowerArmCS.GetLocation() - SolvedUpperArmCS.GetLocation(),
			ShoulderWristAxisCS);
		const FVector ElbowLateralDirectionCS = ElbowLateralCS.GetSafeNormal();
		if (!ElbowLateralDirectionCS.IsNearlyZero() && PreferredPoleOnPlane.Normalize())
		{
			OutDiagnostics.bElbowMeasured = true;
			OutDiagnostics.ElbowPoleAlignment = FVector::DotProduct(
				ElbowLateralDirectionCS,
				PreferredPoleOnPlane);
			OutDiagnostics.bElbowHemisphereFlipped = OutDiagnostics.ElbowPoleAlignment < 0.0f;
		}
	}

	if (bBaselineValid && BaselineHandRelativeToForearm.IsNormalized())
	{
		// 保持握把目标位置不变、仅评估旋转：把基准手腕相对前臂姿态装回当前求解后的前臂，
		// 与末端实际旋转的差即“让手腕回到基础动画姿态”所需的旋转修正。
		const FQuat BaselineTargetRotationCS =
			BaselineHandRelativeToForearm * SolvedLowerArmCS.GetRotation();
		const FQuat BaselineCorrectionCS =
			BaselineTargetRotationCS * SolvedHandCS.GetRotation().Inverse();
		OutDiagnostics.bBaselineCorrectionValid = ComputeSwingTwistDegrees(
			BaselineCorrectionCS,
			ForearmAxisCS,
			OutDiagnostics.BaselineCorrectionTwistDegrees,
			OutDiagnostics.BaselineCorrectionSwingDegrees);
		if (OutDiagnostics.bBaselineCorrectionValid)
		{
			OutDiagnostics.BaselineCorrectionDegrees = FMath::RadiansToDegrees(
				BaselineCorrectionCS.GetAngle());
		}
	}

	return true;
}
