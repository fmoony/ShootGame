// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterPickup.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "ShooterCharacter.h"
#include "ShooterInventoryComponent.h"
#include "ShooterWeapon.h"
#include "Engine/World.h"
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

	if (FWeaponTableRow* WeaponData = WeaponType.GetRow<FWeaponTableRow>(FString()))
	{
		// set the mesh
		Mesh->SetStaticMesh(WeaponData->StaticMesh.LoadSynchronous());
	}
}

void AShooterPickup::BeginPlay()
{
	Super::BeginPlay();

	if (FWeaponTableRow* WeaponData = WeaponType.GetRow<FWeaponTableRow>(FString()))
	{
		// copy the weapon class
		WeaponClass = WeaponData->WeaponToSpawn;
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

	// 同一 Pickup 的连续 Overlap 只在服务器端处理一次；成功授予后才进入隐藏/重生流程。
	bPickupAvailable = false;

	FGuid GrantedInstanceId;
	const EShooterInventoryAddResult AddResult =
		Inventory->TryAddWeapon(WeaponClass, GrantedInstanceId);
	if (AddResult == EShooterInventoryAddResult::Added)
	{
		// R3：拾取后的“立即装备”只通过 Equipment facade 提交，不再直接调用 Character 装备事务。
		UShooterEquipmentComponent* Equipment = ShooterCharacter->GetEquipmentComponent();
		if (!Equipment || !Equipment->EquipWeapon(GrantedInstanceId))
		{
			// 理论不可达：Inventory 刚成功创建了 Actor；失败时明确不消费 Pickup。
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
	else
	{
		// 明确 Reject：重复定义或 Slot 满时不消费 Pickup，允许后续合法拾取重试。
		bPickupAvailable = true;
	}
}

void AShooterPickup::RespawnPickup()
{
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
