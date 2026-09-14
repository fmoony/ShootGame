// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterWeapon.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "ShootGame.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Weapons/Projectile/ShooterProjectile.h"
#include "Weapons/Interfaces/ShooterWeaponHolder.h"
#include "Components/SceneComponent.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"
#include "Weapons/Data/ShooterWeaponConfigRow.h"
#include "Weapons/Subsystems/ShooterWeaponRuntimeSubsystem.h"

namespace
{
	/** 生命周期状态名；用于低噪声状态转换诊断与非法转换拒绝日志。 */
	const TCHAR* LifecycleStateToString(EShooterWeaponLifecycleState State)
	{
		switch (State)
		{
		case EShooterWeaponLifecycleState::InPool: return TEXT("InPool");
		case EShooterWeaponLifecycleState::Holstered: return TEXT("Holstered");
		case EShooterWeaponLifecycleState::Equipping: return TEXT("Equipping");
		case EShooterWeaponLifecycleState::Equipped: return TEXT("Equipped");
		default: return TEXT("Unknown");
		}
	}
}

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
		RestoreInitialAmmo();
	}

}

void AShooterWeapon::OnRep_Owner()
{
	Super::OnRep_Owner();
	InitializeWeaponOwner();
}

void AShooterWeapon::InitializeWeaponOwner()
{
	// Owner 复制可能直接从 A 切到 B，也可能先到 nullptr；必须先解除缓存中的旧 Owner，
	// 不能只从 GetOwner() 读取新值，否则旧 Pawn 销毁时仍会回调已经复用给新玩家的武器。
	ClearWeaponOwner();

	AActor* OwningActor = GetOwner();
	if (!IsValid(OwningActor))
	{
		return;
	}

	CachedWeaponOwnerActor = OwningActor;
	OwningActor->OnDestroyed.AddUniqueDynamic(this, &AShooterWeapon::OnOwnerDestroyed);
	WeaponOwner = Cast<IShooterWeaponHolder>(OwningActor);
	PawnOwner = Cast<APawn>(OwningActor);

	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(OwningActor))
	{
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

void AShooterWeapon::ClearWeaponOwner()
{
	if (CachedWeaponOwnerActor)
	{
		CachedWeaponOwnerActor->OnDestroyed.RemoveAll(this);
	}

	CachedWeaponOwnerActor = nullptr;
	WeaponOwner = nullptr;
	PawnOwner = nullptr;
}

void AShooterWeapon::InitializeWeaponIdentity(FName InWeaponId)
{
	if (!HasAuthority() || WeaponId == InWeaponId)
	{
		return;
	}

	WeaponId = InWeaponId;

	// 静态配置只从 WeaponRuntimeSubsystem 的启动快照应用；快照缺失时保持 Actor 默认配置（测试兼容）。
	if (UShooterWeaponRuntimeSubsystem* Runtime = GetWeaponRuntimeSubsystem())
	{
		if (const FShooterWeaponConfigRow* Config = Runtime->FindRuntimeConfig(WeaponId))
		{
			ApplyWeaponRow(*Config);
		}
	}
}

void AShooterWeapon::OnRep_WeaponId()
{
	// 客户端：WeaponId 是创建后不变的初始复制数据，静态表现从本地启动快照恢复（内存查询，非 DataTable）。
	if (!WeaponId.IsNone())
	{
		if (UShooterWeaponRuntimeSubsystem* Runtime = GetWeaponRuntimeSubsystem())
		{
			if (const FShooterWeaponConfigRow* Config = Runtime->FindRuntimeConfig(WeaponId))
			{
				ApplyWeaponRow(*Config);
			}
			else
			{
				UE_LOG(LogShootGame, Warning, TEXT("WeaponActor cannot apply runtime config: Weapon=%s WeaponId=%s"),
					*GetNameSafe(this), *WeaponId.ToString());
			}
		}
	}

	// 行配置可能改变 AnimClass / Mesh，表现收敛走同一幂等入口补做。
	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(GetOwner()))
	{
		if (UShooterEquipmentComponent* Equipment = ShooterCharacter->GetEquipmentComponent())
		{
			Equipment->HandleWeaponActorReady(this);
		}
	}
}

UShooterWeaponRuntimeSubsystem* AShooterWeapon::GetWeaponRuntimeSubsystem() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UShooterWeaponRuntimeSubsystem>() : nullptr;
}

