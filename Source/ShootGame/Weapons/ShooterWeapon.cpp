// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterWeapon.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "ShootGame.h"
#include "AbilitySystem/Abilities/ShooterGameplayAbility_Fire.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Weapons/Projectile/ShooterProjectile.h"
#include "Weapons/Interfaces/ShooterWeaponHolder.h"
#include "Components/SceneComponent.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"
#include "Weapons/Data/ShooterWeaponConfigRow.h"
#include "Weapons/Subsystems/ShooterWeaponRuntimeSubsystem.h"

namespace
{
	const FName MagazineSocketName(TEXT("MagazineSocket"));
	const FName MagazineHandBoneName(TEXT("hand_l"));

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

	bool ResetMagazineProxyForMesh(USkeletalMeshComponent* SourceMesh, UStaticMeshComponent* Proxy, USceneComponent* WeaponRoot)
	{
		if (SourceMesh && SourceMesh->DoesSocketExist(MagazineSocketName))
		{
			const FName ParentBoneName = SourceMesh->GetSocketBoneName(MagazineSocketName);
			if (!ParentBoneName.IsNone())
			{
				SourceMesh->UnHideBoneByName(ParentBoneName);
			}
		}

		if (!Proxy)
		{
			return false;
		}

		Proxy->SetHiddenInGame(true);
		Proxy->SetVisibility(false);
		Proxy->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		if (WeaponRoot)
		{
			Proxy->AttachToComponent(WeaponRoot, FAttachmentTransformRules::KeepWorldTransform);
			Proxy->SetRelativeTransform(FTransform::Identity);
		}
		return true;
	}

	bool DetachMagazineProxyToHand(USkeletalMeshComponent* SourceMesh, UStaticMeshComponent* Proxy,
		USkeletalMeshComponent* CharacterMesh, const FTransform& GripTransform, USceneComponent* WeaponRoot)
	{
		if (!SourceMesh || !Proxy || !Proxy->GetStaticMesh() || !CharacterMesh || !WeaponRoot ||
			!SourceMesh->DoesSocketExist(MagazineSocketName) ||
			CharacterMesh->GetBoneIndex(MagazineHandBoneName) == INDEX_NONE)
		{
			return false;
		}

		const FName ParentBoneName = SourceMesh->GetSocketBoneName(MagazineSocketName);
		if (ParentBoneName.IsNone())
		{
			return false;
		}

		// 重复 Notify 在同一侧已经完成换手时保持幂等，避免从已隐藏骨骼再次取变换。
		if (SourceMesh->IsBoneHiddenByName(ParentBoneName))
		{
			return Proxy->GetAttachParent() == CharacterMesh &&
				Proxy->GetAttachSocketName() == MagazineHandBoneName && !Proxy->bHiddenInGame;
		}

		// 必须在隐藏原弹匣前读取 Socket 世界变换；代理始终脱离 Weapon 骨骼树。
		const FTransform SocketWorldTransform = SourceMesh->GetSocketTransform(MagazineSocketName, RTS_World);
		Proxy->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		Proxy->SetWorldTransform(SocketWorldTransform);
		Proxy->AttachToComponent(CharacterMesh, FAttachmentTransformRules::KeepWorldTransform, MagazineHandBoneName);
		if (Proxy->GetAttachParent() != CharacterMesh || Proxy->GetAttachSocketName() != MagazineHandBoneName)
		{
			Proxy->AttachToComponent(WeaponRoot, FAttachmentTransformRules::KeepWorldTransform);
			Proxy->SetRelativeTransform(FTransform::Identity);
			return false;
		}

		// GripTransform 是 hand_l 下的最终局部姿态，不是要叠加的 Delta。
		Proxy->SetRelativeTransform(GripTransform);
		Proxy->SetHiddenInGame(false);
		Proxy->SetVisibility(true);
		// 最后才隐藏原弹匣骨骼，避免隐藏骨骼改变 Socket 读取结果。
		SourceMesh->HideBoneByName(ParentBoneName, PBO_None);
		return true;
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

	FirstPersonMagazineProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("First Person Magazine Proxy"));
	FirstPersonMagazineProxy->SetupAttachment(RootComponent);
	FirstPersonMagazineProxy->SetCollisionProfileName(FName("NoCollision"));
	FirstPersonMagazineProxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonMagazineProxy->SetGenerateOverlapEvents(false);
	FirstPersonMagazineProxy->SetIsReplicated(false);
	FirstPersonMagazineProxy->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	FirstPersonMagazineProxy->bOnlyOwnerSee = true;
	FirstPersonMagazineProxy->SetVisibility(false);
	FirstPersonMagazineProxy->SetHiddenInGame(true);

	ThirdPersonMagazineProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Third Person Magazine Proxy"));
	ThirdPersonMagazineProxy->SetupAttachment(RootComponent);
	ThirdPersonMagazineProxy->SetCollisionProfileName(FName("NoCollision"));
	ThirdPersonMagazineProxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ThirdPersonMagazineProxy->SetGenerateOverlapEvents(false);
	ThirdPersonMagazineProxy->SetIsReplicated(false);
	ThirdPersonMagazineProxy->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
	ThirdPersonMagazineProxy->bOwnerNoSee = true;
	ThirdPersonMagazineProxy->SetVisibility(false);
	ThirdPersonMagazineProxy->SetHiddenInGame(true);
}

