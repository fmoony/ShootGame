// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "ShooterPlayerState.generated.h"

class UAbilitySystemComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FPlayerCombatStatsChangedDelegate,
	int32,
	Kills,
	int32,
	Deaths,
	float,
	PersonalScore);

/**
 * 射击模式中随玩家复制的身份与战斗统计。
 * 这些字段只允许服务器修改，客户端只负责显示。
 * 同时是玩家能力系统组件（ASC）的唯一宿主：Owner = PlayerState，Avatar = 当前角色。
 */
UCLASS()
class SHOOTGAME_API AShooterPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	/** 构造函数：创建玩家 ASC 并启用 Mixed 复制模式。 */
	AShooterPlayerState();

	//~Begin IAbilitySystemInterface
	/** 返回由本 PlayerState 持有的玩家 ASC。 */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~End IAbilitySystemInterface

	/** 以指定 Actor 为 Avatar 建立 AbilityActorInfo；Avatar 不变时幂等跳过。 */
	void InitializeAbilityActorInfo(AActor* AvatarActor);

	UPROPERTY(BlueprintAssignable, Category="Shooter|Stats")
	FPlayerCombatStatsChangedDelegate OnCombatStatsChanged;

	void SetTeamId(uint8 NewTeamId);
	void AddKill();
	void AddDeath();

	UFUNCTION(BlueprintPure, Category="Shooter|Stats")
	uint8 GetTeamId() const { return TeamId; }

	UFUNCTION(BlueprintPure, Category="Shooter|Stats")
	int32 GetKills() const { return Kills; }

	UFUNCTION(BlueprintPure, Category="Shooter|Stats")
	int32 GetDeaths() const { return Deaths; }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 随 PlayerState 复制到客户端的玩家能力系统组件。 */
	UPROPERTY(VisibleAnywhere, Category="Abilities")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(ReplicatedUsing=OnRep_TeamId, VisibleAnywhere, Category="Shooter|Stats")
	uint8 TeamId = 0;

	UPROPERTY(ReplicatedUsing=OnRep_CombatStats, VisibleAnywhere, Category="Shooter|Stats")
	int32 Kills = 0;

	UPROPERTY(ReplicatedUsing=OnRep_CombatStats, VisibleAnywhere, Category="Shooter|Stats")
	int32 Deaths = 0;

	UFUNCTION()
	void OnRep_TeamId();

	UFUNCTION()
	void OnRep_CombatStats();
};
