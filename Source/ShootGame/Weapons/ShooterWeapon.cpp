// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterWeapon.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "ShooterCharacter.h"
#include "ShooterInventoryComponent.h"
#include "ShooterProjectile.h"
#include "ShooterWeaponHolder.h"
#include "Components/SceneComponent.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"

AShooterWeapon::AShooterWeapon()
{
	PrimaryActorTick.bCanEverTick = true;

	// Weapons are spawned by the server and replicated to clients.
	bReplicates = true;
	SetReplicateMovement(false);

	// create the root
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// create the first person mesh
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
	FirstPersonMesh->SetupAttachment(RootComponent);

	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	FirstPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	FirstPersonMesh->bOnlyOwnerSee = true;

	// create the third person mesh
	ThirdPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Third Person Mesh"));
	ThirdPersonMesh->SetupAttachment(RootComponent);

	ThirdPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	ThirdPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
	ThirdPersonMesh->bOwnerNoSee = true;
}

void AShooterWeapon::BeginPlay()
{
	Super::BeginPlay();

	InitializeWeaponOwner();

	// 弹药只由服务器初始化，拥有者客户端通过复制获得。
	if (HasAuthority())
	{
		CurrentBullets = MagazineSize;
	}

}

void AShooterWeapon::OnRep_Owner()
{
	Super::OnRep_Owner();
	InitializeWeaponOwner();
}

void AShooterWeapon::InitializeWeaponOwner()
{
	AActor* OwningActor = GetOwner();
	if (!IsValid(OwningActor))
	{
		return;
	}

	OwningActor->OnDestroyed.AddUniqueDynamic(this, &AShooterWeapon::OnOwnerDestroyed);
	WeaponOwner = Cast<IShooterWeaponHolder>(OwningActor);
	PawnOwner = Cast<APawn>(OwningActor);

	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(OwningActor))
	{
		if (UShooterInventoryComponent* Inventory = ShooterCharacter->GetInventoryComponent();
			Inventory && BoundInstanceId.IsValid())
		{
			Inventory->RegisterWeaponActor(this);
		}

		// R4：玩家武器不再由 Weapon.BeginPlay/OwnerRep 自动附着；
		// 是否附着/激活只由 Equipment 根据“是否当前装备”决定。
		if (UShooterEquipmentComponent* Equipment = ShooterCharacter->GetEquipmentComponent())
		{
			Equipment->HandleWeaponActorReady(this);
			return;
		}
	}

	if (WeaponOwner)
	{
		WeaponOwner->AttachWeaponMeshes(this);
	}
}

void AShooterWeapon::OnRep_BoundInstanceId()
{
	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(GetOwner()))
	{
		if (UShooterInventoryComponent* Inventory = ShooterCharacter->GetInventoryComponent();
			Inventory && BoundInstanceId.IsValid())
		{
			Inventory->RegisterWeaponActor(this);
		}

		// BoundInstanceId 到达后，当前武器若已复制到 Equipment，这里补做幂等应用。
		if (UShooterEquipmentComponent* Equipment = ShooterCharacter->GetEquipmentComponent())
		{
			Equipment->HandleWeaponActorReady(this);
		}
	}
}

namespace ShooterWeaponInventory
{
	UShooterInventoryComponent* FindInventory(const AActor* WeaponActor)
	{
		const AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(
			WeaponActor ? WeaponActor->GetOwner() : nullptr);
		return ShooterCharacter ? ShooterCharacter->GetInventoryComponent() : nullptr;
	}

	const FShooterWeaponInstanceData* FindInstance(const AShooterWeapon* Weapon)
	{
		UShooterInventoryComponent* Inventory = FindInventory(Weapon);
		return Inventory ? Inventory->FindWeaponInstance(Weapon->GetBoundInstanceId()) : nullptr;
	}
}