void AShooterWeapon::BeginPlay()
{
	Super::BeginPlay();

	InitializeWeaponOwner();

	if (!HasAuthority())
	{
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("[MagazineNetDiag][WeaponBeginPlay] NetMode=%d Weapon=%s Owner=%s WeaponId=%s"),
			static_cast<int32>(GetNetMode()),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()),
			*WeaponId.ToString());
	}

	// 弹药只由服务器初始化，拥有者客户端通过复制获得。
	if (HasAuthority())
	{
		RestoreInitialAmmo();
		// 结果环在服务器先固定大小，客户端按复制结果补齐。
		FireActivationResults.SetNum(FireActivationResultRingSize);
	}

}

void AShooterWeapon::OnRep_Owner()
{
	Super::OnRep_Owner();
	ResetLocalFireCooldown();
	InitializeWeaponOwner();

	UE_LOG(LogShootGame, Display, TEXT("[MagazineNetDiag][WeaponOwner] Net=%d Weapon=%s Owner=%s Id=%s"),
		static_cast<int32>(GetNetMode()), *GetNameSafe(this), *GetNameSafe(GetOwner()), *WeaponId.ToString());
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
	ResetMagazinePresentation();
	CancelOwnerConfirmedFeedback();
	if (UShooterGameplayAbility_Fire* Ability = Cast<UShooterGameplayAbility_Fire>(BoundPredictionAbility.Get()))
	{
		Ability->InvalidateWeaponPredictionContext(this);
	}
	++AmmoPredictionGeneration;
	NextConfirmedFeedbackTime = 0.0;

	if (CachedWeaponOwnerActor)
	{
		CachedWeaponOwnerActor->OnDestroyed.RemoveAll(this);
	}

	CachedWeaponOwnerActor = nullptr;
	WeaponOwner = nullptr;
	PawnOwner = nullptr;

	// Owner 上下文已失效：旧预测上下文整体作废（Owner 变化 / 归还池 / teardown）。
	ResetAmmoPrediction();
	ClearPredictionAbility();

	// 上一持有者的 Activation 结果不得污染下一轮；权威端清环形记录，客户端只清本地缓存。
	if (HasAuthority())
	{
		ResetFireActivationResultState();
	}
	else
	{
		CachedFireActivationResults.Reset();
	}
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
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("[MagazineNetDiag][WeaponOnRepWeaponId] NetMode=%d Weapon=%s Owner=%s WeaponId=%s"),
		static_cast<int32>(GetNetMode()),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		*WeaponId.ToString());

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
	ResetLocalFireCooldown();

	// 池在调用本回调前已写入新 Owner，这里重新绑定 Owner/Instigator 缓存与销毁委托。
	InitializeWeaponOwner();

	SetLifecycleState(EShooterWeaponLifecycleState::Holstered, TEXT("AcquiredFromWeaponPool"));

	UE_LOG(LogShootGame, Verbose, TEXT("WeaponActor acquired from weapon runtime pool: Weapon=%s WeaponId=%s Owner=%s"),
		*GetNameSafe(this), *WeaponId.ToString(), *GetNameSafe(GetOwner()));
}

