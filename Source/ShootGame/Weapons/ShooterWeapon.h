// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Weapons/Animation/ShooterAnimNotify_WeaponSound.h"
#include "Weapons/Interfaces/ShooterWeaponHolder.h"
#include "Animation/AnimInstance.h"
#include "Weapons/Data/ShooterWeaponConfigRow.h"
#include "ShooterWeapon.generated.h"

class IShooterWeaponHolder;
class AShooterProjectile;
class UShooterWeaponRuntimeSubsystem;
class UShooterGameplayAbility_Fire;
class UStaticMeshComponent;
struct FShooterWeaponConfigRow;

/**
 * WeaponActor 生命周期状态（实施计划 4.4）：InPool -> Holstered -> Equipping -> Equipped -> Holstered -> InPool。
 *
 * 权威边界：
 * - 服务器权威端是唯一执行状态转换拒绝的一端（B2 验证项「非法状态转换被拒绝」只在权威端成立）；
 * - 客户端状态是对复制结果的镜像（Owner / CurrentWeaponActor / WeaponId 的 RepNotify 各自驱动），不参与拒绝判定；
 * - 可见性不属于本状态机契约：隐藏由池统一归还清理、Inventory 授予后隐藏、
 *   Equipment / Character 表现收敛负责激活时解除隐藏。状态本身只表达身份与装备语义。
 */
UENUM()
enum class EShooterWeaponLifecycleState : uint8
{
	/** 在池内：无租用归属、无 Owner。 */
	InPool,
	/** 已租用并归属某个持有者：有 Owner、未装备。 */
	Holstered,
	/** 装备事务提交中：第一版在 Equipment 原子提交内瞬态通过，是 GA_Equip 时序的扩展点。 */
	Equipping,
	/** 当前装备：激活并对外表现。 */
	Equipped,
};

/**
 * 一轮 GA_Fire Activation 的服务器结果，OwnerOnly 复制。
 *
 * 只记录「服务器真实提交了几发」与「这一轮是否已经结束」；
 * 不做 per-shot id，也不承担预测账本，预测/补播账本在 GA_Fire 实例内。
 */
USTRUCT()
struct FShooterFireActivationResult
{
	GENERATED_BODY()

	/** 该轮 Activation 的 PredictionKey.Current；0 表示空槽。 */
	UPROPERTY()
	int32 ActivationKey = 0;

	/** 服务器为该轮真实提交的 Shot 数；只在 ConsumeAmmo 成功提交时递增。 */
	UPROPERTY()
	int32 ProcessedShots = 0;

	/** 服务器不会再为该轮产生新 Shot（正常结束 / 松开 / Cancel / 打空 / 切换 / 死亡等）。 */
	UPROPERTY()
	bool bSettled = false;

	/** 服务器单调轮次号，只用于日志和槽位覆盖诊断，不参与客户端配对。 */
	UPROPERTY()
	int32 ActivationSerial = 0;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FShooterWeaponOutOfAmmoDelegate, AShooterWeapon*);
class USkeletalMeshComponent;
class UAnimMontage;
class UAnimInstance;
class UNiagaraSystem;
class USoundBase;

/**
 *  Base class for a simple first person shooter weapon
 *  Provides both first person and third person perspective meshes
 *  Handles ammo and firing logic
 *  Interacts with the weapon owner through the ShooterWeaponHolder interface
 */
UCLASS(abstract)
class SHOOTGAME_API AShooterWeapon : public AActor
{
	GENERATED_BODY()

	/** First person perspective mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	/** Third person perspective mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* ThirdPersonMesh;

	/** 第一人称独立弹匣视觉代理；默认挂在 Weapon Root，换弹期间临时挂到 Character hand_l。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* FirstPersonMagazineProxy;

	/** 第三人称独立弹匣视觉代理；默认挂在 Weapon Root，换弹期间临时挂到 Character hand_l。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* ThirdPersonMagazineProxy;

	/** 第一人称弹匣代理挂到 Character hand_l 后的最终局部抓握姿态。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Magazine", meta = (AllowPrivateAccess = "true"))
	FTransform FirstPersonMagazineGripTransform = FTransform::Identity;

	/** 第三人称弹匣代理挂到 Character hand_l 后的最终局部抓握姿态。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Magazine", meta = (AllowPrivateAccess = "true"))
	FTransform ThirdPersonMagazineGripTransform = FTransform::Identity;

protected:

	/** Cast pointer to the weapon owner */
	IShooterWeaponHolder* WeaponOwner = nullptr;

	/** 已完成领域绑定的 Owner；用于 Owner 复制切换时解除旧 OnDestroyed 委托。 */
	UPROPERTY(Transient)
	TObjectPtr<AActor> CachedWeaponOwnerActor;

