// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterPlayerState.h"

#include "AbilitySystemComponent.h"
#include "GameplayEffectTypes.h"
#include "ShooterAttributeSet.h"
#include "ShooterCharacter.h"
#include "Net/UnrealNetwork.h"

AShooterPlayerState::AShooterPlayerState()
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	// Mixed：完整能力数据复制给拥有者连接，其他客户端只收到最小集合。
	// 该模式依赖 Owner（PlayerState → PlayerController）的网络连接，由网络测试协调器在运行时验证。
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	AttributeSet = CreateDefaultSubobject<UShooterAttributeSet>(TEXT("AttributeSet"));
}

UAbilitySystemComponent* AShooterPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AShooterPlayerState::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	BindHealthAttributeDelegate();
}

void AShooterPlayerState::BindHealthAttributeDelegate()
{
	if (bHealthAttributeDelegateBound || !AbilitySystemComponent)
	{
		return;
	}

	// 绑定在 PlayerState（ASC 的持久宿主）上，跨角色重生保持有效：
	// 服务器用于死亡桥接，拥有者客户端用于 HUD 事件链。
	AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
		UShooterAttributeSet::GetHealthAttribute())
		.AddUObject(this, &AShooterPlayerState::HandleHealthAttributeChanged);
	bHealthAttributeDelegateBound = true;
}

void AShooterPlayerState::HandleHealthAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	if (AShooterCharacter* AvatarCharacter =
		Cast<AShooterCharacter>(AbilitySystemComponent->GetAvatarActor()))
	{
		AvatarCharacter->HandleHealthAttributeChanged(ChangeData);
	}
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