void AShooterWeapon::OnAcquiredFromWeaponPool()
{
	// 租用复位：开火节拍与开火标志不跨租用继承；WeaponId 与静态配置永久保留。
	TimeOfLastShot = 0.0f;
	bIsFiring = false;

	// 池在调用本回调前已写入新 Owner，这里重新绑定 Owner/Instigator 缓存与销毁委托。
	InitializeWeaponOwner();

	SetLifecycleState(EShooterWeaponLifecycleState::Holstered, TEXT("AcquiredFromWeaponPool"));

	UE_LOG(LogShootGame, Verbose, TEXT("WeaponActor acquired from weapon runtime pool: Weapon=%s WeaponId=%s Owner=%s"),
		*GetNameSafe(this), *WeaponId.ToString(), *GetNameSafe(GetOwner()));
}

void AShooterWeapon::OnReleasedToWeaponPool()
{
	// 纵深防御：归还前必须已脱离装备态（Equipment 先清空当前装备）。
	if (LifecycleState == EShooterWeaponLifecycleState::Equipped || LifecycleState == EShooterWeaponLifecycleState::Equipping)
	{
		DeactivateWeapon();
	}

	// 完整停止开火与换弹相关 Timer / Delegate。
	StopFiring();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefireTimer);
	}

	// 解除 Owner 销毁委托并清空 Owner 侧缓存；通用隐藏/Detach/Owner 清空由池统一执行。
	ClearWeaponOwner();

	// 恢复该武器的初始弹药：静态配置（WeaponId / MagazineSize / 行为实例）永久保留，不重复应用。
	RestoreInitialAmmo();
	OnOutOfAmmo.Clear();
	SetLifecycleState(EShooterWeaponLifecycleState::InPool, TEXT("ReleasedToWeaponPool"));

	UE_LOG(LogShootGame, Verbose, TEXT("WeaponActor released to weapon runtime pool: Weapon=%s WeaponId=%s"),
		*GetNameSafe(this), *WeaponId.ToString());
}

void AShooterWeapon::ApplyWeaponRow(const FShooterWeaponConfigRow& Row)
{
	// 运行时可写镜像：只覆盖 Actor 自身状态，行与表保持只读。
	MagazineSize = Row.MagazineSize;
	InitialReserveAmmo = Row.InitialReserveAmmo;
	bFullAuto = Row.bFullAuto;
	RefireRate = Row.RefireRate;
	AimVariance = Row.AimVariance;
	ShotLoudness = Row.ShotLoudness;
	ShotNoiseRange = Row.ShotNoiseRange;
	ShotNoiseTag = Row.ShotNoiseTag;
	ReloadDuration = Row.ReloadDuration;
	EquipDuration = Row.EquipDuration;
	MuzzleOffset = Row.MuzzleOffset;
	MuzzleSocketName = Row.MuzzleSocketName;
	ThirdPersonLeftHandGripSocketName = Row.LeftHandGripSocketName;
	FiringRecoil = Row.FiringRecoil;
	FirstPersonCompositionDrop = Row.FirstPersonCompositionDrop;

	FiringMontage = Row.FiringMontage;
	MuzzleFlash = Row.MuzzleFlash;
	FireSound = Row.FireSound;
	ReloadMagazineOutSound = Row.ReloadMagazineOutSound;
	ReloadMagazineInSound = Row.ReloadMagazineInSound;
	ReloadCockingSound = Row.ReloadCockingSound;
	FirstPersonAnimInstanceClass = Row.FirstPersonAnimInstanceClass;
	ThirdPersonAnimInstanceClass = Row.ThirdPersonAnimInstanceClass;

	// 弹丸类镜像：唯一弹丸生成路径（FireProjectile）直接读本 Actor 字段，运行时不再查表。
	ProjectileClass = Row.ProjectileClass;

	// 网格沿用当前同步加载边界：行内为软引用，应用时同步加载。
	if (FirstPersonMesh)
	{
		FirstPersonMesh->SetSkeletalMeshAsset(Row.FirstPersonMesh.LoadSynchronous());
	}
	if (ThirdPersonMesh)
	{
		ThirdPersonMesh->SetSkeletalMeshAsset(Row.ThirdPersonMesh.LoadSynchronous());
	}

	// 权威端应用新配置即回到该配置的初始弹药经济：绑定、复用与归还路径都经此收敛。
	if (HasAuthority())
	{
		RestoreInitialAmmo();
	}
}