	/**
	 * 武器种类身份：由 WeaponRuntimeSubsystem 在创建时一次写入，此后永不改变。
	 * 复制给所有端；客户端据此从启动快照恢复静态表现配置（不查询 DataTable）。
	 */
	UPROPERTY(ReplicatedUsing = OnRep_WeaponId, VisibleAnywhere, BlueprintReadOnly, Category="Inventory")
	FName WeaponId;

	UFUNCTION()
	void OnRep_WeaponId();

	/** 生命周期状态；服务器权威，客户端经 OnRep 镜像，不复制。 */
	EShooterWeaponLifecycleState LifecycleState = EShooterWeaponLifecycleState::InPool;

	/** Type of projectiles this weapon will shoot */
	UPROPERTY(EditAnywhere, Category="Ammo")
	TSubclassOf<AShooterProjectile> ProjectileClass;

	/** Number of bullets in a magazine */
	UPROPERTY(EditAnywhere, Category="Ammo", meta = (ClampMin = 0, ClampMax = 100))
	int32 MagazineSize = 10;

	/** 有限备弹声明：本武器弹药经济是封闭模型——入库时按该值授予初始 ReserveAmmo，
	 *  换弹只消耗备弹、不回复，备弹耗尽（ReserveAmmo <= 0）后换弹 Ability 直接拒绝。
	 *  -1 表示自动（MagazineSize × 3，兼容既有资产基线）；>=0 为显式有限备弹值。 */
	UPROPERTY(EditAnywhere, Category="Ammo", meta = (ClampMin = -1, ClampMax = 999))
	int32 InitialReserveAmmo = -1;

	/** 当前弹匣弹药；服务器权威，OwnerOnly 复制，HUD 与本地表现只读该值。 */
	UPROPERTY(ReplicatedUsing=OnRep_MagazineAmmo, VisibleAnywhere, BlueprintReadOnly, Category="Ammo")
	int32 MagazineAmmo = 0;

	/** 当前备用弹药；服务器权威，OwnerOnly 复制；换弹事务从这里转进弹匣，只消耗不回复。 */
	UPROPERTY(ReplicatedUsing=OnRep_ReserveAmmo, VisibleAnywhere, BlueprintReadOnly, Category="Ammo")
	int32 ReserveAmmo = 0;

	UFUNCTION()
	void OnRep_MagazineAmmo(int32 OldMagazineAmmo);

	UFUNCTION()
	void OnRep_ReserveAmmo();

	UFUNCTION()
	void OnRep_FireActivationResults();

	/**
	 * 最近数轮 GA_Fire 的服务器结果环形记录；只有拥有者客户端需要消费。
	 * 保留最近 4 轮，避免一次短 Activation 的最终结果被下一轮覆盖后无法补播。
	 */
	UPROPERTY(ReplicatedUsing=OnRep_FireActivationResults)
	TArray<FShooterFireActivationResult> FireActivationResults;

	/** 服务器结果环大小；最近数轮足够覆盖一次短 Activation 的最终结果。 */
	static constexpr int32 FireActivationResultRingSize = 4;

	/** 服务器环形槽位游标；只在权威端写入。 */
	int32 NextFireActivationResultSlot = 0;

	/** 服务器当前未结清的槽位；INDEX_NONE 表示没有打开的 Activation。 */
	int32 OpenFireActivationResultSlot = INDEX_NONE;

	/** 服务器单调轮次号；只服务日志与槽位覆盖诊断。 */
	int32 NextFireActivationSerial = 1;

	/** 客户端每槽最近一次观察值：槽位换轮时用于结清上一轮。 */
	TArray<FShooterFireActivationResult> CachedFireActivationResults;

	/** 当前绑定到本武器的拥有端 Fire Ability；以 UObject 弱引用避免头文件循环依赖。 */
	TWeakObjectPtr<UObject> BoundPredictionAbility;

	/**
	 * 已本地预测消费、但尚未被新的服务器 Ammo 状态吸收的弹药数量。
	 *
	 * 不复制，只存在于预测型 Owner 客户端（见 IsAmmoPredictionContext）：
	 * MagazineAmmo / ReserveAmmo 始终只表示服务器确认状态，本地预测只增加这个待吸收计数。
	 */
	int32 PendingPredictedShots = 0;

	/** Animation montage to play when firing this weapon */
	UPROPERTY(EditAnywhere, Category="Animation")
	UAnimMontage* FiringMontage;

	/** Niagara muzzle flash spawned at the weapon's muzzle socket when firing */
	UPROPERTY(EditAnywhere, Category="Animation")
	TObjectPtr<UNiagaraSystem> MuzzleFlash;

	/** Sound to play when firing this weapon */
	UPROPERTY(EditAnywhere, Category="Animation")
	TObjectPtr<USoundBase> FireSound;

