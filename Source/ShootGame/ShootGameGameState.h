// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "ShootGameGameState.generated.h"

/**
 * 
 */
UCLASS()
class SHOOTGAME_API AShootGameGameState : public AGameStateBase
{
	GENERATED_BODY()
	
public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void AddMatchCounter(int32 Delta);

	UFUNCTION(BlueprintPure, Category = "Network")
	int32 GetMatchCounter() const { return MatchCounter; }

private:
	UPROPERTY(ReplicatedUsing = OnRep_MatchCounter, VisibleAnywhere, Category = "Network")
	int32 MatchCounter = 0;

	UFUNCTION()
	void OnRep_MatchCounter();

	void LogMatchCounter(const TCHAR* Source) const;
};
