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

/** 诊断分解数学：绕指定轴的 Twist 带符号、垂直分量为 Swing，正交轴复合可精确分解。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterLeftHandSwingTwistDecompositionTest,
	"ShootGame.Aim.LeftHandSwingTwist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterLeftHandSwingTwistDecompositionTest::RunTest(const FString& Parameters)
{
	const FVector ForearmAxis(1.0f, 0.0f, 0.0f);
	const FVector SwingAxis(0.0f, 1.0f, 0.0f);
	float TwistDegrees = 0.0f;
	float SwingDegrees = 0.0f;

	const FQuat PureTwist(ForearmAxis, FMath::DegreesToRadians(30.0f));
	TestTrue(
		TEXT("pure twist about the forearm axis decomposes exactly"),
		FShooterLeftHandIKMath::ComputeSwingTwistDegrees(PureTwist, ForearmAxis, TwistDegrees, SwingDegrees));
	TestTrue(TEXT("pure twist keeps sign"), FMath::IsNearlyEqual(TwistDegrees, 30.0f, 1e-2f));
	TestTrue(TEXT("pure twist has no swing"), FMath::IsNearlyEqual(SwingDegrees, 0.0f, 1e-2f));

	const FQuat ReversedTwist(ForearmAxis, FMath::DegreesToRadians(-30.0f));
	TestTrue(
		TEXT("reversed twist keeps negative sign"),
		FShooterLeftHandIKMath::ComputeSwingTwistDegrees(ReversedTwist, ForearmAxis, TwistDegrees, SwingDegrees) &&
			FMath::IsNearlyEqual(TwistDegrees, -30.0f, 1e-2f));

	const FQuat PureSwing(SwingAxis, FMath::DegreesToRadians(40.0f));
	TestTrue(
		TEXT("rotation perpendicular to the axis is pure swing"),
		FShooterLeftHandIKMath::ComputeSwingTwistDegrees(PureSwing, ForearmAxis, TwistDegrees, SwingDegrees));
	TestTrue(TEXT("pure swing keeps zero twist"), FMath::IsNearlyEqual(TwistDegrees, 0.0f, 1e-2f));
	TestTrue(TEXT("pure swing keeps angle"), FMath::IsNearlyEqual(SwingDegrees, 40.0f, 1e-2f));

	// 绕正交轴的 Swing * Twist 复合：两个分量都能精确还原（ToSwingTwist 的数学性质）。
	const FQuat Combined = PureSwing * PureTwist;
	TestTrue(
		TEXT("combined rotation decomposes"),
		FShooterLeftHandIKMath::ComputeSwingTwistDegrees(Combined, ForearmAxis, TwistDegrees, SwingDegrees));
	TestTrue(TEXT("combined keeps twist angle"), FMath::IsNearlyEqual(TwistDegrees, 30.0f, 1e-2f));
	TestTrue(TEXT("combined keeps swing angle"), FMath::IsNearlyEqual(SwingDegrees, 40.0f, 1e-2f));

	TestFalse(
		TEXT("nan rotation fails soft"),
		FShooterLeftHandIKMath::ComputeSwingTwistDegrees(
			FQuat(NAN, 0.0f, 0.0f, 1.0f),
			ForearmAxis,
			TwistDegrees,
			SwingDegrees));
	TestFalse(
		TEXT("zero axis fails soft"),
		FShooterLeftHandIKMath::ComputeSwingTwistDegrees(
			PureTwist,
			FVector::ZeroVector,
			TwistDegrees,
			SwingDegrees));
	TestFalse(
		TEXT("non-normalized rotation fails soft"),
		FShooterLeftHandIKMath::ComputeSwingTwistDegrees(
			FQuat(PureTwist.X * 2.0f, PureTwist.Y, PureTwist.Z, PureTwist.W * 2.0f),
			ForearmAxis,
			TwistDegrees,
			SwingDegrees));

	return true;
}

namespace ShooterLeftHandWristDiagnosticsTests
{
	/** 与诊断分解共用的固定左臂链条：肩在原点、肘在 (25,Lateral,0)、腕在 (50,0,0)；前臂轴近似 +X。 */
	const FVector ShoulderLocation(0.0f, 0.0f, 0.0f);
	const FVector WristLocation(50.0f, 0.0f, 0.0f);
	const FVector PreferredPole(0.0f, 1.0f, 0.0f);

	FShooterLeftHandWristDiagnostics RunDiagnostics(
		const FTransform& SourceHandCS,
		const FTransform& DesiredLeftHandCS,
		const FTransform& SolvedUpperArmCS,
		const FTransform& SolvedLowerArmCS,
		const FTransform& SolvedHandCS,
		const FQuat& BaselineHandRelativeToForearm,
		bool bBaselineValid,
		bool& bOutComputed)
	{
		FShooterLeftHandWristDiagnostics Diagnostics;
		bOutComputed = FShooterLeftHandIKMath::ComputeWristDiagnostics(
			SourceHandCS,
			DesiredLeftHandCS,
			SolvedUpperArmCS,
			SolvedLowerArmCS,
			SolvedHandCS,
			PreferredPole,
			BaselineHandRelativeToForearm,
			bBaselineValid,
			Diagnostics);
		return Diagnostics;
	}
}

