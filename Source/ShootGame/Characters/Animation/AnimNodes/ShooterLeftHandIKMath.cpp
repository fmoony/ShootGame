// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/Animation/AnimNodes/ShooterLeftHandIKMath.h"

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

bool FShooterLeftHandIKMath::IsUsableFrame(const FTransform& Transform)
{
	return Transform.IsValid() && !Transform.Equals(FTransform::Identity);
}
