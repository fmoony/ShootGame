// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Characters/ShooterCharacter.h"
#include "ShooterAimPresentationTestHarness.generated.h"

/**
 * C2.5 表现目标状态机测试壳。
 * 仅用于 Editor Automation，不进入游戏逻辑：
 * 暴露 UpdatePresentationAimSmoothing / ResetPresentationAimSmoothing 的受保护入口，
 * 并重写视点与基础瞄准旋转，使无 World 对象也能验证状态转换。
 */
UCLASS(NotBlueprintable, Transient)
class AShooterAimPresentationTestHarness : public AShooterCharacter
{
	GENERATED_BODY()

public:
	AShooterAimPresentationTestHarness() = default;

	virtual FVector GetPawnViewLocation() const override { return ViewLocationOverride; }
	virtual FRotator GetBaseAimRotation() const override { return AimRotationOverride; }

	void SetPresentationAimTargetForTest(const FVector& InTarget)
	{
		PresentationAimTarget.X = InTarget.X;
		PresentationAimTarget.Y = InTarget.Y;
		PresentationAimTarget.Z = InTarget.Z;
	}

	void SetSmoothedPresentationAimTargetForTest(const FVector& InTarget)
	{
		SmoothedPresentationAimTarget = InTarget;
	}

	void SetPresentationAimTargetValidForTest(bool bInValid)
	{
		bPresentationAimTargetValid = bInValid;
	}

	void SetLastPresentationAimViewLocationForTest(const FVector& InLocation)
	{
		LastPresentationAimViewLocation = InLocation;
	}

	void SetDeadForTest(bool bInDead)
	{
		bIsDead = bInDead;
	}

	FVector GetSmoothedPresentationAimTargetForTest() const { return SmoothedPresentationAimTarget; }
	bool IsPresentationAimTargetValidForTest() const { return bPresentationAimTargetValid; }
	FVector GetLastPresentationAimViewLocationForTest() const { return LastPresentationAimViewLocation; }

	void CallUpdatePresentationAimSmoothing(float DeltaSeconds)
	{
		UpdatePresentationAimSmoothing(DeltaSeconds);
	}

	void CallResetPresentationAimSmoothing()
	{
		ResetPresentationAimSmoothing();
	}

	FVector ViewLocationOverride = FVector(1000.0, 2000.0, 100.0);
	FRotator AimRotationOverride = FRotator(20.0f, 30.0f, 0.0f);
};
