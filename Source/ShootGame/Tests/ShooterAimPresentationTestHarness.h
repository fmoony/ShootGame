// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Characters/Aim/ShooterAimPresentationComponent.h"
#include "ShooterAimPresentationTestHarness.generated.h"

/**
 * R2 表现目标状态机测试壳。
 * 仅用于 Editor Automation：直接实例化 Component 子类并覆盖运行上下文，
 * 使无 World 对象也能验证 UpdatePresentationAimSmoothing / Reset / Clear 与角度输出。
 */
UCLASS(NotBlueprintable, Transient)
class UShooterAimPresentationTestHarness : public UShooterAimPresentationComponent
{
	GENERATED_BODY()

public:
	UShooterAimPresentationTestHarness() = default;

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
		bDeadOverride = bInDead;
	}

	void SetRoleOverrideForTest(ENetRole InRole)
	{
		RoleOverride = InRole;
	}

	void SetNetModeOverrideForTest(ENetMode InNetMode)
	{
		NetModeOverride = InNetMode;
	}

	void SetLocallyControlledOverrideForTest(bool bInLocallyControlled)
	{
		bLocallyControlledOverride = bInLocallyControlled;
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

	void CallClearPresentationAimSmoothing()
	{
		ClearPresentationAimSmoothing();
	}

	FVector ViewLocationOverride = FVector(1000.0, 2000.0, 100.0);
	FRotator AimRotationOverride = FRotator(20.0f, 30.0f, 0.0f);
	FTransform MeshTransformOverride = FTransform(
		FRotator(0.0f, 10.0f, 0.0f),
		FVector(500.0f, 600.0f, 90.0f));

protected:
	virtual ENetRole GetPresentationLocalRole() const override { return RoleOverride; }
	virtual ENetMode GetPresentationNetMode() const override { return NetModeOverride; }
	virtual bool IsPresentationOwnerLocallyControlled() const override { return bLocallyControlledOverride; }
	virtual bool IsPresentationOwnerDead() const override { return bDeadOverride; }
	virtual FVector GetPresentationPawnViewLocation() const override { return ViewLocationOverride; }
	virtual FRotator GetPresentationBaseAimRotation() const override { return AimRotationOverride; }
	virtual FTransform GetPresentationMeshReferenceTransform() const override { return MeshTransformOverride; }
	virtual float GetPresentationMaxAimDistance() const override { return 10000.0f; }

	ENetRole RoleOverride = ROLE_Authority;
	ENetMode NetModeOverride = NM_Standalone;
	bool bLocallyControlledOverride = true;
	bool bDeadOverride = false;
};
