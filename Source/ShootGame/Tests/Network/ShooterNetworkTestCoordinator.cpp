// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/Network/ShooterNetworkTestCoordinator.h"

#include "ShootGame.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Variant_Shooter/Weapons/ShooterProjectile.h"

namespace ShooterNetworkTest
{
	constexpr float PollIntervalSeconds = 0.1f;
	constexpr float TimeoutSeconds = 20.0f;
	const TCHAR* RifleClassPath =
		TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C");
}

AShooterNetworkTestCoordinator::AShooterNetworkTestCoordinator()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bOnlyRelevantToOwner = true;
	SetReplicateMovement(false);
}

void AShooterNetworkTestCoordinator::BeginPlay()
{
	Super::BeginPlay();

	TestStartTime = GetWorld()->GetTimeSeconds();
	if (HasAuthority())
	{
		ActorSpawnedHandle = GetWorld()->AddOnActorSpawnedHandler(
			FOnActorSpawned::FDelegate::CreateUObject(
				this,
				&AShooterNetworkTestCoordinator::HandleActorSpawned));
	}

	const FTimerDelegate PollDelegate = HasAuthority()
		? FTimerDelegate::CreateUObject(this, &AShooterNetworkTestCoordinator::PollServerState)
		: FTimerDelegate::CreateUObject(this, &AShooterNetworkTestCoordinator::PollClientState);

	GetWorldTimerManager().SetTimer(
		PollTimer,
		PollDelegate,
		ShooterNetworkTest::PollIntervalSeconds,
		true,
		ShooterNetworkTest::PollIntervalSeconds);
}

void AShooterNetworkTestCoordinator::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	if (ActorSpawnedHandle.IsValid())
	{
		GetWorld()->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
	}
	GetWorldTimerManager().ClearTimer(PollTimer);

	Super::EndPlay(EndPlayReason);
}

void AShooterNetworkTestCoordinator::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShooterNetworkTestCoordinator, bServerReadyToFire);
}

void AShooterNetworkTestCoordinator::PollServerState()
{
	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		if (GetWorld()->GetTimeSeconds() - TestStartTime >= ShooterNetworkTest::TimeoutSeconds)
		{
			FailTest(TEXT("Server did not receive a shooter character"));
		}
		return;
	}

	AShooterWeapon* Weapon = GetCurrentWeapon(Character);
	if (!Weapon)
	{
		const TSubclassOf<AShooterWeapon> RifleClass = LoadClass<AShooterWeapon>(
			nullptr,
			ShooterNetworkTest::RifleClassPath);
		if (!RifleClass)
		{
			FailTest(TEXT("Rifle class could not be loaded"));
			return;
		}

		Character->AddWeaponClass(RifleClass);
		return;
	}

	if (InitialBulletCount == INDEX_NONE)
	{
		InitialBulletCount = Weapon->GetBulletCount();
		bServerReadyToFire = true;
		ForceNetUpdate();
		return;
	}

	const int32 CurrentBulletCount = Weapon->GetBulletCount();
	if (bClientObservedWeapon &&
		bClientObservedProjectile &&
		bServerObservedProjectile &&
		bAimDirectionValid &&
		CurrentBulletCount < InitialBulletCount)
	{
		const APlayerController* PlayerController = Cast<APlayerController>(GetOwner());
		const int32 PlayerId = PlayerController && PlayerController->PlayerState
			? PlayerController->PlayerState->GetPlayerId()
			: INDEX_NONE;

		UE_LOG(
			LogShootGame,
			Display,
			TEXT("AUTOMATION_TEST_CLIENT_SUCCESS PlayerId=%d Bullets=%d->%d AimDot=%.3f"),
			PlayerId,
			InitialBulletCount,
			CurrentBulletCount,
			ObservedAimDot);
		GetWorldTimerManager().ClearTimer(PollTimer);
		return;
	}

	if (GetWorld()->GetTimeSeconds() - TestStartTime >= ShooterNetworkTest::TimeoutSeconds)
	{
		FailTest(FString::Printf(
			TEXT("Timed out waiting for client fire; observedWeapon=%s clientObservedProjectile=%s serverObservedProjectile=%s aimValid=%s bullets=%d->%d"),
			bClientObservedWeapon ? TEXT("true") : TEXT("false"),
			bClientObservedProjectile ? TEXT("true") : TEXT("false"),
			bServerObservedProjectile ? TEXT("true") : TEXT("false"),
			bAimDirectionValid ? TEXT("true") : TEXT("false"),
			InitialBulletCount,
			CurrentBulletCount));
	}
}

