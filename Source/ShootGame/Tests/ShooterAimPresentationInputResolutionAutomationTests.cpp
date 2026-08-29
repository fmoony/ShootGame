// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Characters/Aim/ShooterAimPresentationComponent.h"
#include "ShooterAimPresentationTestHarness.h"

namespace ShooterAimPresentationInputResolutionAutomationTests
{
	UShooterAimPresentationTestHarness* CreateResolverHarness(
		FAutomationTestBase& Test,
		ENetRole Role,
		ENetMode NetMode,
		bool bLocallyControlled)
	{
		UShooterAimPresentationTestHarness* Harness =
			NewObject<UShooterAimPresentationTestHarness>();
		Test.TestNotNull(TEXT("Resolver harness created"), Harness);
		if (Harness)
		{
			Harness->SetRoleOverrideForTest(Role);
			Harness->SetNetModeOverrideForTest(NetMode);
			Harness->SetLocallyControlledOverrideForTest(bLocallyControlled);
		}
		return Harness;
	}

	void Resolve(UShooterAimPresentationTestHarness* Harness, FVector& OutDirection, FVector& OutTarget, bool& bOutTargetValid)
	{
		Harness->ResolveAimPresentationInput(OutDirection, OutTarget, bOutTargetValid, 150.0f);
	}
}