FShooterWeaponConfigRow AShooterWeapon::CaptureWeaponConfigRow() const
{
	FShooterWeaponConfigRow Row;

	Row.WeaponActorClass = GetClass();
	Row.MagazineSize = MagazineSize;
	Row.InitialReserveAmmo = InitialReserveAmmo;
	Row.bFullAuto = bFullAuto;
	Row.RefireRate = RefireRate;
	Row.AimVariance = AimVariance;
	Row.ShotLoudness = ShotLoudness;
	Row.ShotNoiseRange = ShotNoiseRange;
	Row.ShotNoiseTag = ShotNoiseTag;
	Row.ReloadDuration = ReloadDuration;
	Row.EquipDuration = EquipDuration;
	Row.ProjectileClass = ProjectileClass;
	Row.MuzzleOffset = MuzzleOffset;
	Row.MuzzleSocketName = MuzzleSocketName;
	Row.LeftHandGripSocketName = ThirdPersonLeftHandGripSocketName;
	Row.FirstPersonAnimInstanceClass = FirstPersonAnimInstanceClass;
	Row.ThirdPersonAnimInstanceClass = ThirdPersonAnimInstanceClass;
	Row.FiringMontage = FiringMontage;
	Row.MuzzleFlash = MuzzleFlash;
	Row.FireSound = FireSound;
	Row.ReloadMagazineOutSound = ReloadMagazineOutSound;
	Row.ReloadMagazineInSound = ReloadMagazineInSound;
	Row.ReloadCockingSound = ReloadCockingSound;
	Row.FiringRecoil = FiringRecoil;
	Row.FirstPersonCompositionDrop = FirstPersonCompositionDrop;

	if (FirstPersonMesh)
	{
		Row.FirstPersonMesh = FirstPersonMesh->GetSkeletalMeshAsset();
	}
	if (ThirdPersonMesh)
	{
		Row.ThirdPersonMesh = ThirdPersonMesh->GetSkeletalMeshAsset();
	}

	return Row;
}

void AShooterWeapon::BeginEquipTransaction()
{
	switch (LifecycleState)
	{
	case EShooterWeaponLifecycleState::Holstered:
		SetLifecycleState(EShooterWeaponLifecycleState::Equipping, TEXT("BeginEquipTransaction"));
		break;
	case EShooterWeaponLifecycleState::Equipping:
		// 同一事务重复提交保持幂等。
		break;
	default:
		UE_LOG(LogShootGame, Warning, TEXT("WeaponActor BeginEquipTransaction rejected in state %s: Weapon=%s"),
			LifecycleStateToString(LifecycleState), *GetNameSafe(this));
		break;
	}
}

void AShooterWeapon::SetLifecycleState(EShooterWeaponLifecycleState NewState, const TCHAR* Reason)
{
	const EShooterWeaponLifecycleState PreviousState = LifecycleState;
	if (PreviousState == NewState)
	{
		return;
	}

	LifecycleState = NewState;

	// 低噪声诊断：只在状态真实变化时输出，表现收敛的重复调用不刷屏。
	UE_LOG(
		LogShootGame,
		Verbose,
		TEXT("WeaponActor lifecycle %s -> %s (%s): Weapon=%s Owner=%s WeaponId=%s"),
		LifecycleStateToString(PreviousState),
		LifecycleStateToString(NewState),
		Reason ? Reason : TEXT("Unspecified"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		*WeaponId.ToString());
}

int32 AShooterWeapon::GetBulletCount() const
{
	// 弹药权威在本 Actor；无 Inventory 的旧路径（NPC / 测试）同样直接读该值。
	return MagazineAmmo;
}

int32 AShooterWeapon::GetReserveAmmo() const
{
	return ReserveAmmo;
}

bool AShooterWeapon::CanConsumeAmmo() const
{
	return MagazineAmmo > 0;
}

bool AShooterWeapon::ConsumeAmmo(int32 Amount)
{
	if (!HasAuthority() || Amount <= 0 || MagazineAmmo < Amount)
	{
		return false;
	}

	MagazineAmmo -= Amount;
	PushAmmoToOwnerHud();
	ForceNetUpdate();
	return true;
}

void AShooterWeapon::RestoreInitialAmmo()
{
	if (!HasAuthority())
	{
		return;
	}

	// 入库/归还即回到该武器静态配置声明的初始弹药经济。
	MagazineAmmo = MagazineSize;
	ReserveAmmo = ResolveInitialReserveAmmo();
	ForceNetUpdate();
}

bool AShooterWeapon::ReloadFromReserve(int32& OutTransferredAmmo)
{
	OutTransferredAmmo = 0;
	if (!HasAuthority() || MagazineSize <= 0)
	{
		return false;
	}

	const int32 Need = FMath::Max(0, MagazineSize - MagazineAmmo);
	const int32 Transfer = FMath::Min(Need, ReserveAmmo);
	if (Transfer <= 0)
	{
		return false;
	}

	MagazineAmmo += Transfer;
	ReserveAmmo -= Transfer;
	OutTransferredAmmo = Transfer;

	PushAmmoToOwnerHud();
	ForceNetUpdate();

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("WeaponActor reload committed: Weapon=%s WeaponId=%s Transfer=%d Mag=%d Reserve=%d"),
		*GetNameSafe(this),
		*WeaponId.ToString(),
		Transfer,
		MagazineAmmo,
		ReserveAmmo);
	return true;
}

