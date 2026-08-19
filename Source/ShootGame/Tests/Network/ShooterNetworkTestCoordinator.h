// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShooterNPC.h"
#include "ShooterWeapon.h"
#include "ShooterNetworkTestCoordinator.generated.h"

class AShooterCharacter;
class AShooterWeapon;
class AShooterPlayerState;
class UAbilitySystemComponent;
struct FOnAttributeChangeData;

/**
 * 仅用于网络测试的 NPC 子类：验证 ShooterNPC C++ 基类的 ASC 生命周期
 * （Owner = Avatar = NPC），避免依赖 BP_ShooterNPC 的自动占有与武器配置。
 */
UCLASS(NotBlueprintable, Transient)
class AShooterNetworkTestNPC : public AShooterNPC
{
	GENERATED_BODY()
};

/** 仅用于 SlotFull 测试的额外武器类；不参与开火，只验证 Inventory 授予路径。 */
UCLASS(NotBlueprintable, Transient)
class AShooterNetworkTestWeapon : public AShooterWeapon
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

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedInventory(
		int32 WeaponCount,
		const FString& ActiveWeaponInstanceId,
		bool bRemoteInventoryHidden);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedPickupAuthority();

	/** 记录角色 OnDamaged 事件值（HUD 事件链证据）。 */
	UFUNCTION()
	void HandleDamagedEvent(float LifePercent);

	/** 记录 ASC Health 属性变化（HUD 事件链源头，跨重生无竞态）。 */
	void HandleClientHealthAttributeChanged(const FOnAttributeChangeData& ChangeData);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedGasHealthInit();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedGasHealthDamage();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedGasHealthRespawn(bool bFullHealthHudEvent);

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
	bool bNpcAiSuppressed = false;
	bool bServerGasRespawnChecked = false;
	bool bServerGasRespawnOk = false;
	bool bClientObservedGasLifecycle = false;
	bool bClientObservedGasRespawn = false;
	bool bClientReportedGasLifecycle = false;
	bool bClientReportedGasRespawn = false;
	bool bServerGasHealthInitChecked = false;
	bool bServerGasHealthInitOk = false;
	float InitialAttributeHealth = 0.0f;
	float ExpectedPartialHealth = 0.0f;
	bool bServerGasDamageChecked = false;
	bool bServerGasDamageOk = false;
	bool bServerGasDeathChecked = false;
	bool bServerGasDeathOk = false;
	bool bNpcGasHealthInitOk = false;
	bool bNpcGasDeathOk = false;
	bool bClientObservedGasHealthInit = false;
	bool bClientObservedGasHealthDamage = false;
	bool bClientObservedGasHealthRespawn = false;
	bool bClientReportedGasHealthInit = false;
	bool bClientReportedGasHealthDamage = false;
	bool bClientReportedGasHealthRespawn = false;
	bool bClientObservedFullHealthHudEvent = false;

	/** Inventory 2A 观测：服务器插入两把测试武器，Owner 完整收到，远端不收到完整列表。 */
	bool bServerInventoryPrepared = false;
	bool bClientObservedPickupAuthority = false;
	bool bClientReportedPickupAuthority = false;
	FGuid ServerInventoryFirstId;
	FGuid ServerInventorySecondId;
	FGuid ServerInventoryActiveId;
	bool bClientObservedOwnerInventory = false;
	bool bClientReportedInventory = false;
	bool bClientObservedRemoteInventoryHidden = false;

	float LastDamagedLifePercent = -1.0f;
	float LastClientAttributeHealth = -1.0f;
	float ClientMaxHealthAttributeValue = 0.0f;
	bool bClientHealthAttributeDelegateBound = false;
	TWeakObjectPtr<AShooterCharacter> HudBoundCharacter;
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
