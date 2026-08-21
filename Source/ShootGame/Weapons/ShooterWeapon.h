// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShooterWeaponHolder.h"
#include "Animation/AnimInstance.h"
#include "ShooterWeapon.generated.h"

class IShooterWeaponHolder;
class AShooterProjectile;

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

protected:

	/** Cast pointer to the weapon owner */
	IShooterWeaponHolder* WeaponOwner = nullptr;

	/** 绑定的 Inventory WeaponInstance 身份；OwnerOnly 复制，远端表现不需要该数据。 */
	UPROPERTY(ReplicatedUsing = OnRep_BoundInstanceId, VisibleAnywhere, BlueprintReadOnly, Category="Inventory")
	FGuid BoundInstanceId;

	UFUNCTION()
	void OnRep_BoundInstanceId();

	/** Type of projectiles this weapon will shoot */
	UPROPERTY(EditAnywhere, Category="Ammo")
	TSubclassOf<AShooterProjectile> ProjectileClass;

	/** Number of bullets in a magazine */
	UPROPERTY(EditAnywhere, Category="Ammo", meta = (ClampMin = 0, ClampMax = 100))
	int32 MagazineSize = 10;

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

	/** Name of the first person muzzle socket where projectiles will spawn */
	UPROPERTY(EditAnywhere, Category="Aim")
	FName MuzzleSocketName;

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

	/** Called when the weapon's owner is destroyed */
	UFUNCTION()
	void OnOwnerDestroyed(AActor* DestroyedActor);

	/** 幂等绑定当前 Owner；允许 Owner 晚于武器 BeginPlay 到达客户端。 */
	void InitializeWeaponOwner();

public:

	/** 刷新本地 CurrentBullets 镜像与拥有者 HUD；Inventory 数据变化时由两边共同调用。 */
	void RefreshAmmoMirror();

	/** Activates this weapon and gets it ready to fire */
	void ActivateWeapon();

	/** Deactivates this weapon */
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

	/** Fire a projectile towards the target location */
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

	/** 返回第三人称网格 Muzzle socket 的世界变换；网格或 socket 缺失时回退到武器 Actor 变换。
	 *  观察端调试与表现测量使用该变换读取“第三人称真实枪口 Forward”。 */
	FTransform GetThirdPersonMuzzleWorldTransform() const;

	/** 返回服务器权威换弹事务等待时长。 */
	float GetReloadDuration() const { return ReloadDuration; }

	/** 返回服务器权威切枪事务等待时长。 */
	float GetEquipDuration() const { return EquipDuration; }

	/** Returns the current bullet count；绑定 Inventory 时从 MagazineAmmo 读取。 */

	/** Returns the first person anim instance class */
	const TSubclassOf<UAnimInstance>& GetFirstPersonAnimInstanceClass() const;

	/** Returns the third person anim instance class */
	const TSubclassOf<UAnimInstance>& GetThirdPersonAnimInstanceClass() const;

	/** Returns the magazine size */
	int32 GetMagazineSize() const { return MagazineSize; };

	/** Returns the current bullet count；绑定 Inventory 时从 MagazineAmmo 读取。 */
	int32 GetBulletCount() const;

	/** 返回绑定的 WeaponInstance ID；无效表示尚未接入 Inventory 的兼容路径。 */
	FGuid GetBoundInstanceId() const { return BoundInstanceId; }

	/** 服务器在创建 WeaponActor 后写入绑定关系。 */
	void SetBoundInstanceId(const FGuid& InInstanceId) { BoundInstanceId = InInstanceId; }

	/** 判断当前是否还有可发射弹药；绑定 Inventory 时检查权威 MagazineAmmo。 */
	bool CanConsumeAmmo() const;

	/** 服务器权威扣减一发；绑定 Inventory 时写入 WeaponInstanceData，否则保留旧 CurrentBullets 兼容路径。 */
	bool ConsumeAmmo();

	/** 弹药在 Fire 事务中耗尽时广播；GA_Fire 用它幂等结束 Ability。 */
	FShooterWeaponOutOfAmmoDelegate OnOutOfAmmo;
};
