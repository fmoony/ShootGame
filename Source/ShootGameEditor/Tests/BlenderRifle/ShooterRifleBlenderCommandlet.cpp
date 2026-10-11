// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterRifleBlenderCommandlet.h"

#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Components/SkeletalMeshComponent.h"

void AShooterRifleBlenderPreview::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (ExperimentAnimClass)
	{
		ThirdPersonMesh->SetAnimInstanceClass(ExperimentAnimClass);
		Tick(0.0f);
	}
}

FString AShooterRifleBlenderPreview::AdvanceRiflePreview(float DeltaSeconds)
{
	ThirdPersonMesh->SetUpdateAnimationInEditor(false);
	Tick(DeltaSeconds);
	ThirdPersonMesh->TickAnimation(DeltaSeconds, false);
	ThirdPersonMesh->RefreshBoneTransforms();
	RifleMesh->UpdateComponentToWorld();
	const auto* Instance = Cast<UShooterRifleTransitionTestAnimInstance>(ThirdPersonMesh->GetAnimInstance());
	const auto* Machine = Instance ? Instance->GetStateMachineInstanceFromName(TEXT("WeaponAction")) : nullptr;
	const auto* Player = Instance ? Instance->GetTransitionPlayer() : nullptr;
	return Machine && Player
		? FString::Printf(TEXT("%s,%.9f,%.3f"),
			*Machine->GetCurrentStateName().ToString(), Player->GetAccumulatedTime(), Player->GetPlayRate())
		: TEXT("Invalid");
}

void AShooterRifleBlenderPreview::SetRiflePreviewGoal(bool bLowReady, float Pitch, float Strength)
{
	RifleTestGoal = bLowReady ? ERifleTransitionTestGoal::LowReady : ERifleTransitionTestGoal::ArmedAim;
	RifleTestPitch = Pitch;
	RifleTestNaturalPitchStrength = FMath::Clamp(Strength, 0.0f, 1.0f);
	Tick(0.0f);
}