void AShooterWeapon::PushAmmoToOwnerHud()
{
	// 弹药字段是 COND_OwnerOnly：OnRep 只在拥有端触发，服务器推送只发生在权威端，
	// 因此无需再判 IsLocallyControlled（无控制器的测试角色同样需要 HUD 事件）。
	// 未装备（隐藏）的武器不推 HUD：拾取入库阶段保持静默，装备表现收敛时统一推送。
	if (!WeaponOwner || IsHidden())
	{
		return;
	}

	WeaponOwner->UpdateWeaponHUD(MagazineAmmo, MagazineSize, ReserveAmmo);
}

void AShooterWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 弹药只与拥有该武器的客户端相关。
	DOREPLIFETIME_CONDITION(AShooterWeapon, MagazineAmmo, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AShooterWeapon, ReserveAmmo, COND_OwnerOnly);
	// 武器种类身份是创建后不变的初始复制数据；客户端从启动快照恢复静态表现配置。
	DOREPLIFETIME(AShooterWeapon, WeaponId);
}

void AShooterWeapon::OnRep_MagazineAmmo()
{
	PushAmmoToOwnerHud();
}

void AShooterWeapon::OnRep_ReserveAmmo()
{
	PushAmmoToOwnerHud();
}

void AShooterWeapon::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the refire timer
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefireTimer);
	}

	// teardown 幂等边界：World 销毁 / 拥有者销毁 / 池溢出销毁都走这里，
	// 必须解除 Owner 销毁委托，避免留下指向已销毁 Actor 的引用。
	// 注意这里不归还池：正在销毁的 Actor 只能被销毁，归还由 Inventory / 拥有者清理路径负责。
	ClearWeaponOwner();

	bIsFiring = false;
	OnOutOfAmmo.Clear();
}

void AShooterWeapon::OnOwnerDestroyed(AActor* DestroyedActor)
{
	// 运行时池租出的武器由池接管回收：归还而不是销毁，否则池会留下 PendingKill 引用，
	// 且该 Actor 只能等 GC 才能复用。归还前由 Inventory 清空 / Equipment 收敛。
	UShooterWeaponRuntimeSubsystem* Runtime = GetWeaponRuntimeSubsystem();
	if (Runtime && Runtime->IsLeased(this))
	{
		if (Runtime->ReleaseWeapon(this))
		{
			return;
		}
	}

	// 非池出生（NPC 兼容路径 / 测试直接 Spawn）保持原有销毁语义。
	Destroy();
}

void AShooterWeapon::ActivateWeapon()
{
	// 池内武器不可直接装备；必须先由运行时池租出（Holstered）。
	// 拒绝只在权威端生效：客户端状态是复制镜像，在那里拒绝会永久丢失远端第三人称表现。
	if (HasAuthority() && LifecycleState == EShooterWeaponLifecycleState::InPool)
	{
		UE_LOG(LogShootGame, Warning, TEXT("WeaponActor ActivateWeapon rejected in InPool state: Weapon=%s"), *GetNameSafe(this));
		return;
	}

	// 重复激活保持幂等，表现收敛入口会多次调用。
	if (LifecycleState == EShooterWeaponLifecycleState::Equipped)
	{
		return;
	}

	SetLifecycleState(EShooterWeaponLifecycleState::Equipped, TEXT("ActivateWeapon"));

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
	// 权威端重复卸下幂等：池内或已收起时不重复触发表现回调。
	// 客户端不做该提前返回，保持与池化前的本地隐藏时机一致。
	if (HasAuthority() && (LifecycleState == EShooterWeaponLifecycleState::InPool ||
			LifecycleState == EShooterWeaponLifecycleState::Holstered))
	{
		return;
	}

	SetLifecycleState(EShooterWeaponLifecycleState::Holstered, TEXT("DeactivateWeapon"));

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
			GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, RemainingRefireTime,
				false);
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

	// Ammo 权威位于 WeaponActor.MagazineAmmo；玩家与 NPC 都读同一值，耗尽后停止开火，不自动换弹。
	// 广播 OutOfAmmo 让 GA_Fire 幂等结束 Ability。
	if (!CanConsumeAmmo() || !ConsumeAmmo())
	{
		StopFiring();
		OnOutOfAmmo.Broadcast(this);
		return;
	}

	// 权威弹药消费成功后才执行开火行为与表现。
	ExecuteFireAtTarget(WeaponOwner->GetWeaponTargetLocation());

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
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::FireCooldownExpired, RefireRate,
			false);

	}
}

