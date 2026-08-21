// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterAimMath.h"

float FShooterAimMath::NormalizeAngleDelta(float AngleDegrees)
{
	return FRotator::NormalizeAxis(AngleDegrees);
}

float FShooterAimMath::ShortestAngleInterp(
	float CurrentDegrees,
	float TargetDegrees,
	float MaxDeltaDegrees)
{
	const float Delta = NormalizeAngleDelta(TargetDegrees - CurrentDegrees);
	const float ClampedStep = FMath::Clamp(
		Delta,
		-FMath::Abs(MaxDeltaDegrees),
		FMath::Abs(MaxDeltaDegrees));
	return CurrentDegrees + ClampedStep;
}

void FShooterAimMath::WorldDirectionToLocalAngles(
	const FVector& WorldDirection,
	const FTransform& ReferenceTransform,
	float& OutAimYaw,
	float& OutAimPitch)
{
	const FVector LocalDirection = ReferenceTransform.InverseTransformVectorNoScale(
		WorldDirection);
	const FVector SafeDirection = LocalDirection.GetSafeNormal();
	if (SafeDirection.IsNearlyZero())
	{
		OutAimYaw = 0.0f;
		OutAimPitch = 0.0f;
		return;
	}

	const FRotator LocalRotator = SafeDirection.Rotation();
	OutAimYaw = FRotator::NormalizeAxis(LocalRotator.Yaw);
	OutAimPitch = FRotator::NormalizeAxis(LocalRotator.Pitch);
}