void AShooterWeapon::OnReleasedToWeaponPool()
{
	ResetMagazinePresentation();

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
	ResetLocalFireCooldown();

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
	FirstPersonMagazineGripTransform = Row.FirstPersonMagazineGripTransform;
	ThirdPersonMagazineGripTransform = Row.ThirdPersonMagazineGripTransform;
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
	if (FirstPersonMagazineProxy)
	{
		FirstPersonMagazineProxy->SetStaticMesh(Row.MagazineMesh.LoadSynchronous());
	}
	if (ThirdPersonMagazineProxy)
	{
		ThirdPersonMagazineProxy->SetStaticMesh(Row.MagazineMesh.LoadSynchronous());
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
	Row.FirstPersonMagazineGripTransform = FirstPersonMagazineGripTransform;
	Row.ThirdPersonMagazineGripTransform = ThirdPersonMagazineGripTransform;
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
	if (FirstPersonMagazineProxy && FirstPersonMagazineProxy->GetStaticMesh())
	{
		Row.MagazineMesh = FirstPersonMagazineProxy->GetStaticMesh();
	}
	else if (ThirdPersonMagazineProxy)
	{
		Row.MagazineMesh = ThirdPersonMagazineProxy->GetStaticMesh();
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

void AShooterWeapon::RefreshAuthorityAmmoDisplaySnapshot()
{
	if (HasAuthority() && (AmmoDisplaySnapshot.Revision == 0 || AmmoDisplaySnapshot.Magazine != MagazineAmmo ||
		AmmoDisplaySnapshot.Reserve != ReserveAmmo))
	{
		++AmmoDisplaySnapshot.Revision;
		AmmoDisplaySnapshot.Magazine = MagazineAmmo;
		AmmoDisplaySnapshot.Reserve = ReserveAmmo;
	}
}

void AShooterWeapon::OnRep_AmmoDisplaySnapshot()
{
	AmmoDisplayState.ReceiveSnapshot(AmmoDisplaySnapshot);
	PushAmmoToOwnerHud();
}

int32 AShooterWeapon::GetDisplayedMagazineAmmo() const
{
	return IsAmmoPredictionContext() ? AmmoDisplayState.GetMagazine(MagazineAmmo) : MagazineAmmo;
}

int32 AShooterWeapon::GetDisplayedReserveAmmo() const
{
	return IsAmmoPredictionContext() ? AmmoDisplayState.GetReserve(ReserveAmmo) : ReserveAmmo;
}

void AShooterWeapon::BeginAmmoDisplayActivation(int32 PredictionKey)
{
	if (IsAmmoPredictionContext())
	{
		FShooterAmmoDisplaySnapshot Initial = AmmoDisplaySnapshot;
		if (Initial.Revision == 0)
		{
			Initial.Magazine = MagazineAmmo;
			Initial.Reserve = ReserveAmmo;
		}
		AmmoDisplayState.BeginActivation(PredictionKey, Initial);
	}
}

void AShooterWeapon::RecordAmmoDisplayPredictedShot(int32 PredictionKey)
{
	if (IsAmmoPredictionContext())
	{
		AmmoDisplayState.PredictShot(PredictionKey);
		PushAmmoToOwnerHud();
	}
}

void AShooterWeapon::RejectAmmoDisplayActivation(int32 PredictionKey)
{
	AmmoDisplayState.RejectActivation(PredictionKey);
	PushAmmoToOwnerHud();
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
	RefreshAuthorityAmmoDisplaySnapshot();
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
	RefreshAuthorityAmmoDisplaySnapshot();
	// 弹药字段是 COND_OwnerOnly：OnRep 只在拥有端触发，服务器推送只发生在权威端，
	// 因此无需再判 IsLocallyControlled（无控制器的测试角色同样需要 HUD 事件）。
	// 未装备（隐藏）的武器不推 HUD：拾取入库阶段保持静默，装备表现收敛时统一推送。
	if (!WeaponOwner || IsHidden())
	{
		return;
	}

	if (const AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwner()))
	{
		// 迟到旧武器裁决仍可结账，但不能覆盖当前装备的 HUD。
		if (Character->GetCurrentWeaponActor() != this)
		{
			return;
		}
	}
	if (!HasAuthority() && !HasOwnerLocalPlayerView())
	{
		return;
	}
	WeaponOwner->UpdateWeaponHUD(GetDisplayedMagazineAmmo(), MagazineSize, GetDisplayedReserveAmmo());
}

void AShooterWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 弹药只与拥有该武器的客户端相关。
	DOREPLIFETIME_CONDITION(AShooterWeapon, MagazineAmmo, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AShooterWeapon, ReserveAmmo, COND_OwnerOnly);
	// 武器种类身份是创建后不变的初始复制数据；客户端从启动快照恢复静态表现配置。
	DOREPLIFETIME(AShooterWeapon, WeaponId);
	// 最近数轮 GA_Fire 的服务器结果只服务拥有者客户端的表现收敛。
	DOREPLIFETIME_CONDITION(AShooterWeapon, FireActivationResults, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AShooterWeapon, AmmoDisplaySnapshot, COND_OwnerOnly);
}

void AShooterWeapon::OnRep_MagazineAmmo(int32 OldMagazineAmmo)
{
	// MagazineAmmo 只表示服务器当前真实弹药，不再用于推断确认了几发预测。
	// PendingPredictedShots 的减少只由当前 GA_Fire Activation 的服务器结果账本负责；
	// 生命周期边界仍由 ResetAmmoPrediction 强制清理。
	PushAmmoToOwnerHud();
}

void AShooterWeapon::OnRep_ReserveAmmo()
{
	PushAmmoToOwnerHud();
}

bool AShooterWeapon::IsAmmoPredictionContext() const
{
	// 只有「非权威端 + 本机拥有者视图」才预测弹药：
	// Listen Host 与 Standalone 的弹药在本地直接写入 MagazineAmmo，自身复制不会触发 OnRep，
	// 一旦维护 PendingPredictedShots 就永远没有下降复制可以吸收它。
	return !HasAuthority() && HasOwnerLocalPlayerView();
}

int32 AShooterWeapon::GetPredictedMagazineAmmo() const
{
	return FMath::Max(0, MagazineAmmo - PendingPredictedShots);
}

bool AShooterWeapon::CanConsumePredictedAmmo(int32 Amount) const
{
	if (Amount <= 0)
	{
		return false;
	}

	if (!IsAmmoPredictionContext())
	{
		// 非预测上下文不做预算门控：权威端由 CanConsumeAmmo 负责。
		return true;
	}

	return GetPredictedMagazineAmmo() >= Amount;
}

bool AShooterWeapon::TryConsumePredictedAmmo(int32 Amount)
{
	if (Amount <= 0)
	{
		return false;
	}

	if (!IsAmmoPredictionContext())
	{
		// 不维护 Pending 的一端直接放行；它不会写任何弹药字段。
		return true;
	}

	if (!CanConsumePredictedAmmo(Amount))
	{
		return false;
	}

	PendingPredictedShots += Amount;
#if WITH_DEV_AUTOMATION_TESTS
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICT_CONSUME Weapon=%s Pending=%d Snapshot=%d Predicted=%d"),
		*GetNameSafe(this), PendingPredictedShots, MagazineAmmo, GetPredictedMagazineAmmo());
#endif
	return true;
}

void AShooterWeapon::RefundPredictedAmmo(int32 Amount)
{
	if (Amount <= 0)
	{
		return;
	}

	PendingPredictedShots = FMath::Max(0, PendingPredictedShots - Amount);
#if WITH_DEV_AUTOMATION_TESTS
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICT_REFUND Weapon=%s Refund=%d Pending=%d Snapshot=%d Predicted=%d"),
		*GetNameSafe(this), Amount, PendingPredictedShots, MagazineAmmo, GetPredictedMagazineAmmo());
#endif
}

