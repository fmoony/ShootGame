// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Characters/Animation/AnimNodes/ShooterLeftHandIKMath.h"

namespace ShooterLeftHandIKAutomationTests
{
	const FTransform NonIdentityLeftHandGripInRightHandSpace(
		FQuat(FRotator(0.0f, 0.0f, 15.0f)),
		FVector(12.0f, 4.0f, 3.0f),
		FVector::OneVector);
	const FTransform NonIdentityHandGripInLeftHandSpace(
		FQuat(FRotator(5.0f, -10.0f, 20.0f)),
		FVector(8.0f, 1.0f, -2.0f),
		FVector::OneVector);
}

/**
 * 左手握把数据契约纯计算测试：握把相对 hand_r 的刚性 Transform。
 * 验证 Identity / NaN 回退、有效世界 Transform 的刚性相对关系与有限性。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterLeftHandGripTransformTest,
	"ShootGame.Aim.LeftHandGripTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterLeftHandGripTransformTest::RunTest(const FString& Parameters)
{
	using namespace ShooterLeftHandIKAutomationTests;

	// Identity 是合法 Transform：输入恰好为 Identity 时 Compute 成功（不再当作“缺失数据”）。
	TestTrue(
		TEXT("identity right hand world computes rigid relative grip"),
		UShooterThirdPersonAnimInstance::ComputeLeftHandGripInRightHandSpace(
			FTransform::Identity,
			FTransform(FVector(100.0f, 200.0f, 300.0f))).Equals(
				FTransform(FVector(100.0f, 200.0f, 300.0f)), 1e-4f));

	TestTrue(
		TEXT("identity grip world computes rigid relative grip"),
		UShooterThirdPersonAnimInstance::ComputeLeftHandGripInRightHandSpace(
			FTransform(FVector(100.0f, 200.0f, 300.0f)),
			FTransform::Identity).Equals(
				FTransform(FVector(-100.0f, -200.0f, -300.0f)), 1e-4f));

	// NaN 输入必须 fail-soft 返回 Identity。
	const FTransform InvalidTransform(
		FQuat(NAN, 0.0f, 0.0f, 1.0f),
		FVector::ZeroVector,
		FVector::OneVector);
	TestTrue(
		TEXT("nan right hand world yields identity grip transform"),
		UShooterThirdPersonAnimInstance::ComputeLeftHandGripInRightHandSpace(
			InvalidTransform,
			NonIdentityLeftHandGripInRightHandSpace).Equals(FTransform::Identity));
	TestTrue(
		TEXT("nan grip world yields identity grip transform"),
		UShooterThirdPersonAnimInstance::ComputeLeftHandGripInRightHandSpace(
			FTransform(FVector(100.0f, 200.0f, 300.0f)),
			InvalidTransform).Equals(FTransform::Identity));

	// 有效输入：Grip 相对 Hand 的刚性 Transform，并且是非 Identity 的有限结果。
	const FTransform ValidRightHandWorld(
		FRotator(0.0f, 0.0f, 10.0f),
		FVector(100.0f, 50.0f, 80.0f));
	const FTransform ValidGripWorld(
		FRotator(0.0f, 5.0f, -5.0f),
		FVector(140.0f, 62.0f, 88.0f));
	const FTransform ComputedGrip =
		UShooterThirdPersonAnimInstance::ComputeLeftHandGripInRightHandSpace(
			ValidRightHandWorld,
			ValidGripWorld);
	const FTransform ExpectedGrip = ValidGripWorld.GetRelativeTransform(ValidRightHandWorld);

	TestTrue(TEXT("computed grip transform is finite"), ComputedGrip.IsValid());
	TestFalse(TEXT("computed grip transform is not identity"), ComputedGrip.Equals(FTransform::Identity));
	TestTrue(
		TEXT("computed grip transform equals rigid relative transform"),
		ComputedGrip.Equals(ExpectedGrip, 1e-4f));

	const FTransform ValidLeftHandWorld(
		FRotator(2.0f, 15.0f, -8.0f),
		FVector(80.0f, 35.0f, 70.0f));
	const FTransform ValidHandGripWorld = NonIdentityHandGripInLeftHandSpace * ValidLeftHandWorld;
	TestTrue(
		TEXT("character palm frame is cached relative to hand_l"),
		UShooterThirdPersonAnimInstance::ComputeHandGripInLeftHandSpace(
			ValidLeftHandWorld,
			ValidHandGripWorld).Equals(NonIdentityHandGripInLeftHandSpace, 1e-4f));

	return true;
}

/** 双参考帧闭环：求出的 hand_l 必须让 HandGrip_L 与武器握把位置和旋转同时重合。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterLeftHandFrameAlignmentTest,
	"ShootGame.Aim.LeftHandFrameAlignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterLeftHandFrameAlignmentTest::RunTest(const FString& Parameters)
{
	using namespace ShooterLeftHandIKAutomationTests;

	const FTransform RightHandCS(
		FRotator(12.0f, 35.0f, -4.0f),
		FVector(42.0f, 18.0f, 120.0f));
	FTransform DesiredLeftHandCS;
	TestTrue(
		TEXT("valid grip frames produce a left hand target"),
		FShooterLeftHandIKMath::CalculateDesiredLeftHandTransform(
			RightHandCS,
			NonIdentityLeftHandGripInRightHandSpace,
			NonIdentityHandGripInLeftHandSpace,
			DesiredLeftHandCS));

	const FTransform TargetWeaponGripCS = NonIdentityLeftHandGripInRightHandSpace * RightHandCS;
	const FTransform ReconstructedHandGripCS = NonIdentityHandGripInLeftHandSpace * DesiredLeftHandCS;
	TestTrue(
		TEXT("HandGrip_L location and rotation align with the weapon grip"),
		ReconstructedHandGripCS.Equals(TargetWeaponGripCS, 1e-4f));

	TestFalse(
		TEXT("missing character palm frame fails soft"),
		FShooterLeftHandIKMath::CalculateDesiredLeftHandTransform(
			RightHandCS,
			NonIdentityLeftHandGripInRightHandSpace,
			FTransform(FQuat(NAN, 0.0f, 0.0f, 1.0f), FVector::ZeroVector, FVector::OneVector),
			DesiredLeftHandCS));

	// Identity 是合法参考帧：不再是“缺失数据”，数学上可解。
	TestTrue(
		TEXT("identity character palm frame is mathematically solvable"),
		FShooterLeftHandIKMath::CalculateDesiredLeftHandTransform(
			RightHandCS,
			NonIdentityLeftHandGripInRightHandSpace,
			FTransform::Identity,
			DesiredLeftHandCS));

	return true;
}

/** Joint Target 保留输入动画弯肘侧，并在旧肘点与新肩腕轴共线时提供稳定横向参考。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterLeftHandStablePoleTest,
	"ShootGame.Aim.LeftHandStablePole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterLeftHandStablePoleTest::RunTest(const FString& Parameters)
{
	const FVector Root(0.0f, 0.0f, 0.0f);
	const FVector Joint(30.0f, 10.0f, 0.0f);
	const FVector End(60.0f, 0.0f, 0.0f);
	FVector PreferredPoleDirection;
	FVector JointTarget;
	FVector ResolvedPoleDirection;

	TestTrue(
		TEXT("source bend plane produces a preferred pole direction"),
		FShooterLeftHandIKMath::CalculateSourcePoleDirection(
			Root, Joint, End, PreferredPoleDirection));
	TestTrue(
		TEXT("source elbow side points toward positive Y"),
		PreferredPoleDirection.Y > 0.0f);

	const FVector BeforeSingularityEffector = FVector(0.5f, 50.0f, 0.0f);
	TestTrue(
		TEXT("preferred pole resolves before the projection singularity"),
		FShooterLeftHandIKMath::CalculateLockedJointTarget(
			Root,
			Joint,
			BeforeSingularityEffector,
			PreferredPoleDirection,
			FVector::ZeroVector,
			5.0f,
			JointTarget,
			ResolvedPoleDirection));
	const FVector BeforeDirection = ResolvedPoleDirection;

	// 目标从缓存 Pole 的一侧越过另一侧时，直接投影会反号；上一帧半球锁应保持连续。
	const FVector AfterSingularityEffector = FVector(-0.5f, 50.0f, 0.0f);
	TestTrue(
		TEXT("previous hemisphere resolves after the projection singularity"),
		FShooterLeftHandIKMath::CalculateLockedJointTarget(
			Root,
			Joint,
			AfterSingularityEffector,
			PreferredPoleDirection,
			BeforeDirection,
			5.0f,
			JointTarget,
			ResolvedPoleDirection));
	TestTrue(
		TEXT("pole does not reverse hemisphere during a fast sweep"),
		FVector::DotProduct(BeforeDirection, ResolvedPoleDirection) > 0.0f);
	const FVector DesiredDirection = AfterSingularityEffector.GetSafeNormal();
	const float LateralDistance = FVector::VectorPlaneProject(
		JointTarget - Root,
		DesiredDirection).Size();
	TestTrue(
		TEXT("resolved pole keeps a non-degenerate lateral offset"),
		LateralDistance >= 5.0f - KINDA_SMALL_NUMBER);

	TestFalse(
		TEXT("invalid input fails soft"),
		FShooterLeftHandIKMath::CalculateLockedJointTarget(
			Root,
			FVector(NAN, 0.0f, 0.0f),
			AfterSingularityEffector,
			PreferredPoleDirection,
			BeforeDirection,
			5.0f,
			JointTarget,
			ResolvedPoleDirection));

	return true;
}

/**
 * 左手 IK 状态矩阵 / 握把缓存重建判定已迁移：
 * 原 IsLeftHandIKEnabledForState / ShouldRefreshLeftHandGripCache 随第三人称 IK Binding
 * 状态机退役（实施计划 6.2 / 6.5），等价场景由 ShootGame.Aim.Binding.* 测试覆盖。
 */

#endif // WITH_DEV_AUTOMATION_TESTS
