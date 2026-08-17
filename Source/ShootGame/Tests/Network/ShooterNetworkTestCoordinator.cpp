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
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystemComponent.h"
#include "ShooterCharacter.h"
#include "ShooterGameState.h"
#include "ShooterPlayerState.h"
#include "ShooterWeapon.h"
#include "ShooterProjectile.h"

namespace ShooterNetworkTest
{
	constexpr float PollIntervalSeconds = 0.1f;
	constexpr float TimeoutSeconds = 60.0f;
	const TCHAR* RifleClassPath =
		TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C");
	const TCHAR* PistolClassPath =
		TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Pistol.BP_ShooterWeapon_Pistol_C");
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

	// ---- GAS ASC 生命周期（服务器视角）：Owner=PlayerState，Avatar=当前角色 ----
	if (!bServerGasLifecycleChecked)
	{
		bServerGasLifecycleChecked = true;

		const APlayerController* PlayerController = Cast<APlayerController>(GetOwner());
		AShooterPlayerState* ShooterPlayerState = PlayerController
			? PlayerController->GetPlayerState<AShooterPlayerState>()
			: nullptr;
		UAbilitySystemComponent* AbilitySystemComponent = ShooterPlayerState
			? ShooterPlayerState->GetAbilitySystemComponent()
			: nullptr;

		bServerGasOwnerOk = AbilitySystemComponent &&
			AbilitySystemComponent->GetOwnerActor() == ShooterPlayerState;
		bServerGasAvatarOk = AbilitySystemComponent &&
			AbilitySystemComponent->GetAvatarActor() == Character;
		// Mixed 复制模式依赖 OwnerActor（PlayerState → PlayerController）的网络连接。
		// 运行时事实：监听服务器的主机玩家是本地权威玩家，PlayerState 没有网络连接，
		// 属于预期情况；连接要求只对远程客户端成立。
		bServerGasConnectionOk = (ShooterPlayerState &&
			ShooterPlayerState->GetNetConnection() != nullptr) ||
			(GetNetMode() == NM_ListenServer && PlayerController && PlayerController->IsLocalController());
		ObservedAbilitySystemComponent = AbilitySystemComponent;

		if (!bServerGasOwnerOk || !bServerGasAvatarOk || !bServerGasConnectionOk)
		{
			FailTest(FString::Printf(
				TEXT("Server-side GAS ASC lifecycle invalid; ASC=%s OwnerOk=%s AvatarOk=%s ConnectionOk=%s Owner=%s Avatar=%s"),
				*GetNameSafe(AbilitySystemComponent),
				bServerGasOwnerOk ? TEXT("true") : TEXT("false"),
				bServerGasAvatarOk ? TEXT("true") : TEXT("false"),
				bServerGasConnectionOk ? TEXT("true") : TEXT("false"),
				*GetNameSafe(AbilitySystemComponent ? AbilitySystemComponent->GetOwnerActor() : nullptr),
				*GetNameSafe(AbilitySystemComponent ? AbilitySystemComponent->GetAvatarActor() : nullptr)));
			return;
		}
	}

