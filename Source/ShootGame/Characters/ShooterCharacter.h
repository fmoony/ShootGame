// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Engine/NetSerialization.h"
#include "ShooterWeaponHolder.h"
#include "ShooterCharacter.generated.h"

class AShooterWeapon;
class UAbilitySystemComponent;
class UInputAction;
class UInputComponent;
class UPawnNoiseEmitterComponent;
class USkeletalMeshComponent;
class UCameraComponent;
class UShooterInventoryComponent;
struct FInputActionValue;
struct FOnAttributeChangeData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBulletCountUpdatedDelegate, int32, MagazineSize, int32, Bullets);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDamagedDelegate, float, LifePercent);

/**
 *  A player controllable first person shooter character
 *  Manages a weapon inventory through the IShooterWeaponHolder interface
 *  Manages health and death
 */
UCLASS(abstract)
class SHOOTGAME_API AShooterCharacter : public ACharacter, public IShooterWeaponHolder
{
	GENERATED_BODY()

	/** Pawn mesh: first person view (arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;

	/** AI Noise emitter component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UPawnNoiseEmitterComponent* PawnNoiseEmitter;

protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* MouseLookAction;

	/** Fire weapon input action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* FireAction;

	/** Switch weapon input action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* SwitchWeaponAction;

	/** 换弹输入动作；只提交 Input.Reload，不直接修改弹药。 */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* ReloadAction;

	/** Name of the first person mesh weapon socket */
	UPROPERTY(EditAnywhere, Category ="Weapons")
	FName FirstPersonWeaponSocket = FName("HandGrip_R");

	/** Name of the third person mesh weapon socket */
	UPROPERTY(EditAnywhere, Category ="Weapons")
	FName ThirdPersonWeaponSocket = FName("HandGrip_R");

	/** Max distance to use for aim traces */
	UPROPERTY(EditAnywhere, Category ="Aim", meta = (ClampMin = 0, ClampMax = 100000, Units = "cm"))
	float MaxAimDistance = 10000.0f;

	/** Max HP this character can have */
	UPROPERTY(EditAnywhere, Category="Health")
	float MaxHP = 500.0f;

	/** Current HP remaining to this character */
	UPROPERTY(ReplicatedUsing=OnRep_CurrentHP, VisibleAnywhere, BlueprintReadOnly, Category="Health")
	float CurrentHP = 0.0f;

	/** 服务器权威死亡状态，所有客户端据此应用死亡表现。 */
	UPROPERTY(ReplicatedUsing=OnRep_IsDead, VisibleAnywhere, BlueprintReadOnly, Category="Health")
	bool bIsDead = false;

	UFUNCTION()
	void OnRep_CurrentHP();

	UFUNCTION()
	void OnRep_IsDead();

	/** 在服务器与客户端应用死亡后的本地状态和表现。 */
	void ApplyDeathState();

	/** Inventory 组件宿主：2A 只创建组件并暴露最薄访问入口，不接 Pickup。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Inventory", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UShooterInventoryComponent> InventoryComponent;

	/** 兼容镜像：Inventory 建立后的 WeaponActor 列表；逻辑权威源是 Inventory WeaponInstances。 */
	TArray<AShooterWeapon*> OwnedWeapons;

	/** 当前装备且可射击的武器。
	 *  服务器权威：只有服务器能修改，客户端通过 OnRep 接收 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeapon, VisibleAnywhere, BlueprintReadOnly, Category = "Weapons")
	TObjectPtr<AShooterWeapon> CurrentWeapon;

	/** 客户端收到服务器复制的当前武器时调用 */
	UFUNCTION()
	void OnRep_CurrentWeapon(AShooterWeapon* PreviousWeapon);

	/** 在服务器和客户端应用当前武器的表现（附着 + AnimBP）。可重复调用，无副作用。 */
	void ApplyCurrentWeapon();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, Category ="Destruction", meta = (ClampMin = 0, ClampMax = 10, Units = "s"))
	float RespawnTime = 5.0f;

	FTimerHandle RespawnTimer;

public:

	/** Bullet count updated delegate */
	FBulletCountUpdatedDelegate OnBulletCountUpdated;

	/** Damaged delegate */
	FDamagedDelegate OnDamaged;

