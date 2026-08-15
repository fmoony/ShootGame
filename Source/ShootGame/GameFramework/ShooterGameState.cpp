// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterGameState.h"

#include "Net/UnrealNetwork.h"

void AShooterGameState::AddTeamScore(uint8 TeamId)
{
	if (!HasAuthority())
	{
		return;
	}

	if (!TeamScores.IsValidIndex(TeamId))
	{
		TeamScores.SetNumZeroed(static_cast<int32>(TeamId) + 1);
	}

	++TeamScores[TeamId];
	OnTeamScoreChanged.Broadcast(TeamId, TeamScores[TeamId]);
	ForceNetUpdate();
}

int32 AShooterGameState::GetTeamScore(uint8 TeamId) const
{
	return TeamScores.IsValidIndex(TeamId) ? TeamScores[TeamId] : 0;
}

void AShooterGameState::OnRep_TeamScores()
{
	for (int32 TeamIndex = 0; TeamIndex < TeamScores.Num(); ++TeamIndex)
	{
		OnTeamScoreChanged.Broadcast(static_cast<uint8>(TeamIndex), TeamScores[TeamIndex]);
	}
}

void AShooterGameState::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShooterGameState, TeamScores);
}
