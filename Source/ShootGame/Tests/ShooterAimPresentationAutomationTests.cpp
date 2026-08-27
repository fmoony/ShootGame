// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Characters/Aim/ShooterAimPresentationComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "ShooterAimPresentationTestHarness.h"
#include "Weapons/ShooterAimMath.h"

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
		UShooterAimPresentationComponent::IsValidPresentationAimTargetValue(FVector(100.0f, -200.0f, 300.0f)));

	TestFalse(
		TEXT("zero vector is not a valid steady-state target"),
		UShooterAimPresentationComponent::IsValidPresentationAimTargetValue(FVector::ZeroVector));

	TestFalse(
		TEXT("nearly-zero vector is not valid"),
		UShooterAimPresentationComponent::IsValidPresentationAimTargetValue(FVector(KINDA_SMALL_NUMBER, 0.0f, 0.0f)));

	TestFalse(
		TEXT("NaN target is not valid"),
		UShooterAimPresentationComponent::IsValidPresentationAimTargetValue(FVector(NAN, 0.0f, 0.0f)));

	TestFalse(
		TEXT("infinite target is not valid"),
		UShooterAimPresentationComponent::IsValidPresentationAimTargetValue(
			FVector(INFINITY, 100.0f, 100.0f)));

	// C2.5 核心回归：稳态有效性与“多久没收到新包”无关。
	// 该函数没有时间参数；UpdatePresentationAimSmoothing 也不再包含超时回退分支。
	// 这里锁定同一目标的重复判定结果，防止未来重新引入时间/无变化失效。
	const FVector SteadyTarget(10000.0f, 0.0f, 2000.0f);
	TestTrue(
		TEXT("steady target remains valid regardless of elapsed network silence"),
		UShooterAimPresentationComponent::IsValidPresentationAimTargetValue(SteadyTarget));

	return true;
}