void AShooterWeapon::ResetAmmoPrediction()
{
	AmmoDisplayState.Reset();
	if (PendingPredictedShots == 0)
	{
		return;
	}

	PendingPredictedShots = 0;
#if WITH_DEV_AUTOMATION_TESTS
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICT_RESET Weapon=%s Snapshot=%d"), *GetNameSafe(this), MagazineAmmo);
#endif
}

void AShooterWeapon::ReducePendingPredictedAmmo(int32 Amount, const TCHAR* Marker)
{
	if (Amount <= 0)
	{
		return;
	}

	PendingPredictedShots = FMath::Max(0, PendingPredictedShots - Amount);
#if WITH_DEV_AUTOMATION_TESTS
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICT_RESOLVE Weapon=%s Marker=%s Amount=%d Pending=%d Snapshot=%d"),
		*GetNameSafe(this), Marker ? Marker : TEXT("-"), Amount, PendingPredictedShots, MagazineAmmo);
#endif
}

void AShooterWeapon::ConfirmPredictedAmmo(int32 Amount)
{
	ReducePendingPredictedAmmo(Amount, TEXT("Confirmed"));
}

void AShooterWeapon::DiscardPhantomPredictedAmmo(int32 Amount)
{
	ReducePendingPredictedAmmo(Amount, TEXT("Phantom"));
}

void AShooterWeapon::BindPredictionAbility(UShooterGameplayAbility_Fire* Ability)
{
	BoundPredictionAbility = Ability;
}

void AShooterWeapon::ClearPredictionAbility()
{
	BoundPredictionAbility.Reset();
}

void AShooterWeapon::BeginAuthorityFireActivation(int32 ActivationKey)
{
	if (!HasAuthority())
	{
		return;
	}

	// 同一时刻只允许一轮打开；若上一轮未结清，先按当前计数结清，避免结果被覆盖。
	if (OpenFireActivationResultSlot != INDEX_NONE)
	{
		SettleAuthorityFireActivation();
	}

	if (FireActivationResults.Num() != FireActivationResultRingSize)
	{
		FireActivationResults.SetNum(FireActivationResultRingSize);
	}

	const int32 Slot = NextFireActivationResultSlot % FireActivationResultRingSize;
	NextFireActivationResultSlot = (Slot + 1) % FireActivationResultRingSize;

	FShooterFireActivationResult& Result = FireActivationResults[Slot];
	Result.ActivationKey = ActivationKey;
	Result.ProcessedShots = 0;
	Result.bSettled = false;
	Result.ActivationSerial = NextFireActivationSerial++;

	OpenFireActivationResultSlot = Slot;
	ForceNetUpdate();
	PublishAuthorityFireActivationResult(Result, /*bForceSettled*/ false);
}

void AShooterWeapon::RecordAuthorityShotCommitted()
{
	if (!HasAuthority() || OpenFireActivationResultSlot == INDEX_NONE)
	{
		return;
	}

	FShooterFireActivationResult& Result = FireActivationResults[OpenFireActivationResultSlot];
	if (Result.bSettled)
	{
		return;
	}

	++Result.ProcessedShots;
	ForceNetUpdate();
	PublishAuthorityFireActivationResult(Result, /*bForceSettled*/ false);
}

void AShooterWeapon::SettleAuthorityFireActivation()
{
	if (!HasAuthority() || OpenFireActivationResultSlot == INDEX_NONE)
	{
		return;
	}

	FShooterFireActivationResult& Result = FireActivationResults[OpenFireActivationResultSlot];
	Result.bSettled = true;
	OpenFireActivationResultSlot = INDEX_NONE;
	ForceNetUpdate();
	PublishAuthorityFireActivationResult(Result, /*bForceSettled*/ true);
	// 一轮只发送一次最终结果。属性环可合并或覆盖，但最终账目不能据此丢失。
	if (Result.ActivationKey > 0 && PawnOwner && PawnOwner->IsPlayerControlled() && !HasOwnerLocalPlayerView())
	{
		RefreshAuthorityAmmoDisplaySnapshot();
		ClientFireActivationSettled(Result, AmmoDisplaySnapshot);
	}
}

void AShooterWeapon::ClientFireActivationSettled_Implementation(const FShooterFireActivationResult& Result,
	const FShooterAmmoDisplaySnapshot& Snapshot)
{
#if WITH_DEV_AUTOMATION_TESTS
	++FinalResultReceivedCountForTest;
#endif
	UE_LOG(LogShootGame, Display, TEXT("FIRE_ACTIVATION_FINAL_RECEIVED Weapon=%s Key=%d Serial=%d Processed=%d"),
		*GetNameSafe(this), Result.ActivationKey, Result.ActivationSerial, Result.ProcessedShots);
	PublishAuthorityFireActivationResult(Result, /*bForceSettled*/ true);
	if (AmmoDisplayState.SettleActivation(Result.ActivationKey, Snapshot))
	{
		PushAmmoToOwnerHud();
	}
}

void AShooterWeapon::PublishAuthorityFireActivationResult(const FShooterFireActivationResult& Result, bool bForceSettled)
{
	// Listen Host / Standalone 的本地玩家同时是权威端，自身复制不会回流，必须直接转发。
	if (!HasOwnerLocalPlayerView() || !BoundPredictionAbility.IsValid())
	{
		return;
	}

	if (UShooterGameplayAbility_Fire* PredictionAbility =
		Cast<UShooterGameplayAbility_Fire>(BoundPredictionAbility.Get()))
	{
		PredictionAbility->HandleAuthorityFireActivationResult(this, Result.ActivationKey, Result.ActivationSerial,
			Result.ProcessedShots, bForceSettled || Result.bSettled);
	}
}

