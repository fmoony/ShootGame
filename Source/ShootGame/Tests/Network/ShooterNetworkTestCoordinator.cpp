// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/Network/ShooterNetworkTestCoordinator.h"

#include "ShootGame.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/ShooterGameState.h"
#include "Variant_Shooter/ShooterPlayerState.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Variant_Shooter/Weapons/ShooterProjectile.h"

namespace ShooterNetworkTest
{
	constexpr float PollIntervalSeconds = 0.1f;
	constexpr float TimeoutSeconds = 60.0f;
	const TCHAR* RifleClassPath =
		TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C");
	const TCHAR* PistolClassPath =
		TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Pistol.BP_ShooterWeapon_Pistol_C");
}

AShooterNetworkTestCoordinator::AShooterNetworkTestCoordinator()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bOnlyRelevantToOwner = true;
	SetReplicateMovement(false);
	bRequireRemoteMontage = !FParse::Param(
		FCommandLine::Get(),
		TEXT("ShootGameSkipRemoteMontage"));
	bDisconnectCleanupMode = FParse::Param(
		FCommandLine::Get(),
		TEXT("ShootGameDisconnectTest"));
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
	DOREPLIFETIME(AShooterNetworkTestCoordinator, bServerReadyToSwitch);
	DOREPLIFETIME(AShooterNetworkTestCoordinator, bServerReadyToFire);
	DOREPLIFETIME(AShooterNetworkTestCoordinator, WeaponBeforeSwitch);
	DOREPLIFETIME(AShooterNetworkTestCoordinator, bRequireRemoteMontage);
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

	const AGameStateBase* GameState = GetWorld()->GetGameState();
	if (!GameState || GameState->PlayerArray.Num() < 2)
	{
		return;
	}

	if (APlayerController* OwnerController = Cast<APlayerController>(GetOwner());
		OwnerController && OwnerController->IsLocalController())
	{
		FRotator ControlRotation = OwnerController->GetControlRotation();
		ControlRotation.Pitch = 30.0f;
		OwnerController->SetControlRotation(ControlRotation);
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

	if (!bSecondaryWeaponGranted)
	{
		const TSubclassOf<AShooterWeapon> PistolClass = LoadClass<AShooterWeapon>(
			nullptr,
			ShooterNetworkTest::PistolClassPath);
		if (!PistolClass)
		{
			FailTest(TEXT("Pistol class could not be loaded"));
			return;
		}

		Character->AddWeaponClass(PistolClass);
		WeaponBeforeSwitch = GetCurrentWeapon(Character);
		bSecondaryWeaponGranted = true;
		bServerReadyToSwitch = true;
		ForceNetUpdate();
		return;
	}

	if (bDisconnectCleanupMode)
	{
		return;
	}

	Weapon = GetCurrentWeapon(Character);
	if (!bClientObservedSwitch || Weapon == WeaponBeforeSwitch)
	{
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
	const bool bFireReplicationVerified = bClientObservedWeapon &&
		bClientObservedProjectile &&
		bClientObservedOwnerAmmo &&
		bClientObservedNonOwnerAmmoHidden &&
		bServerObservedProjectile &&
		bAimDirectionValid &&
		CurrentBulletCount < InitialBulletCount;

	if (bFireReplicationVerified && !bPartialDamageApplied)
	{
		InitialHP = Character->GetCurrentHP();
		UGameplayStatics::ApplyDamage(
			Character,
			FMath::Max(1.0f, Character->GetMaxHP() * 0.25f),
			nullptr,
			this,
			nullptr);
		bPartialDamageApplied = true;
		return;
	}

	if (bPartialDamageApplied && bClientObservedDamage && !bLethalDamageApplied)
	{
		AController* OpponentController = GetOpponentController();
		if (!OpponentController)
		{
			return;
		}

		UGameplayStatics::ApplyDamage(
			Character,
			Character->GetMaxHP() * 2.0f,
			OpponentController,
			this,
			nullptr);
		bLethalDamageApplied = true;
		return;
	}

	if (GetNetMode() == NM_ListenServer && bLethalDamageApplied &&
		Character->IsDead() && !bOpponentKilledForStats)
	{
		APlayerController* OwnerController = Cast<APlayerController>(GetOwner());
		const AShooterPlayerState* OwnerState = OwnerController
			? OwnerController->GetPlayerState<AShooterPlayerState>()
			: nullptr;
		if (OwnerState && OwnerState->GetKills() < 1)
		{
			AController* OpponentController = GetOpponentController();
			AShooterCharacter* OpponentCharacter = OpponentController
				? Cast<AShooterCharacter>(OpponentController->GetPawn())
				: nullptr;
			if (OpponentCharacter && !OpponentCharacter->IsDead())
			{
				UGameplayStatics::ApplyDamage(
					OpponentCharacter,
					OpponentCharacter->GetMaxHP() * 2.0f,
					OwnerController,
					this,
					nullptr);
			}
		}
		bOpponentKilledForStats = true;
	}

	if (bLethalDamageApplied && bClientObservedDeath &&
		bClientObservedMatchState && bClientObservedRemoteAim &&
		(!bRequireRemoteMontage || bClientObservedRemoteMontage) && Character->IsDead())
	{
		const APlayerController* PlayerController = Cast<APlayerController>(GetOwner());
		const int32 PlayerId = PlayerController && PlayerController->PlayerState
			? PlayerController->PlayerState->GetPlayerId()
			: INDEX_NONE;

		UE_LOG(
			LogShootGame,
			Display,
			TEXT("AUTOMATION_TEST_CLIENT_SUCCESS PlayerId=%d Switch=true OwnerAmmo=true NonOwnerAmmoHidden=true Bullets=%d->%d HP=%.0f->%.0f Dead=true AimDot=%.3f Team=%u Kills=%d Deaths=%d TeamScore=%d RemotePitch=%.3f/%.3f RemoteMontage=%s"),
			PlayerId,
			InitialBulletCount,
			CurrentBulletCount,
			InitialHP,
			Character->GetCurrentHP(),
			ObservedAimDot,
			ObservedTeamId,
			ObservedKills,
			ObservedDeaths,
			ObservedTeamScore,
			ObservedRemotePitchN,
			ExpectedRemotePitchN,
			bClientObservedRemoteMontage ? TEXT("true") : TEXT("skipped"));
		GetWorldTimerManager().ClearTimer(PollTimer);
		return;
	}

	if (GetWorld()->GetTimeSeconds() - TestStartTime >= ShooterNetworkTest::TimeoutSeconds)
	{
		FailTest(FString::Printf(
			TEXT("Timed out waiting for network state; switch=%s weapon=%s clientProjectile=%s ownerAmmo=%s nonOwnerAmmo=%s serverProjectile=%s aim=%s damage=%s death=%s matchState=%s remoteAim=%s remoteMontage=%s bullets=%d->%d hp=%.0f"),
			bClientObservedSwitch ? TEXT("true") : TEXT("false"),
			bClientObservedWeapon ? TEXT("true") : TEXT("false"),
			bClientObservedProjectile ? TEXT("true") : TEXT("false"),
			bClientObservedOwnerAmmo ? TEXT("true") : TEXT("false"),
			bClientObservedNonOwnerAmmoHidden ? TEXT("true") : TEXT("false"),
			bServerObservedProjectile ? TEXT("true") : TEXT("false"),
			bAimDirectionValid ? TEXT("true") : TEXT("false"),
			bClientObservedDamage ? TEXT("true") : TEXT("false"),
			bClientObservedDeath ? TEXT("true") : TEXT("false"),
			bClientObservedMatchState ? TEXT("true") : TEXT("false"),
			bClientObservedRemoteAim ? TEXT("true") : TEXT("false"),
			bClientObservedRemoteMontage ? TEXT("true") : TEXT("false"),
			InitialBulletCount,
			CurrentBulletCount,
			Character->GetCurrentHP()));
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
	AShooterWeapon* Weapon = GetCurrentWeapon(Character);
	if (!Character || !Weapon)
	{
		return;
	}

	if (!bClientSetAimPitch)
	{
		FRotator ControlRotation = PlayerController->GetControlRotation();
		ControlRotation.Pitch = 30.0f;
		PlayerController->SetControlRotation(ControlRotation);
		bClientSetAimPitch = true;
	}

	for (TActorIterator<AShooterCharacter> It(GetWorld()); It; ++It)
	{
		AShooterCharacter* RemoteCharacter = *It;
		if (RemoteCharacter == Character ||
			RemoteCharacter->GetLocalRole() != ROLE_SimulatedProxy)
		{
			continue;
		}

		UAnimInstance* RemoteAnimInstance = RemoteCharacter->GetMesh()
			? RemoteCharacter->GetMesh()->GetAnimInstance()
			: nullptr;
		if (!RemoteAnimInstance)
		{
			continue;
		}

		if (!bClientReportedRemoteAim)
		{
			const FNumericProperty* PitchProperty = FindFProperty<FNumericProperty>(
				RemoteAnimInstance->GetClass(),
				TEXT("PitchN"));
			if (PitchProperty && PitchProperty->IsFloatingPoint())
			{
				const void* PitchValue = PitchProperty->ContainerPtrToValuePtr<void>(RemoteAnimInstance);
				const float PitchN = static_cast<float>(
					PitchProperty->GetFloatingPointPropertyValue(PitchValue));
				const float ExpectedPitchN = RemoteCharacter->GetBaseAimRotation().Vector().Z;
				if (FMath::Abs(ExpectedPitchN) >= 0.2f &&
					FMath::IsNearlyEqual(PitchN, ExpectedPitchN, 0.05f))
				{
					bClientReportedRemoteAim = true;
					ServerReportClientObservedRemoteAim(PitchN, ExpectedPitchN);
				}
			}
		}

		if (!bClientReportedRemoteMontage && RemoteAnimInstance->IsAnyMontagePlaying())
		{
			bClientReportedRemoteMontage = true;
			ServerReportClientObservedRemoteMontage();
		}
	}

	if (bServerReadyToSwitch && WeaponBeforeSwitch &&
		Weapon == WeaponBeforeSwitch && !bClientTriggeredSwitch)
	{
		bClientTriggeredSwitch = true;
		InitialClientWeapon = Weapon;
		Character->DoSwitchWeapon();
		return;
	}

	if (bClientTriggeredSwitch && !bClientReportedSwitch &&
		Weapon != InitialClientWeapon.Get() && Weapon->GetBulletCount() > 0)
	{
		bClientReportedSwitch = true;
		InitialClientBulletCount = Weapon->GetBulletCount();
		ServerReportClientObservedSwitch();
	}

	if (!bClientReportedNonOwnerAmmoHidden)
	{
		for (TActorIterator<AShooterWeapon> It(GetWorld()); It; ++It)
		{
			if (It->GetOwner() != Character && It->GetBulletCount() == 0)
			{
				bClientReportedNonOwnerAmmoHidden = true;
				ServerReportNonOwnerAmmoHidden();
				break;
			}
		}
	}

	if (!bServerReadyToFire || !bClientReportedSwitch)
	{
		return;
	}

	if (!bClientTriggeredFire)
	{
		InitialClientBulletCount = Weapon->GetBulletCount();
		bClientTriggeredFire = true;
		ServerReportClientObservedWeapon();
		Character->DoStartFiring();
		Character->DoStopFiring();
	}

	if (!bClientReportedOwnerAmmo && InitialClientBulletCount > 0 &&
		Weapon->GetBulletCount() < InitialClientBulletCount)
	{
		bClientReportedOwnerAmmo = true;
		ServerReportOwnerAmmoReplicated();
	}

	if (!bClientReportedProjectile)
	{
		for (TActorIterator<AShooterProjectile> It(GetWorld()); It; ++It)
		{
			if (It->GetOwner() == Character || It->GetInstigator() == Character)
			{
				bClientReportedProjectile = true;
				ServerReportClientObservedProjectile();
				break;
			}
		}
	}

	if (!bClientReportedDamage && Character->GetCurrentHP() > 0.0f &&
		Character->GetCurrentHP() < Character->GetMaxHP())
	{
		bClientReportedDamage = true;
		ServerReportClientObservedDamage();
	}

	if (!bClientReportedDeath && Character->IsDead())
	{
		bClientReportedDeath = true;
		ServerReportClientObservedDeath();
	}

	if (!bClientReportedMatchState)
	{
		const AShooterPlayerState* ShooterPlayerState =
			PlayerController->GetPlayerState<AShooterPlayerState>();
		const AShooterGameState* ShooterGameState =
			GetWorld()->GetGameState<AShooterGameState>();
		if (ShooterPlayerState && ShooterGameState)
		{
			const uint8 TeamId = ShooterPlayerState->GetTeamId();
			const int32 TeamScore = ShooterGameState->GetTeamScore(TeamId);
			if (TeamId < 2 && ShooterPlayerState->GetKills() >= 1 &&
				ShooterPlayerState->GetDeaths() >= 1 &&
				ShooterPlayerState->GetScore() >= 1.0f && TeamScore >= 1)
			{
				bClientReportedMatchState = true;
				ServerReportClientObservedMatchState(
					TeamId,
					ShooterPlayerState->GetKills(),
					ShooterPlayerState->GetDeaths(),
					TeamScore);
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

void AShooterNetworkTestCoordinator::ServerReportClientObservedSwitch_Implementation()
{
	bClientObservedSwitch = true;
}

void AShooterNetworkTestCoordinator::ServerReportOwnerAmmoReplicated_Implementation()
{
	bClientObservedOwnerAmmo = true;
}

void AShooterNetworkTestCoordinator::ServerReportNonOwnerAmmoHidden_Implementation()
{
	bClientObservedNonOwnerAmmoHidden = true;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedDamage_Implementation()
{
	bClientObservedDamage = true;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedDeath_Implementation()
{
	bClientObservedDeath = true;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedMatchState_Implementation(
	uint8 TeamId,
	int32 Kills,
	int32 Deaths,
	int32 TeamScore)
{
	bClientObservedMatchState = TeamId < 2 && Kills >= 1 && Deaths >= 1 && TeamScore >= 1;
	ObservedTeamId = TeamId;
	ObservedKills = Kills;
	ObservedDeaths = Deaths;
	ObservedTeamScore = TeamScore;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedRemoteAim_Implementation(
	float PitchN,
	float ExpectedPitchN)
{
	bClientObservedRemoteAim = FMath::Abs(ExpectedPitchN) >= 0.2f &&
		FMath::IsNearlyEqual(PitchN, ExpectedPitchN, 0.05f);
	ObservedRemotePitchN = PitchN;
	ExpectedRemotePitchN = ExpectedPitchN;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedRemoteMontage_Implementation()
{
	bClientObservedRemoteMontage = true;
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

AController* AShooterNetworkTestCoordinator::GetOpponentController() const
{
	const AController* OwnerController = Cast<AController>(GetOwner());
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		if (PlayerController && PlayerController != OwnerController)
		{
			return PlayerController;
		}
	}

	return nullptr;
}

void AShooterNetworkTestCoordinator::FailTest(const FString& Reason)
{
	UE_LOG(LogShootGame, Error, TEXT("AUTOMATION_TEST_FAILURE: %s"), *Reason);
	GetWorldTimerManager().ClearTimer(PollTimer);
}
