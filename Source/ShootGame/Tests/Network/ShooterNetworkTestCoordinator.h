// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayAbilitySpecHandle.h"
#include "ShooterNPC.h"
#include "ShooterWeapon.h"
#include "ShooterNetworkTestCoordinator.generated.h"

class AShooterCharacter;
class AShooterWeapon;
class AShooterPlayerState;
class UAbilitySystemComponent;
class UShooterGameplayAbility_Equip;
class UShooterGameplayAbility_Reload;
struct FOnAttributeChangeData;

/**
 * 仅用于网络测试的 NPC 子类：验证 ShooterNPC C++ 基类的 ASC 生命周期
 * （Owner = Avatar = NPC），避免依赖 BP_ShooterNPC 的自动占有与武器配置。
 */
UCLASS(NotBlueprintable, Transient)
class AShooterNetworkTestNPC : public AShooterNPC
{
	GENERATED_BODY()

public:
	AShooterNetworkTestNPC();
};

/** 仅用于 SlotFull 测试的额外武器类；不参与开火，只验证 Inventory 授予路径。 */
UCLASS(NotBlueprintable, Transient)
class AShooterNetworkTestWeapon : public AShooterWeapon
{
	GENERATED_BODY()

public:
	AShooterNetworkTestWeapon();
};

/** 5B 换弹网络测试武器：固定 30 发容量与 1.5s ReloadDuration，在弱网取消窗口与阶段时长间取得平衡。 */
UCLASS(NotBlueprintable, Transient)
class AShooterNetworkTestReloadWeapon : public AShooterNetworkTestWeapon
{
	GENERATED_BODY()

public:
	AShooterNetworkTestReloadWeapon();
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
	void ServerReportClientObservedSwitch(
		const FString& ActiveWeaponInstanceId,
		const FString& CurrentWeaponBoundInstanceId,
		bool bRemoteCurrentWeaponVisible);

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
	void ServerReportClientObservedFireAbilityGrant(
		int32 OwnerFireSpecCount,
		bool bRemoteFireSpecsHidden);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedReloadEquipAbilityGrant(
		int32 OwnerReloadSpecCount,
		bool bRemoteReloadSpecsHidden,
		int32 OwnerEquipSpecCount,
		bool bRemoteEquipSpecsHidden);

	UFUNCTION(Server, Reliable)
	void ServerReportClientTriggeredReload(int32 RequestId);

	UFUNCTION(Server, Reliable)
	void ServerReportClientTriggeredReloadSwitch();

	UFUNCTION(Server, Reliable)
	void ServerReportClientTriggeredReloadSwitchBack();

	UFUNCTION(Server, Reliable)
	void ServerReportClientTriggeredFireAfterReload();

	UFUNCTION(Server, Reliable)
	void ServerReportClientStoppedFireAfterReload();

	UFUNCTION(Server, Reliable)
	void ServerReportClientTriggeredEquipSingleReject();

	UFUNCTION(Server, Reliable)
	void ServerReportFullAutoReleased(int32 BulletCountAfterRelease);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedCancelSwitch(
		const FString& CurrentWeaponBoundInstanceId);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedGasRespawn();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedInventory(
		int32 WeaponCount,
		const FString& ActiveWeaponInstanceId,
		bool bRemoteInventoryHidden);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedPickupAuthority();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedInventoryDeathClear();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedInventoryRespawnEmpty();

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
	int32 CountProjectilesForInstigator(APawn* ProjectileInstigator) const;
	AController* GetOpponentController() const;

	/** 5B 测试辅助：用指定弹药数据替换 Inventory 实例并生成测试 WeaponActor。 */
	bool ReplaceInventoryWeaponForReloadTest(
		AShooterCharacter* Character,
		const FGuid& InstanceId,
		int32 SlotIndex,
		int32 MagazineAmmo,
		int32 ReserveAmmo);