void AShooterWeapon::OnRep_FireActivationResults()
{
	const int32 Num = FireActivationResults.Num();
	CachedFireActivationResults.SetNum(Num);
	for (int32 Index = 0; Index < Num; ++Index)
	{
		const FShooterFireActivationResult& ServerResult = FireActivationResults[Index];
		FShooterFireActivationResult& CachedResult = CachedFireActivationResults[Index];

		// 槽位覆盖不能证明最终发数；旧轮等待可靠最终结果，不按缓存值伪造 Settled。

		if (ServerResult.ActivationKey != 0)
		{
			PublishAuthorityFireActivationResult(ServerResult, /*bForceSettled*/ false);
		}
		CachedResult = ServerResult;
	}
}

void AShooterWeapon::ResetFireActivationResultState()
{
	OpenFireActivationResultSlot = INDEX_NONE;
	NextFireActivationResultSlot = 0;
	FireActivationResults.Reset();
	CachedFireActivationResults.Reset();
	ClearPredictionAbility();
}

void AShooterWeapon::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	ResetMagazinePresentation();
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
	ResetMagazinePresentation();
	CancelOwnerConfirmedFeedback();
	if (UShooterGameplayAbility_Fire* Ability = Cast<UShooterGameplayAbility_Fire>(BoundPredictionAbility.Get()))
	{
		Ability->SuppressWeaponConfirmedFeedback(this);
	}

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

void AShooterWeapon::ShowMagazineProxyInPlace()
{
	auto ShowForMesh = [this](USkeletalMeshComponent* SourceMesh, UStaticMeshComponent* Proxy)
	{
		if (!SourceMesh || !Proxy || !Proxy->GetStaticMesh() || !SourceMesh->DoesSocketExist(MagazineSocketName))
		{
			return;
		}

		const FName ParentBoneName = SourceMesh->GetSocketBoneName(MagazineSocketName);
		if (ParentBoneName.IsNone())
		{
			return;
		}

		// 必须在隐藏父骨骼前读取 Socket 变换；代理不挂在被隐藏的骨骼树下。
		const FTransform SocketWorldTransform = SourceMesh->GetSocketTransform(MagazineSocketName, RTS_World);
		Proxy->SetWorldTransform(SocketWorldTransform);
		Proxy->SetHiddenInGame(false);
		Proxy->SetVisibility(true);
		SourceMesh->HideBoneByName(ParentBoneName, PBO_None);
	};

	ShowForMesh(FirstPersonMesh, FirstPersonMagazineProxy);
	ShowForMesh(ThirdPersonMesh, ThirdPersonMagazineProxy);
}

bool AShooterWeapon::DetachFirstPersonMagazineProxy(USkeletalMeshComponent* CharacterMesh)
{
	return DetachMagazineProxyToHand(FirstPersonMesh, FirstPersonMagazineProxy, CharacterMesh,
		FirstPersonMagazineGripTransform, RootComponent);
}

bool AShooterWeapon::InsertFirstPersonMagazineProxy()
{
	return ResetMagazineProxyForMesh(FirstPersonMesh, FirstPersonMagazineProxy, RootComponent);
}

bool AShooterWeapon::DetachThirdPersonMagazineProxy(USkeletalMeshComponent* CharacterMesh)
{
	return DetachMagazineProxyToHand(ThirdPersonMesh, ThirdPersonMagazineProxy, CharacterMesh,
		ThirdPersonMagazineGripTransform, RootComponent);
}

bool AShooterWeapon::InsertThirdPersonMagazineProxy()
{
	return ResetMagazineProxyForMesh(ThirdPersonMesh, ThirdPersonMagazineProxy, RootComponent);
}

void AShooterWeapon::ResetMagazinePresentation()
{
	ResetMagazineProxyForMesh(FirstPersonMesh, FirstPersonMagazineProxy, RootComponent);
	ResetMagazineProxyForMesh(ThirdPersonMesh, ThirdPersonMagazineProxy, RootComponent);
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

	// 半自动的 RefireTimer 持有 FireCooldownExpired，是"距上一次真实射击是否已满 RefireRate"
	// 的权威冷却标记，不是连发调度器。松开扳机不得取消它，否则 CanStartSemiAutoShotNow()
	// 会在每次释放后立刻恢复为 true，服务器就会接受冷却期内的重复激活
	// （激活成功但 Fire 因 TimeSinceLastShot 不足而静默不发射）。
	// 这种"接受但不开火"的激活会让客户端进入确认回退并补播表现，
	// 形成与权威弹丸数量不符的高速连续枪口 / 声音 / 后坐力。
	// 全自动的 RefireTimer 才是下一发调度器，必须在释放时清除以停止连发。
	if (bFullAuto)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(RefireTimer);
		}
	}
}

bool AShooterWeapon::HasOwnerLocalPlayerView() const
{
	// 必须同时是玩家控制与本机控制：服务器上的 NPC 在 UE 5.6 的 IsLocallyControlled() 也为真。
	return PawnOwner != nullptr && PawnOwner->IsPlayerControlled() && PawnOwner->IsLocallyControlled();
}

