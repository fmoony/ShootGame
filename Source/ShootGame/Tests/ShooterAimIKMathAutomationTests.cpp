// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Characters/Animation/AnimNodes/ShooterAimIKMath.h"

namespace ShooterAimIKMathTestHelpers
{
	/** 两方向三维夹角（度）。 */
	float AngleBetween(const FVector& A, const FVector& B)
	{
		const float Dot = FVector::DotProduct(A.GetSafeNormal(), B.GetSafeNormal());
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f)));
	}

	/** 构造 Component=World（Identity）、hand 无旋转、HandToMuzzle=Identity 的最简场景。 */
	void Solve(
		const FVector& InAimDirectionWorld,
		float InAlpha,
		float InMaxCorrectionDegrees,
		FQuat& OutCorrection,
		bool& OutValid,
		const FTransform& InHandTM = FTransform::Identity,
		const FTransform& InHandToMuzzle = FTransform::Identity)
	{
		OutValid = FShooterAimIKMath::SolveHandCorrection(
			InAimDirectionWorld,
			FTransform::Identity,
			InHandTM,
			InHandToMuzzle,
			InAlpha,
			InMaxCorrectionDegrees,
			OutCorrection);
	}
}

/**
 * C2.3 纯数学测试：Shooter Aim IK 单骨骼校正求解器。
 * 不依赖 AnimBP / Actor；验证几何闭环、fail-soft 与角度限制。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAimIKMathTest,
	"ShootGame.Aim.IKMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAimIKMathTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAimIKMathTestHelpers;

	FQuat Correction;
	bool Valid = false;

	// ---- Identity：瞄准方向与当前 Muzzle 方向一致 → 无需校正 ----
	{
		Correction = FQuat(FVector::UpVector, 1.0f);
		Solve(FVector::ForwardVector, 1.0f, 90.0f, Correction, Valid);
		TestTrue(TEXT("identity case valid"), Valid);
		TestTrue(TEXT("identity correction is identity"), Correction.IsIdentity(1e-3f));
	}

	// ---- Pitch 30°：上仰后 Muzzle 对齐 ----
	{
		const FVector Aim = FRotator(30.0f, 0.0f, 0.0f).RotateVector(FVector::ForwardVector);
		Solve(Aim, 1.0f, 90.0f, Correction, Valid);
		TestTrue(TEXT("pitch30 valid"), Valid);
		const FVector MuzzleAfter = Correction.RotateVector(FVector::ForwardVector);
		TestTrue(TEXT("pitch30 muzzle aligns aim"), AngleBetween(MuzzleAfter, Aim) < 0.1f);
	}

	// ---- Yaw 30° ----
	{
		const FVector Aim = FRotator(0.0f, 30.0f, 0.0f).RotateVector(FVector::ForwardVector);
		Solve(Aim, 1.0f, 90.0f, Correction, Valid);
		TestTrue(TEXT("yaw30 valid"), Valid);
		const FVector MuzzleAfter = Correction.RotateVector(FVector::ForwardVector);
		TestTrue(TEXT("yaw30 muzzle aligns aim"), AngleBetween(MuzzleAfter, Aim) < 0.1f);
	}

	// ---- Combined Pitch/Yaw：20° 上仰 + 40° 偏航 ----
	{
		const FVector Aim = FRotator(20.0f, 40.0f, 0.0f).RotateVector(FVector::ForwardVector);
		Solve(Aim, 1.0f, 90.0f, Correction, Valid);
		TestTrue(TEXT("combined valid"), Valid);
		const FVector MuzzleAfter = Correction.RotateVector(FVector::ForwardVector);
		TestTrue(TEXT("combined muzzle aligns aim"), AngleBetween(MuzzleAfter, Aim) < 0.1f);
	}

	// ---- HandToMuzzle Rotation Offset：Muzzle 相对 hand 绕 Z 转 90°（muzzle 朝 hand +Y）----
	{
		const FTransform HandToMuzzle(FRotator(0.0f, 0.0f, 90.0f));
		const FVector Aim = FVector::UpVector;
		const FTransform HandTM(FRotator(0.0f, 0.0f, 90.0f)); // hand 自身也带旋转
		Valid = FShooterAimIKMath::SolveHandCorrection(
			Aim, FTransform::Identity, HandTM, HandToMuzzle, 1.0f, 90.0f, Correction);
		TestTrue(TEXT("offset valid"), Valid);
		// 校正后 MuzzleForwardInHand（hand 局部 +Y）经手旋转 + 校正应指向 Aim。
		const FVector MuzzleForwardInHand = FShooterAimIKMath::GetMuzzleForwardInHand(HandToMuzzle);
		const FVector MuzzleAfter = Correction.RotateVector(HandTM.GetRotation().RotateVector(MuzzleForwardInHand));
		TestTrue(TEXT("offset muzzle aligns aim"), AngleBetween(MuzzleAfter, Aim) < 0.1f);
	}

	// ---- 非 Identity 组件变换：世界方向转 Component Space 后再对齐 ----
	{
		const FTransform ComponentTM(FRotator(0.0f, 90.0f, 0.0f), FVector(100.0f, 200.0f, 300.0f));
		const FVector AimWorld = ComponentTM.GetRotation().RotateVector(FVector::UpVector);
		Valid = FShooterAimIKMath::SolveHandCorrection(
			AimWorld, ComponentTM, FTransform::Identity, FTransform::Identity, 1.0f, 90.0f, Correction);
		TestTrue(TEXT("component space valid"), Valid);
		const FVector DesiredCS = ComponentTM.InverseTransformVectorNoScale(AimWorld);
		const FVector MuzzleAfter = Correction.RotateVector(FVector::ForwardVector);
		TestTrue(TEXT("component space muzzle aligns aim"), AngleBetween(MuzzleAfter, DesiredCS) < 0.1f);
	}

	// ---- Zero Direction：fail-soft ----
	{
		Solve(FVector::ZeroVector, 1.0f, 90.0f, Correction, Valid);
		TestFalse(TEXT("zero direction invalid"), Valid);
	}

	// ---- Invalid Numeric：NaN 方向 / NaN HandTM ----
	{
		Solve(FVector(NAN, 0.0f, 0.0f), 1.0f, 90.0f, Correction, Valid);
		TestFalse(TEXT("nan direction invalid"), Valid);

		const FTransform NanHandTM(FQuat(NAN, 0.0f, 0.0f, 1.0f), FVector::ZeroVector);
		Valid = FShooterAimIKMath::SolveHandCorrection(
			FVector::UpVector, FTransform::Identity, NanHandTM, FTransform::Identity, 1.0f, 90.0f, Correction);
		TestFalse(TEXT("nan hand transform invalid"), Valid);
	}

	// ---- MaxCorrection：90° 需求被限制到 30° ----
	{
		const FVector Aim = FVector::UpVector; // 与 +X 差 90°
		Solve(Aim, 1.0f, 30.0f, Correction, Valid);
		TestTrue(TEXT("maxcorrection valid"), Valid);
		const FVector MuzzleAfter = Correction.RotateVector(FVector::ForwardVector);
		const float Residual = AngleBetween(MuzzleAfter, Aim);
		TestTrue(TEXT("maxcorrection residual ~60 deg"), FMath::IsNearlyEqual(Residual, 60.0f, 0.5f));
	}

	// ---- Alpha：90° 需求按 0.5 强度 → 残差 ~45° ----
	{
		const FVector Aim = FVector::UpVector;
		Solve(Aim, 0.5f, 90.0f, Correction, Valid);
		TestTrue(TEXT("alpha valid"), Valid);
		const FVector MuzzleAfter = Correction.RotateVector(FVector::ForwardVector);
		const float Residual = AngleBetween(MuzzleAfter, Aim);
		TestTrue(TEXT("alpha residual ~45 deg"), FMath::IsNearlyEqual(Residual, 45.0f, 1.0f));
	}

	// ---- MaxCorrection 0（不限制）：90° 完全校正 ----
	{
		const FVector Aim = FVector::UpVector;
		Solve(Aim, 1.0f, 0.0f, Correction, Valid);
		TestTrue(TEXT("unlimited valid"), Valid);
		const FVector MuzzleAfter = Correction.RotateVector(FVector::ForwardVector);
		TestTrue(TEXT("unlimited muzzle aligns aim"), AngleBetween(MuzzleAfter, Aim) < 0.1f);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
