// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ShooterGameMode.generated.h"

class APlayerController;

/**
 *  Simple GameMode for a first person shooter game
 *  Assigns teams and applies server-only match rules
 */
UCLASS(abstract)
class SHOOTGAME_API AShooterGameMode : public AGameModeBase
{
	GENERATED_BODY()
	
public:
	AShooterGameMode();

protected:

	/** Creates an owning test coordinator when explicitly requested by command line. */
	virtual void PostLogin(APlayerController* NewPlayer) override;

public:

	/** Increases the score for the given team */
	void IncrementTeamScore(uint8 TeamId);
};
