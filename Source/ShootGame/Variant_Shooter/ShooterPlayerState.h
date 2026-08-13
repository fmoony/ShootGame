// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "ShooterPlayerState.generated.h"

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
 */
UCLASS()
class SHOOTGAME_API AShooterPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
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
