// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterGameMode.h"
#include "ShootGame.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ShooterGameState.h"
#include "ShooterPlayerState.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/ShooterWeaponHolder.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Tests/Network/ShooterNetworkTestCoordinator.h"
#include "TimerManager.h"

AShooterGameMode::AShooterGameMode()
{
	GameStateClass = AShooterGameState::StaticClass();
	PlayerStateClass = AShooterPlayerState::StaticClass();
}

void AShooterGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	if (AShooterPlayerState* ShooterPlayerState =
		NewPlayer ? NewPlayer->GetPlayerState<AShooterPlayerState>() : nullptr)
	{
		const int32 JoinedPlayerIndex = FMath::Max(0, GetNumPlayers() - 1);
		ShooterPlayerState->SetTeamId(static_cast<uint8>(JoinedPlayerIndex % 2));
	}

#if WITH_DEV_AUTOMATION_TESTS
	if (NewPlayer && FParse::Param(FCommandLine::Get(), TEXT("ShootGameNetworkTest")))
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = NewPlayer;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GetWorld()->SpawnActor<AShooterNetworkTestCoordinator>(
			AShooterNetworkTestCoordinator::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	}
#endif
}

void AShooterGameMode::Logout(AController* Exiting)
{
#if WITH_DEV_AUTOMATION_TESTS
	const bool bRunDisconnectTest = FParse::Param(
		FCommandLine::Get(),
		TEXT("ShootGameDisconnectTest"));
#endif

	Super::Logout(Exiting);

#if WITH_DEV_AUTOMATION_TESTS
	if (bRunDisconnectTest)
	{
		TWeakObjectPtr<UWorld> TestWorld = GetWorld();
		GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda(
			[TestWorld]()
			{
				if (!TestWorld.IsValid())
				{
					return;
				}

				int32 ActiveWeaponCount = 0;
				int32 OrphanWeaponCount = 0;
				for (TActorIterator<AShooterWeapon> It(TestWorld.Get()); It; ++It)
				{
					if (!It->IsActorBeingDestroyed())
					{
						++ActiveWeaponCount;

						const AActor* WeaponOwner = It->GetOwner();
						if (!IsValid(WeaponOwner)
							|| WeaponOwner->IsActorBeingDestroyed()
							|| !WeaponOwner->Implements<UShooterWeaponHolder>())
						{
							++OrphanWeaponCount;
						}
					}
				}

				if (ActiveWeaponCount <= 0 || OrphanWeaponCount > 0)
				{
					UE_LOG(
						LogShootGame,
						Error,
						TEXT("AUTOMATION_TEST_FAILURE: Disconnect left invalid weapon ownership Active=%d Orphans=%d"),
						ActiveWeaponCount,
						OrphanWeaponCount);
					return;
				}

				UE_LOG(
					LogShootGame,
					Display,
					TEXT("AUTOMATION_TEST_DISCONNECT_SUCCESS ActiveWeapons=%d Orphans=%d"),
					ActiveWeaponCount,
					OrphanWeaponCount);
			}));
	}
#endif
}

void AShooterGameMode::IncrementTeamScore(uint8 TeamId)
{
	if (AShooterGameState* ShooterGameState = GetGameState<AShooterGameState>())
	{
		ShooterGameState->AddTeamScore(TeamId);
	}
}

void AShooterGameMode::RestartPlayerAfterDeath(AController* PlayerController)
{
	if (!IsValid(PlayerController))
	{
		return;
	}

	APawn* DeadPawn = PlayerController->GetPawn();
	PlayerController->UnPossess();
	if (IsValid(DeadPawn))
	{
		DeadPawn->Destroy();
	}

	RestartPlayer(PlayerController);
}