	/** 换弹表现音效：弹匣退出阶段；由换弹 Sequence 内的 WeaponSound Notify 在各端本地触发。 */
	UPROPERTY(EditAnywhere, Category="Sound")
	TObjectPtr<USoundBase> ReloadMagazineOutSound;

	/** 换弹表现音效：弹匣插入阶段。 */
	UPROPERTY(EditAnywhere, Category="Sound")
	TObjectPtr<USoundBase> ReloadMagazineInSound;

	/** 换弹表现音效：拉枪机上膛阶段。 */
	UPROPERTY(EditAnywhere, Category="Sound")
	TObjectPtr<USoundBase> ReloadCockingSound;

	/** AnimInstance class to set for the first person character mesh when this weapon is active */
	UPROPERTY(EditAnywhere, Category="Animation")
	TSubclassOf<UAnimInstance> FirstPersonAnimInstanceClass;

	/** AnimInstance class to set for the third person character mesh when this weapon is active */
	UPROPERTY(EditAnywhere, Category="Animation")
	TSubclassOf<UAnimInstance> ThirdPersonAnimInstanceClass;

	/** Cone half-angle for variance while aiming */
	UPROPERTY(EditAnywhere, Category="Aim", meta = (ClampMin = 0, ClampMax = 90, Units = "Degrees"))
	float AimVariance = 0.0f;

	/** Amount of firing recoil to apply to the owner */
	UPROPERTY(EditAnywhere, Category="Aim", meta = (ClampMin = 0, ClampMax = 100))
	float FiringRecoil = 0.0f;

	/** 第一/第三人称武器共用的枪口 Socket；权威弹丸优先从第三人称世界表现枪口生成。 */
	UPROPERTY(EditAnywhere, Category="Aim")
	FName MuzzleSocketName;

	/** 第三人称左手握把 Socket 名；空名表示该武器没有左手握把配置，左手 IK 自动关闭。 */
	UPROPERTY(EditAnywhere, Category="Aim")
	FName ThirdPersonLeftHandGripSocketName = NAME_None;

	/** Distance ahead of the muzzle that bullets will spawn at */
	UPROPERTY(EditAnywhere, Category="Aim", meta = (ClampMin = 0, ClampMax = 1000, Units = "cm"))
	float MuzzleOffset = 10.0f;

	/** If true, this weapon will automatically fire at the refire rate */
	UPROPERTY(EditAnywhere, Category="Refire")
	bool bFullAuto = false;

	/** Time between shots for this weapon. Affects both full auto and semi auto modes */
	UPROPERTY(EditAnywhere, Category="Refire", meta = (ClampMin = 0, ClampMax = 5, Units = "s"))
	float RefireRate = 0.5f;

	/** 服务器权威换弹事务等待时长；表现 Montage 不得反向决定该值。 */
	UPROPERTY(EditAnywhere, Category="Timing", meta = (ClampMin = 0, Units = "s"))
	float ReloadDuration = 1.5f;

	/** 服务器权威切枪事务等待时长；表现 Montage 不得反向决定该值。 */
	UPROPERTY(EditAnywhere, Category="Timing", meta = (ClampMin = 0, Units = "s"))
	float EquipDuration = 0.5f;

	/** Game time of last shot fired, used to enforce refire rate on semi auto */
	float TimeOfLastShot = 0.0f;

	/** If true, the weapon is currently firing */
	bool bIsFiring = false;

	/** Timer to handle full auto refiring */
	FTimerHandle RefireTimer;

	/**
	 * 本武器下一次允许"本地有效开火"的本地时间。
	 *
	 * 本地开火节拍：半自动用它判断本次点击是否已满足距上一次本地有效开火 >= RefireRate。
	 * 该值只由"本地 Shot Attempt 被接受"推进一次，不复制、不读权威 RefireTimer / TimeOfLastShot，
	 * 与服务器权威射速是两套互不干涉的时钟。
	 */
	float LocalFireCooldownEndTime = -1.0f;

	/** Cast pawn pointer to the owner for AI perception system interactions */
	TObjectPtr<APawn> PawnOwner;

	/** Loudness of the shot for AI perception system interactions */
	UPROPERTY(EditAnywhere, Category="Perception", meta = (ClampMin = 0, ClampMax = 100))
	float ShotLoudness = 1.0f;

	/** Max range of shot AI perception noise */
	UPROPERTY(EditAnywhere, Category="Perception", meta = (ClampMin = 0, ClampMax = 100000, Units = "cm"))
	float ShotNoiseRange = 3000.0f;

	/** Tag to apply to noise generated by shooting this weapon */
	UPROPERTY(EditAnywhere, Category="Perception")
	FName ShotNoiseTag = FName("Shot");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person")
	float FirstPersonCompositionDrop = 0.0f;

public:

	/** Constructor */
	AShooterWeapon();

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** 客户端收到 Owner 复制后补齐武器拥有者初始化。 */
	virtual void OnRep_Owner() override;

	/** Gameplay Cleanup */
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:

	/** Called when the weapon's owner is destroyed（池化武器归还池，非池出生回落销毁） */
	UFUNCTION()
	void OnOwnerDestroyed(AActor* DestroyedActor);

	/** 幂等绑定当前 Owner；绑定前先解除旧 Owner，允许 Owner 晚于武器 BeginPlay 到达客户端。 */
	void InitializeWeaponOwner();

	/** 解除旧 Owner 的销毁委托并清空领域缓存；服务器归还池和客户端 Owner RepNotify 共用。 */
	void ClearWeaponOwner();

	/** 返回本 Actor 所在 World 的武器运行时子系统；World 不支持或已销毁时返回 nullptr。 */
	UShooterWeaponRuntimeSubsystem* GetWeaponRuntimeSubsystem() const;

	/** Owner / 池租用边界复位本地开火节拍，防止跨持有者继承。 */
	void ResetLocalFireCooldown();

	/**
	 * 拥有者纯表现的唯一实现；只由本地开火 / 服务器确认补播入口调用。
	 * bConfirmedBackfill=false 为本地预测来源，true 为服务器确认后的补播来源。
	 */
	bool PlayOwnerShotFeedbackInternal(bool bConfirmedBackfill);

	/** PendingPredictedShots 的统一减少入口；Marker 用于区分 confirm / phantom / reject 诊断。 */
	void ReducePendingPredictedAmmo(int32 Amount, const TCHAR* Marker);

	/** 把服务器结果在本地权威 / 拥有端直接发布；Listen Host 与 Standalone 不经过 OnRep。 */
	void PublishAuthorityFireActivationResult(const FShooterFireActivationResult& Result, bool bForceSettled);

	/** 生命周期边界：清空环形记录与转发状态，避免上一持有者的结果污染下一轮。 */
	void ResetFireActivationResultState();

	/**
	 * 统一状态转换入口：状态未变化时是安全 no-op，真实变化时输出一条 Verbose 诊断。
	 * 表现收敛会重复调用同一转换，诊断必须保持低噪声。
	 */
	void SetLifecycleState(EShooterWeaponLifecycleState NewState, const TCHAR* Reason);

	/**
	 * 把武器模板行的只读配置应用到本 Actor 的运行时镜像（网格、动画类、表现资产、
	 * 弹药经济、开火节奏、时序、Socket、视角参数、弹丸类）。
	 * 模板数据本身只读：本函数只写 Actor 自身状态，不修改行或表。
	 */
	void ApplyWeaponRow(const FShooterWeaponConfigRow& Row);

public:

	/** 服务器权威：按静态配置恢复初始弹药（弹匣回满、备弹回到声明值）。
	 *  绑定/应用行配置与归还池时调用；复用租用不继承上一持有者的弹药。 */
	void RestoreInitialAmmo();

	/**
	 * 服务器权威换弹原子事务：在同一次写入中把 ReserveAmmo 转进 MagazineAmmo。
	 * Transfer = Min(MagazineSize - MagazineAmmo, ReserveAmmo)；
	 * Transfer 不大于 0（弹匣已满或无备弹）时返回 false 且不产生任何变化。
	 */
	bool ReloadFromReserve(int32& OutTransferredAmmo);

	/** 服务器扣弹 / 换弹提交与 Owner 客户端 OnRep 共用的 HUD 推送入口（装备可见时）。 */
	void PushAmmoToOwnerHud();

	/** 返回当前生命周期状态。 */
	EShooterWeaponLifecycleState GetLifecycleState() const { return LifecycleState; }

	/**
	 * 装备事务开始：Holstered -> Equipping。
	 * 由 Equipment 在原子提交入口调用；表现完成（ActivateWeapon）后进入 Equipped。
	 */
	void BeginEquipTransaction();

	/** Activates this weapon and gets it ready to fire（Holstered/Equipping -> Equipped，InPool 拒绝） */
	void ActivateWeapon();

	/** Deactivates this weapon（Equipped/Equipping -> Holstered，InPool 拒绝） */
	void DeactivateWeapon();

	/** Start firing this weapon */
	void StartFiring();

	/** Stop firing this weapon */
	void StopFiring();

	/**
	 * 拥有者本地开火表现入口：唯一的第一人称表现来源。
	 * 内部先判定本地开火节拍，未越过时直接返回 false，绝不播放。
	 * 不写 MagazineAmmo / ReserveAmmo / TimeOfLastShot / bIsFiring / RefireTimer /
	 * Inventory / Projectile，也不建立任何 Timer。
	 * 返回是否向表现通道提交了至少一项；返回值不表示当前机器一定具备音频或渲染设备。
	 */
	bool PlayOwnerPredictedShotFeedback();