bool AShooterWeapon::CanStartSemiAutoShotNow() const
{
	// 全自动不参与该查询：冷却期允许激活，真实补射时机由权威 RefireTimer 决定。
	if (bFullAuto)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// 半自动开火后会把 RefireTimer 排成 FireCooldownExpired：Timer 活动即表示仍在冷却。
	// 池取用与归还都会清除该 Timer，因此首次取用时为未激活，可直接开火。
	return !World->GetTimerManager().IsTimerActive(RefireTimer);
}

bool AShooterWeapon::IsLocalFireCooldownReady() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// 全自动的本地节拍由 PredictedFeedbackTimer 驱动，Timer 到期与本地节拍之间存在浮点边界，
	// 允许 5ms 容差避免恰好在 RefireRate 边界误挡下一拍。
	// 半自动由离散输入驱动，没有 Timer 对齐问题：必须严格比较，
	// 否则玩家可以在节拍结束前数毫秒提前开火并看到一次多余表现。
	const float CooldownTolerance = bFullAuto ? 0.005f : 0.0f;
	return LocalFireCooldownEndTime < 0.0f || World->GetTimeSeconds() + CooldownTolerance >= LocalFireCooldownEndTime;
}

float AShooterWeapon::GetLocalFireCooldownRemaining() const
{
	const UWorld* World = GetWorld();
	return World ? FMath::Max(0.0f, LocalFireCooldownEndTime - World->GetTimeSeconds()) : 0.0f;
}

void AShooterWeapon::AdvanceLocalFireCooldown()
{
	// 只按本武器 RefireRate 推进本地时钟；不读也不写权威 TimeOfLastShot / RefireTimer，
	// 因此服务器节拍与本地节拍各自独立，不会互相纠缠。
	if (const UWorld* World = GetWorld())
	{
		LocalFireCooldownEndTime = World->GetTimeSeconds() + FMath::Max(RefireRate, 0.01f);
	}
}

void AShooterWeapon::ResetLocalFireCooldown()
{
	LocalFireCooldownEndTime = -1.0f;
}

bool AShooterWeapon::PlayOwnerPredictedShotFeedback()
{
	return PlayOwnerShotFeedbackInternal(/*bConfirmedBackfill*/ false);
}

bool AShooterWeapon::PlayOwnerConfirmedShotFeedback()
{
	if (!IsOwnerConfirmedFeedbackTargetValid() || (!FiringMontage && !MuzzleFlash && !FireSound && FMath::IsNearlyZero(FiringRecoil)))
	{
		return false;
	}
	++PendingConfirmedFeedback;
	if (!GetWorld()->GetTimerManager().IsTimerActive(ConfirmedFeedbackTimer))
	{
		DrainOwnerConfirmedFeedback();
	}
	return true;
}

bool AShooterWeapon::IsOwnerConfirmedFeedbackTargetValid() const
{
	const AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwner());
	return GetWorld() && !IsRunningDedicatedServer() && HasOwnerLocalPlayerView() && !IsHidden() &&
		!IsActorBeingDestroyed() && Character && Character->GetCurrentWeaponActor() == this;
}

void AShooterWeapon::CancelOwnerConfirmedFeedback()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ConfirmedFeedbackTimer);
	}
	if (PendingConfirmedFeedback > 0)
	{
		UE_LOG(LogShootGame, Display, TEXT("FIRE_CONFIRMED_QUEUE_CANCEL Weapon=%s Amount=%d"),
			*GetNameSafe(this), PendingConfirmedFeedback);
	}
	PendingConfirmedFeedback = 0;
}

void AShooterWeapon::DrainOwnerConfirmedFeedback()
{
	if (!IsOwnerConfirmedFeedbackTargetValid())
	{
		CancelOwnerConfirmedFeedback();
		return;
	}
	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();
	if (PendingConfirmedFeedback > 0 && Now + UE_SMALL_NUMBER >= NextConfirmedFeedbackTime)
	{
		--PendingConfirmedFeedback;
		if (!PlayOwnerShotFeedbackInternal(/*bConfirmedBackfill*/ true))
		{
			UE_LOG(LogShootGame, Display, TEXT("FIRE_CONFIRMED_FEEDBACK_SUPPRESSED Weapon=%s Reason=Unavailable"), *GetNameSafe(this));
		}
		NextConfirmedFeedbackTime = Now + FMath::Max(RefireRate, 0.01f);
	}
	if (PendingConfirmedFeedback > 0)
	{
		const float Delay = FMath::Max(static_cast<float>(NextConfirmedFeedbackTime - Now), 0.001f);
		World->GetTimerManager().SetTimer(ConfirmedFeedbackTimer, this,
			&AShooterWeapon::DrainOwnerConfirmedFeedback, Delay, false);
	}
}