	/** 5B 测试辅助：返回当前 PlayerState 是否有一个活动 GA_Fire。 */
	bool HasActiveFireAbility(AShooterCharacter* Character) const;

	/** 5C 测试辅助：返回当前 PlayerState 是否有一个活动 GA_Equip。 */
	bool HasActiveEquipAbility(AShooterCharacter* Character) const;

	/** 5B 测试辅助：返回当前 PlayerState 是否有一个活动 GA_Reload。 */
	bool HasActiveReloadAbility(AShooterCharacter* Character) const;

	/** DisconnectCleanup 专用：在断线前主动激活一次 GA_Reload。 */
	void TriggerDisconnectReload();

	/** DisconnectCleanup 专用：延长目标武器 EquipDuration 后激活 GA_Equip。 */
	bool TriggerLongEquip(AShooterCharacter* Character, const TCHAR* Context);

	/** Equip 清理会话：一名玩家保持活动 GA_Equip，等待脚本主动断线。 */
	void TriggerDisconnectEquip();

	/** Equip 清理会话：另一名玩家在活动 GA_Equip 提交前受到致死伤害。 */
	void TriggerEquipDeath();
	void VerifyEquipDeathCleanup();

	FTimerHandle PollTimer;
	FTimerHandle CleanupAbilityTimer;
	FTimerHandle EquipDeathVerifyTimer;
	bool bCleanupAbilityScheduled = false;
	FDelegateHandle ActorSpawnedHandle;

	UPROPERTY(Replicated)
	int32 ReloadInputRequestId = 0;

	UPROPERTY(Replicated)
	bool bServerReadyForReloadSwitch = false;

	UPROPERTY(Replicated)
	bool bServerReadyForReloadSwitchBack = false;

	UPROPERTY(Replicated)
	bool bServerReadyForFireAfterReload = false;

	UPROPERTY(Replicated)
	bool bServerReadyForStopFireAfterReload = false;

	UPROPERTY(Replicated)
	bool bServerReadyForEquipSingleReject = false;

	UPROPERTY(Replicated)
	bool bServerReadyToSwitch = false;

	UPROPERTY(Replicated)
	bool bServerReadyToFire = false;

	UPROPERTY(Replicated)
	bool bServerReadyForFullAuto = false;

	UPROPERTY(Replicated)
	bool bServerReadyForSwitchCancel = false;

	UPROPERTY(Replicated)
	TObjectPtr<AShooterWeapon> WeaponBeforeSwitch;

	UPROPERTY(Replicated)
	bool bRequireRemoteMontage = true;

	UPROPERTY(Replicated)
	bool bRequireRemoteCurrentWeapon = true;

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
	int32 LastObservedReloadInputRequestId = 0;
	bool bClientTriggeredReloadSwitch = false;
	bool bClientTriggeredReloadSwitchBack = false;
	bool bClientReportedProjectile = false;
	bool bClientTriggeredFireAfterReload = false;
	bool bClientStoppedFireAfterReload = false;
	bool bClientTriggeredEquipSingleReject = false;
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

	/** 4A 观测：服务器 / NPC / 重生三个生命周期中 Fire Ability Spec 均有且只有一个。 */
	bool bServerFireGrantChecked = false;
	bool bServerFireGrantOk = false;
	bool bNpcFireGrantOk = false;
	bool bServerFireRespawnGrantOk = false;
	bool bClientObservedFireGrant = false;
	bool bClientReportedFireGrant = false;
	FGameplayAbilitySpecHandle ServerFireAbilityHandle;
	int32 ServerFireAbilityCount = INDEX_NONE;

