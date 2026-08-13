// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShooterNetworkTestCoordinator.generated.h"

class AShooterCharacter;
class AShooterWeapon;

/**
 * Drives one owning client through the server-authoritative weapon fire path.
 * Spawned only when the server is launched with -ShootGameNetworkTest.
 */
UCLASS(NotBlueprintable, Transient)
class SHOOTGAME_API AShooterNetworkTestCoordinator : public AActor
{
	GENERATED_BODY()

public:
	AShooterNetworkTestCoordinator();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	void PollServerState();
	void PollClientState();
	void HandleActorSpawned(AActor* SpawnedActor);
	void FailTest(const FString& Reason);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedWeapon();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedProjectile();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedDamage();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedDeath();

	AShooterCharacter* GetShooterCharacter() const;
	AShooterWeapon* GetCurrentWeapon(AShooterCharacter* Character) const;

	FTimerHandle PollTimer;
	FDelegateHandle ActorSpawnedHandle;

	UPROPERTY(Replicated)
	bool bServerReadyToFire = false;

	float TestStartTime = 0.0f;
	int32 InitialBulletCount = INDEX_NONE;
	bool bClientObservedWeapon = false;
	bool bClientObservedProjectile = false;
	bool bClientObservedDamage = false;
	bool bClientObservedDeath = false;
	bool bClientTriggeredFire = false;
	bool bClientReportedProjectile = false;
	bool bClientReportedDamage = false;
	bool bClientReportedDeath = false;
	bool bServerObservedProjectile = false;
	bool bAimDirectionValid = false;
	bool bPartialDamageApplied = false;
	bool bLethalDamageApplied = false;
	float InitialHP = 0.0f;
	float ObservedAimDot = -1.0f;
};
