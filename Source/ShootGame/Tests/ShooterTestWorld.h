// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace ShooterTestWorld
{
	inline void Destroy(UWorld* World)
	{
		// EndPlay 和子系统清理均可能依赖仍已注册的 WorldContext。
		if (!World->EndPlay(EEndPlayReason::Quit))
		{
			// 部分夹具只向单个 Actor 手动发送 BeginPlay。
			for (FActorIterator It(World); It; ++It)
			{
				if (It->HasActorBegunPlay())
				{
					It->RouteEndPlay(EEndPlayReason::Quit);
				}
			}
		}
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
}
