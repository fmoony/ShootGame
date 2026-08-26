// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "ShooterWeaponHolder.h"
#include "ShooterNPC.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPawnDeathDelegate);

class AShooterWeapon;
class UShooterAbilitySystemComponent;
class UShooterAttributeSet;
class UShooterGameplayAbility_Fire;
struct FOnAttributeChangeData;

/**
 *  A simple AI-controlled shooter game NPC
 *  Executes its behavior through a StateTree managed by its AI Controller
 *  Holds and manages a weapon
 *  NPC 的 ASC 由自身持有：Owner = Avatar = NPC，不依赖 PlayerController / PlayerState。
 */
UCLASS(abstract)
class SHOOTGAME_API AShooterNPC : public ACharacter, public IShooterWeaponHolder, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:

	/** 构造函数：创建 NPC 自身的 ASC 并启用 Minimal 复制模式。 */
	AShooterNPC();

	//~Begin IAbilitySystemInterface
	/** 返回由本 NPC 持有的能力系统组件。 */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~End IAbilitySystemInterface

	/** 返回由本 NPC 持有的属性集（Health / MaxHealth）。 */
	UShooterAttributeSet* GetAttributeSet() const { return AttributeSet; }

	/** 返回服务器配置的开火 Ability 类。 */
	TSubclassOf<UShooterGameplayAbility_Fire> GetFireAbilityClass() const { return FireAbilityClass; }

	/** 返回当前 NPC ASC 中 Fire Ability Spec 的数量。 */
	int32 GetFireAbilitySpecCount() const;

	/** 是否已进入死亡流程。 */
	bool IsDead() const { return bIsDead; }

	/** 服务器幂等授予 Fire Ability。 */
	void GrantFireAbility();

	/** 幂等取消 GA_Fire；死亡与销毁清理共用。 */
	void CancelFireAbility();

	/** Current HP for this character. It dies if it reaches zero through damage */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage")
	float CurrentHP = 100.0f;

protected:

	/** NPC 自身持有的能力系统组件；在专用服务器上无需 Controller 即可独立初始化。 */
	UPROPERTY(VisibleAnywhere, Category="Abilities")
	TObjectPtr<UShooterAbilitySystemComponent> AbilitySystemComponent;

	/** NPC 属性集子对象；NPC 属性不向客户端复制（Minimal），只由服务器维护。 */
	UPROPERTY(VisibleAnywhere, Category="Abilities")
	TObjectPtr<UShooterAttributeSet> AttributeSet;

	/** 服务器授予给 NPC 的开火 Ability 类；默认使用原生 GA_Fire。 */
	UPROPERTY(EditAnywhere, Category="Abilities")
	TSubclassOf<UShooterGameplayAbility_Fire> FireAbilityClass;

	/** 是否已绑定 Health 属性变化回调（幂等保护）。 */
	bool bHealthAttributeDelegateBound = false;

	/** 幂等注册属性集并绑定 Health 属性变化回调。 */
	void BindHealthAttributeDelegate();

	/** Health 属性变化桥接：服务器镜像旧 CurrentHP 并进入现有 NPC 死亡流程。 */
	void HandleHealthAttributeChanged(const FOnAttributeChangeData& ChangeData);

	/** Name of the collision profile to use during ragdoll death */
	UPROPERTY(EditAnywhere, Category="Damage")
	FName RagdollCollisionProfile = FName("Ragdoll");

	/** Time to wait after death before destroying this actor */
	UPROPERTY(EditAnywhere, Category="Damage")
	float DeferredDestructionTime = 5.0f;

	/** Team byte for this character */
	UPROPERTY(EditAnywhere, Category="Team")
	uint8 TeamByte = 1;

	/** Pointer to the equipped weapon */
	TObjectPtr<AShooterWeapon> Weapon;

	/** Type of weapon to spawn for this character */
	UPROPERTY(EditAnywhere, Category="Weapon")
	TSubclassOf<AShooterWeapon> WeaponClass;

	/** Name of the first person mesh weapon socket */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category ="Weapons")
	FName FirstPersonWeaponSocket = FName("HandGrip_R");

	/** Name of the third person mesh weapon socket */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category ="Weapons")
	FName ThirdPersonWeaponSocket = FName("HandGrip_R");

	/** Max range for aiming calculations */
	UPROPERTY(EditAnywhere, Category="Aim")
	float AimRange = 10000.0f;

	/** Cone variance to apply while aiming */
	UPROPERTY(EditAnywhere, Category="Aim")
	float AimVarianceHalfAngle = 10.0f;

	/** Minimum vertical offset from the target center to apply when aiming */
	UPROPERTY(EditAnywhere, Category="Aim")
	float MinAimOffsetZ = -35.0f;

	/** Maximum vertical offset from the target center to apply when aiming */
	UPROPERTY(EditAnywhere, Category="Aim")
	float MaxAimOffsetZ = -60.0f;

	/** Actor currently being targeted */
	TObjectPtr<AActor> CurrentAimTarget;

	/** If true, this character is currently shooting its weapon */
	bool bIsShooting = false;

	/** If true, this character has already died */
	bool bIsDead = false;

	/** Deferred destruction on death timer */
	FTimerHandle DeathTimer;

public:

	/** Delegate called when this NPC dies */
	FPawnDeathDelegate OnPawnDeath;

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Gameplay cleanup */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:

	/** Handle incoming damage */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

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

	/** 返回当前装备的 WeaponActor（IShooterWeaponHolder 最小只读接口）。 */
	virtual AShooterWeapon* GetCurrentWeapon() const override { return Weapon; }

	/** Activates the passed weapon */
	virtual void OnWeaponActivated(AShooterWeapon* Weapon) override;

	/** Deactivates the passed weapon */
	virtual void OnWeaponDeactivated(AShooterWeapon* Weapon) override;

	/** Notifies the owner that the weapon cooldown has expired and it's ready to shoot again */
	virtual void OnSemiWeaponRefire() override;

	//~End IShooterWeaponHolder interface

protected:

	/** Called when HP is depleted and the character should die */
	void Die();

	/** Called after death to destroy the actor */
	void DeferredDestruction();

public:

	/** Signals this character to start shooting at the passed actor */
	void StartShooting(AActor* ActorToShoot);

	/** Signals this character to stop shooting */
	void StopShooting();
};
