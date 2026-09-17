// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayAbilitySpecHandle.h"
#include "AI/ShooterNPC.h"
#include "Weapons/ShooterWeapon.h"
#include "ShooterNetworkTestCoordinator.generated.h"

class AShooterCharacter;
class AShooterWeapon;
class AShooterPlayerState;
class UAbilitySystemComponent;
class UBoxComponent;
class USkeletalMeshComponent;
class UShooterGameplayAbility_Equip;
class UShooterGameplayAbility_Fire;
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
	virtual void Tick(float DeltaSeconds) override;
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
	void ServerReportClientObservedSwitch(AShooterWeapon* ActiveWeapon, AShooterWeapon* CurrentWeapon, bool bRemoteCurrentWeaponVisible);

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
	void ServerReportClientObservedMatchState(uint8 TeamId, int32 Kills, int32 Deaths, int32 TeamScore);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedRemoteAim(float PitchN, float ExpectedPitchN);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedGasLifecycle();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedFireAbilityGrant(int32 OwnerFireSpecCount, bool bRemoteFireSpecsHidden);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedReloadEquipAbilityGrant(int32 OwnerReloadSpecCount, bool bRemoteReloadSpecsHidden,
		int32 OwnerEquipSpecCount, bool bRemoteEquipSpecsHidden);

	UFUNCTION(Server, Reliable)
	void ServerReportClientTriggeredReload(int32 RequestId);

	UFUNCTION(Server, Reliable)
	void ServerReportClientTriggeredReloadSwitch();

	UFUNCTION(Server, Reliable)
	void ServerReportClientTriggeredReloadSwitchBack();

	UFUNCTION(Server, Reliable)
	void ServerReportClientTriggeredFireAfterReload(bool bReloadingTagPresentAtInput);

	UFUNCTION(Server, Reliable)
	void ServerReportClientStoppedFireAfterReload(int32 OwnerFeedbackDelta, int32 OwnerConfirmationDelta);

	UFUNCTION(Server, Reliable)
	void ServerReportClientTriggeredEquipSingleReject();

	/**
	 * P1 换弹中开火报告：客户端在换弹状态下按下开火后回报本地观测。
	 * FireCase：1 = 客户端已知 State.Reloading（8A，要求本地预测增量为 0）；
	 *           2 = 客户端尚未收到 State.Reloading（8B，允许有限的纯本地预测表现）。
	 */
	UFUNCTION(Server, Reliable)
	void ServerReportReloadFireResult(int32 RequestId, int32 FireCase, int32 PredictedDelta,
		bool bKnownBlockerObserved, bool bTargetStable, bool bClientConverged);

	UFUNCTION(Server, Reliable)
	void ServerReportFullAutoReleased(int32 BulletCountAfterRelease, int32 OwnerFeedbackDelta,
		float MinimumFeedbackInterval, bool bLocalTimerStopped, bool bTargetStable);

	/**
	 * Invariant 2 半自动快速连点证据：客户端在真实按下 / 释放输入下跑完一整轮连点后上报本机观测。
	 * 服务器用同窗口的权威弹药、弹丸与权威射击增量做一一对应比较，
	 * 并要求本地可见表现的最小间隔不低于武器 RefireRate。
	 */
	UFUNCTION(Server, Reliable)
	void ServerReportSemiAutoRapidClick(int32 ClicksAttempted, int32 OwnerFeedbackDelta,
		float MinimumFeedbackInterval, float RefireRate, bool bTargetStable);

	/**
	 * 半自动连点窗口的分通道表现证据。
	 * 聚合计数只能证明"至少提交了一项表现"，无法发现枪口 / 声音 / 后坐力 / Montage 中
	 * 某一路在本机静默失效；因此四路各自上报增量，服务器逐路断言。
	 */
	UFUNCTION(Server, Reliable)
	void ServerReportSemiAutoRapidClickChannels(int32 MontageDelta, int32 MuzzleDelta, int32 SoundDelta,
		int32 RecoilDelta, bool bTargetStable);

	UFUNCTION(Server, Reliable)
	void ServerReportOwnerAcceptedShotEvidence(
		int32 OwnerFeedbackDelta,
		int32 MontageDelta,
		int32 MuzzleDelta,
		int32 SoundDelta,
		int32 RecoilDelta,
		int32 ConfirmationDelta,
		bool bFeedbackBeforeConfirmation,
		bool bTargetStable);

	/**
	 * P1-D 远端第三人称确认表现证据：观测端上报“另一名玩家武器”的确认表现增量。
	 * 观测源是生产计数器 AShooterWeapon::RemoteConfirmedFeedbackCount，不在协调器里复制实现。
	 */
	UFUNCTION(Server, Reliable)
	void ServerReportRemoteConfirmedFeedback(int32 Count, int32 MontageCount, int32 MuzzleCount, int32 SoundCount, bool bTargetStable);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedCancelSwitch(AShooterWeapon* CurrentWeapon);

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedGasRespawn();

	UFUNCTION(Server, Reliable)
	void ServerReportClientObservedInventory(int32 WeaponCount, AShooterWeapon* ActiveWeapon,
		bool bRemoteInventoryHidden, bool bInventoryComponentInitialized);

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

	/** P1-D 观测端：解析另一名玩家当前装备的武器，作为第三人称确认表现的观测源。 */
	AShooterWeapon* FindRemoteObservedWeapon(AShooterCharacter* LocalCharacter) const;

	/** P1-D 服务器侧：解析对手玩家，用于读取其武器在同一窗口内的权威射击计数。 */
	AShooterCharacter* GetOpponentCharacter() const;

	/** 5B 测试辅助：把指定 WeaponActor 的权威弹药直接设置为测试起点值。 */
	bool SetReloadTestAmmo(AShooterWeapon* Weapon, int32 MagazineAmmo, int32 ReserveAmmo);

	/** 5B 测试辅助：返回当前 PlayerState 是否有一个活动 GA_Fire。 */
	bool HasActiveFireAbility(AShooterCharacter* Character) const;
	const UShooterGameplayAbility_Fire* GetFireAbilityInstanceForTest(AShooterCharacter* Character) const;

	/**
	 * 4C 测试辅助：查询服务器对 GA_Fire 的权威激活结论。
	 * GA_Fire 为 LocalPredicted 后，服务器对非本机控制玩家调用公开 TryActivateAbility
	 * 会经 bAllowRemoteActivation 把请求转回拥有者客户端并返回 true，拿不到校验结论；
	 * 因此直接调用服务器实例的 CanActivateAbility。返回 true 表示服务器允许激活。
	 */
	bool CanServerActivateFireAbility(UAbilitySystemComponent* AbilitySystemComponent) const;

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

	/** P1 换弹中开火阶段：0=未开始，2=同帧换弹加开火（8B），1=等到本地 State.Reloading 再开火（8A）。 */
	UPROPERTY(Replicated)
	int32 ReloadFirePhase = 0;

	UPROPERTY(Replicated)
	int32 ReloadFireRequestId = 0;

	UPROPERTY(Replicated)
	bool bServerReadyToSwitch = false;

	UPROPERTY(Replicated)
	bool bServerReadyToFire = false;

	UPROPERTY(Replicated)
	bool bServerReadyForFullAuto = false;

	/** 半自动快速连点阶段的起跑许可：客户端收到后开始真实连点输入。 */
	UPROPERTY(Replicated)
	bool bServerReadyForSemiAutoRapidClick = false;

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
	bool bClientReportedOwnerAcceptedShot = false;
	float ClientOwnerAcceptedShotStartTime = 0.0f;
	/** 拥有者本地动作互斥导致本次开火输入等待的起始时间；0 表示没有等待。 */
	float ClientOwnerAcceptedShotLocalWaitStart = 0.0f;
	TWeakObjectPtr<AShooterWeapon> ClientOwnerAcceptedShotWeapon;
	int32 ClientOwnerFeedbackBefore = INDEX_NONE;
	int32 ClientOwnerMontageBefore = INDEX_NONE;
	int32 ClientOwnerMuzzleBefore = INDEX_NONE;
	int32 ClientOwnerSoundBefore = INDEX_NONE;
	int32 ClientOwnerRecoilBefore = INDEX_NONE;
	int32 ClientOwnerConfirmationBefore = INDEX_NONE;
	bool bClientTriggeredSwitch = false;
	int32 LastObservedReloadInputRequestId = 0;
	bool bClientTriggeredReloadSwitch = false;
	bool bClientTriggeredReloadSwitchBack = false;
	bool bClientReportedProjectile = false;
	bool bClientTriggeredFireAfterReload = false;
	bool bClientStoppedFireAfterReload = false;
	int32 FireAfterReloadOwnerFeedbackBefore = 0;
	int32 FireAfterReloadOwnerConfirmationBefore = 0;
	float FireAfterReloadStopReadyTime = 0.0f;
	bool bClientTriggeredEquipSingleReject = false;

	// ---- P1 换弹中开火：客户端侧跟踪 ----
	int32 LastObservedReloadFirePhase = 0;
	bool bClientReloadFireInputSent = false;
	bool bClientReloadFireTagSeen = false;
	bool bClientReportedReloadFire = false;
	float ClientReloadFireSettleTime = 0.0f;
	int32 ClientReloadFirePredictedBefore = 0;
	TWeakObjectPtr<AShooterWeapon> ClientReloadFireTargetWeapon;
	bool bClientReportedSwitch = false;
	bool bClientReportedOwnerAmmo = false;
	bool bClientReportedNonOwnerAmmoHidden = false;
	bool bClientReportedDamage = false;
	bool bClientReportedDeath = false;
	bool bClientReportedRespawn = false;
	bool bClientReportedMatchState = false;
	bool bClientSetAimPitch = false;
	bool bClientReportedRemoteAim = false;
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

	/**
	 * 5C 观测：GA_Equip 在初始切枪、取消 Reload 切枪与切回阶段均被服务器激活，
	 * 提交后 CurrentWeaponActor 与 Inventory Entry 一致。
	 */
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
	bool bFireAfterReloadStaleTagObserved = false;
	bool bFireAfterReloadOwnerFeedbackVerified = false;
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
	bool bOwnerAcceptedShotEvidenceVerified = false;
	int32 AuthorityShotsBeforeSingleFire = INDEX_NONE;
	int32 ProjectileCountBeforeSingleFire = INDEX_NONE;
	bool bFullAutoPhaseTriggered = false;
	bool bFullAutoActiveObserved = false;
	bool bClientReportedFullAutoRelease = false;
	bool bFullAutoReleaseVerified = false;
	bool bFullAutoLocalCadenceVerified = false;
	bool bFullAutoAuthorityExactlyOnceVerified = false;
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
	TWeakObjectPtr<AShooterWeapon> ClientFullAutoTargetWeapon;
	int32 ClientFullAutoOwnerFeedbackBefore = INDEX_NONE;

	// ---- Invariant 2 半自动快速连点：服务器侧窗口快照与验收 ----
	bool bSemiAutoRapidClickPhaseTriggered = false;
	bool bClientReportedSemiAutoRapidClick = false;
	bool bSemiAutoRapidClickVerified = false;
	bool bSemiAutoRapidInputVerified = false;
	bool bSemiAutoLocalCadenceVerified = false;
	bool bSemiAutoAuthorityExactlyOnceVerified = false;
	int32 SemiAutoRapidAuthorityShotsBefore = INDEX_NONE;
	int32 SemiAutoRapidRejectsBefore = INDEX_NONE;
	int32 SemiAutoRapidProjectilesBefore = INDEX_NONE;
	int32 SemiAutoRapidAmmoBefore = INDEX_NONE;
	int32 SemiAutoRapidClicksObserved = INDEX_NONE;
	int32 SemiAutoRapidAuthorityShotDelta = INDEX_NONE;
	int32 SemiAutoRapidFeedbackDelta = INDEX_NONE;
	float SemiAutoRapidMinFeedbackInterval = -1.0f;
	float SemiAutoRapidClickStartTime = 0.0f;
	bool bSemiAutoRapidChannelsVerified = false;
	int32 SemiAutoRapidMontageDelta = INDEX_NONE;
	int32 SemiAutoRapidMuzzleDelta = INDEX_NONE;
	int32 SemiAutoRapidSoundDelta = INDEX_NONE;
	int32 SemiAutoRapidRecoilDelta = INDEX_NONE;
	TWeakObjectPtr<AShooterWeapon> SemiAutoRapidTargetWeapon;

	// ---- Invariant 2 半自动快速连点：客户端侧真实输入与观测窗口 ----
	bool bClientSemiAutoBurstStarted = false;
	bool bClientSemiAutoBurstReported = false;
	int32 SemiAutoBurstClicksDone = 0;
	int32 SemiAutoBurstTargetClicks = 0;
	int32 SemiAutoBurstFeedbackBefore = INDEX_NONE;
	int32 SemiAutoBurstMontageBefore = INDEX_NONE;
	int32 SemiAutoBurstMuzzleBefore = INDEX_NONE;
	int32 SemiAutoBurstSoundBefore = INDEX_NONE;
	int32 SemiAutoBurstRecoilBefore = INDEX_NONE;
	float SemiAutoBurstNextClickTime = 0.0f;
	float SemiAutoBurstSettleStartTime = 0.0f;
	TWeakObjectPtr<AShooterWeapon> ClientSemiAutoBurstWeapon;

	// ---- P1-D 远端第三人称确认表现：同一个全自动窗口内的观测端增量与权威增量 ----
	/** 服务器侧：全自动窗口起点，我方与对手武器的权威射击计数。 */
	int32 AuthorityShotsBeforeFullAuto = INDEX_NONE;
	int32 RemoteAuthorityShotsBeforeFullAuto = INDEX_NONE;
	TWeakObjectPtr<AShooterWeapon> RemoteObservedWeaponAtBurstStart;
	/** 观测端上报的远端确认表现增量，与同窗口内对手武器的权威射击增量比较。 */
	int32 RemoteConfirmedDeltaObserved = INDEX_NONE;
	int32 RemoteMontageDeltaObserved = INDEX_NONE;
	int32 RemoteMuzzleDeltaObserved = INDEX_NONE;
	int32 RemoteSoundDeltaObserved = INDEX_NONE;
	int32 RemoteAuthorityShotsForBurst = INDEX_NONE;
	bool bRemoteConfirmedVerified = false;
	/** 远端确认必须绑定到同一窗口内的对手权威射击；无权威增量时不得伪造通过。 */
	/** 客户端侧：窗口起点快照与观测源，窗口在"观测到确认表现后稳定"或超时时结束。 */
	TWeakObjectPtr<AShooterWeapon> ClientObservedRemoteWeapon;
	TWeakObjectPtr<AShooterCharacter> ClientObservedRemoteCharacter;
	int32 ClientRemoteConfirmedBefore = INDEX_NONE;
	int32 ClientRemoteMontageBefore = INDEX_NONE;
	int32 ClientRemoteMuzzleBefore = INDEX_NONE;
	int32 ClientRemoteSoundBefore = INDEX_NONE;
	int32 ClientRemoteConfirmedLastValue = INDEX_NONE;
	float ClientRemoteConfirmedStableTime = 0.0f;
	bool bClientReportedRemoteConfirmed = false;
	/**
	 * Unreliable 纯表现 / 确认通道是否要求精确送达。
	 * 无丢包无延迟（Dedicated / Listen）时要求逐份送达；Emulated 下契约不承诺 Unreliable 表现必达，
	 * 只要求不重复。远端第三人称确认与拥有者权威确认两处共用该判据。
	 */
	bool bRequireExactRemoteConfirmed = true;

	/** 4C 观测：死亡 / 无武器 / 无弹药拒绝、切枪取消、重生 Tag 清理与 NPC Ability 链路。 */
	bool bSwitchCancelPhaseTriggered = false;
	bool bSwitchCancelActiveObserved = false;
	bool bClientObservedSwitchCancel = false;
	bool bClientReportedSwitchCancel = false;
	bool bSwitchCancelVerified = false;
	bool bSwitchCancelQuiescentConfirmed = false;
	bool bNoAmmoRejectVerified = false;

	// ---- P1 换弹中开火：服务器侧记录与验收 ----
	bool bReloadFireImmediateVerified = false;
	bool bReloadFireAfterTagVerified = false;
	int32 ReloadFireActiveRequestId = 0;
	/** 阶段起点武器引用：归还池后 GetCurrentWeapon 会变空，但该 Actor 仍可用于读取权威计数。 */
	TWeakObjectPtr<AShooterWeapon> ReloadFireTargetWeapon;
	int32 ReloadFireAmmoBefore = INDEX_NONE;
	int32 ReloadFireAuthorityShotsBefore = INDEX_NONE;
	int32 ReloadFireAuthorityRejectsBefore = INDEX_NONE;
	int32 ReloadFireProjectilesBefore = INDEX_NONE;
	float ReloadFirePhaseStartTime = 0.0f;
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
	TWeakObjectPtr<AShooterWeapon> ServerInventoryFirstWeapon;
	TWeakObjectPtr<AShooterWeapon> ServerInventorySecondWeapon;
	TWeakObjectPtr<AShooterWeapon> ServerInventoryActiveWeapon;
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

	/** B1 瞄准表现基线模式：-ShootGameAimRotationTest 开启。只测量，不新增网络字段。 */
	bool bAimRotationMode = false;

	/** 快慢转向 CSV 诊断模式：-ShootGameAimTurnCsvTest 开启；正常玩法与常规自动化均不运行。 */
	bool bAimTurnCsvMode = false;

	/** CSV 模式下只存在于拥有者进程的 Visibility 薄墙，用来稳定复现近命中/远端回退切换。 */
	UPROPERTY(VisibleAnywhere, Category = "Automation|Aim Turn CSV")
	TObjectPtr<UBoxComponent> AimTurnCsvObstacleComponent;

	struct FAimTurnCsvPreviousSample
	{
		bool bValid = false;
		FVector TraceTarget = FVector::ZeroVector;
		FVector RawTarget = FVector::ZeroVector;
		FVector SmoothedTarget = FVector::ZeroVector;
		FVector AimDirection = FVector::ZeroVector;
		FVector MuzzleLocation = FVector::ZeroVector;
		FVector MuzzleForward = FVector::ZeroVector;
		FVector FinalizedMuzzleLocation = FVector::ZeroVector;
		FVector FeedbackDirection = FVector::ZeroVector;
		FVector ReferenceDirection = FVector::ZeroVector;
		float AimPitchN = 0.0f;
		FString TraceKind;
		FString HitIdentity;
	};

	/** 骨骼求值完成后的只读采样；只在 AimTurnCsv 诊断模式注册。 */
	struct FAimTurnCsvPoseProbe
	{
		TWeakObjectPtr<AShooterCharacter> Subject;
		TWeakObjectPtr<USkeletalMeshComponent> Mesh;
		FDelegateHandle DelegateHandle;
		FTransform FinalizedHandWorld = FTransform::Identity;
		FTransform FinalizedMuzzleWorld = FTransform::Identity;
		FTransform ReferenceMuzzleInMeshSpace = FTransform::Identity;
		uint64 FinalizedFrame = 0;
		bool bHasFinalizedSample = false;
		bool bHasReferenceMuzzle = false;
		bool bLoggedAnimGraphClass = false;
	};

	bool bAimTurnCsvStarted = false;
	bool bAimTurnCsvWritten = false;
	float AimTurnCsvStartTime = 0.0f;
	FRotator AimTurnCsvStartRotation = FRotator::ZeroRotator;
	FString AimTurnCsvBuffer;
	FString AimTurnCsvOutputPath;
	int32 AimTurnCsvRowCount = 0;
	FAimTurnCsvPreviousSample AimTurnCsvOwnerPrevious;
	FAimTurnCsvPreviousSample AimTurnCsvObserverPrevious;
	FAimTurnCsvPoseProbe AimTurnCsvOwnerPoseProbe;
	FAimTurnCsvPoseProbe AimTurnCsvObserverPoseProbe;

	void RunAimTurnCsvFrame(float DeltaSeconds);
	void CaptureAimTurnCsvSubject(const TCHAR* SampleRole, AShooterCharacter* Subject, float PhaseTime,
		float DeltaSeconds, FAimTurnCsvPreviousSample& PreviousSample, FAimTurnCsvPoseProbe& PoseProbe);
	void EnsureAimTurnCsvPoseProbe(AShooterCharacter* Subject, FAimTurnCsvPoseProbe& PoseProbe);
	void CaptureAimTurnCsvFinalizedPose(FAimTurnCsvPoseProbe* PoseProbe);
	void UnregisterAimTurnCsvPoseProbe(FAimTurnCsvPoseProbe& PoseProbe);
	void FlushAimTurnCsv();

	/** 服务器开启瞄准旋转阶段后复制给客户端。 */
	UPROPERTY(Replicated)
	bool bAimRotationPhaseActive = false;

	/** 服务器完成跟踪验证后复制给客户端。 */
	UPROPERTY(Replicated)
	bool bAimRotationServerDone = false;

	/** 服务器完成表现目标复制验证后复制给客户端（B2）。 */
	UPROPERTY(Replicated)
	bool bAimRotationServerPresentationVerified = false;

	/** B1 服务器侧阶段状态（服务器时间）。 */
	float AimRotationServerPhaseStartTime = 0.0f;
	float AimRotationLastSampleTime = -1.0f;
	int32 AimRotationServerSampleCount = 0;
	int32 AimRotationServerYawGoodSamples = 0;
	int32 AimRotationServerPitchGoodSamples = 0;
	bool bAimRotationServerYawTracked = false;
	bool bAimRotationServerPitchTracked = false;
	float AimRotationServerMuzzleAngleMin = 180.0f;
	float AimRotationServerMuzzleAngleMax = 0.0f;

	/** B1 客户端侧阶段状态（客户端时间）。 */
	float AimRotationClientPhaseStartTime = 0.0f;
	bool bClientAimRotationStarted = false;
	bool bClientAimRotationReported = false;
	FRotator AimRotationClientStartRotation = FRotator::ZeroRotator;
	float AimRotationLastObserverSampleTime = -1.0f;
	float AimRotationLastObserverRemoteYaw = 0.0f;
	int32 AimRotationObserverSampleCount = 0;
	int32 AimRotationObserverYawGoodSamples = 0;
	float AimRotationObserverMaxPitch = -90.0f;
	float AimRotationObserverMinPitch = 90.0f;
	float AimRotationObserverMuzzleAngleMin = 180.0f;
	float AimRotationObserverMuzzleAngleMax = 0.0f;
	TWeakObjectPtr<AShooterCharacter> AimRotationObservedCharacter;

	// ---- B2 表现目标复制验证 ----
	int32 AimRotationServerPresentationGoodSamples = 0;
	int32 AimRotationServerPresentationChanges = 0;
	FVector AimRotationServerPresentationPrevTarget = FVector::ZeroVector;
	bool bAimRotationServerPresentationPrevValid = false;
	float AimRotationServerMaxPresentationMagnitude = 0.0f;
	bool bAimRotationOwnerPresentationUntouched = false;
	int32 AimRotationObserverPresentationGoodSamples = 0;
	int32 AimRotationObserverQuantizedSamples = 0;
	bool bAimRotationObserverPresentationSeen = false;

	// ---- B3 观察端平滑与局部角度契约验证 ----
	float AimRotationObserverMinSmoothGap = FLT_MAX;
	int32 AimRotationObserverPitchContractSamples = 0;
	int32 AimRotationObserverYawStableSamples = 0;
	float AimRotationObserverAimYawFirst = 0.0f;
	bool bAimRotationObserverAimYawFirstSet = false;
	bool bAimRotationObserverSmoothingSeen = false;

	/** B1 服务器侧瞄准旋转阶段（PollServerState 专用分支）。 */
	void RunAimRotationServerPhase();

	/** B1 客户端侧瞄准旋转阶段（PollClientState 专用分支）。 */
	void RunAimRotationClientPhase();

	/** B1 观察端采样与验证：远端角色方向 + 枪口 Forward 夹角。 */
	void RunAimRotationObserverPhase();
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
