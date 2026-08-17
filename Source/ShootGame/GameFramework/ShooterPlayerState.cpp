// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterPlayerState.h"

#include "AbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"

AShooterPlayerState::AShooterPlayerState()
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	// Mixed：完整能力数据复制给拥有者连接，其他客户端只收到最小集合。
	// 该模式依赖 Owner（PlayerState → PlayerController）的网络连接，由网络测试协调器在运行时验证。
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
}

UAbilitySystemComponent* AShooterPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AShooterPlayerState::InitializeAbilityActorInfo(AActor* AvatarActor)
{
	if (!AbilitySystemComponent || AbilitySystemComponent->GetAvatarActor() == AvatarActor)
	{
		return;
	}

	AbilitySystemComponent->InitAbilityActorInfo(this, AvatarActor);
}

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
