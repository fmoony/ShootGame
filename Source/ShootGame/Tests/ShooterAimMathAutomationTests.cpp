// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ShooterAimMath.h"

/**
 * B1 纯计算测试：瞄准角度数学（四象限、±180 环绕、最短路径插值）。
 * 不依赖网络字段，不生成 Actor；断言语义与 UE 5.6 FRotator 行为一致。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAimMathTest,
	"ShootGame.Aim.Math",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAimMathTest::RunTest(const FString& Parameters)
{
	// ---- NormalizeAngleDelta：(-180, 180] 环绕 ----
	TestEqual(TEXT("normalize 0"), FShooterAimMath::NormalizeAngleDelta(0.0f), 0.0f);
	TestEqual(TEXT("normalize 90"), FShooterAimMath::NormalizeAngleDelta(90.0f), 90.0f);
	TestEqual(TEXT("normalize 180 stays 180"), FShooterAimMath::NormalizeAngleDelta(180.0f), 180.0f);
	TestEqual(TEXT("normalize 181 -> -179"), FShooterAimMath::NormalizeAngleDelta(181.0f), -179.0f);
	TestEqual(TEXT("normalize -180 -> 180"), FShooterAimMath::NormalizeAngleDelta(-180.0f), 180.0f);
	TestEqual(TEXT("normalize -181 -> 179"), FShooterAimMath::NormalizeAngleDelta(-181.0f), 179.0f);
	TestEqual(TEXT("normalize 359 -> -1"), FShooterAimMath::NormalizeAngleDelta(359.0f), -1.0f);
	TestEqual(TEXT("normalize 360 -> 0"), FShooterAimMath::NormalizeAngleDelta(360.0f), 0.0f);
	TestEqual(TEXT("normalize 720 -> 0"), FShooterAimMath::NormalizeAngleDelta(720.0f), 0.0f);
	TestEqual(TEXT("normalize -360 -> 0"), FShooterAimMath::NormalizeAngleDelta(-360.0f), 0.0f);
	TestEqual(TEXT("normalize 540 -> 180"), FShooterAimMath::NormalizeAngleDelta(540.0f), 180.0f);

	// ---- ShortestAngleInterp：最短路径步进 ----
	TestEqual(TEXT("interp toward 30 by 20"), FShooterAimMath::ShortestAngleInterp(0.0f, 30.0f, 20.0f), 20.0f);
	TestEqual(TEXT("interp 0->350 shortest is -10"), FShooterAimMath::ShortestAngleInterp(0.0f, 350.0f, 30.0f), -10.0f);
	TestEqual(TEXT("interp 170->-170 crosses +180"), FShooterAimMath::ShortestAngleInterp(170.0f, -170.0f, 20.0f), 190.0f);
	TestEqual(TEXT("interp 10->350 clamped to -15"), FShooterAimMath::ShortestAngleInterp(10.0f, 350.0f, 15.0f), -5.0f);
	TestEqual(TEXT("interp 0->180 tie resolves +180"), FShooterAimMath::ShortestAngleInterp(0.0f, 180.0f, 5.0f), 5.0f);
	TestEqual(TEXT("interp already at target"), FShooterAimMath::ShortestAngleInterp(123.0f, 123.0f, 10.0f), 123.0f);

	// ---- WorldDirectionToLocalAngles：四象限与 ±180 环绕 ----
	const FTransform Identity = FTransform::Identity;
	const FTransform Yaw90 = FTransform(FRotator(0.0f, 90.0f, 0.0f));
	const FTransform Yaw180 = FTransform(FRotator(0.0f, 180.0f, 0.0f));
	const FTransform YawMinus90 = FTransform(FRotator(0.0f, -90.0f, 0.0f));
	const FTransform Pitch45 = FTransform(FRotator(45.0f, 0.0f, 0.0f));

	float Yaw = 0.0f;
	float Pitch = 0.0f;

	// 单位参考系四象限 + 垂直
	FShooterAimMath::WorldDirectionToLocalAngles(FVector(1, 0, 0), Identity, Yaw, Pitch);
	TestTrue(TEXT("identity forward yaw 0"), FMath::IsNearlyEqual(Yaw, 0.0f, 0.01f));
	TestTrue(TEXT("identity forward pitch 0"), FMath::IsNearlyEqual(Pitch, 0.0f, 0.01f));

	FShooterAimMath::WorldDirectionToLocalAngles(FVector(0, 1, 0), Identity, Yaw, Pitch);
	TestTrue(TEXT("identity +Y yaw 90"), FMath::IsNearlyEqual(Yaw, 90.0f, 0.01f));

	FShooterAimMath::WorldDirectionToLocalAngles(FVector(0, -1, 0), Identity, Yaw, Pitch);
	TestTrue(TEXT("identity -Y yaw -90"), FMath::IsNearlyEqual(Yaw, -90.0f, 0.01f));

	FShooterAimMath::WorldDirectionToLocalAngles(FVector(-1, 0, 0), Identity, Yaw, Pitch);
	TestTrue(TEXT("identity -X yaw 180"), FMath::IsNearlyEqual(Yaw, 180.0f, 0.01f));

	FShooterAimMath::WorldDirectionToLocalAngles(FVector(0, 0, 1), Identity, Yaw, Pitch);
	TestTrue(TEXT("identity +Z pitch 90"), FMath::IsNearlyEqual(Pitch, 90.0f, 0.01f));

	FShooterAimMath::WorldDirectionToLocalAngles(FVector(0, 0, -1), Identity, Yaw, Pitch);
	TestTrue(TEXT("identity -Z pitch -90"), FMath::IsNearlyEqual(Pitch, -90.0f, 0.01f));

	FShooterAimMath::WorldDirectionToLocalAngles(FVector(0, 0.7071f, 0.7071f), Identity, Yaw, Pitch);
	TestTrue(TEXT("identity NE pitch 45"), FMath::IsNearlyEqual(Pitch, 45.0f, 0.01f));
	TestTrue(TEXT("identity NE yaw 90"), FMath::IsNearlyEqual(Yaw, 90.0f, 0.01f));

	// 参考系旋转：世界前方在不同参考系下的局部角。
	// 依据 UE 5.6 FQuat(FRotator) 实现（pitch 绕 -Y、yaw 绕 +Z 的左手约定），
	// 参考系 +Yaw 旋转后，世界前方在局部表现为 -Yaw（符号与右手系直觉相反）。
	FShooterAimMath::WorldDirectionToLocalAngles(FVector(1, 0, 0), Yaw90, Yaw, Pitch);
	TestTrue(TEXT("ref yaw90 world forward local yaw -90"), FMath::IsNearlyEqual(Yaw, -90.0f, 0.01f));

	FShooterAimMath::WorldDirectionToLocalAngles(FVector(1, 0, 0), Yaw180, Yaw, Pitch);
	TestTrue(TEXT("ref yaw180 world forward local yaw 180"), FMath::IsNearlyEqual(Yaw, 180.0f, 0.01f));

	FShooterAimMath::WorldDirectionToLocalAngles(FVector(1, 0, 0), YawMinus90, Yaw, Pitch);
	TestTrue(TEXT("ref yaw-90 world forward local yaw +90"), FMath::IsNearlyEqual(Yaw, 90.0f, 0.01f));

	// 参考系自身前向在局部恒为 +X（yaw 0 / pitch 0）
	FShooterAimMath::WorldDirectionToLocalAngles(
		FRotator(45.0f, 0.0f, 0.0f).Vector(), Pitch45, Yaw, Pitch);
	TestTrue(TEXT("ref pitch45 own forward local yaw 0"),
		FMath::IsNearlyEqual(Yaw, 0.0f, 0.01f) && FMath::IsNearlyEqual(Pitch, 0.0f, 0.01f));

	// 世界前方在 pitch 45 参考系中表现为局部 -45° 俯角（pitch 绕 -Y 的约定）
	FShooterAimMath::WorldDirectionToLocalAngles(FVector(1, 0, 0), Pitch45, Yaw, Pitch);
	TestTrue(TEXT("ref pitch45 world forward local pitch -45"), FMath::IsNearlyEqual(Pitch, -45.0f, 0.01f));

	// 零向量回退 0/0
	FShooterAimMath::WorldDirectionToLocalAngles(FVector::ZeroVector, Identity, Yaw, Pitch);
	TestTrue(TEXT("zero direction falls back 0/0"),
		FMath::IsNearlyEqual(Yaw, 0.0f, 0.01f) && FMath::IsNearlyEqual(Pitch, 0.0f, 0.01f));

	return true;
}

#endif
