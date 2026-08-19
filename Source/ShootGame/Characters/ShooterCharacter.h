// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
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

	/** List of weapons picked up by the character */
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

	/** 服务器（含监听主机）在占有发生时建立 ASC ActorInfo。 */
	virtual void PossessedBy(AController* NewController) override;

	/** 客户端在 PlayerState 复制到达后建立 ASC ActorInfo（含重生后的新 Pawn）。 */
	virtual void OnRep_PlayerState() override;

	/** 幂等建立 PlayerState ASC 的 Owner/Avatar 关系。 */
	void InitializeAbilityActorInfo();

	/** 客户端收到 RemoteViewPitch 后刷新远端第三人称瞄准俯仰。 */
	virtual void PostNetReceive() override;

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

	/** 当前由服务器选择并复制给客户端的武器。 */
	AShooterWeapon* GetCurrentWeapon() const { return CurrentWeapon; }

	/** 返回角色的 Inventory 组件；该组件是武器持有关系的逻辑权威源。 */
	UShooterInventoryComponent* GetInventoryComponent() const { return InventoryComponent; }

public:

	/** 处理开火输入：本地只向服务器提交请求，由服务器权威执行 */
	UFUNCTION(BlueprintCallable, Category="Input")
	void DoStartFiring();

	/** 处理停止开火输入：本地只向服务器提交请求，由服务器权威执行 */
	UFUNCTION(BlueprintCallable, Category="Input")
	void DoStopFiring();

	/** 服务器端：校验后开始开火 */
	UFUNCTION(Server, Reliable)
	void ServerStartFire();

	/** 服务器端：停止开火 */
	UFUNCTION(Server, Reliable)
	void ServerStopFire();

	/** 广播开火动画到所有客户端（不可靠，允许丢失） */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayFiringMontage(UAnimMontage* Montage);

	/** 处理切换武器输入 */
	UFUNCTION(BlueprintCallable, Category="Input")
	void DoSwitchWeapon();

	/** 服务器校验并执行切枪。 */
	UFUNCTION(Server, Reliable)
	void ServerSwitchWeapon();

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

	/** 将 Character 自带的远程视角数据写入模板第三人称 AnimBP。 */
	void ApplyRemoteAimPitch();

	/** 本次伤害的击杀者；Health 归零桥接时用于现有 Death/Kill/计分闭环。 */
	AController* PendingDeathInstigator = nullptr;

	/** Called when this character's HP is depleted */
	void Die(AController* KillerController);

	/** Called to allow Blueprint code to react to this character's death */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta = (DisplayName = "On Death"))
	void BP_OnDeath();

	/** Called from the respawn timer to destroy this character and force the PC to respawn */
	void OnRespawn();
};
