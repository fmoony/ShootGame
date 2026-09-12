// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterPickup.h"
#include "AbilitySystemComponent.h"
#include "ShooterGameplayTags.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "ShooterCharacter.h"
#include "ShooterInventoryComponent.h"
#include "ShooterWeapon.h"
#include "ShooterWeaponConfigRow.h"
#include "ShooterWeaponRuntimeSubsystem.h"
#include "Engine/World.h"
#include "ShootGame.h"
#include "TimerManager.h"

AShooterPickup::AShooterPickup()
{
 	PrimaryActorTick.bCanEverTick = true;

	// create the root
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// create the collision sphere
	SphereCollision = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere Collision"));
	SphereCollision->SetupAttachment(RootComponent);

	SphereCollision->SetRelativeLocation(FVector(0.0f, 0.0f, 84.0f));
	SphereCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SphereCollision->SetCollisionObjectType(ECC_WorldStatic);
	SphereCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	SphereCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	SphereCollision->bFillCollisionUnderneathForNavmesh = true;

	// 开启复制，让隐藏/碰撞状态能同步到客户端
	bReplicates = true;

	// subscribe to the collision overlap on the sphere
	SphereCollision->OnComponentBeginOverlap.AddDynamic(this, &AShooterPickup::OnOverlap);

	// create the mesh
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(SphereCollision);

	Mesh->SetCollisionProfileName(FName("NoCollision"));
}

void AShooterPickup::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Pickup 只负责把所选行的预览网格贴到自己的 Mesh 上；行本身仍是唯一配置来源。
	if (const FShooterWeaponConfigRow* WeaponData = WeaponType.GetRow<FShooterWeaponConfigRow>(FString()))
	{
		Mesh->SetStaticMesh(WeaponData->PickupMesh.LoadSynchronous());
	}
}

void AShooterPickup::BeginPlay()
{
	Super::BeginPlay();

	// 行选择完全由 WeaponType 表达；缺失行在 Overlap 时 fail closed，这里不复制任何配置。
	if (!WeaponType.GetRow<FShooterWeaponConfigRow>(FString()))
	{
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("Pickup %s has no resolvable weapon row: Row=%s"),
			*GetNameSafe(this),
			*WeaponType.RowName.ToString());
	}
}

void AShooterPickup::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the respawn timer
	GetWorld()->GetTimerManager().ClearTimer(RespawnTimer);
}

void AShooterPickup::OnOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// 只有服务器可以发放武器；客户端只观察复制的结果。
	if (!HasAuthority() || !bPickupAvailable)
	{
		return;
	}

	AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(OtherActor);
	UShooterInventoryComponent* Inventory = ShooterCharacter
		? ShooterCharacter->GetInventoryComponent()
		: nullptr;
	if (!Inventory || ShooterCharacter->IsDead())
	{
		return;
	}

	// 拾取会立即装备新武器，必须在授予 Inventory 和占用 Pickup 前拒绝换弹中的角色。
	const UAbilitySystemComponent* AbilitySystemComponent = ShooterCharacter->GetAbilitySystemComponent();
	if (AbilitySystemComponent &&
		AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Reloading))
	{
		return;
	}

	// S3 授予链（重构方案 3.3 / 4.6）：行名值即 WeaponId（S4 由资产层把属性改为 WeaponId）。
	const FName WeaponId = WeaponType.RowName;
	if (WeaponId.IsNone())
	{
		return;
	}

	// 重复武器类型：不消费 Pickup，明确拒绝后允许后续合法拾取重试。
	if (Inventory->HasWeaponId(WeaponId))
	{
		return;
	}

	UShooterWeaponRuntimeSubsystem* Runtime = GetWorld()
		? GetWorld()->GetSubsystem<UShooterWeaponRuntimeSubsystem>()
		: nullptr;
	if (!Runtime || !Runtime->HasWeaponId(WeaponId))
	{
		// 未知 WeaponId：配置缺失，不消费 Pickup。
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("Pickup %s references unknown WeaponId=%s"),
			*GetNameSafe(this),
			*WeaponId.ToString());
		return;
	}

	// 同一 Pickup 的连续 Overlap 只在服务器端处理一次；成功授予后才进入隐藏/重生流程。
	bPickupAvailable = false;

	// 先 Acquire（池命中或弹性 Spawn）：失败不提交任何数据，Pickup 保持可拾取。
	AShooterWeapon* Weapon = Runtime->AcquireWeapon(WeaponId, ShooterCharacter, ShooterCharacter);
	if (!Weapon)
	{
		bPickupAvailable = true;
		return;
	}

	// 后提交：Inventory 校验（判重 / Slot）失败时把 Actor 归还池，Pickup 保持可拾取。
	const EShooterInventoryAddResult AddResult = Inventory->AddWeapon(Weapon);
	if (AddResult != EShooterInventoryAddResult::Added)
	{
		Runtime->ReleaseWeapon(Weapon);
		bPickupAvailable = true;
		return;
	}

	// 装备步骤意外失败（含 Equipment 缺失）只执行纵深防御：
	// 移除未完成 Entry 并把 Actor 归还池，Pickup 恢复可拾取（重构方案 3.3）。
	UShooterEquipmentComponent* Equipment = ShooterCharacter->GetEquipmentComponent();
	if (!Equipment || !Equipment->EquipWeapon(Weapon))
	{
		if (!Inventory->RemoveWeapon(Weapon))
		{
			UE_LOG(
				LogShootGame,
				Warning,
				TEXT("Pickup equip rollback failed to remove granted weapon: Actor=%s WeaponId=%s"),
				*GetNameSafe(ShooterCharacter),
				*WeaponId.ToString());
		}

		bPickupAvailable = true;
		return;
	}

	// hide this mesh
	SetActorHiddenInGame(true);

	// disable collision
	SetActorEnableCollision(false);

	// disable ticking
	SetActorTickEnabled(false);

	// schedule the respawn
	GetWorld()->GetTimerManager().SetTimer(RespawnTimer, this, &AShooterPickup::RespawnPickup, RespawnTime, false);
}

void AShooterPickup::RespawnPickup()
{
	bPickupAvailable = true;

	// unhide this pickup
	SetActorHiddenInGame(false);

	// call the BP handler
	BP_OnRespawn();
}

void AShooterPickup::FinishRespawn()
{
	// enable collision
	SetActorEnableCollision(true);

	// enable tick
	SetActorTickEnabled(true);
}