/**
 * 左手 IK 数值诊断快照：IK 前后位置/旋转差、Twist/Swing、肘部翻转，
 * 以及水平基准修正的 Socket 可吸收性判据（修正恒定 ⇔ 握把 Socket 可完全吸收）。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterLeftHandWristDiagnosticsTest,
	"ShootGame.Aim.LeftHandWristDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterLeftHandWristDiagnosticsTest::RunTest(const FString& Parameters)
{
	using namespace ShooterLeftHandWristDiagnosticsTests;

	bool bComputed = false;

	// 基础指标：位置差 / 求解残差 / 总旋转差 / 垂直前臂轴的纯 Swing；肘点共线时不可测量。
	{
		const FTransform SourceHandCS(FQuat::Identity, FVector(50.0f, 2.0f, 0.0f));
		const FTransform DesiredLeftHandCS(
			FRotator(0.0f, 25.0f, 0.0f).Quaternion(),
			FVector(50.0f, 0.0f, 5.0f));
		const FTransform SolvedUpperArmCS(FQuat::Identity, ShoulderLocation);
		const FTransform SolvedLowerArmCS(FQuat::Identity, FVector(25.0f, 0.0f, 0.0f));
		const FTransform SolvedHandCS(DesiredLeftHandCS.GetRotation(), WristLocation);

		const FShooterLeftHandWristDiagnostics Diagnostics = RunDiagnostics(
			SourceHandCS, DesiredLeftHandCS, SolvedUpperArmCS, SolvedLowerArmCS, SolvedHandCS,
			FQuat::Identity, false, bComputed);
		TestTrue(TEXT("valid frame computes diagnostics"), bComputed);
		TestTrue(
			TEXT("target position delta covers source-to-grip distance"),
			FMath::IsNearlyEqual(Diagnostics.TargetPositionDelta, FMath::Sqrt(29.0f), 1e-2f));
		TestTrue(
			TEXT("post solve residual reports unreachable part"),
			FMath::IsNearlyEqual(Diagnostics.PostSolvePositionResidual, 5.0f, 1e-2f));
		TestTrue(
			TEXT("total rotation delta matches yaw angle"),
			FMath::IsNearlyEqual(Diagnostics.TotalRotationDeltaDegrees, 25.0f, 1e-2f));
		TestTrue(
			TEXT("rotation perpendicular to forearm axis has zero twist"),
			FMath::IsNearlyEqual(Diagnostics.TwistDegrees, 0.0f, 1e-2f));
		TestTrue(
			TEXT("rotation perpendicular to forearm axis is pure swing"),
			FMath::IsNearlyEqual(Diagnostics.SwingDegrees, 25.0f, 1e-2f));
		TestFalse(TEXT("collinear elbow cannot be measured"), Diagnostics.bElbowMeasured);
		TestFalse(TEXT("missing baseline skips correction"), Diagnostics.bBaselineCorrectionValid);
	}

	// 肘部翻转：解算后肘点换到缓存 Pole 另一侧时 alignment 反号并触发翻转标记。
	{
		const FTransform SourceHandCS = FTransform::Identity;
		const FTransform DesiredLeftHandCS = FTransform::Identity;
		const FTransform SolvedUpperArmCS(FQuat::Identity, ShoulderLocation);
		const FTransform SolvedHandCS(FQuat::Identity, WristLocation);

		const FTransform SameSideElbow(FQuat::Identity, FVector(25.0f, 8.0f, 0.0f));
		const FShooterLeftHandWristDiagnostics SameSide = RunDiagnostics(
			SourceHandCS, DesiredLeftHandCS, SolvedUpperArmCS, SameSideElbow, SolvedHandCS,
			FQuat::Identity, false, bComputed);
		TestTrue(TEXT("same side elbow computes"), bComputed && SameSide.bElbowMeasured);
		TestTrue(TEXT("same side alignment is positive"), SameSide.ElbowPoleAlignment > 0.9f);
		TestFalse(TEXT("same side does not flip"), SameSide.bElbowHemisphereFlipped);

		const FTransform FlippedElbow(FQuat::Identity, FVector(25.0f, -8.0f, 0.0f));
		const FShooterLeftHandWristDiagnostics Flipped = RunDiagnostics(
			SourceHandCS, DesiredLeftHandCS, SolvedUpperArmCS, FlippedElbow, SolvedHandCS,
			FQuat::Identity, false, bComputed);
		TestTrue(TEXT("flipped elbow computes"), bComputed && Flipped.bElbowMeasured);
		TestTrue(TEXT("flipped alignment is negative"), Flipped.ElbowPoleAlignment < -0.9f);
		TestTrue(TEXT("flipped elbow is flagged"), Flipped.bElbowHemisphereFlipped);
	}

	// Socket 可完全吸收判据：末端目标 = K^-1 * Baseline * Forearm 时，
	// 修正 C = Baseline * Forearm * End^-1 = K 在所有前臂旋转下恒定；
	// 一个第三人称专用握把 Socket（旋转 = K）即可在所有俯仰下把修正归零。
	{
		const FQuat Baseline = FRotator(10.0f, 20.0f, 30.0f).Quaternion();
		const FQuat SocketRotation = FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(-18.0f));
		const FQuat ForearmRotations[] = {
			FQuat::Identity,
			FRotator(-40.0f, 0.0f, 0.0f).Quaternion(),
			FRotator(45.0f, 0.0f, 0.0f).Quaternion(),
			FRotator(-60.0f, 25.0f, 5.0f).Quaternion(),
		};

		for (const FQuat& ForearmRotation : ForearmRotations)
		{
			const FTransform SourceHandCS = FTransform::Identity;
			const FTransform DesiredLeftHandCS(
				SocketRotation.Inverse() * Baseline * ForearmRotation,
				WristLocation);
			const FTransform SolvedUpperArmCS(FQuat::Identity, ShoulderLocation);
			const FTransform SolvedLowerArmCS(ForearmRotation, FVector(25.0f, 0.0f, 0.0f));
			const FTransform SolvedHandCS(DesiredLeftHandCS.GetRotation(), WristLocation);

			const FShooterLeftHandWristDiagnostics Diagnostics = RunDiagnostics(
				SourceHandCS, DesiredLeftHandCS, SolvedUpperArmCS, SolvedLowerArmCS, SolvedHandCS,
				Baseline, true, bComputed);
			TestTrue(TEXT("constant offset case computes"), bComputed && Diagnostics.bBaselineCorrectionValid);
			TestTrue(
				TEXT("baseline correction equals the constant socket offset at every forearm rotation"),
				FMath::IsNearlyEqual(Diagnostics.BaselineCorrectionDegrees, 18.0f, 1e-2f));
			TestTrue(
				TEXT("constant offset keeps zero twist about the forearm axis"),
				FMath::IsNearlyEqual(Diagnostics.BaselineCorrectionTwistDegrees, 0.0f, 1e-2f));
			TestTrue(
				TEXT("constant offset keeps constant swing"),
				FMath::IsNearlyEqual(Diagnostics.BaselineCorrectionSwingDegrees, 18.0f, 1e-2f));
		}
	}

	// Socket 不可完全吸收判据：前臂按 AO/脊柱分布获得 2 倍俯仰、末端目标随武器刚性获得 1 倍俯仰时，
	// 修正随俯仰变化（水平时为 0），不存在任何常量 Socket 能在所有俯仰下归零；
	// 该残余只能由 LeftHand IK 的末端旋转策略承担。
	{
		const FQuat Baseline = FRotator(10.0f, 20.0f, 30.0f).Quaternion();
		const float PitchDegrees[] = { 0.0f, 30.0f, 60.0f };
		float PreviousCorrection = 0.0f;
		for (const float Pitch : PitchDegrees)
		{
			const FQuat ForearmRotation = FRotator(2.0f * Pitch, 0.0f, 0.0f).Quaternion();
			const FQuat EndRotation = FRotator(Pitch, 0.0f, 0.0f).Quaternion() * Baseline;
			const FTransform SourceHandCS = FTransform::Identity;
			const FTransform DesiredLeftHandCS(EndRotation, WristLocation);
			const FTransform SolvedUpperArmCS(FQuat::Identity, ShoulderLocation);
			const FTransform SolvedLowerArmCS(ForearmRotation, FVector(25.0f, 0.0f, 0.0f));
			const FTransform SolvedHandCS(EndRotation, WristLocation);

			const FShooterLeftHandWristDiagnostics Diagnostics = RunDiagnostics(
				SourceHandCS, DesiredLeftHandCS, SolvedUpperArmCS, SolvedLowerArmCS, SolvedHandCS,
				Baseline, true, bComputed);
			TestTrue(TEXT("pitch dependent case computes"), bComputed && Diagnostics.bBaselineCorrectionValid);

			if (FMath::IsNearlyEqual(Pitch, 0.0f))
			{
				TestTrue(
					TEXT("horizontal calibration starts at zero correction"),
					Diagnostics.BaselineCorrectionDegrees < 0.5f);
			}
			else
			{
				TestTrue(
					TEXT("non-horizontal pitch leaves non-zero correction"),
					Diagnostics.BaselineCorrectionDegrees > 5.0f);
				if (PreviousCorrection > 0.0f)
				{
					TestTrue(
						TEXT("correction differs across pitches"),
						!FMath::IsNearlyEqual(
							Diagnostics.BaselineCorrectionDegrees,
							PreviousCorrection,
							1e-2f));
				}
			}
			PreviousCorrection = Diagnostics.BaselineCorrectionDegrees;
		}
	}

	// 基准姿态捕获：hand 相对前臂的旋转可以装回前臂还原 hand 旋转。
	{
		const FTransform HandCS(
			FRotator(5.0f, 10.0f, -3.0f).Quaternion(),
			WristLocation);
		const FTransform LowerArmCS(
			FRotator(20.0f, -15.0f, 8.0f).Quaternion(),
			FVector(25.0f, 0.0f, 0.0f));
		FQuat Baseline = FQuat::Identity;
		TestTrue(
			TEXT("wrist attitude relative to forearm computes"),
			FShooterLeftHandIKMath::ComputeHandRelativeToForearm(HandCS, LowerArmCS, Baseline));
		TestTrue(
			TEXT("baseline reassembles the source hand rotation"),
			(Baseline * LowerArmCS.GetRotation()).Equals(HandCS.GetRotation(), 1e-4f));

		TestFalse(
			TEXT("nan hand fails soft"),
			FShooterLeftHandIKMath::ComputeHandRelativeToForearm(
				FTransform(FQuat(NAN, 0.0f, 0.0f, 1.0f), WristLocation),
				LowerArmCS,
				Baseline));
	}

	// 非法输入 fail-soft：整帧诊断拒绝输出。
	{
		const FTransform SourceHandCS = FTransform::Identity;
		const FTransform DesiredLeftHandCS = FTransform::Identity;
		const FTransform SolvedUpperArmCS = FTransform::Identity;
		const FTransform SolvedLowerArmCS = FTransform::Identity;
		const FTransform InvalidSolvedHandCS(
			FQuat(NAN, 0.0f, 0.0f, 1.0f),
			WristLocation,
			FVector::OneVector);
		const FShooterLeftHandWristDiagnostics Diagnostics = RunDiagnostics(
			SourceHandCS, DesiredLeftHandCS, SolvedUpperArmCS, SolvedLowerArmCS, InvalidSolvedHandCS,
			FQuat::Identity, false, bComputed);
		TestFalse(TEXT("invalid solved frame fails soft"), bComputed);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
