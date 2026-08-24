// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Characters/ShooterCharacter.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "ShooterAimPresentationTestHarness.h"
#include "Weapons/ShooterAimMath.h"

namespace ShooterAimPresentationAutomationTests
{
	const FTransform NonIdentityHandToMuzzle(
		FQuat(FRotator(0.0f, 0.0f, 15.0f)),
		FVector(12.0f, 4.0f, 3.0f),
		FVector::OneVector);
}

/**
 * C2.5 纯数据测试：PresentationAimTarget 本地有效状态。
 * 有效目标不应再依赖“距上次收到网络包的时间”；有限非零即有效，零向量 / NaN / Inf 无效。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAimPresentationTargetValidityTest,
	"ShootGame.Aim.PresentationTargetValidity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAimPresentationTargetValidityTest::RunTest(const FString& Parameters)
{
	TestTrue(
		TEXT("finite non-zero target is valid"),
		AShooterCharacter::IsValidPresentationAimTargetValue(FVector(100.0f, -200.0f, 300.0f)));

	TestFalse(
		TEXT("zero vector is not a valid steady-state target"),
		AShooterCharacter::IsValidPresentationAimTargetValue(FVector::ZeroVector));

	TestFalse(
		TEXT("nearly-zero vector is not valid"),
		AShooterCharacter::IsValidPresentationAimTargetValue(FVector(KINDA_SMALL_NUMBER, 0.0f, 0.0f)));

	TestFalse(
		TEXT("NaN target is not valid"),
		AShooterCharacter::IsValidPresentationAimTargetValue(FVector(NAN, 0.0f, 0.0f)));

	TestFalse(
		TEXT("infinite target is not valid"),
		AShooterCharacter::IsValidPresentationAimTargetValue(
			FVector(INFINITY, 100.0f, 100.0f)));

	// C2.5 核心回归：稳态有效性与“多久没收到新包”无关。
	// 该函数没有时间参数；UpdatePresentationAimSmoothing 也不再包含超时回退分支。
	// 这里锁定同一目标的重复判定结果，防止未来重新引入时间/无变化失效。
	const FVector SteadyTarget(10000.0f, 0.0f, 2000.0f);
	TestTrue(
		TEXT("steady target remains valid regardless of elapsed network silence"),
		AShooterCharacter::IsValidPresentationAimTargetValue(SteadyTarget));

	return true;
}

/**
 * C2.5 消费角色矩阵：SimulatedProxy 与 Listen Server 远端观察都使用平滑目标；
 * 本地拥有者和 Dedicated Server 不进入平滑/远端表现消费路径。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAimPresentationSmoothingRoleTest,
	"ShootGame.Aim.PresentationSmoothingRole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAimPresentationSmoothingRoleTest::RunTest(const FString& Parameters)
{
	// 普通客户端观察其他玩家的 SimulatedProxy：运行平滑。
	TestTrue(
		TEXT("simulated proxy runs smoothing"),
		AShooterCharacter::ShouldRunPresentationAimSmoothing(
			ROLE_SimulatedProxy, NM_Client, false));

	// Listen Server 观察远端客户端 Pawn：Authority 且非 LocallyControlled，运行平滑。
	TestTrue(
		TEXT("listen server observing remote authority pawn runs smoothing"),
		AShooterCharacter::ShouldRunPresentationAimSmoothing(
			ROLE_Authority, NM_ListenServer, false));

	// Listen Server 主机自己的 Pawn：Authority 但 LocallyControlled，不得被表现缓存覆盖。
	TestFalse(
		TEXT("listen server local owner does not run smoothing"),
		AShooterCharacter::ShouldRunPresentationAimSmoothing(
			ROLE_Authority, NM_ListenServer, true));

	// 远端客户端自己的 Pawn：AutonomousProxy，使用本地即时视角。
	TestFalse(
		TEXT("autonomous proxy does not run smoothing"),
		AShooterCharacter::ShouldRunPresentationAimSmoothing(
			ROLE_AutonomousProxy, NM_Client, true));

	// Dedicated Server：不做不可见动画的高成本表现工作。
	TestFalse(
		TEXT("dedicated server does not run smoothing"),
		AShooterCharacter::ShouldRunPresentationAimSmoothing(
			ROLE_Authority, NM_DedicatedServer, false));

	// Standalone 本地 Pawn：Authority 且 LocallyControlled，同样不走平滑路径。
	TestFalse(
		TEXT("standalone local owner does not run smoothing"),
		AShooterCharacter::ShouldRunPresentationAimSmoothing(
			ROLE_Authority, NM_Standalone, true));

	return true;
}

/**
 * C2.5 IK 开关：FTransform::Identity 虽然 IsValid()==true，但没有 CurrentWeapon /
 * Muzzle socket / 非 Identity HandToMuzzle / 有限非零 AimDirection 时不得开启 IK。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAimIKEnabledStateTest,
	"ShootGame.Aim.IKEnabledState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAimIKEnabledStateTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAimPresentationAutomationTests;

	const FVector ValidAimDirection = FVector(0.5f, 0.5f, 0.70710678f).GetSafeNormal();

	// 前提：Identity 确实是 Valid Transform，但不得因此开启 IK。
	TestTrue(TEXT("FTransform::Identity is a valid transform"), FTransform::Identity.IsValid());

	TestFalse(
		TEXT("no character disables IK"),
		UShooterThirdPersonAnimInstance::IsAimIKEnabledForState(
			false, true, true, true, NonIdentityHandToMuzzle, ValidAimDirection));

	TestFalse(
		TEXT("no third-person mesh disables IK"),
		UShooterThirdPersonAnimInstance::IsAimIKEnabledForState(
			true, false, true, true, NonIdentityHandToMuzzle, ValidAimDirection));

	TestFalse(
		TEXT("no current weapon disables IK even when HandToMuzzle is identity-valid"),
		UShooterThirdPersonAnimInstance::IsAimIKEnabledForState(
			true, true, false, false, FTransform::Identity, ValidAimDirection));

	TestFalse(
		TEXT("missing third-person muzzle socket disables IK"),
		UShooterThirdPersonAnimInstance::IsAimIKEnabledForState(
			true, true, true, false, NonIdentityHandToMuzzle, ValidAimDirection));

	TestFalse(
		TEXT("identity HandToMuzzle disables IK despite being valid"),
		UShooterThirdPersonAnimInstance::IsAimIKEnabledForState(
			true, true, true, true, FTransform::Identity, ValidAimDirection));

	TestFalse(
		TEXT("zero aim direction disables IK"),
		UShooterThirdPersonAnimInstance::IsAimIKEnabledForState(
			true, true, true, true, NonIdentityHandToMuzzle, FVector::ZeroVector));

	TestFalse(
		TEXT("NaN aim direction disables IK"),
		UShooterThirdPersonAnimInstance::IsAimIKEnabledForState(
			true, true, true, true, NonIdentityHandToMuzzle, FVector(NAN, 0.0f, 0.0f)));

	// C2.5 正向用例：Character / 第三人称 Mesh / CurrentWeapon / 第三人称 Muzzle /
	// 非 Identity 且有效的 HandToMuzzle / 有限非零 AimDirection 全部成立时必须开启。
	TestTrue(
		TEXT("complete valid state enables IK"),
		UShooterThirdPersonAnimInstance::IsAimIKEnabledForState(
			true, true, true, true, NonIdentityHandToMuzzle, ValidAimDirection));

	// C4：HandToMuzzle 缺失（Identity 输入）必须返回 Identity，不得得到“原点相对关系”后误开 IK。
	TestTrue(
		TEXT("identity hand world yields identity HandToMuzzle"),
		UShooterThirdPersonAnimInstance::ComputeHandToMuzzleTransform(
			FTransform::Identity,
			FTransform(FVector(100.0, 200.0, 300.0))).Equals(FTransform::Identity));

	const FTransform ValidHandWorld(FRotator(0.0f, 0.0f, 10.0f), FVector(100.0f, 50.0f, 80.0f));
	const FTransform ValidMuzzleWorld(FRotator(0.0f, 5.0f, -5.0f), FVector(140.0f, 62.0f, 88.0f));
	TestTrue(
		TEXT("valid world transforms still compute rigid HandToMuzzle"),
		UShooterThirdPersonAnimInstance::ComputeHandToMuzzleTransform(
			ValidHandWorld, ValidMuzzleWorld).Equals(
				ValidMuzzleWorld.GetRelativeTransform(ValidHandWorld), 1e-4f));

	return true;
}

/**
 * C4 纯计算测试：AimDirectionWorld 的 Muzzle→稳定目标方向与角色矩阵。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAimDirectionSourceTest,
	"ShootGame.Aim.AimDirectionSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAimDirectionSourceTest::RunTest(const FString& Parameters)
{
	const FVector MuzzleLocation(100.0f, 200.0f, 150.0f);
	const FVector StableTarget(400.0f, -100.0f, 250.0f);
	const FVector ExpectedMuzzleToTarget = (StableTarget - MuzzleLocation).GetSafeNormal();

	// Muzzle 到稳定目标的方向计算：必须从 Muzzle 出发，而不是从 Actor/View 出发。
	const FVector Direction = UShooterThirdPersonAnimInstance::ComputeMuzzleToTargetDirection(
		MuzzleLocation,
		StableTarget);
	TestTrue(
		TEXT("muzzle to target direction is normalized"),
		FMath::IsNearlyEqual(Direction.Size(), 1.0f, KINDA_SMALL_NUMBER));
	TestTrue(
		TEXT("muzzle to target direction matches expected"),
		Direction.Equals(ExpectedMuzzleToTarget, 1e-3f));

	// 目标与 Muzzle 重合 / NaN 输入时返回零向量，关闭 IK。
	TestTrue(
		TEXT("coincident target and muzzle returns zero"),
		UShooterThirdPersonAnimInstance::ComputeMuzzleToTargetDirection(
			MuzzleLocation, MuzzleLocation).IsNearlyZero());
	TestTrue(
		TEXT("NaN target returns zero"),
		UShooterThirdPersonAnimInstance::ComputeMuzzleToTargetDirection(
			MuzzleLocation, FVector(NAN, 0.0f, 0.0f)).IsNearlyZero());

	const FVector LocalAimDirection = FRotator(20.0f, 30.0f, 0.0f).Vector();
	const bool bHasMuzzle = true;

	// SimulatedProxy 观察端：应运行平滑且目标有效，消费 Muzzle → 稳定目标。
	const bool bSimulatedProxyRunsSmoothing = AShooterCharacter::ShouldRunPresentationAimSmoothing(
		ROLE_SimulatedProxy, NM_Client, false);
	TestTrue(TEXT("simulated proxy runs presentation smoothing"), bSimulatedProxyRunsSmoothing);
	TestTrue(
		TEXT("simulated proxy uses muzzle to stable target"),
		UShooterThirdPersonAnimInstance::ComputeAimDirectionWorldForState(
			false,
			bSimulatedProxyRunsSmoothing,
			true,
			LocalAimDirection,
			MuzzleLocation,
			StableTarget,
			bHasMuzzle).Equals(ExpectedMuzzleToTarget, 1e-3f));

	// Listen Server 观察远端 Pawn（Authority 且非 LocallyControlled）：使用同一目标语义。
	const bool bListenRemoteRunsSmoothing = AShooterCharacter::ShouldRunPresentationAimSmoothing(
		ROLE_Authority, NM_ListenServer, false);
	TestTrue(TEXT("listen server remote pawn runs presentation smoothing"), bListenRemoteRunsSmoothing);
	TestTrue(
		TEXT("listen server remote pawn uses muzzle to stable target"),
		UShooterThirdPersonAnimInstance::ComputeAimDirectionWorldForState(
			false,
			bListenRemoteRunsSmoothing,
			true,
			LocalAimDirection,
			MuzzleLocation,
			StableTarget,
			bHasMuzzle).Equals(ExpectedMuzzleToTarget, 1e-3f));

	// 本地拥有者：即使传入有效远端目标，也永远使用本地即时 AimDirection。
	TestTrue(
		TEXT("local owner keeps immediate base aim direction"),
		UShooterThirdPersonAnimInstance::ComputeAimDirectionWorldForState(
			true,
			bSimulatedProxyRunsSmoothing,
			true,
			LocalAimDirection,
			MuzzleLocation,
			StableTarget,
			bHasMuzzle).Equals(LocalAimDirection, 1e-3f));

	// Dedicated Server 与无效目标 / 缺失 Muzzle：零向量关闭 IK。
	TestTrue(
		TEXT("dedicated server returns zero aim direction"),
		UShooterThirdPersonAnimInstance::ComputeAimDirectionWorldForState(
			false,
			false,
			true,
			LocalAimDirection,
			MuzzleLocation,
			StableTarget,
			bHasMuzzle).IsNearlyZero());
	TestTrue(
		TEXT("invalid presentation target returns zero aim direction"),
		UShooterThirdPersonAnimInstance::ComputeAimDirectionWorldForState(
			false,
			bSimulatedProxyRunsSmoothing,
			false,
			LocalAimDirection,
			MuzzleLocation,
			StableTarget,
			bHasMuzzle).IsNearlyZero());
	TestTrue(
		TEXT("missing third-person muzzle returns zero aim direction"),
		UShooterThirdPersonAnimInstance::ComputeAimDirectionWorldForState(
			false,
			bSimulatedProxyRunsSmoothing,
			true,
			LocalAimDirection,
			MuzzleLocation,
			StableTarget,
			false).IsNearlyZero());

	// 权威开火不读取表现缓存：生产实现 AShooterCharacter::GetWeaponTargetLocation
	// 只现场调用 ComputePreSpreadAimTarget（见 ShooterCharacter.cpp），
	// 真实多客户端弹丸/伤害验证由 ShooterNetworkTestCoordinator 的服务器开火阶段执行。
	// 本文件保持纯计算，不伪造世界 Trace。

	return true;
}

/**
 * C2.5 行为状态测试：用无 World 测试壳直接驱动 UpdatePresentationAimSmoothing，
 * 覆盖稳态无回退、死亡/复活、切枪重置、传送重置、SimulatedProxy 平滑消费与本地拥有者不被覆盖。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAimPresentationStateBehaviorTest,
	"ShootGame.Aim.PresentationStateBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAimPresentationStateBehaviorTest::RunTest(const FString& Parameters)
{
	AShooterAimPresentationTestHarness* Harness = NewObject<AShooterAimPresentationTestHarness>();
	if (!TestNotNull(TEXT("test harness created"), Harness))
	{
		return false;
	}

	const FVector ViewLocation = Harness->ViewLocationOverride;
	const FVector TargetA(10000.0, 2000.0, 1500.0);
	const FVector TargetB(4000.0, 9000.0, 1200.0);

	// SimulatedProxy 观察端路径。
	Harness->SetRole(ROLE_SimulatedProxy);

	// 稳态目标：第一次 Tick 直接采用，之后无论再跑多少 Tick（模拟超过旧 0.5s 回退窗口）
	// 都保持有效，且平滑目标不跳回 ActorForward。
	Harness->SetPresentationAimTargetForTest(TargetA);
	Harness->CallUpdatePresentationAimSmoothing(0.1f);
	TestTrue(TEXT("first valid target establishes local validity"), Harness->IsPresentationAimTargetValidForTest());
	TestTrue(
		TEXT("first valid target is adopted immediately"),
		Harness->GetSmoothedPresentationAimTargetForTest().Equals(TargetA, 0.1f));

	for (int32 Step = 0; Step < 30; ++Step)
	{
		Harness->CallUpdatePresentationAimSmoothing(0.1f);
	}
	TestTrue(TEXT("steady target stays valid after 3 simulated seconds"), Harness->IsPresentationAimTargetValidForTest());
	TestTrue(
		TEXT("steady target never falls back to actor forward"),
		Harness->GetSmoothedPresentationAimTargetForTest().Equals(TargetA, 0.1f));

	// 死亡：显式生命周期重置，旧目标立即失效并清零。
	Harness->SetDeadForTest(true);
	Harness->CallUpdatePresentationAimSmoothing(0.1f);
	TestFalse(TEXT("death invalidates presentation target"), Harness->IsPresentationAimTargetValidForTest());
	TestTrue(TEXT("death clears smoothed target"), Harness->GetSmoothedPresentationAimTargetForTest().IsNearlyZero());

	// 复活 / Pawn 重建：下一次有效目标重新建立状态，不从死亡前旧值插值。
	Harness->SetDeadForTest(false);
	Harness->CallUpdatePresentationAimSmoothing(0.1f);
	TestTrue(TEXT("respawn re-establishes validity"), Harness->IsPresentationAimTargetValidForTest());
	TestTrue(
		TEXT("respawn adopts latest target instead of interpolating dead value"),
		Harness->GetSmoothedPresentationAimTargetForTest().Equals(TargetA, 0.1f));

	// 切枪：消费路径显式调用 ResetPresentationAimSmoothing，直接采用最新目标。
	Harness->SetSmoothedPresentationAimTargetForTest(FVector(500.0, 500.0, 500.0));
	Harness->SetPresentationAimTargetForTest(TargetB);
	Harness->CallResetPresentationAimSmoothing();
	TestTrue(TEXT("weapon switch keeps latest target valid"), Harness->IsPresentationAimTargetValidForTest());
	TestTrue(
		TEXT("weapon switch resets smoothing instead of interpolating old value"),
		Harness->GetSmoothedPresentationAimTargetForTest().Equals(TargetB, 0.1f));

	// 传送：视点发生 1000cm 跳变时重置，而不是对旧世界点做指数插值。
	Harness->SetPresentationAimTargetValidForTest(true);
	Harness->SetSmoothedPresentationAimTargetForTest(TargetA);
	Harness->SetLastPresentationAimViewLocationForTest(ViewLocation + FVector(1000.0, 0.0, 0.0));
	Harness->CallUpdatePresentationAimSmoothing(0.016f);
	TestTrue(TEXT("teleport keeps target valid"), Harness->IsPresentationAimTargetValidForTest());
	TestTrue(
		TEXT("teleport snaps to latest target instead of interpolating"),
		Harness->GetSmoothedPresentationAimTargetForTest().Equals(TargetB, 0.1f));

	// SimulatedProxy 消费：GetAimPresentationAngles 使用平滑目标，而不是 GetBaseAimRotation。
	float SimulatedYaw = 0.0f;
	float SimulatedPitch = 0.0f;
	Harness->GetAimPresentationAngles(SimulatedYaw, SimulatedPitch);
	float ExpectedSimulatedYaw = 0.0f;
	float ExpectedSimulatedPitch = 0.0f;
	const FVector SmoothedDirection =
		(Harness->GetSmoothedPresentationAimTargetForTest() - ViewLocation).GetSafeNormal();
	FShooterAimMath::WorldDirectionToLocalAngles(
		SmoothedDirection,
		Harness->GetMesh()->GetComponentTransform(),
		ExpectedSimulatedYaw,
		ExpectedSimulatedPitch);
	TestTrue(
		TEXT("simulated proxy consumes smoothed presentation target"),
		FMath::IsNearlyEqual(SimulatedYaw, ExpectedSimulatedYaw, 0.01f) &&
		FMath::IsNearlyEqual(SimulatedPitch, ExpectedSimulatedPitch, 0.01f));

	// 本地拥有者（Standalone Authority）：即使内存中存在远端/表现目标，角度仍来自本地基础瞄准旋转。
	Harness->SetRole(ROLE_Authority);
	Harness->SetPresentationAimTargetValidForTest(true);
	Harness->SetPresentationAimTargetForTest(TargetB);
	Harness->SetSmoothedPresentationAimTargetForTest(TargetB);
	float OwnerYaw = 0.0f;
	float OwnerPitch = 0.0f;
	Harness->GetAimPresentationAngles(OwnerYaw, OwnerPitch);
	float ExpectedOwnerYaw = 0.0f;
	float ExpectedOwnerPitch = 0.0f;
	FShooterAimMath::WorldDirectionToLocalAngles(
		Harness->AimRotationOverride.Vector(),
		Harness->GetMesh()->GetComponentTransform(),
		ExpectedOwnerYaw,
		ExpectedOwnerPitch);
	TestTrue(
		TEXT("local owner is not overwritten by remote presentation target"),
		FMath::IsNearlyEqual(OwnerYaw, ExpectedOwnerYaw, 0.01f) &&
		FMath::IsNearlyEqual(OwnerPitch, ExpectedOwnerPitch, 0.01f));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
