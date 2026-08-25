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

	bool HasCompleteLeftHandIKState(
		bool bHasCharacter,
		bool bHasThirdPersonMesh,
		bool bHasCurrentWeapon,
		bool bWeaponThirdPersonMeshAttached,
		bool bHasRightHandBone,
		bool bHasLeftHandBone,
		bool bHasHandGripSocket,
		bool bHasThirdPersonLeftHandGripSocket,
		const FTransform& GripInRightHandSpace,
		const FTransform& HandGripInLeftHandSpace)
	{
		return UShooterThirdPersonAnimInstance::IsLeftHandIKEnabledForState(
			bHasCharacter,
			bHasThirdPersonMesh,
			bHasCurrentWeapon,
			bWeaponThirdPersonMeshAttached,
			bHasRightHandBone,
			bHasLeftHandBone,
			bHasHandGripSocket,
			bHasThirdPersonLeftHandGripSocket,
			GripInRightHandSpace,
			HandGripInLeftHandSpace);
	}
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

	// Identity 是合法 Transform，但在这里代表缺失数据，不得继续参与 IK。
	TestTrue(
		TEXT("identity right hand world yields identity grip transform"),
		UShooterThirdPersonAnimInstance::ComputeLeftHandGripInRightHandSpace(
			FTransform::Identity,
			FTransform(FVector(100.0f, 200.0f, 300.0f))).Equals(FTransform::Identity));

	TestTrue(
		TEXT("identity grip world yields identity grip transform"),
		UShooterThirdPersonAnimInstance::ComputeLeftHandGripInRightHandSpace(
			FTransform(FVector(100.0f, 200.0f, 300.0f)),
			FTransform::Identity).Equals(FTransform::Identity));

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
			FTransform::Identity,
			DesiredLeftHandCS));

	return true;
}

/**
 * 左手 IK 状态矩阵测试：无武器、无第三人称 Mesh、无握把 Socket、未附着或缓存无效时关闭。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterLeftHandIKEnabledStateTest,
	"ShootGame.Aim.LeftHandIKEnabledState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterLeftHandIKEnabledStateTest::RunTest(const FString& Parameters)
{
	using namespace ShooterLeftHandIKAutomationTests;

	// 前提：Identity 本身是 Valid Transform，但不能证明握把数据已建立。
	TestTrue(TEXT("FTransform::Identity is a valid transform"), FTransform::Identity.IsValid());

	TestFalse(
		TEXT("no character disables left hand IK"),
		HasCompleteLeftHandIKState(
			false, true, true, true, true, true, true, true,
			NonIdentityLeftHandGripInRightHandSpace, NonIdentityHandGripInLeftHandSpace));
	TestFalse(
		TEXT("no third-person mesh disables left hand IK"),
		HasCompleteLeftHandIKState(
			true, false, true, true, true, true, true, true,
			NonIdentityLeftHandGripInRightHandSpace, NonIdentityHandGripInLeftHandSpace));
	TestFalse(
		TEXT("no current weapon disables left hand IK"),
		HasCompleteLeftHandIKState(
			true, true, false, false, true, true, true, false,
			NonIdentityLeftHandGripInRightHandSpace, NonIdentityHandGripInLeftHandSpace));
	TestFalse(
		TEXT("weapon third-person mesh not attached disables left hand IK"),
		HasCompleteLeftHandIKState(
			true, true, true, false, true, true, true, true,
			NonIdentityLeftHandGripInRightHandSpace, NonIdentityHandGripInLeftHandSpace));
	TestFalse(
		TEXT("missing right hand bone disables left hand IK"),
		HasCompleteLeftHandIKState(
			true, true, true, true, false, true, true, true,
			NonIdentityLeftHandGripInRightHandSpace, NonIdentityHandGripInLeftHandSpace));
	TestFalse(
		TEXT("missing left hand bone disables left hand IK"),
		HasCompleteLeftHandIKState(
			true, true, true, true, true, false, true, true,
			NonIdentityLeftHandGripInRightHandSpace, NonIdentityHandGripInLeftHandSpace));
	TestFalse(
		TEXT("missing character HandGrip_L disables left hand IK"),
		HasCompleteLeftHandIKState(
			true, true, true, true, true, true, false, true,
			NonIdentityLeftHandGripInRightHandSpace, NonIdentityHandGripInLeftHandSpace));
	TestFalse(
		TEXT("missing weapon grip socket disables left hand IK"),
		HasCompleteLeftHandIKState(
			true, true, true, true, true, true, true, false,
			NonIdentityLeftHandGripInRightHandSpace, NonIdentityHandGripInLeftHandSpace));
	TestFalse(
		TEXT("identity weapon grip transform disables left hand IK despite being valid"),
		HasCompleteLeftHandIKState(
			true, true, true, true, true, true, true, true,
			FTransform::Identity, NonIdentityHandGripInLeftHandSpace));
	TestFalse(
		TEXT("identity character palm transform disables left hand IK"),
		HasCompleteLeftHandIKState(
			true, true, true, true, true, true, true, true,
			NonIdentityLeftHandGripInRightHandSpace, FTransform::Identity));

	const FTransform InvalidGripTransform(
		FQuat(NAN, 0.0f, 0.0f, 1.0f),
		FVector::ZeroVector,
		FVector::OneVector);
	TestFalse(
		TEXT("nan grip transform disables left hand IK"),
		HasCompleteLeftHandIKState(
			true, true, true, true, true, true, true, true,
			InvalidGripTransform, NonIdentityHandGripInLeftHandSpace));

	// 正向用例：所有真实数据齐备时开启；与 AimIK 是否开启无关。
	TestTrue(
		TEXT("complete valid state enables left hand IK"),
		HasCompleteLeftHandIKState(
			true, true, true, true, true, true, true, true,
			NonIdentityLeftHandGripInRightHandSpace, NonIdentityHandGripInLeftHandSpace));

	return true;
}

/**
 * 左手握把缓存重建判定测试：只在缓存未建立、武器变化、附着变化或缓存无效时重建；
 * 已确认缺失 Socket 的稳定 Identity 状态不得逐帧追逐世界点。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterLeftHandGripCacheRefreshTest,
	"ShootGame.Aim.LeftHandGripCacheRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterLeftHandGripCacheRefreshTest::RunTest(const FString& Parameters)
{
	TestTrue(
		TEXT("dirty cache triggers refresh"),
		UShooterThirdPersonAnimInstance::ShouldRefreshLeftHandGripCache(
			true, false, false, false));
	TestTrue(
		TEXT("weapon switch triggers refresh"),
		UShooterThirdPersonAnimInstance::ShouldRefreshLeftHandGripCache(
			false, true, false, false));
	TestTrue(
		TEXT("same weapon reattach triggers refresh"),
		UShooterThirdPersonAnimInstance::ShouldRefreshLeftHandGripCache(
			false, false, true, false));
	TestTrue(
		TEXT("invalid cached grip transform triggers refresh"),
		UShooterThirdPersonAnimInstance::ShouldRefreshLeftHandGripCache(
			false, false, false, true));
	TestFalse(
		TEXT("steady valid cache does not refresh every frame"),
		UShooterThirdPersonAnimInstance::ShouldRefreshLeftHandGripCache(
			false, false, false, false));
	TestFalse(
		TEXT("missing socket stable identity cache does not chase world point every frame"),
		UShooterThirdPersonAnimInstance::ShouldRefreshLeftHandGripCache(
			false, false, false, false));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