	/** 5A 观测：玩家 Reload / Equip Ability 在出生与重生后均有且只有一个 Spec，重复授予不增长。 */
	bool bServerReloadEquipGrantChecked = false;
	bool bServerReloadEquipGrantOk = false;
	bool bServerReloadEquipRespawnGrantOk = false;
	bool bClientObservedReloadEquipGrant = false;
	bool bClientReportedReloadEquipGrant = false;
	FGameplayAbilitySpecHandle ServerReloadAbilityHandle;
	FGameplayAbilitySpecHandle ServerEquipAbilityHandle;
	int32 ServerReloadAbilityCount = INDEX_NONE;
	int32 ServerEquipAbilityCount = INDEX_NONE;

	/** 5C 观测：GA_Equip 在初始切枪、取消 Reload 切枪与切回阶段均被服务器激活，提交后 InstanceId 与 CurrentWeapon 一致。 */
	bool bEquipInitialCommitConsistent = false;
	bool bEquipCancelReloadActiveObserved = false;
	bool bEquipSwitchBackActiveObserved = false;

	bool bEquipSingleRejectPhaseTriggered = false;
	bool bEquipSingleRejectVerified = false;
	float EquipSingleRejectCheckTime = 0.0f;
	bool bEquipRejectDeadVerified = false;

	/** 5B 观测：FullMagazine / Transfer / EquipCancel / NoReserve / DeathCancel 五个换弹事务边界。 */
	bool bReloadFullRejectPhaseTriggered = false;
	bool bReloadFullRejectVerified = false;
	bool bClientTriggeredReload = false;
	int32 ReloadMagazineBeforeFullReject = INDEX_NONE;
	int32 ReloadReserveBeforeFullReject = INDEX_NONE;
	float ReloadFullRejectCheckTime = 0.0f;

	bool bReloadTransferPhaseTriggered = false;
	bool bReloadTransferActiveObserved = false;
	bool bReloadTransferVerified = false;
	int32 ReloadMagazineBeforeTransfer = INDEX_NONE;
	int32 ReloadReserveBeforeTransfer = INDEX_NONE;
	int32 ExpectedReloadTransfer = INDEX_NONE;
	int32 ReloadMagazineAfterTransfer = INDEX_NONE;
	int32 ReloadReserveAfterTransfer = INDEX_NONE;
	float ReloadTransferCheckTime = 0.0f;

	/** 5B 弱网 Fire-after-Reload：Reload 完成后单次 Fire 必须到达服务器且只激活 / 射击 / 扣弹一次。 */
	bool bFireAfterReloadPhaseTriggered = false;
	bool bFireAfterReloadActiveObserved = false;
	bool bFireAfterReloadSingleShotVerified = false;
	bool bClientTriggeredStopFireAfterReload = false;
	bool bFireAfterReloadQuiescentVerified = false;
	int32 FireAfterReloadMagazineBefore = INDEX_NONE;
	int32 FireAfterReloadProjectileBefore = INDEX_NONE;
	float FireAfterReloadQuiescenceCheckTime = 0.0f;

	bool bReloadCancelEquipPhaseTriggered = false;
	bool bReloadCancelEquipActiveObserved = false;
	bool bReloadCancelEquipVerified = false;
	int32 ReloadMagazineBeforeCancelEquip = INDEX_NONE;
	int32 ReloadReserveBeforeCancelEquip = INDEX_NONE;

	bool bReloadSwitchBackPhaseTriggered = false;
	bool bReloadSwitchBackVerified = false;
	float ReloadCancelEquipCheckTime = 0.0f;
	float ReloadSwitchBackCheckTime = 0.0f;

	bool bReloadNoReservePhaseTriggered = false;
	bool bReloadNoReserveVerified = false;
	int32 ReloadMagazineBeforeNoReserve = INDEX_NONE;
	int32 ReloadReserveBeforeNoReserve = INDEX_NONE;
	float ReloadNoReserveCheckTime = 0.0f;

	bool bReloadCancelDeathPhaseTriggered = false;
	bool bReloadCancelDeathActiveObserved = false;
	bool bReloadCancelDeathAmmoUnchanged = false;
	bool bReloadCancelDeathVerified = false;
	int32 ReloadMagazineBeforeCancelDeath = INDEX_NONE;
	int32 ReloadReserveBeforeCancelDeath = INDEX_NONE;