bool AShooterWeapon::PlayOwnerShotFeedbackInternal(bool bConfirmedBackfill)
{
	// Dedicated Server 没有拥有者本地视图；非本地玩家视图也不是本入口的职责。
	if (IsRunningDedicatedServer() || !HasOwnerLocalPlayerView())
	{
		return false;
	}

	// 本入口只负责四路 cosmetic：不判定本地开火节拍，也不推进它。
	// 节拍只由"本地 Shot Attempt 被接受"处推进，表现通道的成败不得反过来决定射击节拍。

	const bool bHasMontage = FiringMontage != nullptr;
	const bool bHasMuzzle = MuzzleFlash != nullptr;
	const bool bHasSound = FireSound != nullptr;
	const bool bHasRecoil = !FMath::IsNearlyZero(FiringRecoil);

	// 四项表现资产全空时没有任何可提交项。
	if (!bHasMontage && !bHasMuzzle && !bHasSound && !bHasRecoil)
	{
		return false;
	}

	bool bPlayedAny = false;

	// Montage 与 Recoil 共用持有者本地入口；两者各自独立校验，
	// 缺 Montage 不影响 Recoil，缺 Recoil 也不影响 Montage。
	if (WeaponOwner)
	{
		bPlayedAny |= WeaponOwner->PlayOwnerLocalFiringFeedback(FiringMontage, FiringRecoil);
	}

	// 第一人称枪口：资产、第一人称网格与 Muzzle Socket 都有效时才提交。
	if (bHasMuzzle && FirstPersonMesh && FirstPersonMesh->DoesSocketExist(MuzzleSocketName))
	{
		UNiagaraFunctionLibrary::SpawnSystemAttached(MuzzleFlash, FirstPersonMesh, MuzzleSocketName,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true);
		bPlayedAny = true;
#if WITH_DEV_AUTOMATION_TESTS
		++OwnerMuzzleFeedbackCount;
#endif
	}

	// 本地音效挂 RootComponent，不依赖 Muzzle Socket 是否有效。
	if (bHasSound && RootComponent)
	{
		UGameplayStatics::SpawnSoundAttached(FireSound, RootComponent, NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true);
		bPlayedAny = true;
#if WITH_DEV_AUTOMATION_TESTS
		++OwnerSoundFeedbackCount;
#endif
	}

	// 表现提交成功与否都不影响本地开火节拍（节拍已由 Shot Attempt 处推进）。

#if WITH_DEV_AUTOMATION_TESTS
	if (bPlayedAny)
	{
		if (bConfirmedBackfill)
		{
			// 确认补播与本地预测分开计数：它晚于 Confirmation，绝不能参与"预测早于确认"的断言。
			++ConfirmedBackfillFeedbackCount;
			LogFireFeedbackMarker(TEXT("FIRE_CONFIRMED_BACKFILL_OWNER"), ConfirmedBackfillFeedbackCount,
				ConfirmedBackfillFeedbackCount);
		}
		else
		{
			++PredictedOwnerFeedbackCount;
			++FireFeedbackEventSequence;
			LastOwnerFeedbackSequence = FireFeedbackEventSequence;
			if (const UWorld* World = GetWorld())
			{
				const float Now = World->GetTimeSeconds();
				if (LastOwnerFeedbackTime >= 0.0f)
				{
					MinimumOwnerFeedbackInterval = FMath::Min(MinimumOwnerFeedbackInterval, Now - LastOwnerFeedbackTime);
				}
				LastOwnerFeedbackTime = Now;
			}
		}
	}
#endif

	if (bPlayedAny && GetWorld())
	{
		NextConfirmedFeedbackTime = GetWorld()->GetTimeSeconds() + FMath::Max(RefireRate, 0.01f);
	}
	return bPlayedAny;
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

	// ConsumeAmmo 成功即代表这一发已被服务器正式提交；
	// Activation 结果账本在这里 +1，后续任何表现/弹丸失败都不回退。
	RecordAuthorityShotCommitted();

	// 权威弹药消费成功后才执行开火行为与表现。
	ExecuteFireAtTarget(WeaponOwner->GetWeaponTargetLocation());

#if WITH_DEV_AUTOMATION_TESTS
	++AuthorityShotCount;
	LogFireFeedbackMarker(TEXT("FIRE_AUTHORITY_COMMIT"), AuthorityShotCount, AuthorityShotCount);
#endif

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
	// 半自动冷却到期通知。该 Timer 现在可以跨过输入释放继续存活，
	// 因此 Owner 已解除（提池、切枪、销毁）时必须静默，不得解引用空接口。
	if (WeaponOwner)
	{
		WeaponOwner->OnSemiWeaponRefire();
	}
}

