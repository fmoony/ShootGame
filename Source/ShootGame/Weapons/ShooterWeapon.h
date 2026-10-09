// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Weapons/Animation/ShooterAnimNotify_WeaponSound.h"
#include "Weapons/Interfaces/ShooterWeaponHolder.h"
#include "Animation/AnimInstance.h"
#include "Weapons/Data/ShooterWeaponConfigRow.h"
#include "Weapons/ShooterAmmoDisplayState.h"
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

class USkeletalMeshComponent;
class UAnimMontage;
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

	/** 当前弹匣弹药；服务器权威，OwnerOnly 复制，Gameplay 与预算只读该值，HUD 使用独立显示入口。 */
	UPROPERTY(ReplicatedUsing=OnRep_MagazineAmmo, VisibleAnywhere, BlueprintReadOnly, Category="Ammo")
	int32 MagazineAmmo = 0;

	/** 当前备用弹药；服务器权威，OwnerOnly 复制；换弹事务从这里转进弹匣，只消耗不回复。 */
	UPROPERTY(ReplicatedUsing=OnRep_ReserveAmmo, VisibleAnywhere, BlueprintReadOnly, Category="Ammo")
	int32 ReserveAmmo = 0;

	UFUNCTION()
	void OnRep_MagazineAmmo(int32 OldMagazineAmmo);

	UFUNCTION()
	void OnRep_ReserveAmmo();

	/**
	 * 服务器权威一发 Shot 的裁决通知（OwnerOnly、可靠）。
	 *
	 * 只在服务器真实提交这一发时发出：一次 GA_Fire Activation 至多对应一条通知。
	 * 携带提交时点的显示快照，供拥有端 HUD 在不依赖属性到达顺序的前提下结清本次显示扣减。
	 *
	 * Reject 不需要本通道：引擎的 ClientActivateAbilityFailed（Client, Reliable）已经按
	 * 同一个 PredictionKey 送达拒绝结果，拥有端直接绑定该 PredictionKey 的 Rejected 委托。
	 * 两条通道都不使用 PredictionKey CaughtUp 代表任何裁决。
	 */
	UFUNCTION(Client, Reliable)
	void ClientShotCommitted(int32 ActivationKey, const FShooterAmmoDisplaySnapshot& Snapshot);

	/** 当前绑定到本武器的拥有端 Fire Ability；以 UObject 弱引用避免头文件循环依赖。 */
	TWeakObjectPtr<UObject> BoundPredictionAbility;

	/** 本地上下文代次；Owner 解绑时递增，不要求两端代次数值相同。 */
	uint32 AmmoPredictionGeneration = 0;

	/**
	 * 已本地预测消费、但尚未由 Activation 结果结算的弹药数量。
	 *
	 * 不复制，只存在于预测型 Owner 客户端（见 IsAmmoPredictionContext）：
	 * MagazineAmmo / ReserveAmmo 始终只表示服务器确认状态，本地预测只增加这个待结算计数。
	 */
	int32 PendingPredictedShots = 0;

	/** 显示真值与最终结果一起结清，不用预算 Pending 推断属性到达顺序。 */
	UPROPERTY(ReplicatedUsing=OnRep_AmmoDisplaySnapshot)
	FShooterAmmoDisplaySnapshot AmmoDisplaySnapshot;

	FShooterAmmoDisplayState AmmoDisplayState;

	UFUNCTION()
	void OnRep_AmmoDisplaySnapshot();

	void RefreshAuthorityAmmoDisplaySnapshot();

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

	/** 从启动快照应用的 TP 持姿资源，不逐帧查询 DataTable。 */
	UPROPERTY(EditAnywhere, Category="Animation")
	TObjectPtr<UAnimSequence> ThirdPersonHoldSequence;

	/** 从启动快照应用的 TP AimOffset。 */
	UPROPERTY(EditAnywhere, Category="Animation")
	TObjectPtr<UAimOffsetBlendSpace> ThirdPersonAimOffset;

	/** 从启动快照应用的 TP Reload Sequence。 */
	UPROPERTY(EditAnywhere, Category="Animation")
	TObjectPtr<UAnimSequence> ThirdPersonReloadSequence;

	/** 原 TP Aim IK 枪口安全距离参数。 */
	UPROPERTY(EditAnywhere, Category="Animation", meta=(ClampMin="0.0", Units="cm"))
	float ThirdPersonMinimumAimTargetDistanceFromMuzzle = 100.0f;

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

	/**
	 * 服务器权威最近一次真实提交 Shot 的游戏时间；未开火时为 NeverFiredShotTime。
	 *
	 * 只服务一个判据：本次提交是否已越过权威 RefireRate（CanCommitAuthorityShot）。
	 * 它不再被当作"是否开过枪"的哨兵分支，也不驱动任何连发；取用、归还、恢复初始弹药
	 * 都会把它复位为哨兵值，因此世界启动初期的第一枪不会被误判为仍在冷却。
	 */
	float TimeOfLastShot = NeverFiredShotTime;

	/** "本租用尚未开火"的哨兵时间：远早于任何世界时间，使射速判据自然成立。 */
	static constexpr float NeverFiredShotTime = -1000000.0f;

	/**
	 * 权威射速到期通知 Timer。
	 *
	 * 它只通知持有者"权威节拍已就绪"（NPC 用它决定是否再次提交开火意图），
	 * 绝不回调任何产生 Gameplay Shot 的函数：本武器没有任何"连续开火 Timer"。
	 */
	FTimerHandle RefireTimer;

	/**
	 * 本武器下一次允许"本地有效开火"的本地时间。
	 *
	 * 本地开火节拍同时承担两件事：半自动的"这次点击是否构成一次有效开火"，
	 * 以及全自动按住时"何时才值得再提交一次 GA_Fire 请求"的节流。
	 * 它只由"本地提交了一次 Shot Attempt"推进一次（无论这次是否提前表现），
	 * 不复制、不读权威 RefireTimer / TimeOfLastShot，与服务器权威射速是两套互不干涉的时钟。
	 *
	 * 推进方式按武器模式分裂，见 AdvanceLocalFireCooldown：
	 * 半自动按本次实际击发重新锚定；全自动保持理论射速网格的相位，只在明显滞后时重新基线。
	 */
	float LocalFireCooldownEndTime = NoLocalCadenceTime;

	/** "本租用尚未建立本地节拍"的哨兵时间：远早于任何世界时间，使就绪判据无需特例分支。 */
	static constexpr float NoLocalCadenceTime = -1000000.0f;

	/**
	 * 权威射速比较容差。
	 *
	 * 客户端按自己的节拍提交请求，请求到达服务器的间隔会带上网络与帧量化抖动；
	 * 没有容差时，节拍正确的连发请求会有一半落在 RefireRate 之前被拒绝，表现为掉发。
	 * 容差只影响"这一发是否早了一点点"，不累积：每次提交都会把 TimeOfLastShot 置为当前时间，
	 * 因此持续射速不可能超过 RefireRate。
	 */
	static constexpr float AuthorityRefireTolerance = 0.015f;

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

	/** 拥有者纯表现的唯一实现；只由本地预测开火入口调用。 */
	bool PlayOwnerShotFeedbackInternal();

	/** PendingPredictedShots 的统一减少入口；Marker 用于区分 Committed / Rejected 诊断。 */
	void ReducePendingPredictedAmmo(int32 Amount, const TCHAR* Marker);

	/** 权威射速到期：只通知持有者节拍已就绪，不产生任何 Shot。 */
	void HandleAuthorityRefireReady();

	/** 权威射速时钟复位为"已就绪"；生命周期边界与恢复初始弹药共用。 */
	void ResetAuthorityRefireClock();

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

	/**
	 * 服务器权威射速资格：距上一次真实提交是否已满 RefireRate。
	 *
	 * 半自动与全自动共用同一判据：一次 Activation 只提交一发，节拍由服务器独立判定，
	 * 不接受任何客户端上报的射击时间。无副作用，只读权威时钟。
	 */
	bool CanCommitAuthorityShot() const;

	/**
	 * 服务器权威单发提交：一次调用最多产生一发权威 Shot。
	 *
	 * 调用方（GA_Fire 的权威路径）必须已经通过 CanActivateAbility 的完整校验；
	 * 本函数按顺序执行权威扣弹 → 开火行为（弹丸与表现）→ 记录权威射速时钟 → 排下一次节拍通知，
	 * 并在真实提交后向拥有者客户端发送这一发的 Committed 裁决通知。
	 * 扣弹失败时不产生任何权威结果并返回 false。
	 */
	bool CommitSingleShot(int32 ActivationKey);

	/**
	 * 拥有者本地开火表现入口：唯一的第一人称表现来源。
	 * 内部先判定本地开火节拍，未越过时直接返回 false，绝不播放。
	 * 不写 MagazineAmmo / ReserveAmmo / TimeOfLastShot / RefireTimer /
	 * Inventory / Projectile，也不建立任何 Timer。
	 * 返回是否向表现通道提交了至少一项；返回值不表示当前机器一定具备音频或渲染设备。
	 */
	bool PlayOwnerPredictedShotFeedback();

	/**
	 * 本地开火节拍是否已越过：距上一次本地提交是否已满 RefireRate。
	 *
	 * 客户端两种模式都只用它决定"这次输入是否值得形成一次本地 Shot Attempt"：
	 * 半自动是硬失败判据，全自动是请求节流。该判定只读本地时钟，
	 * 不读权威 RefireTimer / TimeOfLastShot，也不读 Ammo / Reloading / Equipping / Dead 等复制状态。
	 */
	bool IsLocalFireCooldownReady() const;

	/** 距本地开火节拍结束还剩多久；已就绪时返回 0。 */
	float GetLocalFireCooldownRemaining() const;

	/**
	 * 按本武器 RefireRate 推进本地开火节拍。
	 *
	 * 唯一调用点是"本地提交了一次 Shot Attempt"处：无论这次是否提前表现（弹药预算不足时不表现），
	 * 都必须推进，否则按住输入会变成每帧一次的请求洪水。
	 * 与 Montage / Niagara / Sound 是否成功播放无关：表现通道的成败不得决定射击节拍。
	 *
	 * ActivationKey 只用于开发构建的节拍取证（把样本与这一发的 PredictionKey 对齐），
	 * 不参与任何判定；监听主机与 Standalone 传 0。
	 *
	 * 推进语义按模式分裂：全自动是"理论射速网格"追踪器，按上一次目标时间 + RefireRate
	 * 推进以保持相位，只在落后达到一个完整节拍时重新基线（防长卡顿后补发），
	 * 且不会把下一发排到"本发实际时间 - 权威容差"之前；
	 * 半自动是"单次击发后的冷却闸门"，始终以本次实际击发为锚点。
	 */
	void AdvanceLocalFireCooldown(int32 ActivationKey = 0);

	/** 本地预测节拍只读配置。 */
	bool IsFullAuto() const { return bFullAuto; }
	float GetRefireRate() const { return RefireRate; }