	/** 4B 观测：单次按下只生成一颗弹丸，全自动保持期间只有一个活动 GA_Fire，释放后计时器无残留。 */
	int32 ProjectileSpawnCount = 0;
	bool bSingleProjectileVerified = false;
	bool bFullAutoPhaseTriggered = false;
	bool bFullAutoActiveObserved = false;
	bool bClientReportedFullAutoRelease = false;
	bool bFullAutoReleaseVerified = false;
	bool bFullAutoQuiescentConfirmed = false;
	bool bClientTriggeredFullAuto = false;
	bool bClientStoppedFullAuto = false;
	bool bClientReportedFullAuto = false;
	float FullAutoReleaseWaitStartTime = 0.0f;
	int32 BulletCountBeforeFullAuto = INDEX_NONE;
	int32 ProjectileCountBeforeFullAuto = INDEX_NONE;
	int32 ProjectileCountAfterRelease = INDEX_NONE;
	int32 AmmoAfterRelease = INDEX_NONE;
	int32 ClientBulletCountAfterRelease = INDEX_NONE;
	float FullAutoReleaseCheckTime = 0.0f;

	/** 4C 观测：死亡 / 无武器 / 无弹药拒绝、切枪取消、重生 Tag 清理与 NPC Ability 链路。 */
	bool bSwitchCancelPhaseTriggered = false;
	bool bSwitchCancelActiveObserved = false;
	bool bClientObservedSwitchCancel = false;
	bool bClientReportedSwitchCancel = false;
	bool bSwitchCancelVerified = false;
	bool bSwitchCancelQuiescentConfirmed = false;
	bool bNoAmmoRejectVerified = false;
	bool bFireRejectDeadVerified = false;
	bool bFireRejectNoWeaponVerified = false;
	bool bRespawnTagCleanupVerified = false;
	bool bClientTriggeredSwitchCancel = false;
	bool bClientSwitchCancelRequested = false;
	TWeakObjectPtr<AShooterWeapon> ClientWeaponBeforeSwitchCancel;
	int32 BulletCountBeforeClientSwitchCancel = INDEX_NONE;
	int32 ProjectileCountBeforeSwitchCancel = INDEX_NONE;
	int32 RifleAmmoBeforeSwitchCancel = INDEX_NONE;
	int32 ProjectileCountAfterSwitchCancel = INDEX_NONE;
	int32 RifleAmmoAfterSwitchCancel = INDEX_NONE;
	float SwitchCancelCheckTime = 0.0f;

	bool bNpcFireActivated = false;
	bool bNpcFireStopOk = false;
	bool bNpcFireQuiescenceConfirmed = false;
	int32 NpcProjectileCountAtStop = INDEX_NONE;
	float NpcFireStopCheckTime = 0.0f;
	TWeakObjectPtr<AShooterNPC> NpcFireTestNpc;

	/** Inventory 2A 观测：服务器插入两把测试武器，Owner 完整收到，远端不收到完整列表。 */
	bool bServerInventoryPrepared = false;
	bool bClientObservedPickupAuthority = false;
	bool bClientReportedPickupAuthority = false;
	int32 InitialRifleMagazineAmmo = INDEX_NONE;
	int32 InitialPistolMagazineAmmo = INDEX_NONE;
	bool bAmmoIsolationVerified = false;
	bool bServerDeathInventoryCleared = false;
	bool bClientObservedDeathInventoryClear = false;
	bool bClientReportedDeathInventoryClear = false;
	bool bServerRespawnInventoryEmpty = false;
	bool bClientObservedRespawnInventoryEmpty = false;
	bool bClientReportedRespawnInventoryEmpty = false;
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
	bool bDisconnectEquipMode = false;
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