/**
 * 阶段 3 纯策略测试：20Hz 本地提交的变化门槛、保活、服务端距离边界与 16 位包序号。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAimPresentationSubmitPolicyTest,
	"ShootGame.Aim.PresentationSubmitPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAimPresentationSubmitPolicyTest::RunTest(const FString& Parameters)
{
	const FVector ViewLocation(100.0f, 200.0f, 300.0f);
	const FVector PreviousTarget = ViewLocation + FVector::ForwardVector * 100.0f;

	TestTrue(
		TEXT("first valid target submits immediately"),
		UShooterAimPresentationComponent::ShouldSubmitPresentationAimTarget(
			PreviousTarget,
			FVector::ZeroVector,
			ViewLocation,
			-1.0f,
			20.0f,
			1.0f,
			0.2f));

	TestFalse(
		TEXT("sub-threshold steady target does not submit before keepalive"),
		UShooterAimPresentationComponent::ShouldSubmitPresentationAimTarget(
			PreviousTarget + FVector(1.0f, 0.0f, 0.0f),
			PreviousTarget,
			ViewLocation,
			0.05f,
			20.0f,
			1.0f,
			0.2f));

	const FVector RotatedTarget =
		ViewLocation + FRotator(0.0f, 2.0f, 0.0f).Vector() * 100.0f;
	TestTrue(
		TEXT("angle threshold submits fast view change"),
		UShooterAimPresentationComponent::ShouldSubmitPresentationAimTarget(
			RotatedTarget,
			PreviousTarget,
			ViewLocation,
			0.05f,
			20.0f,
			1.0f,
			0.2f));

	TestTrue(
		TEXT("keepalive resubmits unchanged target"),
		UShooterAimPresentationComponent::ShouldSubmitPresentationAimTarget(
			PreviousTarget,
			PreviousTarget,
			ViewLocation,
			0.2f,
			20.0f,
			1.0f,
			0.2f));

	TestFalse(
		TEXT("invalid target never submits"),
		UShooterAimPresentationComponent::ShouldSubmitPresentationAimTarget(
			FVector(NAN, 0.0f, 0.0f),
			PreviousTarget,
			ViewLocation,
			1.0f,
			20.0f,
			1.0f,
			0.2f));

	TestTrue(
		TEXT("server accepts target inside max distance plus tolerance"),
		UShooterAimPresentationComponent::IsClientPresentationAimTargetWithinBounds(
			ViewLocation + FVector::ForwardVector * 10499.0f,
			ViewLocation,
			10000.0f,
			500.0f));
	TestFalse(
		TEXT("server rejects target outside max distance plus tolerance"),
		UShooterAimPresentationComponent::IsClientPresentationAimTargetWithinBounds(
			ViewLocation + FVector::ForwardVector * 10501.0f,
			ViewLocation,
			10000.0f,
			500.0f));

	TestTrue(
		TEXT("newer sequence is accepted"),
		UShooterAimPresentationComponent::IsNewerPresentationAimSequence(2, 1));
	TestFalse(
		TEXT("duplicate sequence is rejected"),
		UShooterAimPresentationComponent::IsNewerPresentationAimSequence(7, 7));
	TestFalse(
		TEXT("older sequence is rejected"),
		UShooterAimPresentationComponent::IsNewerPresentationAimSequence(6, 7));
	TestTrue(
		TEXT("sequence wraparound is accepted"),
		UShooterAimPresentationComponent::IsNewerPresentationAimSequence(0, MAX_uint16));

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
		UShooterAimPresentationComponent::ShouldRunPresentationAimSmoothing(
			ROLE_SimulatedProxy, NM_Client, false));

	// Listen Server 观察远端客户端 Pawn：Authority 且非 LocallyControlled，运行平滑。
	TestTrue(
		TEXT("listen server observing remote authority pawn runs smoothing"),
		UShooterAimPresentationComponent::ShouldRunPresentationAimSmoothing(
			ROLE_Authority, NM_ListenServer, false));

	// Listen Server 主机自己的 Pawn：Authority 但 LocallyControlled，不得被表现缓存覆盖。
	TestFalse(
		TEXT("listen server local owner does not run smoothing"),
		UShooterAimPresentationComponent::ShouldRunPresentationAimSmoothing(
			ROLE_Authority, NM_ListenServer, true));

	// 远端客户端自己的 Pawn：AutonomousProxy，使用本地即时视角。
	TestFalse(
		TEXT("autonomous proxy does not run smoothing"),
		UShooterAimPresentationComponent::ShouldRunPresentationAimSmoothing(
			ROLE_AutonomousProxy, NM_Client, true));

	// Dedicated Server：不做不可见动画的高成本表现工作。
	TestFalse(
		TEXT("dedicated server does not run smoothing"),
		UShooterAimPresentationComponent::ShouldRunPresentationAimSmoothing(
			ROLE_Authority, NM_DedicatedServer, false));

	// Standalone 本地 Pawn：Authority 且 LocallyControlled，同样不走平滑路径。
	TestFalse(
		TEXT("standalone local owner does not run smoothing"),
		UShooterAimPresentationComponent::ShouldRunPresentationAimSmoothing(
			ROLE_Authority, NM_Standalone, true));

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
	const FVector StableViewLocation = StableTarget - LocalAimDirection.GetSafeNormal() * 500.0f;
	const bool bHasMuzzle = true;

	// SimulatedProxy 观察端：应运行平滑且目标有效，消费 Muzzle → 稳定目标。
	const bool bSimulatedProxyRunsSmoothing = UShooterAimPresentationComponent::ShouldRunPresentationAimSmoothing(
		ROLE_SimulatedProxy, NM_Client, false);
	TestTrue(TEXT("simulated proxy runs presentation smoothing"), bSimulatedProxyRunsSmoothing);
	TestTrue(
		TEXT("simulated proxy uses muzzle to stable target"),
		UShooterThirdPersonAnimInstance::ComputeAimDirectionWorldForState(
			false,
			bSimulatedProxyRunsSmoothing,
			true,
			LocalAimDirection,
			StableViewLocation,
			MuzzleLocation,
			StableTarget,
			bHasMuzzle).Equals(ExpectedMuzzleToTarget, 1e-3f));

	// 第三人称近点安全门：沿原始视点射线把姿势目标投影到安全深度，同时保留横向偏移。
	const FVector NearViewLocation = MuzzleLocation + FVector(-20.0f, 10.0f, 5.0f);
	const FVector NearTarget =
		NearViewLocation +
		LocalAimDirection.GetSafeNormal() * 25.0f +
		FVector(0.0f, 2.0f, 0.0f);
	const float NearTargetForwardDistance = FVector::DotProduct(
		NearTarget - NearViewLocation,
		LocalAimDirection.GetSafeNormal());
	const FVector ExpectedSafeNearTarget =
		NearTarget +
		LocalAimDirection.GetSafeNormal() * (150.0f - NearTargetForwardDistance);
	const FVector ExpectedSafeNearDirection =
		(ExpectedSafeNearTarget - MuzzleLocation).GetSafeNormal();
	const FVector SafeNearDirection =
		UShooterThirdPersonAnimInstance::ComputeAimDirectionWorldForState(
			false,
			bSimulatedProxyRunsSmoothing,
			true,
			LocalAimDirection,
			NearViewLocation,
			MuzzleLocation,
			NearTarget,
			bHasMuzzle,
			150.0f);
	TestTrue(
		TEXT("remote near target projects onto safe forward plane"),
		SafeNearDirection.Equals(ExpectedSafeNearDirection, 1e-3f));
	TestFalse(
		TEXT("remote near target keeps lateral convergence instead of collapsing to base direction"),
		SafeNearDirection.Equals(LocalAimDirection.GetSafeNormal(), 1e-3f));

	// 长枪枪口接近视点安全平面时，安全目标还必须位于枪口前方并保留横向汇聚。
	const FVector LongWeaponViewLocation = FVector::ZeroVector;
	const FVector LongWeaponMuzzleLocation(140.0f, 0.0f, 0.0f);
	const FVector LongWeaponNearTarget(100.0f, 10.0f, 0.0f);
	const FVector ExpectedLongWeaponSafeTarget(190.0f, 10.0f, 0.0f);
	const FVector LongWeaponSafeDirection =
		UShooterThirdPersonAnimInstance::ComputeAimDirectionWorldForState(
			false,
			bSimulatedProxyRunsSmoothing,
			true,
			FVector::ForwardVector,
			LongWeaponViewLocation,
			LongWeaponMuzzleLocation,
			LongWeaponNearTarget,
			bHasMuzzle,
			150.0f,
			50.0f);
	TestTrue(
		TEXT("remote long weapon keeps safe target ahead of muzzle"),
		LongWeaponSafeDirection.Equals(
			(ExpectedLongWeaponSafeTarget - LongWeaponMuzzleLocation).GetSafeNormal(),
			1e-3f));
	TestFalse(
		TEXT("remote long weapon keeps lateral convergence"),
		LongWeaponSafeDirection.Equals(FVector::ForwardVector, 1e-3f));

	// 位于基础视线后方的目标同样投影到安全平面。
	const FVector BehindTarget = NearViewLocation - LocalAimDirection.GetSafeNormal() * 500.0f;
	const FVector ExpectedSafeBehindTarget =
		NearViewLocation + LocalAimDirection.GetSafeNormal() * 150.0f;
	TestTrue(
		TEXT("remote target behind view direction projects onto safe forward plane"),
		UShooterThirdPersonAnimInstance::ComputeAimDirectionWorldForState(
			false,
			bSimulatedProxyRunsSmoothing,
			true,
			LocalAimDirection,
			NearViewLocation,
			MuzzleLocation,
			BehindTarget,
			bHasMuzzle,
			150.0f).Equals(
				(ExpectedSafeBehindTarget - MuzzleLocation).GetSafeNormal(),
				1e-3f));

	// 安全平面边界必须连续：边界前后相同横向偏移不能产生可见方向跳变。
	const FVector LateralOffset(0.0f, 20.0f, 0.0f);
	const FVector DirectionJustInside =
		UShooterThirdPersonAnimInstance::ComputeAimDirectionWorldForState(
			false,
			bSimulatedProxyRunsSmoothing,
			true,
			FVector::ForwardVector,
			FVector::ZeroVector,
			FVector::ZeroVector,
			FVector(149.9f, 20.0f, 0.0f),
			bHasMuzzle,
			150.0f);
	const FVector DirectionJustOutside =
		UShooterThirdPersonAnimInstance::ComputeAimDirectionWorldForState(
			false,
			bSimulatedProxyRunsSmoothing,
			true,
			FVector::ForwardVector,
			FVector::ZeroVector,
			FVector(0.0f, 0.0f, 0.0f),
			FVector(150.1f, LateralOffset.Y, LateralOffset.Z),
			bHasMuzzle,
			150.0f);
	TestTrue(
		TEXT("safe forward plane transition is directionally continuous"),
		FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(DirectionJustInside, DirectionJustOutside),
			-1.0f,
			1.0f))) < 0.1f);

	// Listen Server 观察远端 Pawn（Authority 且非 LocallyControlled）：使用同一目标语义。
	const bool bListenRemoteRunsSmoothing = UShooterAimPresentationComponent::ShouldRunPresentationAimSmoothing(
		ROLE_Authority, NM_ListenServer, false);
	TestTrue(TEXT("listen server remote pawn runs presentation smoothing"), bListenRemoteRunsSmoothing);
	TestTrue(
		TEXT("listen server remote pawn uses muzzle to stable target"),
		UShooterThirdPersonAnimInstance::ComputeAimDirectionWorldForState(
			false,
			bListenRemoteRunsSmoothing,
			true,
			LocalAimDirection,
			StableViewLocation,
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
			StableViewLocation,
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
			StableViewLocation,
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
			StableViewLocation,
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
			StableViewLocation,
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
	UShooterAimPresentationTestHarness* Harness = NewObject<UShooterAimPresentationTestHarness>();
	if (!TestNotNull(TEXT("test harness created"), Harness))
	{
		return false;
	}

	const FVector ViewLocation = Harness->ViewLocationOverride;
	const FVector TargetA(10000.0, 2000.0, 1500.0);
	const FVector TargetB(4000.0, 9000.0, 1200.0);

	// SimulatedProxy 观察端路径。
	Harness->SetRoleOverrideForTest(ROLE_SimulatedProxy);
	Harness->SetNetModeOverrideForTest(NM_Client);
	Harness->SetLocallyControlledOverrideForTest(false);

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
		Harness->MeshTransformOverride,
		ExpectedSimulatedYaw,
		ExpectedSimulatedPitch);
	TestTrue(
		TEXT("simulated proxy consumes smoothed presentation target"),
		FMath::IsNearlyEqual(SimulatedYaw, ExpectedSimulatedYaw, 0.01f) &&
		FMath::IsNearlyEqual(SimulatedPitch, ExpectedSimulatedPitch, 0.01f));

	// 本地拥有者（Standalone Authority）：即使内存中存在远端/表现目标，角度仍来自本地基础瞄准旋转。
	Harness->SetRoleOverrideForTest(ROLE_Authority);
	Harness->SetNetModeOverrideForTest(NM_Standalone);
	Harness->SetLocallyControlledOverrideForTest(true);
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
		Harness->MeshTransformOverride,
		ExpectedOwnerYaw,
		ExpectedOwnerPitch);
	TestTrue(
		TEXT("local owner is not overwritten by remote presentation target"),
		FMath::IsNearlyEqual(OwnerYaw, ExpectedOwnerYaw, 0.01f) &&
		FMath::IsNearlyEqual(OwnerPitch, ExpectedOwnerPitch, 0.01f));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