public:

	/** Returns the first person mesh */
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

	/** Returns first person camera component */
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

	/** Constructor */
	AShooterCharacter();

	/** 返回 PlayerState 持有的玩家 ASC；PlayerState 或 ASC 不存在时返回 nullptr。 */
	UAbilitySystemComponent* GetAbilitySystemComponent() const;

	/** Health 属性变化桥接：服务器镜像旧 CurrentHP 并触发死亡闭环；客户端驱动 HUD 事件链。
	 *  由 PlayerState 的持久绑定转发调用。 */
	void HandleHealthAttributeChanged(const FOnAttributeChangeData& ChangeData);

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** 观察端每帧平滑表现目标（Tick）。 */
	virtual void Tick(float DeltaSeconds) override;

	/** 绘制只读瞄准调试；实现集中在 Characters/Debug/ShooterAimDebug.cpp。 */
	void DrawAimDebug() const;

	/** 与权威开火共用的目标 Trace；可选输出仅供只读调试复用命中信息。 */
	static FVector TracePreSpreadAimTarget(
		UWorld* World,
		const FVector& Start,
		const FVector& End,
		const AActor* IgnoredActor,
		FHitResult* OutHit = nullptr,
		bool* bOutPawnHit = nullptr);

	/** 服务器（含监听主机）在占有发生时建立 ASC ActorInfo。 */
	virtual void PossessedBy(AController* NewController) override;

	/** 客户端在 PlayerState 复制到达后建立 ASC ActorInfo（含重生后的新 Pawn）。 */
	virtual void OnRep_PlayerState() override;

	/** 幂等建立 PlayerState ASC 的 Owner/Avatar 关系。 */
	void InitializeAbilityActorInfo();

	/** 服务器表现目标：服务器从 Pawn 视点沿当前 ControlRotation 做无散布 Visibility Trace，
	 *  命中时保存 Impact Point，未命中时保存最大瞄准距离终点（B2）。
	 *  表现快照，不是命中缓存：开火时仍由 GetWeaponTargetLocation 现场重算权威目标。
	 *  只复制给非拥有者（COND_SkipOwner）：拥有者继续使用本地即时视角。 */
	UPROPERTY(ReplicatedUsing = OnRep_PresentationAimTarget, VisibleAnywhere, BlueprintReadOnly, Category = "Aim")
	FVector_NetQuantize PresentationAimTarget = FVector::ZeroVector;

	UFUNCTION()
	void OnRep_PresentationAimTarget();

	/** 表现目标服务器采样间隔（秒）。B2 调试起点 10Hz；最终频率按网络模拟与带宽统计决定。 */
	UPROPERTY(EditAnywhere, Category = "Aim", meta = (ClampMin = 0.02, Units = "s"))
	float PresentationAimSampleInterval = 0.1f;

	/** 表现目标变化门槛：目标位移小于该值且方向夹角小于门槛角度时跳过更新（减少无意义更新）。 */
	UPROPERTY(EditAnywhere, Category = "Aim", meta = (ClampMin = 0, Units = "cm"))
	float PresentationAimMinChangeDistance = 50.0f;

	/** 表现目标变化门槛：方向夹角（度）。 */
	UPROPERTY(EditAnywhere, Category = "Aim", meta = (ClampMin = 0, ClampMax = 90, Units = "Degrees"))
	float PresentationAimMinChangeAngle = 3.0f;

	/** 服务器表现目标采样定时器。 */
	FTimerHandle PresentationAimTimer;

	/** 服务器启动受限频率的表现目标采样（BeginPlay，仅 Authority）。 */
	void StartPresentationAimSampling();

	/** 服务器采样一次表现目标：超过变化门槛才写入复制字段。 */
	void SamplePresentationAimTarget();

	// ---- B3 观察端平滑与数据契约 ----

	/** 观察端当前平滑目标（本地成员，不复制）：每帧向最新网络目标指数插值。 */
	FVector SmoothedPresentationAimTarget = FVector::ZeroVector;

	/** 观察端本地有效标记（不复制）：
	 *  一旦收到有效的 PresentationAimTarget 就保持有效，直到死亡、切枪、传送等生命周期事件显式重置；
	 *  不再把“复制值未变化 / 未收到新包”当作目标失效。 */
	bool bPresentationAimTargetValid = false;

	/** 观察端上一帧视点位置（传送/重生检测）。 */
	FVector LastPresentationAimViewLocation = FVector::ZeroVector;

	/** 平滑指数速率（1/s）：越大收敛越快。B3 调试起点，最终值由 B6 观感验收决定。 */
	UPROPERTY(EditAnywhere, Category = "Aim", meta = (ClampMin = 0.0))
	float PresentationAimSmoothingRate = 8.0f;

	/** 观察端每帧平滑更新（SimulatedProxy，或 Listen Server 观察远端客户端 Pawn 的 Authority 非本地方）。 */
	void UpdatePresentationAimSmoothing(float DeltaSeconds);

	/** 重置平滑状态：武器切换、死亡、传送、首次初始化时直接采用最新目标，不从旧值插值。 */
	void ResetPresentationAimSmoothing();

	/** Gameplay cleanup */
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

	/** Called from Input Actions for movement input */
	void MoveInput(const FInputActionValue& Value);

	/** Called from Input Actions for looking input */
	void LookInput(const FInputActionValue& Value);

	/** Handles aim inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoAim(float Yaw, float Pitch);

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles jump start inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump end inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;

public:

	/** Handle incoming damage */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** 当前服务器权威生命值的本地副本。 */
	float GetCurrentHP() const { return CurrentHP; }

	/** 配置的最大生命值。 */
	float GetMaxHP() const { return MaxHP; }

	/** 当前是否处于已死亡状态。 */
	bool IsDead() const { return bIsDead; }

	/** 当前由服务器选择并复制给客户端的武器（IShooterWeaponHolder 最小只读接口）。 */
	virtual AShooterWeapon* GetCurrentWeapon() const override { return CurrentWeapon; }

	/** 公共表现侧命名：CurrentWeapon 即阶段计划中的 CurrentWeaponActor。 */
	AShooterWeapon* GetCurrentWeaponActor() const { return CurrentWeapon; }

	/** 2B 桥接：Inventory 成功创建 WeaponActor 后，由 Pickup 路径调用以更新兼容列表与当前武器表现。 */
	void HandleWeaponAddedToInventory(const FGuid& InstanceId);
	/** 服务器权威：原子提交 ActiveWeaponInstanceId、Weapon 可见性与 CurrentWeapon。 */
	bool CommitActiveWeapon(const FGuid& InstanceId);

	/** 返回角色的 Inventory 组件；该组件是武器持有关系的逻辑权威源。 */
	UShooterInventoryComponent* GetInventoryComponent() const { return InventoryComponent; }

	/** 只读访问服务器表现目标（观察端读取复制值；拥有者始终为零向量）。 */
	const FVector& GetPresentationAimTarget() const { return PresentationAimTarget; }

	/** 只读访问观察端平滑后的表现目标（本地成员；观察端消费入口）。 */
	const FVector& GetSmoothedPresentationAimTarget() const { return SmoothedPresentationAimTarget; }

	/** 观察端本地有效标记（Debug / 自动化验证只读入口）。 */
	bool IsPresentationAimTargetValid() const { return bPresentationAimTargetValid; }

	/** 纯判定：PresentationAimTarget 是否可建立有效状态（有限且非零）。 */
	static bool IsValidPresentationAimTargetValue(const FVector& Target);

	/** 纯判定：该 Role / NetMode / 本地控制组合是否应运行表现目标平滑。
	 *  SimulatedProxy 始终运行；Listen Server 观察远端客户端 Pawn 的 Authority 非本地方也运行；
	 *  Dedicated Server 不运行（不增加不可见动画的高成本表现工作）。 */
	static bool ShouldRunPresentationAimSmoothing(ENetRole LocalRole, ENetMode NetMode, bool bLocallyControlled);

	UFUNCTION(BlueprintPure, Category = "Aim")
	FVector GetPresentationAimTargetBP() const { return PresentationAimTarget; }

	UFUNCTION(BlueprintPure, Category = "Aim|Debug")
	FVector GetSmoothedPresentationAimTargetBP() const
	{
		return SmoothedPresentationAimTarget;
	}

	/** 观察端平滑后的表现角度（局部坐标，度）——AnimBP 数据契约出口（B3）。
	 *  模拟代理：由平滑目标相对 Mesh 变换计算局部 AimYaw / AimPitch；
	 *  拥有者或回退：基于 GetBaseAimRotation（本地视角 / 角色 Forward + 远端 Pitch）。 */
	UFUNCTION(BlueprintCallable, Category = "Aim")
	void GetAimPresentationAngles(float& OutAimYaw, float& OutAimPitch) const;

	/** AO 就绪形式的垂直瞄准值（sin(radians(AimPitch))，值域 [-1, 1]）：
	 *  与旧 PitchN 契约（瞄准前向与世界 Up 的点积）语义一致，供 AimOffset Y 轴直接消费。
	 *  AnimBP 蓝图契约改造后，运行时不再需要外部反射写入。 */
	UFUNCTION(BlueprintCallable, Category = "Aim")
	float GetAimPitchN() const;

	/** 最大瞄准距离（预散布目标 Trace 长度）。 */
	float GetMaxAimDistance() const { return MaxAimDistance; }

	/** 无副作用公共计算入口：从 Pawn 视点沿瞄准旋转做无散布 Visibility Trace，
	 *  命中返回 Impact Point，未命中返回最大距离终点。
	 *  表现同步采样与每次权威开火共用同一规则，但各自现场计算，不共享缓存值（B2 §避免第二套真相）。 */
	static FVector ComputePreSpreadAimTarget(const APawn* Pawn, float MaxDistance);