/**
 * 阶段 1：本地控制角色解析为 BaseAimDirection，且不消费远端平滑 Target。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAimPresentationResolveLocalOwnerTest,
	"ShootGame.Aim.PresentationInputResolve.LocalOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAimPresentationResolveLocalOwnerTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAimPresentationInputResolutionAutomationTests;

	UShooterAimPresentationTestHarness* Harness = CreateResolverHarness(
		*this,
		ROLE_Authority,
		NM_ListenServer,
		true);
	if (!Harness)
	{
		return false;
	}

	// 即使平滑目标有效，本地拥有者也必须只使用即时 BaseAimDirection。
	Harness->SetPresentationAimTargetValidForTest(true);
	Harness->SetSmoothedPresentationAimTargetForTest(FVector(5000.0f, 0.0f, 1000.0f));

	FVector OutDirection = FVector::ZeroVector;
	FVector OutTarget = FVector::ZeroVector;
	bool bOutTargetValid = true;
	Resolve(Harness, OutDirection, OutTarget, bOutTargetValid);

	TestTrue(
		TEXT("local owner consumes BaseAimDirection"),
		OutDirection.Equals(Harness->AimRotationOverride.Vector().GetSafeNormal(), 1e-4f));
	TestTrue(TEXT("local owner leaves Target zero"), OutTarget.Equals(FVector::ZeroVector));
	TestFalse(TEXT("local owner marks Target invalid"), bOutTargetValid);
	return true;
}

/**
 * 阶段 1：SimulatedProxy 且平滑目标有效时输出有效 Target。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAimPresentationResolveSimulatedProxyTest,
	"ShootGame.Aim.PresentationInputResolve.SimulatedProxy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAimPresentationResolveSimulatedProxyTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAimPresentationInputResolutionAutomationTests;

	UShooterAimPresentationTestHarness* Harness = CreateResolverHarness(
		*this,
		ROLE_SimulatedProxy,
		NM_Client,
		false);
	if (!Harness)
	{
		return false;
	}

	const FVector BaseDirection = Harness->AimRotationOverride.Vector().GetSafeNormal();
	const FVector SmoothedTarget =
		Harness->ViewLocationOverride + BaseDirection * 1000.0f + FVector(50.0f, -30.0f, 10.0f);
	Harness->SetPresentationAimTargetValidForTest(true);
	Harness->SetSmoothedPresentationAimTargetForTest(SmoothedTarget);

	FVector OutDirection = FVector::ZeroVector;
	FVector OutTarget = FVector::ZeroVector;
	bool bOutTargetValid = false;
	Resolve(Harness, OutDirection, OutTarget, bOutTargetValid);

	TestTrue(TEXT("simulated proxy keeps BaseAimDirection"), OutDirection.Equals(BaseDirection, 1e-4f));
	TestTrue(TEXT("simulated proxy outputs the smoothed target"), OutTarget.Equals(SmoothedTarget, 1e-4f));
	TestTrue(TEXT("simulated proxy marks Target valid"), bOutTargetValid);
	return true;
}

/**
 * 阶段 1：Listen Server 观察远端 Pawn（Authority + 非本地控制）时同样启用平滑 Target。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAimPresentationResolveListenServerRemoteTest,
	"ShootGame.Aim.PresentationInputResolve.ListenServerRemote",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAimPresentationResolveListenServerRemoteTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAimPresentationInputResolutionAutomationTests;

	UShooterAimPresentationTestHarness* Harness = CreateResolverHarness(
		*this,
		ROLE_Authority,
		NM_ListenServer,
		false);
	if (!Harness)
	{
		return false;
	}

	const FVector BaseDirection = Harness->AimRotationOverride.Vector().GetSafeNormal();
	const FVector SmoothedTarget =
		Harness->ViewLocationOverride + BaseDirection * 800.0f;
	Harness->SetPresentationAimTargetValidForTest(true);
	Harness->SetSmoothedPresentationAimTargetForTest(SmoothedTarget);

	FVector OutDirection = FVector::ZeroVector;
	FVector OutTarget = FVector::ZeroVector;
	bool bOutTargetValid = false;
	Resolve(Harness, OutDirection, OutTarget, bOutTargetValid);

	TestTrue(TEXT("listen server observer keeps BaseAimDirection"), OutDirection.Equals(BaseDirection, 1e-4f));
	TestTrue(TEXT("listen server observer outputs the smoothed target"), OutTarget.Equals(SmoothedTarget, 1e-4f));
	TestTrue(TEXT("listen server observer marks Target valid"), bOutTargetValid);
	return true;
}

/**
 * 阶段 1：无效标记或非有限平滑目标不会输出有效 Target，并关闭远端 Aim 输入。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAimPresentationResolveInvalidTargetTest,
	"ShootGame.Aim.PresentationInputResolve.InvalidTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAimPresentationResolveInvalidTargetTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAimPresentationInputResolutionAutomationTests;

	UShooterAimPresentationTestHarness* Harness = CreateResolverHarness(
		*this,
		ROLE_SimulatedProxy,
		NM_Client,
		false);
	if (!Harness)
	{
		return false;
	}

	// 有效性标记为 false。
	Harness->SetPresentationAimTargetValidForTest(false);
	Harness->SetSmoothedPresentationAimTargetForTest(FVector(1000.0f, 0.0f, 0.0f));
	FVector OutDirection = FVector::ZeroVector;
	FVector OutTarget = FVector::ZeroVector;
	bool bOutTargetValid = false;
	Resolve(Harness, OutDirection, OutTarget, bOutTargetValid);
	TestTrue(TEXT("invalid flag leaves direction zero"), OutDirection.IsNearlyZero());
	TestTrue(TEXT("invalid flag leaves target zero"), OutTarget.IsNearlyZero());
	TestFalse(TEXT("invalid flag keeps Target invalid"), bOutTargetValid);

	// 标记有效但平滑目标 NaN / Inf。
	Harness->SetPresentationAimTargetValidForTest(true);
	Harness->SetSmoothedPresentationAimTargetForTest(FVector(NAN, 0.0f, 0.0f));
	bOutTargetValid = true;
	Resolve(Harness, OutDirection, OutTarget, bOutTargetValid);
	TestTrue(TEXT("NaN target leaves direction zero"), OutDirection.IsNearlyZero());
	TestTrue(TEXT("NaN target leaves target zero"), OutTarget.IsNearlyZero());
	TestFalse(TEXT("NaN target keeps Target invalid"), bOutTargetValid);

	Harness->SetSmoothedPresentationAimTargetForTest(FVector(0.0f, INFINITY, 0.0f));
	bOutTargetValid = true;
	Resolve(Harness, OutDirection, OutTarget, bOutTargetValid);
	TestTrue(TEXT("infinite target leaves direction zero"), OutDirection.IsNearlyZero());
	TestTrue(TEXT("infinite target leaves target zero"), OutTarget.IsNearlyZero());
	TestFalse(TEXT("infinite target keeps Target invalid"), bOutTargetValid);
	return true;
}

/**
 * 阶段 1：目标距视点不足 150cm 时沿 BaseAimDirection 投影到最小安全深度。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAimPresentationResolveViewSafeDepthTest,
	"ShootGame.Aim.PresentationInputResolve.ViewSafeDepth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAimPresentationResolveViewSafeDepthTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAimPresentationInputResolutionAutomationTests;

	UShooterAimPresentationTestHarness* Harness = CreateResolverHarness(
		*this,
		ROLE_SimulatedProxy,
		NM_Client,
		false);
	if (!Harness)
	{
		return false;
	}

	const FVector BaseDirection = Harness->AimRotationOverride.Vector().GetSafeNormal();
	constexpr float MinimumViewDepth = 150.0f;
	const FVector NearTarget =
		Harness->ViewLocationOverride + BaseDirection * 20.0f;
	Harness->SetPresentationAimTargetValidForTest(true);
	Harness->SetSmoothedPresentationAimTargetForTest(NearTarget);

	FVector OutDirection = FVector::ZeroVector;
	FVector OutTarget = FVector::ZeroVector;
	bool bOutTargetValid = false;
	Harness->ResolveAimPresentationInput(
		OutDirection,
		OutTarget,
		bOutTargetValid,
		MinimumViewDepth);

	const FVector ExpectedSafeTarget =
		NearTarget + BaseDirection * (MinimumViewDepth - 20.0f);
	TestTrue(TEXT("near target is projected to minimum view depth"), OutTarget.Equals(ExpectedSafeTarget, 1e-3f));
	TestTrue(TEXT("projected target stays valid"), bOutTargetValid);
	TestTrue(TEXT("view projection keeps BaseAimDirection"), OutDirection.Equals(BaseDirection, 1e-4f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
