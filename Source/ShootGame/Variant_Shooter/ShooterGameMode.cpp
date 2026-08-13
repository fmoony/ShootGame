// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/ShooterGameMode.h"
#include "Engine/World.h"
#include "ShooterGameState.h"
#include "ShooterPlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Tests/Network/ShooterNetworkTestCoordinator.h"

AShooterGameMode::AShooterGameMode()
{
	GameStateClass = AShooterGameState::StaticClass();
	PlayerStateClass = AShooterPlayerState::StaticClass();
}

void AShooterGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	if (AShooterPlayerState* ShooterPlayerState =
		NewPlayer ? NewPlayer->GetPlayerState<AShooterPlayerState>() : nullptr)
	{
		const int32 JoinedPlayerIndex = FMath::Max(0, GetNumPlayers() - 1);
		ShooterPlayerState->SetTeamId(static_cast<uint8>(JoinedPlayerIndex % 2));
	}

#if WITH_DEV_AUTOMATION_TESTS
	if (NewPlayer && FParse::Param(FCommandLine::Get(), TEXT("ShootGameNetworkTest")))
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = NewPlayer;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GetWorld()->SpawnActor<AShooterNetworkTestCoordinator>(
			AShooterNetworkTestCoordinator::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	}
#endif
}

void AShooterGameMode::IncrementTeamScore(uint8 TeamId)
{
	if (AShooterGameState* ShooterGameState = GetGameState<AShooterGameState>())
	{
		ShooterGameState->AddTeamScore(TeamId);
	}
}