void AShooterNetworkTestCoordinator::HandleActorSpawned(AActor* SpawnedActor)
{
	const AShooterProjectile* Projectile = Cast<AShooterProjectile>(SpawnedActor);
	AShooterCharacter* Character = GetShooterCharacter();
	if (!Projectile || !Character || Projectile->GetInstigator() != Character)
	{
		return;
	}

	bServerObservedProjectile = true;
	const FVector ExpectedDirection = Character->GetControlRotation().Vector();
	const FVector ProjectileDirection = Projectile->GetActorForwardVector();
	ObservedAimDot = FVector::DotProduct(ExpectedDirection, ProjectileDirection);
	// 枪口会朝摄像机射线的实际命中点发射；近处遮挡会让它明显偏离控制器前向，
	// 但正常弹道不应落入控制器朝向的后半球。该条件仍能捕获远程摄像机失效时的反向弹道。
	bAimDirectionValid = ObservedAimDot >= 0.0f;

	if (!bAimDirectionValid)
	{
		FailTest(FString::Printf(
			TEXT("Projectile aim differs from server control rotation; dot=%.3f"),
			ObservedAimDot));
	}
}

void AShooterNetworkTestCoordinator::PollClientState()
{
	APlayerController* PlayerController = Cast<APlayerController>(GetOwner());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!bServerReadyToFire || !Character || !GetCurrentWeapon(Character))
	{
		return;
	}

	if (!bClientTriggeredFire)
	{
		bClientTriggeredFire = true;
		ServerReportClientObservedWeapon();
		Character->DoStartFiring();
		Character->DoStopFiring();
	}

	if (!bClientReportedProjectile)
	{
		for (TActorIterator<AShooterProjectile> It(GetWorld()); It; ++It)
		{
			if (It->GetOwner() == Character || It->GetInstigator() == Character)
			{
				bClientReportedProjectile = true;
				ServerReportClientObservedProjectile();
				GetWorldTimerManager().ClearTimer(PollTimer);
				break;
			}
		}
	}
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedWeapon_Implementation()
{
	bClientObservedWeapon = true;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedProjectile_Implementation()
{
	bClientObservedProjectile = true;
}

AShooterCharacter* AShooterNetworkTestCoordinator::GetShooterCharacter() const
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetOwner());
	return PlayerController ? Cast<AShooterCharacter>(PlayerController->GetPawn()) : nullptr;
}

AShooterWeapon* AShooterNetworkTestCoordinator::GetCurrentWeapon(AShooterCharacter* Character) const
{
	if (!Character)
	{
		return nullptr;
	}

	const FObjectPropertyBase* CurrentWeaponProperty =
		FindFProperty<FObjectPropertyBase>(Character->GetClass(), TEXT("CurrentWeapon"));
	return CurrentWeaponProperty
		? Cast<AShooterWeapon>(CurrentWeaponProperty->GetObjectPropertyValue_InContainer(Character))
		: nullptr;
}

void AShooterNetworkTestCoordinator::FailTest(const FString& Reason)
{
	UE_LOG(LogShootGame, Error, TEXT("AUTOMATION_TEST_FAILURE: %s"), *Reason);
	GetWorldTimerManager().ClearTimer(PollTimer);
}
