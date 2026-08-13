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
	void ServerReportClientObservedSwitch();

	UFUNCTION(Server, Reliable)
	void ServerReportOwnerAmmoReplicated();

	UFUNCTION(Server, Reliable)
	void ServerReportNonOwnerAmmoHidden();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedDamage();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedDeath();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedMatchState(
		uint8 TeamId,
		int32 Kills,
		int32 Deaths,
		int32 TeamScore);

	AShooterCharacter* GetShooterCharacter() const;
	AShooterWeapon* GetCurrentWeapon(AShooterCharacter* Character) const;
	AController* GetOpponentController() const;

	FTimerHandle PollTimer;
	FDelegateHandle ActorSpawnedHandle;

	UPROPERTY(Replicated)
	bool bServerReadyToSwitch = false;

	UPROPERTY(Replicated)
	bool bServerReadyToFire = false;

	UPROPERTY(Replicated)
	TObjectPtr<AShooterWeapon> WeaponBeforeSwitch;

	float TestStartTime = 0.0f;
	int32 InitialBulletCount = INDEX_NONE;
	bool bClientObservedWeapon = false;
	bool bClientObservedProjectile = false;
	bool bClientObservedSwitch = false;
	bool bClientObservedOwnerAmmo = false;
	bool bClientObservedNonOwnerAmmoHidden = false;
	bool bClientObservedDamage = false;
	bool bClientObservedDeath = false;
	bool bClientObservedMatchState = false;
	bool bClientTriggeredFire = false;
	bool bClientTriggeredSwitch = false;
	bool bClientReportedProjectile = false;
	bool bClientReportedSwitch = false;
	bool bClientReportedOwnerAmmo = false;
	bool bClientReportedNonOwnerAmmoHidden = false;
	bool bClientReportedDamage = false;
	bool bClientReportedDeath = false;
	bool bClientReportedMatchState = false;
	bool bServerObservedProjectile = false;
	bool bAimDirectionValid = false;
	bool bPartialDamageApplied = false;
	bool bLethalDamageApplied = false;
	bool bSecondaryWeaponGranted = false;
	int32 InitialClientBulletCount = INDEX_NONE;
	float InitialHP = 0.0f;
	float ObservedAimDot = -1.0f;
	uint8 ObservedTeamId = 0;
	int32 ObservedKills = 0;
	int32 ObservedDeaths = 0;
	int32 ObservedTeamScore = 0;
	TWeakObjectPtr<AShooterWeapon> InitialClientWeapon;
};
