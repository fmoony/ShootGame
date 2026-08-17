// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShooterNPC.h"
#include "ShooterNetworkTestCoordinator.generated.h"

class AShooterCharacter;
class AShooterWeapon;
class AShooterPlayerState;
class UAbilitySystemComponent;

/**
 * 仅用于网络测试的 NPC 子类：验证 ShooterNPC C++ 基类的 ASC 生命周期
 * （Owner = Avatar = NPC），避免依赖 BP_ShooterNPC 的自动占有与武器配置。
 */
UCLASS(NotBlueprintable, Transient)
class AShooterNetworkTestNPC : public AShooterNPC
{
	GENERATED_BODY()
};

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
	void ServerReportClientObservedRespawn();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedMatchState(
		uint8 TeamId,
		int32 Kills,
		int32 Deaths,
		int32 TeamScore);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedRemoteAim(float PitchN, float ExpectedPitchN);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedRemoteMontage();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedGasLifecycle();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedGasRespawn();

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

	UPROPERTY(Replicated)
	bool bRequireRemoteMontage = true;

	float TestStartTime = 0.0f;
	int32 InitialBulletCount = INDEX_NONE;
	int32 BulletCountAfterFire = INDEX_NONE;
	bool bClientObservedWeapon = false;
	bool bClientObservedProjectile = false;
	bool bClientObservedSwitch = false;
	bool bClientObservedOwnerAmmo = false;
	bool bClientObservedNonOwnerAmmoHidden = false;
	bool bClientObservedDamage = false;
	bool bClientObservedDeath = false;
	bool bClientObservedRespawn = false;
	bool bClientObservedMatchState = false;
	bool bClientObservedRemoteAim = false;
	bool bClientObservedRemoteMontage = false;
	bool bClientTriggeredFire = false;
	bool bClientTriggeredSwitch = false;
	bool bClientReportedProjectile = false;
	bool bClientReportedSwitch = false;
	bool bClientReportedOwnerAmmo = false;
	bool bClientReportedNonOwnerAmmoHidden = false;
	bool bClientReportedDamage = false;
	bool bClientReportedDeath = false;
	bool bClientReportedRespawn = false;
	bool bClientReportedMatchState = false;
	bool bClientSetAimPitch = false;
	bool bClientReportedRemoteAim = false;
	bool bClientReportedRemoteMontage = false;
	bool bServerGasLifecycleChecked = false;
	bool bServerGasOwnerOk = false;
	bool bServerGasAvatarOk = false;
	bool bServerGasConnectionOk = false;
	bool bNpcGasLifecycleChecked = false;
	bool bNpcGasLifecycleOk = false;
	bool bServerGasRespawnChecked = false;
	bool bServerGasRespawnOk = false;
	bool bClientObservedGasLifecycle = false;
	bool bClientObservedGasRespawn = false;
	bool bClientReportedGasLifecycle = false;
	bool bClientReportedGasRespawn = false;
	TWeakObjectPtr<UAbilitySystemComponent> ObservedAbilitySystemComponent;
	bool bServerObservedProjectile = false;
	bool bAimDirectionValid = false;
	bool bPartialDamageApplied = false;
	bool bLethalDamageApplied = false;
	bool bSecondaryWeaponGranted = false;
	bool bOpponentKilledForStats = false;
	bool bDisconnectCleanupMode = false;
	int32 InitialClientBulletCount = INDEX_NONE;
	float InitialHP = 0.0f;
	float ObservedAimDot = -1.0f;
	uint8 ObservedTeamId = 0;
	int32 ObservedKills = 0;
	int32 ObservedDeaths = 0;
	int32 ObservedTeamScore = 0;
	float ObservedRemotePitchN = 0.0f;
	float ExpectedRemotePitchN = 0.0f;
	TWeakObjectPtr<AShooterWeapon> InitialClientWeapon;
	TWeakObjectPtr<AShooterCharacter> CharacterBeforeDeath;
};