protected:

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

	/** 只读动画配置出口；资源与 WeaponActor 的永久配置共同存活。 */
	UAnimSequence* GetThirdPersonHoldSequence() const { return ThirdPersonHoldSequence; }
	UAimOffsetBlendSpace* GetThirdPersonAimOffset() const { return ThirdPersonAimOffset; }
	UAnimSequence* GetThirdPersonReloadSequence() const { return ThirdPersonReloadSequence; }
	float GetThirdPersonMinimumAimTargetDistanceFromMuzzle() const
	{
		return ThirdPersonMinimumAimTargetDistanceFromMuzzle;
	}

	/** Returns the magazine size；应用武器模板行后即为该行的弹匣容量。 */
	int32 GetMagazineSize() const { return MagazineSize; };

	/** Returns the current bullet count；弹药权威在本 Actor 的 MagazineAmmo。 */
	int32 GetBulletCount() const;

	/** HUD 唯一弹匣与备弹入口，真实 Ammo Getter 的语义保持不变。 */
	int32 GetDisplayedMagazineAmmo() const;
	int32 GetDisplayedReserveAmmo() const;

	void BeginAmmoDisplayActivation(int32 PredictionKey);
	void RecordAmmoDisplayPredictedShot(int32 PredictionKey);

	/**
	 * 撤销该 PredictionKey 的本地显示预测（中性语义）。
	 * 服务器拒绝与 Spec 生命周期退休共用这一条实现：显示层做的动作完全相同，
	 * 区别只在调用方的日志与统计，因此这里不携带任何 Reject 语义。
	 */
	void RetireAmmoDisplayActivation(int32 PredictionKey);

	/** 服务器拒绝语义入口：只转发到中性实现，行为完全相同。 */
	void RejectAmmoDisplayActivation(int32 PredictionKey) { RetireAmmoDisplayActivation(PredictionKey); }
	int32 GetUnsettledAmmoDisplayCount() const { return AmmoDisplayState.GetUnsettledCount(); }

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
	 * 因此预算对账只用于非权威的拥有者客户端。
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

	/** 服务器提交确认：减少本次 Activation 的 PendingPredictedShots（只由拥有端 Shot 记录调用）。 */
	void ConfirmPredictedAmmo(int32 Amount);

	/** 拥有端 Fire Ability 在本地激活时绑定；用于接收服务器每一发的裁决通知。 */
	void BindPredictionAbility(UShooterGameplayAbility_Fire* Ability);

	/**
	 * 条件解绑：仅当当前绑定对象就是调用者时才清空。
	 *
	 * pooled WeaponActor 会被同一 Owner 复用：旧 Spec H1 的迟到移除（OnRemoveAbility）
	 * 绝不能把新 Spec H2 已经建立的绑定清掉，因此解绑必须是"谁的绑定谁解"。
	 */
	void UnbindPredictionAbilityIfBoundTo(const UShooterGameplayAbility_Fire* Ability);

	/** Owner / 池 / Destroy 边界解绑裁决转发；不依赖 PredictionKey 大小。 */
	void ClearPredictionAbility();

	/** 账本记录与校验本地上下文代次。 */
	uint32 GetAmmoPredictionGeneration() const { return AmmoPredictionGeneration; }

	/** 生命周期边界：旧预测上下文整体失效（Owner 变化 / 归还池 / teardown）。 */
	void ResetAmmoPrediction();

	/** 只读观测：当前待结清的预测发数。 */
	int32 GetPendingPredictedShots() const { return PendingPredictedShots; }

