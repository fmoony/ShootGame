// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ShooterPlayerController.generated.h"

class UInputMappingContext;
class AShooterCharacter;
class AShooterGameState;
class UShooterBulletCounterUI;
class UShooterUI;

/**
 *  Simple PlayerController for a first person shooter game
 *  Manages input mappings
 *  Respawns the player pawn when it's destroyed
 */
UCLASS(abstract)
class SHOOTGAME_API AShooterPlayerController : public APlayerController
{
	GENERATED_BODY()
	
protected:

	/** Input mapping contexts for this player */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** Type of bullet counter UI widget to spawn */
	UPROPERTY(EditAnywhere, Category="Shooter|UI")
	TSubclassOf<UShooterBulletCounterUI> BulletCounterUIClass;

	/** Tag to grant the possessed pawn to flag it as the player */
	UPROPERTY(EditAnywhere, Category="Shooter|Player")
	FName PlayerPawnTag = FName("Player");

	/** Pointer to the bullet counter UI widget */
	TObjectPtr<UShooterBulletCounterUI> BulletCounterUI;

	/** 计分板 Widget 类型；未配置时使用 Shooter 模板默认资源。 */
	UPROPERTY(EditAnywhere, Category="Shooter|UI")
	TSubclassOf<UShooterUI> ShooterUIClass;

	/** 只存在于本地 PlayerController 的计分板实例。 */
	UPROPERTY(Transient)
	TObjectPtr<UShooterUI> ShooterUI;

	UPROPERTY(Transient)
	TObjectPtr<AShooterCharacter> BoundShooterCharacter;

	UPROPERTY(Transient)
	TObjectPtr<AShooterGameState> BoundShooterGameState;

protected:

	/** Gameplay Initialization */
	virtual void BeginPlay() override;

	/** Initialize input bindings */
	virtual void SetupInputComponent() override;

	/** Pawn initialization */
	virtual void OnPossess(APawn* InPawn) override;

	/** 客户端收到 Pawn 复制后绑定本地 HUD。 */
	virtual void OnRep_Pawn() override;

	void BindToShooterCharacter(AShooterCharacter* ShooterCharacter);
	void BindToShooterGameState();

	/** 本地 Pawn 销毁时清理 HUD 引用；复活由服务器 GameMode 负责。 */
	UFUNCTION()
	void OnPawnDestroyed(AActor* DestroyedActor);

	/** Called when the bullet count on the possessed pawn is updated */
	UFUNCTION()
	void OnBulletCountUpdated(int32 MagazineSize, int32 Bullets);

	/** Called when the possessed pawn is damaged */
	UFUNCTION()
	void OnPawnDamaged(float LifePercent);

	/** 队伍分数复制后更新本地计分板。 */
	UFUNCTION()
	void OnTeamScoreChanged(uint8 TeamId, int32 Score);
};
