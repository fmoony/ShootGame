// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "ShooterPoolableActor.h"
#include "ShooterActorPoolTestTypes.generated.h"

/** 轻量池化测试 Actor A：记录池回调次数。 */
UCLASS(NotBlueprintable, Transient)
class AShooterPoolTestActorA : public AActor, public IShooterPoolableActor
{
	GENERATED_BODY()

public:
	AShooterPoolTestActorA()
	{
		PrimaryActorTick.bCanEverTick = true;
		RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("TestRoot"));
	}

	virtual void OnAcquiredFromPool() override
	{
		++AcquireCallbackCount;
	}

	virtual void OnReleasedToPool() override
	{
		++ReleaseCallbackCount;
	}

	int32 AcquireCallbackCount = 0;
	int32 ReleaseCallbackCount = 0;
};

/** 轻量池化测试 Actor B：与 A 不同类，验证跨 Class 不串池。 */
UCLASS(NotBlueprintable, Transient)
class AShooterPoolTestActorB : public AActor, public IShooterPoolableActor
{
	GENERATED_BODY()

public:
	AShooterPoolTestActorB()
	{
		PrimaryActorTick.bCanEverTick = true;
		RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("TestRoot"));
	}

	virtual void OnAcquiredFromPool() override
	{
	}

	virtual void OnReleasedToPool() override
	{
	}
};

/** 未实现 Poolable 契约的普通 Actor：验证池对非池化类同样可用。 */
UCLASS(NotBlueprintable, Transient)
class AShooterPoolTestActorPlain : public AActor
{
	GENERATED_BODY()

public:
	AShooterPoolTestActorPlain()
	{
		RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("TestRoot"));
	}
};