void AShooterWeapon::FireCooldownExpired()
{
	// notify the owner
	WeaponOwner->OnSemiWeaponRefire();
}

void AShooterWeapon::ExecuteFireAtTarget(const FVector& TargetLocation)
{
	// 攻击结果边界：生成弹丸；表现入口统一留在本 Actor（行为边界当前休眠，不参与本路径）。
	FireProjectile(TargetLocation);

	// play the firing montage
	WeaponOwner->PlayFiringMontage(FiringMontage);

	// broadcast the muzzle flash and firing sound
	MulticastPlayFiringFX();

	// add recoil
	WeaponOwner->AddWeaponRecoil(FiringRecoil);

}

void AShooterWeapon::FireProjectile(const FVector& TargetLocation)
{
	// 唯一弹丸生成路径：玩家与 NPC 共用，弹丸类来自 ApplyWeaponRow 镜像的行配置。
	// 仅服务器调用（Fire 入口已做权威校验）；客户端不进入本函数。
	// get the projectile transform
	FTransform ProjectileTransform = CalculateProjectileSpawnTransform(TargetLocation);

	// spawn the projectile
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::OverrideRootScale;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = PawnOwner;

	GetWorld()->SpawnActor<AShooterProjectile>(ProjectileClass, ProjectileTransform, SpawnParams);
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
	if (!ControlForward.IsNearlyZero() && FVector::DotProduct(MuzzleToTarget, ControlForward) <= 0.0f)
	{
		MuzzleToTarget = ControlForward;
	}

	// calculate the spawn location ahead of the muzzle
	const FVector SpawnLoc = MuzzleLoc + (MuzzleToTarget * MuzzleOffset);

	// find the aim rotation vector while applying some variance to the target
	FVector AimDirection = (TargetLocation + (UKismetMathLibrary::RandomUnitVector() * AimVariance) - SpawnLoc).GetSafeNormal();
	if (!ControlForward.IsNearlyZero() && FVector::DotProduct(AimDirection, ControlForward) <= 0.0f)
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
			UNiagaraFunctionLibrary::SpawnSystemAttached(MuzzleFlash, MuzzleMesh, MuzzleSocketName,
				FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true);
		}
	}

	// 开火音效：所有端在武器位置播放，距离衰减由音频系统处理
	if (FireSound)
	{
		UGameplayStatics::SpawnSoundAttached(FireSound, RootComponent, NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true);
	}
}

void AShooterWeapon::PlayReloadSoundStage(EShooterReloadSoundStage Stage)
{
	// Dedicated 服务器没有监听者；换弹音效是可丢失的纯本地表现，各端各自触发。
	if (IsRunningDedicatedServer())
	{
		return;
	}

	USoundBase* StageSound = nullptr;
	switch (Stage)
	{
	case EShooterReloadSoundStage::MagazineOut:
		StageSound = ReloadMagazineOutSound;
		break;
	case EShooterReloadSoundStage::MagazineIn:
		StageSound = ReloadMagazineInSound;
		break;
	case EShooterReloadSoundStage::Cocking:
		StageSound = ReloadCockingSound;
		break;
	}

	// NoSound 网络会话下核对各端 Notify 触发时序的日志标记（含未配置音效的情况）。
	UE_LOG(LogShootGame, Log, TEXT("%s: Reload sound stage %d triggered (Sound=%s)"),
		*GetNameSafe(this), static_cast<int32>(Stage), *GetNameSafe(StageSound));

	if (!StageSound || !ThirdPersonMesh)
	{
		return;
	}

	// 挂在第三人称武器 Mesh 的 Muzzle Socket（骨骼位置）上，随换弹动画移动；
	// 本地与远端共用同一路径，距离衰减由音频系统处理。
	UGameplayStatics::SpawnSoundAttached(StageSound, ThirdPersonMesh, MuzzleSocketName,
		FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTargetIncludingScale, true);
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
	return ThirdPersonMesh != nullptr && !ThirdPersonLeftHandGripSocketName.IsNone() &&
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