	// ---- GAS NPC ASC 生命周期（服务器视角）：Owner=Avatar=NPC ----
	if (!bNpcGasLifecycleChecked)
	{
		bNpcGasLifecycleChecked = true;

		// 生成无控制器的测试 NPC，验证其 ASC 后立即销毁。
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AShooterNetworkTestNPC* TestNpc = GetWorld()->SpawnActor<AShooterNetworkTestNPC>(
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			SpawnParameters);
		if (!TestNpc)
		{
			FailTest(TEXT("Server could not spawn the GAS NPC lifecycle test actor"));
			return;
		}

		UAbilitySystemComponent* NpcAbilitySystemComponent = TestNpc->GetAbilitySystemComponent();
		bNpcGasLifecycleOk = NpcAbilitySystemComponent &&
			NpcAbilitySystemComponent->GetOwnerActor() == TestNpc &&
			NpcAbilitySystemComponent->GetAvatarActor() == TestNpc;
		if (!bNpcGasLifecycleOk)
		{
			FailTest(FString::Printf(
				TEXT("NPC GAS ASC lifecycle invalid; ASC=%s Owner=%s Avatar=%s"),
				*GetNameSafe(NpcAbilitySystemComponent),
				*GetNameSafe(NpcAbilitySystemComponent ? NpcAbilitySystemComponent->GetOwnerActor() : nullptr),
				*GetNameSafe(NpcAbilitySystemComponent ? NpcAbilitySystemComponent->GetAvatarActor() : nullptr)));
		}
		TestNpc->Destroy();
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
		// 网络测试验证权威瞄准链路，关闭单次随机散布以保证方向断言可重复。
		if (FFloatProperty* AimVarianceProperty =
			FindFProperty<FFloatProperty>(Weapon->GetClass(), TEXT("AimVariance")))
		{
			AimVarianceProperty->SetPropertyValue_InContainer(Weapon, 0.0f);
		}
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
		BulletCountAfterFire = CurrentBulletCount;
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

		CharacterBeforeDeath = Character;
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
		if (OwnerState && OwnerState->GetKills() >= 1)
		{
			bOpponentKilledForStats = true;
		}
		else if (OwnerState)
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
				bOpponentKilledForStats = OwnerState->GetKills() >= 1;
			}
		}
	}

	const APlayerController* PlayerController = Cast<APlayerController>(GetOwner());
	const bool bServerObservedRespawn = bLethalDamageApplied &&
		Character != CharacterBeforeDeath.Get() &&
		!Character->IsDead() &&
		Character->GetCurrentHP() > 0.0f &&
		Character->GetCharacterMovement()->MovementMode != MOVE_None &&
		PlayerController && PlayerController->GetPawn() == Character &&
		Character->GetController() == PlayerController;

	// ---- GAS ASC 重生生命周期（服务器视角）：ASC 本体与 Owner 不变，Avatar 切换到新角色 ----
	if (bServerObservedRespawn && bLethalDamageApplied && !bServerGasRespawnChecked)
	{
		bServerGasRespawnChecked = true;

		AShooterPlayerState* ShooterPlayerState = PlayerController
			? PlayerController->GetPlayerState<AShooterPlayerState>()
			: nullptr;
		UAbilitySystemComponent* AbilitySystemComponent = ShooterPlayerState
			? ShooterPlayerState->GetAbilitySystemComponent()
			: nullptr;
		bServerGasRespawnOk = AbilitySystemComponent &&
			AbilitySystemComponent == ObservedAbilitySystemComponent.Get() &&
			AbilitySystemComponent->GetOwnerActor() == ShooterPlayerState &&
			AbilitySystemComponent->GetAvatarActor() == Character &&
			Character != CharacterBeforeDeath.Get();

		if (!bServerGasRespawnOk)
		{
			FailTest(FString::Printf(
				TEXT("Server-side GAS ASC respawn avatar invalid; ASC=%s SameASC=%s OwnerOk=%s Avatar=%s OldAvatar=%s"),
				*GetNameSafe(AbilitySystemComponent),
				AbilitySystemComponent == ObservedAbilitySystemComponent.Get() ? TEXT("true") : TEXT("false"),
				AbilitySystemComponent && AbilitySystemComponent->GetOwnerActor() == ShooterPlayerState ? TEXT("true") : TEXT("false"),
				*GetNameSafe(AbilitySystemComponent ? AbilitySystemComponent->GetAvatarActor() : nullptr),
				*GetNameSafe(CharacterBeforeDeath.Get())));
			return;
		}
	}

	if (bLethalDamageApplied && bClientObservedDeath && bClientObservedRespawn &&
		bClientObservedMatchState && bClientObservedRemoteAim &&
		(!bRequireRemoteMontage || bClientObservedRemoteMontage) && bServerObservedRespawn &&
		bClientObservedGasLifecycle && bClientObservedGasRespawn && bServerGasRespawnOk)
	{
		const int32 PlayerId = PlayerController && PlayerController->PlayerState
			? PlayerController->PlayerState->GetPlayerId()
			: INDEX_NONE;

		UE_LOG(
			LogShootGame,
			Display,
			TEXT("AUTOMATION_TEST_CLIENT_SUCCESS PlayerId=%d Switch=true OwnerAmmo=true NonOwnerAmmoHidden=true Bullets=%d->%d HP=%.0f->0 Dead=true Respawn=true RespawnHP=%.0f AimDot=%.3f Team=%u Kills=%d Deaths=%d TeamScore=%d RemotePitch=%.3f/%.3f RemoteMontage=%s GasServer=%s/%s/%s GasNPC=%s GasRespawn=%s GasClient=%s GasClientRespawn=%s"),
			PlayerId,
			InitialBulletCount,
			BulletCountAfterFire,
			InitialHP,
			Character->GetCurrentHP(),
			ObservedAimDot,
			ObservedTeamId,
			ObservedKills,
			ObservedDeaths,
			ObservedTeamScore,
			ObservedRemotePitchN,
			ExpectedRemotePitchN,
			bClientObservedRemoteMontage ? TEXT("true") : TEXT("skipped"),
			bServerGasOwnerOk ? TEXT("true") : TEXT("false"),
			bServerGasAvatarOk ? TEXT("true") : TEXT("false"),
			bServerGasConnectionOk ? TEXT("true") : TEXT("false"),
			bNpcGasLifecycleOk ? TEXT("true") : TEXT("false"),
			bServerGasRespawnOk ? TEXT("true") : TEXT("false"),
			bClientObservedGasLifecycle ? TEXT("true") : TEXT("false"),
			bClientObservedGasRespawn ? TEXT("true") : TEXT("false"));
		GetWorldTimerManager().ClearTimer(PollTimer);
		return;
	}

	if (GetWorld()->GetTimeSeconds() - TestStartTime >= ShooterNetworkTest::TimeoutSeconds)
	{
		const AShooterPlayerState* TimeoutPlayerState = PlayerController
			? PlayerController->GetPlayerState<AShooterPlayerState>()
			: nullptr;
		const AShooterGameState* TimeoutGameState =
			GetWorld()->GetGameState<AShooterGameState>();
		const uint8 TimeoutTeamId = TimeoutPlayerState
			? TimeoutPlayerState->GetTeamId()
			: MAX_uint8;
		const int32 TimeoutTeamScore = TimeoutGameState
			? TimeoutGameState->GetTeamScore(TimeoutTeamId)
			: INDEX_NONE;

		FailTest(FString::Printf(
			TEXT("Timed out waiting for network state; switch=%s weapon=%s clientProjectile=%s ownerAmmo=%s nonOwnerAmmo=%s serverProjectile=%s aim=%s damage=%s death=%s respawn=%s matchState=%s remoteAim=%s remoteMontage=%s gasOwner=%s gasAvatar=%s gasConnection=%s gasNpc=%s gasRespawn=%s gasClient=%s gasClientRespawn=%s bullets=%d->%d hp=%.0f team=%u kills=%d deaths=%d score=%.0f teamScore=%d"),
			bClientObservedSwitch ? TEXT("true") : TEXT("false"),
			bClientObservedWeapon ? TEXT("true") : TEXT("false"),
			bClientObservedProjectile ? TEXT("true") : TEXT("false"),
			bClientObservedOwnerAmmo ? TEXT("true") : TEXT("false"),
			bClientObservedNonOwnerAmmoHidden ? TEXT("true") : TEXT("false"),
			bServerObservedProjectile ? TEXT("true") : TEXT("false"),
			bAimDirectionValid ? TEXT("true") : TEXT("false"),
			bClientObservedDamage ? TEXT("true") : TEXT("false"),
			bClientObservedDeath ? TEXT("true") : TEXT("false"),
			bClientObservedRespawn ? TEXT("true") : TEXT("false"),
			bClientObservedMatchState ? TEXT("true") : TEXT("false"),
			bClientObservedRemoteAim ? TEXT("true") : TEXT("false"),
			bClientObservedRemoteMontage ? TEXT("true") : TEXT("false"),
			bServerGasOwnerOk ? TEXT("true") : TEXT("false"),
			bServerGasAvatarOk ? TEXT("true") : TEXT("false"),
			bServerGasConnectionOk ? TEXT("true") : TEXT("false"),
			bNpcGasLifecycleOk ? TEXT("true") : TEXT("false"),
			bServerGasRespawnOk ? TEXT("true") : TEXT("false"),
			bClientObservedGasLifecycle ? TEXT("true") : TEXT("false"),
			bClientObservedGasRespawn ? TEXT("true") : TEXT("false"),
			InitialBulletCount,
			CurrentBulletCount,
			Character->GetCurrentHP(),
			TimeoutTeamId,
			TimeoutPlayerState ? TimeoutPlayerState->GetKills() : INDEX_NONE,
			TimeoutPlayerState ? TimeoutPlayerState->GetDeaths() : INDEX_NONE,
			TimeoutPlayerState ? TimeoutPlayerState->GetScore() : -1.0f,
			TimeoutTeamScore));
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

	// ---- GAS ASC 生命周期（拥有者客户端视角）：Owner=PlayerState，Avatar=本地角色 ----
	if (!bClientReportedGasLifecycle)
	{
		AShooterPlayerState* ShooterPlayerState =
			PlayerController->GetPlayerState<AShooterPlayerState>();
		UAbilitySystemComponent* AbilitySystemComponent = ShooterPlayerState
			? ShooterPlayerState->GetAbilitySystemComponent()
			: nullptr;
		if (AbilitySystemComponent &&
			AbilitySystemComponent->GetOwnerActor() == ShooterPlayerState &&
			AbilitySystemComponent->GetAvatarActor() == Character &&
			Character->IsLocallyControlled())
		{
			bClientReportedGasLifecycle = true;
			ServerReportClientObservedGasLifecycle();
		}
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

	if (bClientReportedDeath && !bClientReportedRespawn && !Character->IsDead() &&
		Character->GetCurrentHP() > 0.0f && Character->IsLocallyControlled() &&
		Character->GetCharacterMovement()->MovementMode != MOVE_None &&
		PlayerController->GetPawn() == Character && Character->GetController() == PlayerController)
	{
		bClientReportedRespawn = true;
		ServerReportClientObservedRespawn();
	}

	// ---- GAS ASC 重生生命周期（拥有者客户端视角）：Avatar 切换到复活后的新角色 ----
	if (bClientReportedRespawn && !bClientReportedGasRespawn)
	{
		AShooterPlayerState* ShooterPlayerState =
			PlayerController->GetPlayerState<AShooterPlayerState>();
		UAbilitySystemComponent* AbilitySystemComponent = ShooterPlayerState
			? ShooterPlayerState->GetAbilitySystemComponent()
			: nullptr;
		if (AbilitySystemComponent &&
			AbilitySystemComponent->GetOwnerActor() == ShooterPlayerState &&
			AbilitySystemComponent->GetAvatarActor() == Character)
		{
			bClientReportedGasRespawn = true;
			ServerReportClientObservedGasRespawn();
		}
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

void AShooterNetworkTestCoordinator::ServerReportClientObservedRespawn_Implementation()
{
	bClientObservedRespawn = true;
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

void AShooterNetworkTestCoordinator::ServerReportClientObservedGasLifecycle_Implementation()
{
	bClientObservedGasLifecycle = true;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedGasRespawn_Implementation()
{
	bClientObservedGasRespawn = true;
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