	/**
	 * 服务器确认补播入口：只播放四路纯表现，绝不消费 Pending、推进节拍、发 RPC、
	 * 生成 Projectile 或修改 Ammo；只由拥有端 Activation 账本按缺发数调用。
	 */
	bool PlayOwnerConfirmedShotFeedback();

	/**
	 * 本地开火节拍是否已越过：距上一次本地有效开火是否已满 RefireRate。
	 *
	 * 客户端半自动只用它决定"这次输入是否构成一次有效 Local Shot Attempt"。
	 * 该判定只读本地时钟，不读权威 RefireTimer / TimeOfLastShot，
	 * 也不读 Ammo / Reloading / Equipping / Dead 等可能过期的复制状态。
	 */
	bool IsLocalFireCooldownReady() const;

	/** 距本地开火节拍结束还剩多久；已就绪时返回 0。全自动首拍用它安排"剩余 cooldown"。 */
	float GetLocalFireCooldownRemaining() const;

	/**
	 * 按本武器 RefireRate 推进本地开火节拍。
	 *
	 * 唯一调用点是"本地 Shot Attempt 被接受"处，与 Montage / Niagara / Sound
	 * 是否成功播放无关：表现通道的成败不得决定射击节拍。
	 */
	void AdvanceLocalFireCooldown();

	/** 本地预测节拍只读配置。 */
	bool IsFullAuto() const { return bFullAuto; }
	float GetRefireRate() const { return RefireRate; }

	/**
	 * 服务器只读射速资格查询，无副作用，仅供半自动使用。
	 * 权威 RefireTimer 未激活时返回 true；全自动恒返回 false，不参与该查询。
	 * 不使用 TimeOfLastShot == 0.0f 作为“从未开火”哨兵：池取用与归还都会把它复位为 0。
	 */
	bool CanStartSemiAutoShotNow() const;

protected:

	/** Fire the weapon */
	virtual void Fire();

	/** Called when the refire rate time has passed while shooting semi auto weapons */
	void FireCooldownExpired();

	/**
	 * 服务器权威开火执行：生成弹丸，并把表现入口（Montage / Multicast FX / 后坐力）统一留在本 Actor。
	 * 开火行为边界当前休眠，本函数只调用 FireProjectile；弹丸类来自 ApplyWeaponRow 镜像的行配置。
	 */
	void ExecuteFireAtTarget(const FVector& TargetLocation);

	/** 唯一弹丸生成路径：使用 ApplyWeaponRow 从行镜像的 ProjectileClass 生成弹丸；只在服务器执行。 */
	virtual void FireProjectile(const FVector& TargetLocation);

	/**
	 * 本机是否以“本地玩家”视角拥有该武器：要求 PawnOwner 同时被玩家控制且由本机控制。
	 * 不得退化为普通 IsLocallyControlled()：服务器上的 NPC 在 UE 5.6 也会返回 true。
	 */
	bool HasOwnerLocalPlayerView() const;

	/** Broadcast firing effects (muzzle flash + sound) to all clients. Unreliable: dropping a flash is acceptable */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayFiringFX();

	/** Calculates the spawn transform for projectiles shot by this weapon */
	FTransform CalculateProjectileSpawnTransform(const FVector& TargetLocation) const;

public:

