// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterCameraManager.h"

AShooterCameraManager::AShooterCameraManager()
{
	// 与原模板 AShootGameCameraManager 保持一致：限制上下视角俯仰范围。
	ViewPitchMin = -70.0f;
	ViewPitchMax = 80.0f;
}
