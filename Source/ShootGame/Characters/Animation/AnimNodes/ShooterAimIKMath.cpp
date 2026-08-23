// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/Animation/AnimNodes/ShooterAimIKMath.h"

FVector FShooterAimIKMath::GetMuzzleForwardInHand(const FTransform& InHandToMuzzle)
{
	return InHandToMuzzle.GetRotation().RotateVector(FVector::ForwardVector);
}

bool FShooterAimIKMath::IsFinite(const FVector& InV)
{
	return FMath::IsFinite(InV.X) && FMath::IsFinite(InV.Y) && FMath::IsFinite(InV.Z);
}

bool FShooterAimIKMath::SolveHandCorrection(
	const FVector& InAimDirectionWorld,
	const FTransform& InComponentTransform,
	const FTransform& InHandTM,
	const FTransform& InHandToMuzzle,
	float InAlpha,
	float InMaxCorrectionDegrees,
	FQuat& OutCorrectionDelta)
{
	// fail-soft：任何输入非有限 / 无效，直接判定不可校正。
	if (!IsFinite(InAimDirectionWorld) || InAimDirectionWorld.IsNearlyZero())
	{
		return false;
	}
	if (!InComponentTransform.IsValid() || !InHandTM.IsValid() || !InHandToMuzzle.IsValid())
	{
		return false;
	}

	// 世界 → Component Space（无缩放，避免 Component 缩放污染方向）。
	const FVector DesiredCS = InComponentTransform.InverseTransformVectorNoScale(InAimDirectionWorld);
	if (!IsFinite(DesiredCS) || DesiredCS.IsNearlyZero())
	{
		return false;
	}

	const FVector MuzzleForwardInHand = GetMuzzleForwardInHand(InHandToMuzzle);
	if (!IsFinite(MuzzleForwardInHand) || MuzzleForwardInHand.IsNearlyZero())
	{
		return false;
	}

	const FVector CurrentMuzzleForwardCS = InHandTM.GetRotation().RotateVector(MuzzleForwardInHand);
	if (!IsFinite(CurrentMuzzleForwardCS) || CurrentMuzzleForwardCS.IsNearlyZero())
	{
		return false;
	}

	const FVector From = CurrentMuzzleForwardCS.GetSafeNormal();
	const FVector To = DesiredCS.GetSafeNormal();

	// 已对齐：无需校正。
	if (FVector::DotProduct(From, To) > 1.0f - KINDA_SMALL_NUMBER)
	{
		OutCorrectionDelta = FQuat::Identity;
		return true;
	}

	FQuat Delta = FQuat::FindBetweenNormals(From, To);
	Delta.Normalize();

	// MaxCorrectionAngle 限制（度 → 弧度）。
	const float MaxRadians = FMath::DegreesToRadians(FMath::Max(InMaxCorrectionDegrees, 0.0f));
	const float DeltaAngle = Delta.GetAngle();
	if (MaxRadians > 0.0f && DeltaAngle > MaxRadians)
	{
		Delta = FQuat::Slerp(FQuat::Identity, Delta, MaxRadians / DeltaAngle);
	}

	// Alpha 强度（节点侧已由基类 ActualAlpha 混合时传 1，此处保留独立强度入口）。
	const float ClampedAlpha = FMath::Clamp(InAlpha, 0.0f, 1.0f);
	if (ClampedAlpha < 1.0f)
	{
		Delta = FQuat::Slerp(FQuat::Identity, Delta, ClampedAlpha);
	}

	OutCorrectionDelta = Delta;
	return true;
}