int32 AShooterWeapon::GetBulletCount() const
{
	if (BoundInstanceId.IsValid())
	{
		if (const FShooterWeaponInstanceData* Instance = ShooterWeaponInventory::FindInstance(this))
		{
			return Instance->MagazineAmmo;
		}
	}

	return CurrentBullets;
}

bool AShooterWeapon::CanConsumeAmmo() const
{
	if (BoundInstanceId.IsValid())
	{
		if (UShooterInventoryComponent* Inventory = ShooterWeaponInventory::FindInventory(this))
		{
			return Inventory->CanConsumeMagazineAmmo(BoundInstanceId);
		}
	}

	return CurrentBullets > 0;
}

bool AShooterWeapon::ConsumeAmmo()
{
	if (!HasAuthority())
	{
		return false;
	}

	if (BoundInstanceId.IsValid())
	{
		if (UShooterInventoryComponent* Inventory = ShooterWeaponInventory::FindInventory(this))
		{
			return Inventory->ConsumeMagazineAmmo(BoundInstanceId, 1);
		}
	}

	if (CurrentBullets <= 0)
	{
		return false;
	}

	--CurrentBullets;
	return true;
}

void AShooterWeapon::RefreshAmmoMirror()
{
	if (!BoundInstanceId.IsValid())
	{
		return;
	}

	const FShooterWeaponInstanceData* Instance = ShooterWeaponInventory::FindInstance(this);
	if (!Instance)
	{
		return;
	}

	CurrentBullets = Instance->MagazineAmmo;
	if (WeaponOwner && !IsHidden())
	{
		WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
	}

	if (HasAuthority())
	{
		ForceNetUpdate();
	}
}

void AShooterWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 弹药只与拥有该武器的客户端相关。
	DOREPLIFETIME_CONDITION(AShooterWeapon, CurrentBullets, COND_OwnerOnly);
	// 只有 Owner 需要知道该 Actor 对应哪个 WeaponInstance；远端表现只看 Character.CurrentWeapon。
	DOREPLIFETIME_CONDITION(AShooterWeapon, BoundInstanceId, COND_OwnerOnly);
}

void AShooterWeapon::OnRep_CurrentBullets()
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	IShooterWeaponHolder* OwnerHolder = Cast<IShooterWeaponHolder>(GetOwner());
	if (OwnerPawn && OwnerPawn->IsLocallyControlled() && OwnerHolder)
	{
		OwnerHolder->UpdateWeaponHUD(CurrentBullets, MagazineSize);
	}
}

void AShooterWeapon::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the refire timer
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
}

void AShooterWeapon::OnOwnerDestroyed(AActor* DestroyedActor)
{
	// ensure this weapon is destroyed when the owner is destroyed
	Destroy();
}

void AShooterWeapon::ActivateWeapon()
{
	// unhide this weapon
	SetActorHiddenInGame(false);

	// 客户端可能先收到 Character 的武器 RepNotify，再执行武器 BeginPlay。
	if (WeaponOwner)
	{
		WeaponOwner->OnWeaponActivated(this);
	}
}

void AShooterWeapon::DeactivateWeapon()
{
	// ensure we're no longer firing this weapon while deactivated
	StopFiring();

	// hide the weapon
	SetActorHiddenInGame(true);

	// 复制初始化顺序不保证 WeaponOwner 已经在 BeginPlay 中完成赋值。
	if (WeaponOwner)
	{
		WeaponOwner->OnWeaponDeactivated(this);
	}
}

void AShooterWeapon::StartFiring()
{
	// raise the firing flag
	bIsFiring = true;

	// check how much time has passed since we last shot
	// this may be under the refire rate if the weapon shoots slow enough and the player is spamming the trigger
	const float TimeSinceLastShot = GetWorld()->GetTimeSeconds() - TimeOfLastShot;

	if (TimeSinceLastShot >= RefireRate)
	{
		// fire the weapon right away
		Fire();

	} else {

		// if we're full auto, schedule the next shot
		if (bFullAuto)
		{
			const float RemainingRefireTime = RefireRate - TimeSinceLastShot;
			GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, RemainingRefireTime, false);
		}

	}
}

