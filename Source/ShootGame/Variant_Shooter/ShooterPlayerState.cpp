// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variant_Shooter/ShooterPlayerState.h"

#include "Net/UnrealNetwork.h"

void AShooterPlayerState::SetTeamId(uint8 NewTeamId)
{
	if (!HasAuthority())
	{
		return;
	}

	TeamId = NewTeamId;
	ForceNetUpdate();
}

void AShooterPlayerState::AddKill()
{
	if (!HasAuthority())
	{
		return;
	}

	++Kills;
	SetScore(GetScore() + 1.0f);
	OnRep_CombatStats();
	ForceNetUpdate();
}

void AShooterPlayerState::AddDeath()
{
	if (!HasAuthority())
	{
		return;
	}

	++Deaths;
	OnRep_CombatStats();
	ForceNetUpdate();
}

void AShooterPlayerState::OnRep_TeamId()
{
	OnRep_CombatStats();
}

void AShooterPlayerState::OnRep_CombatStats()
{
	OnCombatStatsChanged.Broadcast(Kills, Deaths, GetScore());
}

void AShooterPlayerState::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShooterPlayerState, TeamId);
	DOREPLIFETIME(AShooterPlayerState, Kills);
	DOREPLIFETIME(AShooterPlayerState, Deaths);
}
