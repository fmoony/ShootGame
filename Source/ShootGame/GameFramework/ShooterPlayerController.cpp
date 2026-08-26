// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "ShooterCharacter.h"
#include "ShooterGameState.h"
#include "ShooterCameraManager.h"
#include "ShooterUI.h"
#include "ShooterBulletCounterUI.h"
#include "Weapons/ShooterWeapon.h"
#include "ShootGame.h"
#include "Widgets/Input/SVirtualJoystick.h"

AShooterPlayerController::AShooterPlayerController()
{
	// 使用 Shooter 摄像机管理器，保持与原模板一致的俯仰范围限制。
	PlayerCameraManagerClass = AShooterCameraManager::StaticClass();
}

void AShooterPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Widget 和视口只属于本地控制器，专用服务器不会进入这里。
	if (!IsLocalPlayerController())
	{
		return;
	}

	if (SVirtualJoystick::ShouldDisplayTouchInterface())
	{
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);
		if (MobileControlsWidget)
		{
			MobileControlsWidget->AddToPlayerScreen(0);
		}
		else
		{
			UE_LOG(LogShootGame, Error, TEXT("Could not spawn mobile controls widget."));
		}
	}

	BulletCounterUI = CreateWidget<UShooterBulletCounterUI>(this, BulletCounterUIClass);
	if (BulletCounterUI)
	{
		BulletCounterUI->AddToPlayerScreen(0);
	}
	else
	{
		UE_LOG(LogShootGame, Error, TEXT("Could not spawn bullet counter widget."));
	}

	if (!ShooterUIClass)
	{
		ShooterUIClass = LoadClass<UShooterUI>(
			nullptr,
			TEXT("/Game/Shooter/UI/UI_Shooter.UI_Shooter_C"));
	}

	ShooterUI = CreateWidget<UShooterUI>(this, ShooterUIClass);
	if (ShooterUI)
	{
		ShooterUI->AddToPlayerScreen(0);
	}
	else
	{
		UE_LOG(LogShootGame, Error, TEXT("Could not spawn shooter scoreboard widget."));
	}

	BindToShooterGameState();
	BindToShooterCharacter(Cast<AShooterCharacter>(GetPawn()));
}

void AShooterPlayerController::SetupInputComponent()
{
	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// add the input mapping contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!SVirtualJoystick::ShouldDisplayTouchInterface())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
}

void AShooterPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(InPawn))
	{
		if (HasAuthority())
		{
			ShooterCharacter->Tags.AddUnique(PlayerPawnTag);
		}

		BindToShooterCharacter(ShooterCharacter);
	}
}

void AShooterPlayerController::OnRep_Pawn()
{
	Super::OnRep_Pawn();
	BindToShooterGameState();
	BindToShooterCharacter(Cast<AShooterCharacter>(GetPawn()));
}

void AShooterPlayerController::BindToShooterCharacter(AShooterCharacter* ShooterCharacter)
{
	if (!IsLocalController())
	{
		return;
	}

	if (BoundShooterCharacter && BoundShooterCharacter != ShooterCharacter)
	{
		BoundShooterCharacter->OnDestroyed.RemoveDynamic(
			this,
			&AShooterPlayerController::OnPawnDestroyed);
		BoundShooterCharacter->OnBulletCountUpdated.RemoveDynamic(
			this,
			&AShooterPlayerController::OnBulletCountUpdated);
		BoundShooterCharacter->OnDamaged.RemoveDynamic(
			this,
			&AShooterPlayerController::OnPawnDamaged);
	}

	BoundShooterCharacter = ShooterCharacter;
	if (!BoundShooterCharacter)
	{
		return;
	}

	BoundShooterCharacter->OnDestroyed.AddUniqueDynamic(
		this,
		&AShooterPlayerController::OnPawnDestroyed);
	BoundShooterCharacter->OnBulletCountUpdated.AddUniqueDynamic(
		this,
		&AShooterPlayerController::OnBulletCountUpdated);
	BoundShooterCharacter->OnDamaged.AddUniqueDynamic(
		this,
		&AShooterPlayerController::OnPawnDamaged);

	OnPawnDamaged(BoundShooterCharacter->GetHealthRatio());
	if (const AShooterWeapon* Weapon = BoundShooterCharacter->GetCurrentWeapon())
	{
		OnBulletCountUpdated(Weapon->GetMagazineSize(), Weapon->GetBulletCount());
	}
}

void AShooterPlayerController::BindToShooterGameState()
{
	if (!IsLocalController())
	{
		return;
	}

	AShooterGameState* ShooterGameState = GetWorld()
		? GetWorld()->GetGameState<AShooterGameState>()
		: nullptr;
	if (BoundShooterGameState == ShooterGameState)
	{
		return;
	}

	if (BoundShooterGameState)
	{
		BoundShooterGameState->OnTeamScoreChanged.RemoveDynamic(
			this,
			&AShooterPlayerController::OnTeamScoreChanged);
	}

	BoundShooterGameState = ShooterGameState;
	if (!BoundShooterGameState)
	{
		return;
	}

	BoundShooterGameState->OnTeamScoreChanged.AddUniqueDynamic(
		this,
		&AShooterPlayerController::OnTeamScoreChanged);
	for (int32 TeamIndex = 0; TeamIndex < BoundShooterGameState->GetTeamCount(); ++TeamIndex)
	{
		OnTeamScoreChanged(
			static_cast<uint8>(TeamIndex),
			BoundShooterGameState->GetTeamScore(static_cast<uint8>(TeamIndex)));
	}
}

void AShooterPlayerController::OnPawnDestroyed(AActor* DestroyedActor)
{
	// 只有本地控制器拥有 HUD；服务器复活由 ShooterGameMode 统一负责。
	if (IsLocalController() && IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_UpdateBulletCounter(0, 0);
	}
	if (DestroyedActor == BoundShooterCharacter)
	{
		BoundShooterCharacter = nullptr;
	}
}

void AShooterPlayerController::OnBulletCountUpdated(int32 MagazineSize, int32 Bullets)
{
	// update the UI
	if (BulletCounterUI)
	{
		BulletCounterUI->BP_UpdateBulletCounter(MagazineSize, Bullets);
	}
}

void AShooterPlayerController::OnPawnDamaged(float LifePercent)
{
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_Damaged(LifePercent);
	}
}

void AShooterPlayerController::OnTeamScoreChanged(uint8 TeamId, int32 Score)
{
	if (IsValid(ShooterUI))
	{
		ShooterUI->BP_UpdateScore(TeamId, Score);
	}
}