	/** Returns the first person mesh */
	UFUNCTION(BlueprintPure, Category="Weapon")
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; };

	/** Returns the third person mesh */
	UFUNCTION(BlueprintPure, Category="Weapon")
	USkeletalMeshComponent* GetThirdPersonMesh() const { return ThirdPersonMesh; };

	/** 返回第一人称换弹视觉代理。 */
	UStaticMeshComponent* GetFirstPersonMagazineProxy() const { return FirstPersonMagazineProxy; }

	/** 返回第三人称换弹视觉代理。 */
	UStaticMeshComponent* GetThirdPersonMagazineProxy() const { return ThirdPersonMagazineProxy; }

	/** 在各自整枪 MagazineSocket 位置显示独立弹匣，并隐藏 Socket 的父骨骼弹匣。 */
	UFUNCTION(BlueprintCallable, Category="Weapon")
	void ShowMagazineProxyInPlace();

	/** 将第一人称弹匣代理从 MagazineSocket 原位换手到 Character 的 hand_l。 */
	UFUNCTION(BlueprintCallable, Category="Weapon")
	bool DetachFirstPersonMagazineProxy(USkeletalMeshComponent* CharacterMesh);

	/** 将第一人称弹匣代理从 Character 的 hand_l 插回武器并恢复原弹匣。 */
	UFUNCTION(BlueprintCallable, Category="Weapon")
	bool InsertFirstPersonMagazineProxy();

	/** 将第三人称弹匣代理从 MagazineSocket 原位换手到 Character 的 hand_l。 */
	UFUNCTION(BlueprintCallable, Category="Weapon")
	bool DetachThirdPersonMagazineProxy(USkeletalMeshComponent* CharacterMesh);

	/** 将第三人称弹匣代理从 Character 的 hand_l 插回武器并恢复原弹匣。 */
	UFUNCTION(BlueprintCallable, Category="Weapon")
	bool InsertThirdPersonMagazineProxy();

	/** 恢复原弹匣并清理独立弹匣代理的临时可见性、附着和变换。 */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void ResetMagazinePresentation();

	/** 返回第三人称网格 Muzzle socket 的世界变换；网格或 socket 缺失时返回 Identity（不回退 Actor 变换）。
	 *  调用方必须先确认 HasThirdPersonMuzzleSocket() 为真。 */
	FTransform GetThirdPersonMuzzleWorldTransform() const;

	/** 返回 Muzzle socket 名（第一/第三人称共用配置；IK Binding 依赖签名使用）。 */
	FName GetMuzzleSocketName() const { return MuzzleSocketName; }

	/** 第三人称网格是否真实拥有 Muzzle socket（Aim IK 启用条件；不存在时不得回退启用）。 */
	bool HasThirdPersonMuzzleSocket() const;

	/** 返回第三人称左手握把 Socket 名；空名表示当前武器未配置左手握把。 */
	FName GetThirdPersonLeftHandGripSocketName() const { return ThirdPersonLeftHandGripSocketName; }

	/** 第三人称网格是否真实拥有配置的左手握把 Socket；无配置或 Socket 缺失时返回 false。 */
	bool HasThirdPersonLeftHandGripSocket() const;

	/** 返回第三人称网格左手握把 Socket 的世界变换；无效状态返回 Identity，不回退到 Actor 变换。 */
	FTransform GetThirdPersonLeftHandGripWorldTransform() const;


	/** 返回服务器权威换弹事务等待时长。 */
	float GetReloadDuration() const { return ReloadDuration; }

	/** 在第三人称武器 Mesh 的 Muzzle Socket 位置本地播放指定阶段的换弹音效。
	 *  纯本地表现，不经网络；各端由各自同步播放的换弹动画 Notify 触发。 */
	UFUNCTION(BlueprintCallable, Category="Weapon")
	void PlayReloadSoundStage(EShooterReloadSoundStage Stage);

	/** 返回服务器权威切枪事务等待时长。 */
	float GetEquipDuration() const { return EquipDuration; }

	/** Returns the first person anim instance class */
	const TSubclassOf<UAnimInstance>& GetFirstPersonAnimInstanceClass() const;

	/** Returns the third person anim instance class */
	const TSubclassOf<UAnimInstance>& GetThirdPersonAnimInstanceClass() const;

	/** Returns the magazine size；应用武器模板行后即为该行的弹匣容量。 */
	int32 GetMagazineSize() const { return MagazineSize; };

	/** Returns the current bullet count；弹药权威在本 Actor 的 MagazineAmmo。 */
	int32 GetBulletCount() const;

	/** 返回初始备弹声明值；-1 表示自动（MagazineSize × 3），>=0 为显式有限值。 */
	int32 GetInitialReserveAmmo() const { return InitialReserveAmmo; }

	/** 解析实际初始备弹：显式 >=0 直接采用，-1 回落 MagazineSize × 3。 */
	int32 ResolveInitialReserveAmmo() const
	{
		return InitialReserveAmmo >= 0
			? InitialReserveAmmo
			: FMath::Max(0, MagazineSize * 3);
	}

	/** 返回当前备弹；弹药权威在本 Actor 的 ReserveAmmo。 */
	int32 GetReserveAmmo() const;

	/**
	 * 把本 Actor 当前的配置镜像导出为一条武器模板行。
	 * 只服务一次性资产迁移工具与自动化测试构造行数据，不参与运行时游戏逻辑。
	 */
	FShooterWeaponConfigRow CaptureWeaponConfigRow() const;

	/**
	 * 服务器在 WeaponRuntimeSubsystem 预创建 / 弹性 Spawn 后一次调用：
	 * 写入永久 WeaponId，并从启动冻结的 RuntimeConfig 快照应用静态配置。
	 * 身份写入后不再改写；测试注入的子系统快照缺失时保持 Actor 默认配置。
	 */
	void InitializeWeaponIdentity(FName InWeaponId);

	/** 返回武器种类身份；空名表示尚未由 WeaponRuntimeSubsystem 创建身份。 */
	FName GetWeaponId() const { return WeaponId; }

	/** 从 WeaponRuntimeSubsystem 取出后调用：复位开火节拍并重新绑定 Owner 缓存；静态配置与 WeaponId 不变。 */
	void OnAcquiredFromWeaponPool();
	/** 归还 WeaponRuntimeSubsystem 前调用：停 Timer、解委托、回 InPool；WeaponId 与静态配置永久保留。 */
	void OnReleasedToWeaponPool();

	/** 判断当前是否还有可发射弹药；直接检查本 Actor 的 MagazineAmmo。 */
	bool CanConsumeAmmo() const;

	/** 服务器权威扣减并推送 Owner HUD；弹匣不足或 Amount 非法时不产生任何变化。 */
	bool ConsumeAmmo(int32 Amount = 1);

	/**
	 * 是否处于 Ammo 预测上下文：只有「非权威端 + 本机拥有者视图」才预测。
	 *
	 * Listen Host 与 Standalone 是权威端，弹药在本地直接写入 MagazineAmmo，
	 * 自身复制不会触发 OnRep，因此它们不允许维护 PendingPredictedShots，
	 * 否则这份待吸收计数永远没有下降复制来收敛。
	 */
	bool IsAmmoPredictionContext() const;

	/** 本地当前预测可用弹药 = max(0, MagazineAmmo - PendingPredictedShots)；权威端等于 MagazineAmmo。 */
	int32 GetPredictedMagazineAmmo() const;

	/** 本地弹药预算是否足够；非预测上下文与 Amount 非法时的语义见实现。 */
	bool CanConsumePredictedAmmo(int32 Amount = 1) const;

	/**
	 * 预测消费一次弹药：只增加 PendingPredictedShots，绝不写 replicated MagazineAmmo / ReserveAmmo。
	 *
	 * 非预测上下文（权威端 / 非拥有者视图）不维护 Pending，直接返回 true 放行，
	 * 权威扣弹仍然只由 ConsumeAmmo 负责。
	 */
	bool TryConsumePredictedAmmo(int32 Amount = 1);

	/** Reject 退还：只减少 PendingPredictedShots 并 clamp 到 >= 0。 */
	void RefundPredictedAmmo(int32 Amount);

	/** 服务器结果确认：减少对应数量的 PendingPredictedShots（只由 Activation 账本调用）。 */
	void ConfirmPredictedAmmo(int32 Amount);

	/** 本轮结束后清理服务器最终没有执行的预测发数（phantom），不补任何表现。 */
	void DiscardPhantomPredictedAmmo(int32 Amount);

	/** 拥有端 Fire Ability 在本地激活时绑定；用于接收服务器 Activation 结果复制。 */
	void BindPredictionAbility(UShooterGameplayAbility_Fire* Ability);

	/** Owner / 池 / Destroy 边界解绑结果转发；不依赖 PredictionKey 大小。 */
	void ClearPredictionAbility();

	/** 服务器：开始一轮 GA_Fire Activation 的结果记录；同一时刻只允许一轮打开。 */
	void BeginAuthorityFireActivation(int32 ActivationKey);

	/** 服务器：真实提交一发 Shot 时递增当前轮的 ProcessedShots。 */
	void RecordAuthorityShotCommitted();

	/** 服务器：GA_Fire 结束（任何路径）时标记当前轮 Settled。 */
	void SettleAuthorityFireActivation();

	/** 生命周期边界：旧预测上下文整体失效（Owner 变化 / 归还池 / teardown）。 */
	void ResetAmmoPrediction();

	/** 只读观测：当前待吸收的预测发数。 */
	int32 GetPendingPredictedShots() const { return PendingPredictedShots; }

	/** 弹药在 Fire 事务中耗尽时广播；GA_Fire 用它幂等结束 Ability。 */
	FShooterWeaponOutOfAmmoDelegate OnOutOfAmmo;