void AShooterWeapon::StopFiring()
{
	// lower the firing flag
	bIsFiring = false;

	// clear the refire timer
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
}

void AShooterWeapon::Fire()
{
	// 纵深防御：即使客户端绕过开火 RPC 直接调用，弹丸也只在服务器生成
	if (!HasAuthority())
	{
		return;
	}

	// ensure the player still wants to fire. They may have let go of the trigger
	if (!bIsFiring)
	{
		return;
	}

	// Ammo 权威位于 Inventory.MagazineAmmo；耗尽后停止开火，不自动换弹。
	// 广播 OutOfAmmo 让 GA_Fire 幂等结束 Ability；未绑定的 NPC 旧路径不会进入这里。
	if (!CanConsumeAmmo() || !ConsumeAmmo())
	{
		StopFiring();
		OnOutOfAmmo.Broadcast(this);
		return;
	}

	// fire a projectile at the target
	FireProjectile(WeaponOwner->GetWeaponTargetLocation());

	// update the time of our last shot
	TimeOfLastShot = GetWorld()->GetTimeSeconds();

	// make noise so the AI perception system can hear us
	MakeNoise(ShotLoudness, PawnOwner, PawnOwner->GetActorLocation(), ShotNoiseRange, ShotNoiseTag);

	// are we full auto?
	if (bFullAuto)
	{
		// schedule the next shot
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, RefireRate, false);
	} else {

		// for semi-auto weapons, schedule the cooldown notification
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::FireCooldownExpired, RefireRate, false);

	}
}

void AShooterWeapon::FireCooldownExpired()
{
	// notify the owner
	WeaponOwner->OnSemiWeaponRefire();
}

void AShooterWeapon::FireProjectile(const FVector& TargetLocation)
{
	// get the projectile transform
	FTransform ProjectileTransform = CalculateProjectileSpawnTransform(TargetLocation);
	
	// spawn the projectile
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::OverrideRootScale;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = PawnOwner;

	AShooterProjectile* Projectile = GetWorld()->SpawnActor<AShooterProjectile>(ProjectileClass, ProjectileTransform, SpawnParams);

	// play the firing montage
	WeaponOwner->PlayFiringMontage(FiringMontage);

	// broadcast the muzzle flash and firing sound
	MulticastPlayFiringFX();

	// add recoil
	WeaponOwner->AddWeaponRecoil(FiringRecoil);

	// 未绑定 Inventory 的旧路径（如 NPC）保留兼容镜像扣减与自动补弹。
	if (!BoundInstanceId.IsValid())
	{
		if (CurrentBullets <= 0)
		{
			CurrentBullets = MagazineSize;
		}

		WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
		ForceNetUpdate();
	}
}

FTransform AShooterWeapon::CalculateProjectileSpawnTransform(const FVector& TargetLocation) const
{
	// 弹丸是服务器权威的世界对象，生成基点应来自第三人称世界表现枪口。
	// 第一人称网格只属于拥有者视图；远端玩家在服务器上的该网格并不是可靠的世界枪口来源。
	// 旧资产若暂时没有第三人称 Muzzle socket，则保留第一人称回退，避免直接落到世界原点。
	const FVector MuzzleLoc = HasThirdPersonMuzzleSocket()
		? GetThirdPersonMuzzleWorldTransform().GetLocation()
		: FirstPersonMesh->GetSocketLocation(MuzzleSocketName);
	const FVector ControlForward = PawnOwner
		? PawnOwner->GetControlRotation().Vector().GetSafeNormal()
		: FVector::ZeroVector;

	FVector MuzzleToTarget = (TargetLocation - MuzzleLoc).GetSafeNormal();
	// 摄像机可能命中位于枪口后方的近距离物体，此时不能让弹丸反向生成。
	if (!ControlForward.IsNearlyZero() &&
		FVector::DotProduct(MuzzleToTarget, ControlForward) <= 0.0f)
	{
		MuzzleToTarget = ControlForward;
	}

	// calculate the spawn location ahead of the muzzle
	const FVector SpawnLoc = MuzzleLoc + (MuzzleToTarget * MuzzleOffset);

	// find the aim rotation vector while applying some variance to the target 
	FVector AimDirection =
		(TargetLocation + (UKismetMathLibrary::RandomUnitVector() * AimVariance) - SpawnLoc).GetSafeNormal();
	if (!ControlForward.IsNearlyZero() &&
		FVector::DotProduct(AimDirection, ControlForward) <= 0.0f)
	{
		AimDirection = ControlForward;
	}
	const FRotator AimRot = AimDirection.Rotation();

	// return the built transform
	return FTransform(AimRot, SpawnLoc, FVector::OneVector);
}

