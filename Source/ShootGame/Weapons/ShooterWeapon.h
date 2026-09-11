// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShooterAnimNotify_WeaponSound.h"
#include "ShooterWeaponHolder.h"
#include "Animation/AnimInstance.h"
#include "ShooterPoolableActor.h"
#include "ShooterWeapon.generated.h"

class IShooterWeaponHolder;
class AShooterProjectile;
class UShooterActorPoolSubsystem;
class UShooterWeaponFireBehavior;
struct FShooterWeaponConfigRow;
struct FShooterWeaponFireContext;

/**
 * WeaponActor 生命周期状态（实施计划 4.4）：InPool -> Holstered -> Equipping -> Equipped -> Holstered -> InPool。
 *
 * 权威边界：
 * - 服务器权威端是唯一执行状态转换拒绝的一端（B2 验证项「非法状态转换被拒绝」只在权威端成立）；
 * - 客户端状态是对复制结果的镜像。BoundInstanceId 是 COND_OwnerOnly，远端客户端永远读不到，
 *   因此客户端不参与拒绝判定，避免破坏远端第三人称武器表现（B2 修正项）；
 * - 可见性不属于本状态机契约：隐藏由池统一归还清理、Inventory 授予后隐藏、
 *   Equipment / Character 表现收敛负责激活时解除隐藏。状态本身只表达身份与装备语义。
 */
UENUM()
enum class EShooterWeaponLifecycleState : uint8
{
	/** 在池内：无 Instance 绑定、无 Owner 缓存。 */
	InPool,
	/** 已授予并绑定 Instance：有 Owner、未装备。 */
	Holstered,
	/** 装备事务提交中：第一版在 Equipment 原子提交内瞬态通过，是 GA_Equip 时序的扩展点。 */
	Equipping,
	/** 当前装备：激活并对外表现。 */
	Equipped,
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
class SHOOTGAME_API AShooterWeapon : public AActor, public IShooterPoolableActor
{
	GENERATED_BODY()
	
	/** First person perspective mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	/** Third person perspective mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* ThirdPersonMesh;

protected:

	/** Cast pointer to the weapon owner */
	IShooterWeaponHolder* WeaponOwner = nullptr;

	/** 已完成领域绑定的 Owner；用于 Owner 复制切换时解除旧 OnDestroyed 委托。 */
	UPROPERTY(Transient)
	TObjectPtr<AActor> CachedWeaponOwnerActor;

	/** 绑定的 Inventory WeaponInstance 身份；OwnerOnly 复制，远端表现不需要该数据。 */
	UPROPERTY(ReplicatedUsing = OnRep_BoundInstanceId, VisibleAnywhere, BlueprintReadOnly, Category="Inventory")
	FGuid BoundInstanceId;

	/**
	 * 武器模板行名；复制给所有端，客户端与服务器通过同一张 DT_WeaponData 恢复只读配置。
	 * 空名表示尚未绑定模板行（NPC / 测试兼容路径），此时沿用 WeaponActor 自身的默认配置。
	 */
	UPROPERTY(ReplicatedUsing = OnRep_WeaponRowName, VisibleAnywhere, BlueprintReadOnly, Category="Inventory")
	FName WeaponRowName;

	/** 行配置实例化出的开火行为；无持久可变状态，不复制，两端各自按行创建。 */
	UPROPERTY(Transient)
	TObjectPtr<UShooterWeaponFireBehavior> FireBehaviorInstance;

	/** 生命周期状态；服务器权威，客户端经 OnRep 镜像，不复制。 */
	EShooterWeaponLifecycleState LifecycleState = EShooterWeaponLifecycleState::InPool;

	UFUNCTION()
	void OnRep_BoundInstanceId();

	UFUNCTION()
	void OnRep_WeaponRowName();

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

	/** 兼容镜像：Inventory 建立后复制 Inventory.MagazineAmmo；未绑定的旧路径仍直接使用该字段。 */
	UPROPERTY(ReplicatedUsing=OnRep_CurrentBullets, VisibleAnywhere, Category="Ammo")
	int32 CurrentBullets = 0;

	UFUNCTION()
	void OnRep_CurrentBullets();
	
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

	/** Game time of last shot fired, used to enforce refire rate on semi auto */
	float TimeOfLastShot = 0.0f;

	/** If true, the weapon is currently firing */
	bool bIsFiring = false;

	/** Timer to handle full auto refiring */
	FTimerHandle RefireTimer;

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

	/** 返回本 Actor 所在 World 的对象池；World 不支持或已销毁时返回 nullptr。 */
	UShooterActorPoolSubsystem* GetPoolSubsystem() const;

	/**
	 * 统一状态转换入口：状态未变化时是安全 no-op，真实变化时输出一条 Verbose 诊断。
	 * 表现收敛会重复调用同一转换，诊断必须保持低噪声。
	 */
	void SetLifecycleState(EShooterWeaponLifecycleState NewState, const TCHAR* Reason);

	/** 解析本 Actor 绑定的武器模板行；未绑定行名、表缺失或行缺失时返回 nullptr。 */
	const FShooterWeaponConfigRow* ResolveWeaponRow() const;

