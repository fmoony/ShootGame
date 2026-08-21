// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterGameMode.h"
#include "ShootGame.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "ShooterGameState.h"
#include "ShooterNPC.h"
#include "ShooterPlayerState.h"
#include "ShooterAbilitySystemComponent.h"
#include "ShooterGameplayAbility_Reload.h"
#include "ShooterGameplayAbility_Equip.h"
#include "ShooterGameplayTags.h"
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
		// 网络测试期间停用地图中 NPC 的 AI 与射击，消除旧基线 NPC 干扰对确定性验证的影响。
		// 只停止行为，不销毁 NPC：GAS 生命周期检查仍覆盖真实 BP_ShooterNPC 实例。
		for (TActorIterator<AShooterNPC> It(GetWorld()); It; ++It)
		{
			AShooterNPC* Npc = *It;
			Npc->StopShooting();
			if (AController* NpcController = Npc->GetController())
			{
				NpcController->Destroy();
			}
		}

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
	const bool bRunEquipDisconnectTest = FParse::Param(
		FCommandLine::Get(),
		TEXT("ShootGameDisconnectEquip"));
#endif

	TWeakObjectPtr<AController> DisconnectController = Exiting;
	Super::Logout(Exiting);

#if WITH_DEV_AUTOMATION_TESTS
	if (bRunDisconnectTest)
	{
		TWeakObjectPtr<UWorld> TestWorld = GetWorld();
		FTimerHandle DisconnectCheckTimer;
		// 5B：等待 0.5 秒让 Pawn EndPlay 完成 GA_Reload 取消后再检查，避免断线下一帧误报。
		GetWorldTimerManager().SetTimer(
			DisconnectCheckTimer,
			FTimerDelegate::CreateLambda(
				[TestWorld, DisconnectController, bRunEquipDisconnectTest]()
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

				// 5B / 5C Cancel.Disconnect：只检查真正断线连接的 PlayerState，
				// 不得残留活动 GA_Reload / GA_Equip 或 State.Reloading / State.Equipping。
				int32 ActiveReloadAbilityCount = 0;
				int32 ReloadingTagCount = 0;
				int32 ActiveEquipAbilityCount = 0;
				int32 EquippingTagCount = 0;
				AShooterPlayerState* DisconnectedPlayerState = DisconnectController.IsValid()
					? DisconnectController->GetPlayerState<AShooterPlayerState>()
					: nullptr;
				UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
					DisconnectedPlayerState
						? Cast<UShooterAbilitySystemComponent>(
							DisconnectedPlayerState->GetAbilitySystemComponent())
						: nullptr;
				if (ShooterAbilitySystemComponent)
				{
					ActiveReloadAbilityCount += ShooterAbilitySystemComponent->GetActiveAbilityCountForClass(
						DisconnectedPlayerState->GetReloadAbilityClass());
					ActiveEquipAbilityCount += ShooterAbilitySystemComponent->GetActiveAbilityCountForClass(
						DisconnectedPlayerState->GetEquipAbilityClass());
					if (ShooterAbilitySystemComponent->HasMatchingGameplayTag(
						ShooterGameplayTags::State_Reloading))
					{
						++ReloadingTagCount;
					}
					if (ShooterAbilitySystemComponent->HasMatchingGameplayTag(
						ShooterGameplayTags::State_Equipping))
					{
						++EquippingTagCount;
					}
				}

				if (ActiveWeaponCount <= 0 || OrphanWeaponCount > 0 ||
					ActiveReloadAbilityCount > 0 || ReloadingTagCount > 0 ||
					ActiveEquipAbilityCount > 0 || EquippingTagCount > 0)
				{
					UE_LOG(
						LogShootGame,
						Error,
						TEXT("AUTOMATION_TEST_FAILURE: Disconnect left invalid weapon ownership Active=%d Orphans=%d ActiveReload=%d ReloadingTags=%d ActiveEquip=%d EquippingTags=%d"),
						ActiveWeaponCount,
						OrphanWeaponCount,
						ActiveReloadAbilityCount,
						ReloadingTagCount,
						ActiveEquipAbilityCount,
						EquippingTagCount);
					return;
				}

				if (bRunEquipDisconnectTest)
				{
					UE_LOG(
						LogShootGame,
						Display,
						TEXT("AUTOMATION_TEST_EQUIP_CLEANUP_SUCCESS Kind=Disconnect ActiveWeapons=%d Orphans=%d ActiveEquip=%d EquippingTags=%d"),
						ActiveWeaponCount,
						OrphanWeaponCount,
						ActiveEquipAbilityCount,
						EquippingTagCount);
				}
				else
				{
					UE_LOG(
						LogShootGame,
						Display,
						TEXT("AUTOMATION_TEST_DISCONNECT_SUCCESS ActiveWeapons=%d Orphans=%d ActiveReload=%d ReloadingTags=%d ActiveEquip=%d EquippingTags=%d"),
						ActiveWeaponCount,
						OrphanWeaponCount,
						ActiveReloadAbilityCount,
						ReloadingTagCount,
						ActiveEquipAbilityCount,
						EquippingTagCount);
				}
				}),
			0.5f,
			false);
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