public:

	/** 处理开火输入：本地只向服务器提交请求，由服务器权威执行 */
	UFUNCTION(BlueprintCallable, Category="Input")
	void DoStartFiring();

	/** 处理停止开火输入：本地只向服务器提交请求，由服务器权威执行 */
	UFUNCTION(BlueprintCallable, Category="Input")
	void DoStopFiring();

	/** 处理换弹输入：本地只向服务器提交 Input.Reload，由服务器权威执行。 */
	UFUNCTION(BlueprintCallable, Category="Input")
	void DoReload();

	/** 广播开火动画到所有客户端（不可靠，允许丢失） */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayFiringMontage(UAnimMontage* Montage);

	/** 处理切换武器输入 */
	UFUNCTION(BlueprintCallable, Category="Input")
	void DoSwitchWeapon();


public:

	//~Begin IShooterWeaponHolder interface

	/** Attaches a weapon's meshes to the owner */
	virtual void AttachWeaponMeshes(AShooterWeapon* Weapon) override;

	/** Plays the firing montage for the weapon */
	virtual void PlayFiringMontage(UAnimMontage* Montage) override;

	/** Applies weapon recoil to the owner */
	virtual void AddWeaponRecoil(float Recoil) override;

	/** Updates the weapon's HUD with the current ammo count */
	virtual void UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize) override;

	/** Calculates and returns the aim location for the weapon */
	virtual FVector GetWeaponTargetLocation() override;

	/** Gives a weapon of this class to the owner */
	virtual void AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass) override;

	/** Activates the passed weapon */
	virtual void OnWeaponActivated(AShooterWeapon* Weapon) override;

	/** Deactivates the passed weapon */
	virtual void OnWeaponDeactivated(AShooterWeapon* Weapon) override;

	/** Notifies the owner that the weapon cooldown has expired and it's ready to shoot again */
	virtual void OnSemiWeaponRefire() override;

	//~End IShooterWeaponHolder interface

protected:

	/** Returns true if the character already owns a weapon of the given class */
	AShooterWeapon* FindWeaponOfType(TSubclassOf<AShooterWeapon> WeaponClass) const;

	/** 本次伤害的击杀者；Health 归零桥接时用于现有 Death/Kill/计分闭环。 */
	AController* PendingDeathInstigator = nullptr;

	/** 幂等取消 GA_Fire；死亡、切枪、角色销毁共用。 */
	void CancelFireAbility();

	/** 幂等取消 GA_Reload；死亡、切枪、角色销毁共用。 */
	void CancelReloadAbility();

	/** 幂等取消 GA_Equip；死亡、角色销毁与重生防御性清理共用。 */
	void CancelEquipAbility();

	/** Called when this character's HP is depleted */
	void Die(AController* KillerController);

	/** Called to allow Blueprint code to react to this character's death */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta = (DisplayName = "On Death"))
	void BP_OnDeath();

	/** Called from the respawn timer to destroy this character and force the PC to respawn */
	void OnRespawn();
};
