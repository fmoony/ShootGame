// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterPlayerState.h"

#include "Abilities/GameplayAbility.h"
#include "GameplayEffectTypes.h"
#include "ShooterAbilitySystemComponent.h"
#include "ShooterAttributeSet.h"
#include "ShooterCharacter.h"
#include "ShooterGameplayAbility_Fire.h"
#include "ShooterGameplayAbility_Equip.h"
#include "ShooterGameplayAbility_Reload.h"
#include "Net/UnrealNetwork.h"

AShooterPlayerState::AShooterPlayerState()
{
	AbilitySystemComponent = CreateDefaultSubobject<UShooterAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	ReloadAbilityClass = UShooterGameplayAbility_Reload::StaticClass();
	EquipAbilityClass = UShooterGameplayAbility_Equip::StaticClass();
	FireAbilityClass = UShooterGameplayAbility_Fire::StaticClass();
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
	GrantFireAbility();
	GrantReloadAbility();
	GrantEquipAbility();
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

void AShooterPlayerState::GrantFireAbility()
{
	if (!HasAuthority() || !AbilitySystemComponent || !FireAbilityClass)
	{
		return;
	}

	// 幂等授予：同一个 PlayerState 只允许存在一个 Fire Ability Spec。
	// 重生不会重新调用这里，只会通过 InitializeAbilityActorInfo 更新 Avatar。
	if (AbilitySystemComponent->FindAbilitySpecFromClass(FireAbilityClass))
	{
		return;
	}

	const FGameplayAbilitySpec FireAbilitySpec(
		FireAbilityClass,
		/*AbilityLevel*/1,
		INDEX_NONE,
		this);
	AbilitySystemComponent->GiveAbility(FireAbilitySpec);
}

int32 AShooterPlayerState::GetFireAbilitySpecCount() const
{
	if (!AbilitySystemComponent)
	{
		return 0;
	}

	return AbilitySystemComponent->GetAbilitySpecCountForClass(FireAbilityClass);
}

void AShooterPlayerState::GrantReloadAbility()
{
	if (!HasAuthority() || !AbilitySystemComponent || !ReloadAbilityClass)
	{
		return;
	}

	GrantAbilityIfMissing(ReloadAbilityClass);
}

int32 AShooterPlayerState::GetReloadAbilitySpecCount() const
{
	if (!AbilitySystemComponent)
	{
		return 0;
	}

	return AbilitySystemComponent->GetAbilitySpecCountForClass(ReloadAbilityClass);
}

void AShooterPlayerState::GrantEquipAbility()
{
	if (!HasAuthority() || !AbilitySystemComponent || !EquipAbilityClass)
	{
		return;
	}

	GrantAbilityIfMissing(EquipAbilityClass);
}

int32 AShooterPlayerState::GetEquipAbilitySpecCount() const
{
	if (!AbilitySystemComponent)
	{
		return 0;
	}

	return AbilitySystemComponent->GetAbilitySpecCountForClass(EquipAbilityClass);
}

void AShooterPlayerState::GrantAbilityIfMissing(TSubclassOf<UGameplayAbility> AbilityClass)
{
	if (!AbilityClass || !AbilitySystemComponent ||
		AbilitySystemComponent->FindAbilitySpecFromClass(AbilityClass))
	{
		return;
	}

	const FGameplayAbilitySpec AbilitySpec(
		AbilityClass,
		/*AbilityLevel*/1,
		INDEX_NONE,
		this);
	AbilitySystemComponent->GiveAbility(AbilitySpec);
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
