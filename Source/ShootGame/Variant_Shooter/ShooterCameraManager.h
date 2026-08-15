// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "ShooterCameraManager.generated.h"

/**
 * Shooter 玩家摄像机管理器。
 * 限制上下视角俯仰范围，保持第一人称射击手感。
 */
UCLASS()
class SHOOTGAME_API AShooterCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:

	/** 构造函数，设置俯仰范围限制。 */
	AShooterCameraManager();
};