#if WITH_DEV_AUTOMATION_TESTS
public:
	/** 测试专用：直接写权威弹药（换弹网络测试构造任意起点状态）；生产代码不得调用。 */
	void SetAmmoForAutomationTest(int32 InMagazineAmmo, int32 InReserveAmmo)
	{
		MagazineAmmo = InMagazineAmmo;
		ReserveAmmo = InReserveAmmo;
		RefreshAuthorityAmmoDisplaySnapshot();
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

	/** 测试诊断哨兵：Owner historical cosmetic replay 已无生产入口，当前值必须恒为 0。 */
	int32 GetOwnerConfirmedReplayCountForAutomationTest() const;

	/** 测试专用：直接建立 PendingPredictedShots 起点；只用于验证预测预算算术。 */
	void SetPendingPredictedShotsForAutomationTest(int32 InPendingShots);

	/**
	 * 测试专用：无条件建立一次显示预测激活。
	 *
	 * 生产入口 BeginAmmoDisplayActivation 只在拥有端预测上下文生效，而单机测试世界没有预测视图；
	 * 本入口只写本地显示覆盖层，不碰弹药、预算或复制字段。
	 */
	void SeedAmmoDisplayActivationForAutomationTest(int32 PredictionKey);

	/** 测试专用：设置本地开火节拍剩余时间；用于构造"本地节拍尚未就绪"的夹具。 */
	void SetLocalFireCooldownRemainingForAutomationTest(float RemainingSeconds);

	/** 测试专用：把本地节拍终点设成"当前时间 - LagSeconds"，用于构造节拍已经迟到的场景。 */
	void SetLocalFireCooldownLagForAutomationTest(float LagSeconds);

	/** 测试专用：写入权威射速时钟；RemainingSeconds > 0 表示仍在冷却。 */
	void SetAuthorityRefireRemainingForAutomationTest(float RemainingSeconds);

	/**
	 * 测试专用：在两端写入同一个 RefireRate，构造指定 RPM 的射速场景。
	 * 生产代码里该值只来自武器模板行；网络测试必须两端一致，否则客户端请求节拍与服务器权威射速会错位。
	 */
	void SetRefireRateForAutomationTest(float InRefireRate) { RefireRate = FMath::Max(InRefireRate, 0.001f); }

	/** 拥有者本地预测反馈提交次数，由 PlayOwnerPredictedShotFeedback 递增。 */
	int32 GetPredictedOwnerFeedbackCountForAutomationTest() const;

	/** Multicast 到达拥有者并跳过可见 FX 的次数；由 MulticastPlayFiringFX 递增。 */
	void RecordOwnerAuthorityConfirmationForAutomationTest();
	int32 GetOwnerAuthorityConfirmationCountForAutomationTest() const;

	/** 权威提交次数：CommitSingleShot 成功扣弹并执行开火行为后递增。 */
	int32 GetAuthorityShotCountForAutomationTest() const;

	/** 远端确认反馈次数；由 MulticastPlayFiringFX 在非拥有端递增。 */
	int32 GetRemoteConfirmedFeedbackCountForAutomationTest() const;
	int32 GetOwnerMuzzleFeedbackCountForAutomationTest() const { return OwnerMuzzleFeedbackCount; }
	/** 拥有端单发裁决通知（Committed / Rejected）的接收次数。 */
	int32 GetShotVerdictReceivedCountForTest() const { return ShotVerdictReceivedCountForTest; }

	/**
	 * 本武器上"旧 Spec 生命周期结束导致的解绑"次数。
	 *
	 * Fire Spec 被移除时它的实例会条件解绑（仅当绑定对象就是它自己）；
	 * 该计数是"Spec Removal 清理确实发生过、且没有误清新 Spec 绑定"的直接证据。
	 */
	int32 GetSpecRemovalUnbindCountForTest() const { return SpecRemovalUnbindCountForTest; }

	/**
	 * 本武器上"旧 Spec 生命周期结束触发预测清算"的次数。
	 *
	 * 无论当时是否还有未结记录都会计数：它证明 Spec Removal 的收口确实执行过
	 * （清理可能因为武器侧生命周期已经先一步结清而无可清理）。
	 */
	int32 GetSpecRemovalCleanupCountForTest() const { return SpecRemovalCleanupCountForTest; }

	/** 只在 Spec Removal 清算路径内调用；开发构建登记次数。 */
	void RecordSpecRemovalCleanupForTest();

	int32 GetOwnerSoundFeedbackCountForAutomationTest() const { return OwnerSoundFeedbackCount; }
	int32 GetRemoteMuzzleFeedbackCountForAutomationTest() const { return RemoteMuzzleFeedbackCount; }
	int32 GetRemoteSoundFeedbackCountForAutomationTest() const { return RemoteSoundFeedbackCount; }
	int32 GetLastOwnerFeedbackSequenceForAutomationTest() const { return LastOwnerFeedbackSequence; }
	int32 GetLastOwnerConfirmationSequenceForAutomationTest() const { return LastOwnerConfirmationSequence; }
	float GetMinimumOwnerFeedbackIntervalForAutomationTest() const { return MinimumOwnerFeedbackInterval; }
	void ResetOwnerFeedbackTimingForAutomationTest();

	// ---- 本地开火节拍取证：只在开发构建存在，只记录事实，不参与任何 Gameplay 判定 ----

	/**
	 * 一次本地 GA_Fire 激活的节拍取证样本。
	 *
	 * 它只回答一个问题——"这一发为什么发生在此时"：期望时间来自上一发推进出的本地节拍终点，
	 * 实际时间是本帧真正进入 ActivateAbility 的时间，两者之差就是这一发被帧边界推迟的量。
	 * 期望时间与上一发的实际时间之差（而不是与理论网格之差）正是"迟到是否被永久累计"的判据。
	 */
	struct FShooterLocalFireCadenceSample
	{
		/** 本租用内的本地激活序号，从 1 开始。 */
		int32 Ordinal = 0;

		/** 本次激活的 PredictionKey；监听主机与 Standalone 没有预测键时为 0。 */
		int32 ActivationKey = 0;

		/** 本次激活前的"下一次允许本地开火时间"；< 0 表示本租用尚未建立过本地节拍。 */
		float ExpectedDeadline = -1.0f;

		/** 本次激活真实发生的本地时间。 */
		float ActivationTime = 0.0f;

		/** ActivationTime - ExpectedDeadline：这一发被帧边界推迟的量；< 0 表示没有可比较的期望时间。 */
		float LagSeconds = -1.0f;

		/** 与上一次本地激活的时间差；< 0 表示本租用第一发。 */
		float IntervalSeconds = -1.0f;

		/** 本次激活所在帧的 DeltaSeconds。 */
		float FrameDeltaSeconds = 0.0f;

		/** 上一次本地 GA_Fire 结束 → 本次激活；< 0 表示本租用还没有结束过一发。 */
		float EndToActivationSeconds = -1.0f;

		/** 本次激活时本武器的 RefireRate。 */
		float RefireRate = 0.0f;
	};

	/** 一段连续本地开火窗口的节拍统计；样本不足以计算某项时对应字段保持 -1。 */
	struct FShooterLocalFireCadenceStats
	{
		int32 Samples = 0;
		int32 IntervalSamples = 0;
		float RefireRate = 0.0f;
		float MeanIntervalSeconds = -1.0f;
		float MinIntervalSeconds = -1.0f;
		float MaxIntervalSeconds = -1.0f;
		float MeanLagSeconds = -1.0f;
		float MaxLagSeconds = -1.0f;
		float MeanFrameDeltaSeconds = -1.0f;
		float MaxFrameDeltaSeconds = -1.0f;
		float MeanEndToActivationSeconds = -1.0f;
		float SpanSeconds = -1.0f;

		/**
		 * 累计相位误差 = (最后一发 - 第一发) - 间隔样本数 * RefireRate。
		 *
		 * 它把"每一发的迟到是否被写进后续节拍"压缩成一个数：相位被保持时它在一个帧间隔内往返，
		 * 相位被累计时它随发数单调增长。
		 */
		float CumulativePhaseErrorSeconds = -1.0f;
	};

	/** 清空本地节拍取证样本；夹具在每个测量步骤起点调用一次。 */
	void ResetLocalFireCadenceTraceForAutomationTest();

	/** 记录一次本地 GA_Fire 结束时间；由 GA_Fire 的本地结束路径调用，只服务 End → 再激活间隔。 */
	void RecordLocalFireEndForAutomationTest();

	/** 只读取证样本；超过上限后不再追加，Ordinal 仍然继续递增。 */
	const TArray<FShooterLocalFireCadenceSample>& GetLocalFireCadenceSamplesForAutomationTest() const
	{
		return LocalFireCadenceSamples;
	}

	/** 只读取证统计。 */
	FShooterLocalFireCadenceStats GetLocalFireCadenceStatsForAutomationTest() const;

	/** 只读探针：最近一次权威提交的游戏时间；小于 0 表示本租用尚未开火。 */
	float GetTimeOfLastShotForAutomationTest() const { return TimeOfLastShot; }
	/** 只读探针：权威射速到期通知 Timer 是否活动。 */
	bool IsRefireTimerActiveForAutomationTest() const;

	/** P1 统一预测日志标记（武器端）。只输出武器自身持有的字段，
	 *  不把 GA 的 PredictionKey 持久写进池化 Weapon。 */
	void LogFireFeedbackMarker(const TCHAR* Marker, int32 ShotOrdinal, int32 Count) const;

	/** 追加一条本地节拍取证样本；只由 AdvanceLocalFireCooldown 调用。 */
	void RecordLocalFireCadenceSampleForAutomationTest(int32 ActivationKey, float ActivationTime, float FrameDeltaSeconds);

	/** 权威提交节拍取证：提交时刻、与上一发的间隔、相对 RefireRate 的余量；只由 CommitSingleShot 调用。 */
	void LogAuthorityCommitCadenceForAutomationTest(int32 ActivationKey, float PreviousShotTime, float CommitTime) const;

	/** 取证样本上限：只限制内存占用，不改变任何行为。 */
	static constexpr int32 MaxLocalFireCadenceSamples = 1024;

private:
	TArray<FShooterLocalFireCadenceSample> LocalFireCadenceSamples;
	int32 LocalFireCadenceOrdinal = 0;
	float LastLocalFireEndTime = -1.0f;
	int32 PredictedOwnerFeedbackCount = 0;
	int32 OwnerConfirmedReplayFeedbackCount = 0;
	int32 OwnerAuthorityConfirmationCount = 0;
	int32 AuthorityShotCount = 0;
	int32 RemoteConfirmedFeedbackCount = 0;
	int32 OwnerMuzzleFeedbackCount = 0;
	int32 ShotVerdictReceivedCountForTest = 0;
	int32 SpecRemovalUnbindCountForTest = 0;
	int32 SpecRemovalCleanupCountForTest = 0;
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
