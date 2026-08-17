// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterNPC.h"
#include "ShooterWeapon.h"
#include "ShootGame.h"
#include "AbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/World.h"
#include "ShooterGameMode.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

AShooterNPC::AShooterNPC()
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	// Minimal：NPC 只需服务器维护能力状态，客户端只同步最小数据。
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
}

UAbilitySystemComponent* AShooterNPC::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AShooterNPC::BeginPlay()
{
	Super::BeginPlay();

	// NPC 的 ASC Owner 与 Avatar 都是自身，在专用服务器上无需 Controller 也能独立初始化。
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("GAS NPC ActorInfo init: Actor=%s Role=%d NetMode=%d ASC=%s OwnerActor=%s AvatarActor=%s"),
			*GetName(),
			static_cast<int32>(GetLocalRole()),
			static_cast<int32>(GetNetMode()),
			*GetNameSafe(AbilitySystemComponent),
			*GetNameSafe(AbilitySystemComponent->GetOwnerActor()),
			*GetNameSafe(AbilitySystemComponent->GetAvatarActor()));
	}

	// spawn the weapon
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	Weapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponClass, GetActorTransform(), SpawnParams);
}

void AShooterNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// 销毁时解除 ASC ActorInfo，避免残留已销毁 Actor 的引用。
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->ClearActorInfo();
	}

	// clear the death timer
	GetWorld()->GetTimerManager().ClearTimer(DeathTimer);
}

float AShooterNPC::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// ignore if already dead
	if (bIsDead)
	{
		return 0.0f;
	}

	// Reduce HP
	CurrentHP -= Damage;

	// Have we depleted HP?
	if (CurrentHP <= 0.0f)
	{
		Die();
	}

	return Damage;
}

void AShooterNPC::AttachWeaponMeshes(AShooterWeapon* WeaponToAttach)
{
	const FAttachmentTransformRules AttachmentRule(EAttachmentRule::SnapToTarget, false);

	// attach the weapon actor
	WeaponToAttach->AttachToActor(this, AttachmentRule);

	// NPC 只挂接第三人称武器 Mesh，不再借用玩家第一人称手臂。
	WeaponToAttach->GetThirdPersonMesh()->AttachToComponent(GetMesh(), AttachmentRule, ThirdPersonWeaponSocket);
}

void AShooterNPC::PlayFiringMontage(UAnimMontage* Montage)
{
	// unused
}

void AShooterNPC::AddWeaponRecoil(float Recoil)
{
	// unused
}

void AShooterNPC::UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize)
{
	// unused
}

FVector AShooterNPC::GetWeaponTargetLocation()
{
	// NPC 不再借用玩家第一人称摄像机，瞄准起点改用 Pawn 视点。
	const FVector AimSource = GetPawnViewLocation();

	FVector AimDir, AimTarget = FVector::ZeroVector;

	// do we have an aim target?
	if (CurrentAimTarget)
	{
		// target the actor location
		AimTarget = CurrentAimTarget->GetActorLocation();

		// apply a vertical offset to target head/feet
		AimTarget.Z += FMath::RandRange(MinAimOffsetZ, MaxAimOffsetZ);

		// get the aim direction and apply randomness in a cone
		AimDir = (AimTarget - AimSource).GetSafeNormal();
		AimDir = UKismetMathLibrary::RandomUnitVectorInConeInDegrees(AimDir, AimVarianceHalfAngle);

		
	} else {

		// no aim target, so use the NPC's own facing (driven by the AI Controller)
		AimDir = UKismetMathLibrary::RandomUnitVectorInConeInDegrees(GetActorForwardVector(), AimVarianceHalfAngle);

	}

	// calculate the unobstructed aim target location
	AimTarget = AimSource + (AimDir * AimRange);

	// run a visibility trace to see if there's obstructions
	FHitResult OutHit;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	GetWorld()->LineTraceSingleByChannel(OutHit, AimSource, AimTarget, ECC_Visibility, QueryParams);

	// return either the impact point or the trace end
	return OutHit.bBlockingHit ? OutHit.ImpactPoint : OutHit.TraceEnd;
}

void AShooterNPC::AddWeaponClass(const TSubclassOf<AShooterWeapon>& InWeaponClass)
{
	// unused
}

void AShooterNPC::OnWeaponActivated(AShooterWeapon* InWeapon)
{
	// unused
}

void AShooterNPC::OnWeaponDeactivated(AShooterWeapon* InWeapon)
{
	// unused
}

void AShooterNPC::OnSemiWeaponRefire()
{
	// are we still shooting?
	if (bIsShooting)
	{
		// fire the weapon
		Weapon->StartFiring();
	}
}

void AShooterNPC::Die()
{
	// ignore if already dead
	if (bIsDead)
	{
		return;
	}

	// raise the dead flag
	bIsDead = true;

	// increment the team score
	if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->IncrementTeamScore(TeamByte);
	}

	// disable capsule collision
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// stop movement
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->StopActiveMovement();

	// enable ragdoll physics on the third person mesh
	GetMesh()->SetCollisionProfileName(RagdollCollisionProfile);
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetPhysicsBlendWeight(1.0f);

	// schedule actor destruction
	GetWorld()->GetTimerManager().SetTimer(DeathTimer, this, &AShooterNPC::DeferredDestruction, DeferredDestructionTime, false);
}

void AShooterNPC::DeferredDestruction()
{
	Destroy();
}

void AShooterNPC::StartShooting(AActor* ActorToShoot)
{
	// save the aim target
	CurrentAimTarget = ActorToShoot;

	// raise the flag
	bIsShooting = true;

	// signal the weapon
	Weapon->StartFiring();
}

void AShooterNPC::StopShooting()
{
	// lower the flag
	bIsShooting = false;

	// signal the weapon
	Weapon->StopFiring();
}
