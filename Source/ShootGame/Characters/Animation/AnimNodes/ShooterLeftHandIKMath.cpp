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
	return Transform.IsValid() && !Transform.Equals(FTransform::Identity);
}