	/**
	 * 把武器模板行的只读配置应用到本 Actor 的运行时镜像（网格、动画类、表现资产、
	 * 弹药经济、开火节奏、时序、Socket、视角参数）并实例化行的 FireBehaviorClass。
	 * 模板数据本身只读：本函数只写 Actor 自身状态，不修改行或表。
	 */
	void ApplyWeaponRow(const FShooterWeaponConfigRow& Row);

public:

	/** 刷新本地 CurrentBullets 镜像与拥有者 HUD；Inventory 数据变化时由两边共同调用。 */
	void RefreshAmmoMirror();

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

protected:

	/** Fire the weapon */
	virtual void Fire();

	/** Called when the refire rate time has passed while shooting semi auto weapons */
	void FireCooldownExpired();

	/**
	 * 服务器权威开火执行：绑定武器模板行且该行配置了行为类时把弹丸生成委托给行为，
	 * 否则走 NPC / 旧测试兼容的 FireProjectile 路径；表现入口统一留在本 Actor。
	 */
	void ExecuteFireAtTarget(const FVector& TargetLocation);

	/** 旧弹丸生成路径：仅在武器模板行未配置行为类时执行（NPC / 测试兼容，B4 记录遗留边界）。 */
	virtual void FireProjectile(const FVector& TargetLocation);

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

	/** Returns the current bullet count；绑定 Inventory 时从 MagazineAmmo 读取。 */

	/** Returns the first person anim instance class */
	const TSubclassOf<UAnimInstance>& GetFirstPersonAnimInstanceClass() const;

	/** Returns the third person anim instance class */
	const TSubclassOf<UAnimInstance>& GetThirdPersonAnimInstanceClass() const;

	/** Returns the magazine size；绑定武器模板行后即为该行的弹匣容量。 */
	int32 GetMagazineSize() const { return MagazineSize; };

	/** Returns the current bullet count；绑定 Inventory 时从 MagazineAmmo 读取。 */
	int32 GetBulletCount() const;

	/** 返回初始备弹声明值；-1 表示自动（MagazineSize × 3），>=0 为显式有限值。 */
	int32 GetInitialReserveAmmo() const { return InitialReserveAmmo; }

	/** 返回当前备弹；绑定 Inventory 时从权威 ReserveAmmo 读取，未绑定旧路径没有备弹概念、返回 0。 */
	int32 GetReserveAmmo() const;

	/** 返回绑定的 WeaponInstance ID；无效表示尚未接入 Inventory 的兼容路径。 */
	FGuid GetBoundInstanceId() const { return BoundInstanceId; }

	/** 返回武器模板行名；空名表示尚未绑定模板行（NPC / 测试兼容路径）。 */
	FName GetWeaponRowName() const { return WeaponRowName; }

	/**
	 * 解析本次开火应使用的正式行为：绑定模板行时由行的 FireBehaviorClass 实例化。
	 * 返回空表示走兼容路径（未绑定模板行或该行未配置行为类）。
	 */
	UShooterWeaponFireBehavior* ResolveFireBehavior() const;

	/**
	 * 服务器写入 Instance 与武器模板行绑定，并驱动 InPool <-> Holstered 转换。
	 * 行名有效时立即把该行的只读配置应用到本 Actor；
	 * 权威端在 Equipped/Equipping 状态下拒绝改写（非法转换 fail closed）；
	 * 客户端只镜像（远端读不到 OwnerOnly 的 BoundInstanceId，行名按复制顺序各自应用）。
	 */
	void SetInstanceBinding(const FGuid& InInstanceId, FName InWeaponRowName = NAME_None);

	/**
	 * 把本 Actor 当前的配置镜像导出为一条武器模板行。
	 * 只服务一次性资产迁移工具与自动化测试构造行数据，不参与运行时游戏逻辑。
	 */
	FShooterWeaponConfigRow CaptureWeaponConfigRow() const;

	/**
	 * 服务器权威：只绑定武器模板行、不建立 Inventory 实例（NPC 等无 Inventory 的拥有者），
	 * 并立即把该行的只读配置应用到本 Actor。空行名表示继续使用 WeaponActor 自身默认配置。
	 */
	void SetWeaponRow(FName InWeaponRowName);

	//~ Begin IShooterPoolableActor
	/** 池取出复位：清零开火节拍等运行时状态，并重新绑定新 Owner；Instance 绑定由 Inventory 在取出后写入。 */
	virtual void OnAcquiredFromPool() override;
	/** 池归还幂等清理：停 Timer、解 Delegate、清 Owner 缓存与 Instance 绑定，回到 InPool。 */
	virtual void OnReleasedToPool() override;
	//~ End IShooterPoolableActor

	/** 判断当前是否还有可发射弹药；绑定 Inventory 时检查权威 MagazineAmmo。 */
	bool CanConsumeAmmo() const;

	/** 服务器权威扣减一发；绑定 Inventory 时写入 WeaponInstanceData，否则保留旧 CurrentBullets 兼容路径。 */
	bool ConsumeAmmo();

	/** 弹药在 Fire 事务中耗尽时广播；GA_Fire 用它幂等结束 Ability。 */
	FShooterWeaponOutOfAmmoDelegate OnOutOfAmmo;

	float GetFirstPersonCompositionDrop() const { return FirstPersonCompositionDrop; }
};