#if WITH_DEV_AUTOMATION_TESTS
public:
	/** 测试专用：直接写权威弹药（换弹网络测试构造任意起点状态）；生产代码不得调用。 */
	void SetAmmoForAutomationTest(int32 InMagazineAmmo, int32 InReserveAmmo)
	{
		MagazineAmmo = InMagazineAmmo;
		ReserveAmmo = InReserveAmmo;
	}

	/** 测试专用：写入两侧弹匣抓握姿态；生产代码不得调用。 */
	void SetMagazineGripTransformsForAutomationTest(const FTransform& InFirstPersonTransform, const FTransform& InThirdPersonTransform)
	{
		FirstPersonMagazineGripTransform = InFirstPersonTransform;
		ThirdPersonMagazineGripTransform = InThirdPersonTransform;
	}

	// ---- P1 开火表现计数与权威字段只读探针：仅在开发构建存在，Shipping 零增量 ----

	/** 清空本武器的开火表现计数；测试在场景起点调用一次。 */
	void ResetFireFeedbackCountersForAutomationTest();

	/** 拥有者服务器确认补播的提交次数，由 PlayOwnerConfirmedShotFeedback 递增。 */
	int32 GetConfirmedBackfillCountForAutomationTest() const;

	/** 测试专用：直接建立 PendingPredictedShots 起点；只用于验证 Activation 对账算术。 */
	void SetPendingPredictedShotsForAutomationTest(int32 InPendingShots);

	/** 测试观察：指定槽位的服务器结果记录。 */
	FShooterFireActivationResult GetFireActivationResultForTest(int32 SlotIndex) const;

	/** 测试观察：当前未结清的服务器槽位；INDEX_NONE 表示没有打开的 Activation。 */
	int32 GetOpenFireActivationResultSlotForTest() const { return OpenFireActivationResultSlot; }

	/** 测试专用：服务器侧复位结果环，构造独立起点。 */
	void ResetFireActivationStateForAutomationTest();

	/** 测试专用：设置本地开火节拍剩余时间；用于构造"本地 cadence 落后于服务器"的夹具。 */
	void SetLocalFireCooldownRemainingForAutomationTest(float RemainingSeconds);

	/** 拥有者本地预测反馈提交次数，由 PlayOwnerPredictedShotFeedback 递增。 */
	int32 GetPredictedOwnerFeedbackCountForAutomationTest() const;

	/** Multicast 到达拥有者并跳过可见 FX 的次数；P1-B 起由 MulticastPlayFiringFX 递增。 */
	void RecordOwnerAuthorityConfirmationForAutomationTest();
	int32 GetOwnerAuthorityConfirmationCountForAutomationTest() const;

	/** 权威提交次数：Fire 成功扣弹并执行开火行为后递增。 */
	int32 GetAuthorityShotCountForAutomationTest() const;

	/** 远端确认反馈次数；P1-B 起由 MulticastPlayFiringFX 在非拥有端递增。 */
	int32 GetRemoteConfirmedFeedbackCountForAutomationTest() const;
	int32 GetOwnerMuzzleFeedbackCountForAutomationTest() const { return OwnerMuzzleFeedbackCount; }
	int32 GetOwnerSoundFeedbackCountForAutomationTest() const { return OwnerSoundFeedbackCount; }
	int32 GetRemoteMuzzleFeedbackCountForAutomationTest() const { return RemoteMuzzleFeedbackCount; }
	int32 GetRemoteSoundFeedbackCountForAutomationTest() const { return RemoteSoundFeedbackCount; }
	int32 GetLastOwnerFeedbackSequenceForAutomationTest() const { return LastOwnerFeedbackSequence; }
	int32 GetLastOwnerConfirmationSequenceForAutomationTest() const { return LastOwnerConfirmationSequence; }
	float GetMinimumOwnerFeedbackIntervalForAutomationTest() const { return MinimumOwnerFeedbackInterval; }
	void ResetOwnerFeedbackTimingForAutomationTest();

	/** 只读探针：当前开火标志。 */
	bool IsFiringForAutomationTest() const { return bIsFiring; }
	/** 只读探针：最近一次权威射击的游戏时间。 */
	float GetTimeOfLastShotForAutomationTest() const { return TimeOfLastShot; }
	/** 只读探针：权威 RefireTimer 是否活动。 */
	bool IsRefireTimerActiveForAutomationTest() const;

	/** P1 统一预测日志标记（武器端）。只输出武器自身持有的字段，
	 *  不把 GA 的 PredictionKey 持久写进池化 Weapon。 */
	void LogFireFeedbackMarker(const TCHAR* Marker, int32 ShotOrdinal, int32 Count) const;

private:
	int32 PredictedOwnerFeedbackCount = 0;
	int32 ConfirmedBackfillFeedbackCount = 0;
	int32 OwnerAuthorityConfirmationCount = 0;
	int32 AuthorityShotCount = 0;
	int32 RemoteConfirmedFeedbackCount = 0;
	int32 OwnerMuzzleFeedbackCount = 0;
	int32 OwnerSoundFeedbackCount = 0;
	int32 RemoteMuzzleFeedbackCount = 0;
	int32 RemoteSoundFeedbackCount = 0;
	int32 FireFeedbackEventSequence = 0;
	int32 LastOwnerFeedbackSequence = 0;
	int32 LastOwnerConfirmationSequence = 0;
	float LastOwnerFeedbackTime = -1.0f;
	float MinimumOwnerFeedbackInterval = TNumericLimits<float>::Max();

public:
#endif

	float GetFirstPersonCompositionDrop() const { return FirstPersonCompositionDrop; }
};
