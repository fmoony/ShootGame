// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "ShooterGameState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FTeamScoreChangedDelegate,
	uint8,
	TeamId,
	int32,
	Score);

/**
 * 射击模式的全局比赛状态。
 * 服务器修改队伍分数，所有客户端通过复制获得相同结果。
 */
UCLASS()
class SHOOTGAME_API AShooterGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	/** 任意队伍分数变化时广播，供本地 PlayerController 更新 HUD。 */
	UPROPERTY(BlueprintAssignable, Category="Shooter|Score")
	FTeamScoreChangedDelegate OnTeamScoreChanged;

	/** 服务器为指定队伍加一分。 */
	void AddTeamScore(uint8 TeamId);

	/** 读取指定队伍的当前分数。 */
	UFUNCTION(BlueprintPure, Category="Shooter|Score")
	int32 GetTeamScore(uint8 TeamId) const;

	/** 返回当前已经出现过的队伍数量。 */
	int32 GetTeamCount() const { return TeamScores.Num(); }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing=OnRep_TeamScores, VisibleAnywhere, Category="Shooter|Score")
	TArray<int32> TeamScores;

	UFUNCTION()
	void OnRep_TeamScores();
};
