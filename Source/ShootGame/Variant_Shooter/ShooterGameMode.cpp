// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/ShooterGameMode.h"
#include "ShooterUI.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

void AShooterGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Dedicated servers do not have a local player or a viewport.
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (!IsValid(PlayerController) || !PlayerController->IsLocalController() || !ShooterUIClass)
	{
		return;
	}

	// create the UI for the local standalone or listen-server player
	ShooterUI = CreateWidget<UShooterUI>(PlayerController, ShooterUIClass);
	if (IsValid(ShooterUI))
	{
		ShooterUI->AddToViewport(0);
	}
}

void AShooterGameMode::IncrementTeamScore(uint8 TeamByte)
{
	// retrieve the team score if any
	int32 Score = 0;
	if (int32* FoundScore = TeamScores.Find(TeamByte))
	{
		Score = *FoundScore;
	}

	// increment the score for the given team
	++Score;
	TeamScores.Add(TeamByte, Score);

	// Dedicated servers have no UI. Client score presentation will move to
	// PlayerController/GameState when the shooter mode is fully networked.
	if (IsValid(ShooterUI))
	{
		ShooterUI->BP_UpdateScore(TeamByte, Score);
	}
}