void AShooterWeapon::MulticastPlayFiringFX_Implementation()
{
	// 没有配置任何表现资产时直接返回
	if (!MuzzleFlash && !FireSound)
	{
		return;
	}

	// 拥有者只看第一人称 mesh，其他客户端（及服务器模拟端）看第三人称 mesh
	const bool bLocalOwner = PawnOwner && PawnOwner->IsLocallyControlled();

	// 枪口闪光：挂在 muzzle socket 上，随武器移动
	if (MuzzleFlash)
	{
		USkeletalMeshComponent* MuzzleMesh = bLocalOwner ? FirstPersonMesh : ThirdPersonMesh;
		if (MuzzleMesh)
		{
			UNiagaraFunctionLibrary::SpawnSystemAttached(
				MuzzleFlash, MuzzleMesh, MuzzleSocketName,
				FVector::ZeroVector, FRotator::ZeroRotator,
				EAttachLocation::SnapToTarget, true);
		}
	}

	// 开火音效：所有端在武器位置播放，距离衰减由音频系统处理
	if (FireSound)
	{
		UGameplayStatics::SpawnSoundAttached(FireSound, RootComponent, NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::SnapToTarget, true);
	}
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetFirstPersonAnimInstanceClass() const
{
	return FirstPersonAnimInstanceClass;
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetThirdPersonAnimInstanceClass() const
{
	return ThirdPersonAnimInstanceClass;
}

FTransform AShooterWeapon::GetThirdPersonMuzzleWorldTransform() const
{
	// 与 GetThirdPersonLeftHandGripWorldTransform 一致：缺失时返回 Identity，不回退 Actor 变换，
	// 否则会把错误的“原点枪口”喂给表现计算；Binding Rebuild 只在 HasThirdPersonMuzzleSocket() 为真后调用。
	if (ThirdPersonMesh && ThirdPersonMesh->DoesSocketExist(MuzzleSocketName))
	{
		return ThirdPersonMesh->GetSocketTransform(MuzzleSocketName);
	}

	return FTransform::Identity;
}

bool AShooterWeapon::HasThirdPersonMuzzleSocket() const
{
	return ThirdPersonMesh != nullptr && ThirdPersonMesh->DoesSocketExist(MuzzleSocketName);
}

bool AShooterWeapon::HasThirdPersonLeftHandGripSocket() const
{
	return ThirdPersonMesh != nullptr &&
		!ThirdPersonLeftHandGripSocketName.IsNone() &&
		ThirdPersonMesh->DoesSocketExist(ThirdPersonLeftHandGripSocketName);
}

FTransform AShooterWeapon::GetThirdPersonLeftHandGripWorldTransform() const
{
	// 与 Muzzle 不同，握把缺失时不能回退 Actor 变换，否则会把错误的“原点握把”喂给左手 IK。
	if (!HasThirdPersonLeftHandGripSocket())
	{
		return FTransform::Identity;
	}

	return ThirdPersonMesh->GetSocketTransform(ThirdPersonLeftHandGripSocketName);
}