void AShooterWeapon::ExecuteFireAtTarget(const FVector& TargetLocation)
{
	// 攻击结果边界：生成弹丸；表现入口统一留在本 Actor（行为边界当前休眠，不参与本路径）。
	FireProjectile(TargetLocation);

	// play the firing montage
	WeaponOwner->PlayFiringMontage(FiringMontage);

	// broadcast the muzzle flash and firing sound
	MulticastPlayFiringFX();

	// 拥有者的 Recoil 由本地拥有者表现路径负责；权威端只对非本地玩家视图保留现状。
	// 否则 Listen Host 与 Standalone 会在本地路径与权威路径各施加一次。
	if (!HasOwnerLocalPlayerView())
	{
		WeaponOwner->AddWeaponRecoil(FiringRecoil);
	}
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
	// Dedicated 服务器没有本地观察者；纯表现不在服务器生成。
	if (IsRunningDedicatedServer())
	{
		return;
	}

	// 没有配置任何表现资产时直接返回
	if (!MuzzleFlash && !FireSound)
	{
		return;
	}

	if (HasOwnerLocalPlayerView())
	{
		// 拥有者的第一人称枪口与音效已由本地预测路径承担；Multicast 到达这里只登记确认。
#if WITH_DEV_AUTOMATION_TESTS
		RecordOwnerAuthorityConfirmationForAutomationTest();
		LastOwnerConfirmationSequence = ++FireFeedbackEventSequence;
		LogFireFeedbackMarker(TEXT("FIRE_AUTHORITY_CONFIRMATION_RECEIVED"), OwnerAuthorityConfirmationCount,
			OwnerAuthorityConfirmationCount);
#endif
		return;
	}

	// Remote 与 NPC 使用第三人称世界网格；拥有者不可见该网格，两者互不重复。
	bool bPlayedAny = false;
	if (MuzzleFlash && ThirdPersonMesh)
	{
		UNiagaraFunctionLibrary::SpawnSystemAttached(MuzzleFlash, ThirdPersonMesh, MuzzleSocketName,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true);
		bPlayedAny = true;
#if WITH_DEV_AUTOMATION_TESTS
		++RemoteMuzzleFeedbackCount;
#endif
	}

	// 开火音效：远端与服务器在武器位置播放，距离衰减由音频系统处理
	if (FireSound)
	{
		UGameplayStatics::SpawnSoundAttached(FireSound, RootComponent, NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true);
		bPlayedAny = true;
#if WITH_DEV_AUTOMATION_TESTS
		++RemoteSoundFeedbackCount;
#endif
	}

#if WITH_DEV_AUTOMATION_TESTS
	if (bPlayedAny)
	{
		++RemoteConfirmedFeedbackCount;
		LogFireFeedbackMarker(TEXT("FIRE_REMOTE_CONFIRMED"), RemoteConfirmedFeedbackCount, RemoteConfirmedFeedbackCount);
	}
#endif
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

#if WITH_DEV_AUTOMATION_TESTS
void AShooterWeapon::ResetFireFeedbackCountersForAutomationTest()
{
	PredictedOwnerFeedbackCount = 0;
	OwnerAuthorityConfirmationCount = 0;
	AuthorityShotCount = 0;
	RemoteConfirmedFeedbackCount = 0;
	OwnerMuzzleFeedbackCount = 0;
	OwnerSoundFeedbackCount = 0;
	RemoteMuzzleFeedbackCount = 0;
	RemoteSoundFeedbackCount = 0;
	FireFeedbackEventSequence = 0;
	LastOwnerFeedbackSequence = 0;
	LastOwnerConfirmationSequence = 0;
	LastOwnerFeedbackTime = -1.0f;
	MinimumOwnerFeedbackInterval = TNumericLimits<float>::Max();
}

int32 AShooterWeapon::GetPredictedOwnerFeedbackCountForAutomationTest() const
{
	return PredictedOwnerFeedbackCount;
}

void AShooterWeapon::RecordOwnerAuthorityConfirmationForAutomationTest()
{
	++OwnerAuthorityConfirmationCount;
}

int32 AShooterWeapon::GetOwnerAuthorityConfirmationCountForAutomationTest() const
{
	return OwnerAuthorityConfirmationCount;
}

int32 AShooterWeapon::GetAuthorityShotCountForAutomationTest() const
{
	return AuthorityShotCount;
}

int32 AShooterWeapon::GetRemoteConfirmedFeedbackCountForAutomationTest() const
{
	return RemoteConfirmedFeedbackCount;
}

void AShooterWeapon::ResetOwnerFeedbackTimingForAutomationTest()
{
	LastOwnerFeedbackTime = -1.0f;
	MinimumOwnerFeedbackInterval = TNumericLimits<float>::Max();
}

bool AShooterWeapon::IsRefireTimerActiveForAutomationTest() const
{
	// 只读现有 RefireTimer，不为测试复制生产判定。
	const UWorld* World = GetWorld();
	return World != nullptr && World->GetTimerManager().IsTimerActive(RefireTimer);
}

void AShooterWeapon::LogFireFeedbackMarker(const TCHAR* Marker, int32 ShotOrdinal, int32 Count) const
{
	// 武器端只输出自己持有的字段。GA 的 PredictionKey 由 GA 侧标记输出，
	// 不持久写进池化 Weapon，避免跨租用残留。
	const APlayerState* OwnerPlayerState = PawnOwner ? PawnOwner->GetPlayerState() : nullptr;
	const int32 PlayerId = OwnerPlayerState ? OwnerPlayerState->GetPlayerId() : INDEX_NONE;
	const UWorld* World = GetWorld();
	const float LocalTime = World ? World->GetTimeSeconds() : 0.0f;

	UE_LOG(LogShootGame, Display, TEXT("%s PlayerId=%d Weapon=%s ShotOrdinal=%d Count=%d LocalTime=%.3f NetMode=%d"),
		Marker, PlayerId, *GetNameSafe(this), ShotOrdinal, Count, LocalTime, static_cast<int32>(GetNetMode()));
}

int32 AShooterWeapon::GetConfirmedBackfillCountForAutomationTest() const
{
	return ConfirmedBackfillFeedbackCount;
}

void AShooterWeapon::SetPendingPredictedShotsForAutomationTest(int32 InPendingShots)
{
	PendingPredictedShots = FMath::Max(0, InPendingShots);
}

FShooterFireActivationResult AShooterWeapon::GetFireActivationResultForTest(int32 SlotIndex) const
{
	return FireActivationResults.IsValidIndex(SlotIndex)
		? FireActivationResults[SlotIndex]
		: FShooterFireActivationResult();
}

void AShooterWeapon::ResetFireActivationStateForAutomationTest()
{
	ResetFireActivationResultState();
}

void AShooterWeapon::SetLocalFireCooldownRemainingForAutomationTest(float RemainingSeconds)
{
	if (const UWorld* World = GetWorld())
	{
		LocalFireCooldownEndTime = World->GetTimeSeconds() + FMath::Max(0.0f, RemainingSeconds);
	}
}
#endif
