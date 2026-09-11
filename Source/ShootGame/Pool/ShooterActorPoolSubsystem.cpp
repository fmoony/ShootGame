// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterActorPoolSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ShootGame.h"
#include "ShooterPoolableActor.h"

namespace
{
	bool IsPoolableClass(UClass* ActorClass)
	{
		return ActorClass &&
			ActorClass->IsChildOf<AActor>() &&
			!ActorClass->HasAnyClassFlags(CLASS_Abstract);
	}
}

bool UShooterActorPoolSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// 只在真实游戏 World 存在；编辑器预览 World 不创建。
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UShooterActorPoolSubsystem::Deinitialize()
{
	// World teardown 的幂等清理边界：显式销毁全部池内与在用 Actor，不留悬空对象。
	if (UWorld* World = GetWorld())
	{
		for (TPair<TObjectPtr<UClass>, FShooterActorPool>& Pair : Pools)
		{
			for (AActor* Pooled : Pair.Value.PooledActors)
			{
				if (IsValid(Pooled))
				{
					Pooled->Destroy();
				}
			}
			Pair.Value.PooledActors.Empty();
		}

		for (AActor* Managed : ManagedActors)
		{
			if (IsValid(Managed))
			{
				Managed->Destroy();
			}
		}
		ManagedActors.Empty();
	}

	Super::Deinitialize();
}

bool UShooterActorPoolSubsystem::IsPoolAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

AActor* UShooterActorPoolSubsystem::Acquire(
	UClass* ActorClass,
	const FTransform& SpawnTransform,
	const FActorSpawnParameters& SpawnParameters)
{
	if (!IsPoolAuthority())
	{
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("ActorPool Acquire rejected: non-authority world"));
		return nullptr;
	}

	if (!IsPoolableClass(ActorClass))
	{
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("ActorPool Acquire rejected: invalid actor class %s"),
			*GetNameSafe(ActorClass));
		return nullptr;
	}

	FShooterActorPool& Pool = FindOrAddPool(ActorClass);

	// 池命中：弹出并做通用复位。
	while (Pool.PooledActors.Num() > 0)
	{
		AActor* Pooled = Pool.PooledActors.Pop();
		if (!IsValid(Pooled))
		{
			continue;
		}

		ManagedActors.Add(Pooled);
		ApplyAcquiredState(Pooled, SpawnTransform, SpawnParameters);
		UE_LOG(
			LogShootGame,
			Verbose,
			TEXT("ActorPool Acquire reused: Class=%s Actor=%s"),
			*GetNameSafe(ActorClass),
			*GetNameSafe(Pooled));
		return Pooled;
	}

	// 池未命中：生成新 Actor。
	FActorSpawnParameters SpawnParams = SpawnParameters;
	if (SpawnParams.SpawnCollisionHandlingOverride == ESpawnActorCollisionHandlingMethod::Undefined)
	{
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	}
	AActor* Spawned = GetWorld()->SpawnActor<AActor>(ActorClass, SpawnTransform, SpawnParams);
	if (!Spawned)
	{
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("ActorPool Acquire spawn failed: Class=%s"),
			*GetNameSafe(ActorClass));
		return nullptr;
	}

	ManagedActors.Add(Spawned);
	// 新生成的 Actor 与复用 Actor 走同一获取复位，保证两条路径状态一致。
	ApplyAcquiredState(Spawned, SpawnTransform, SpawnParameters);
	UE_LOG(
		LogShootGame,
		Verbose,
		TEXT("ActorPool Acquire spawned: Class=%s Actor=%s"),
		*GetNameSafe(ActorClass),
		*GetNameSafe(Spawned));
	return Spawned;
}

bool UShooterActorPoolSubsystem::Release(AActor* Actor)
{
	if (!IsPoolAuthority())
	{
		return false;
	}

	// PendingKill / 无效对象与未纳入管理的 Actor 全部 fail closed；
	// 重复 Release 因首次归还后已移出 ManagedActors，在这里被拒绝且不崩溃。
	if (!IsValid(Actor) || !ManagedActors.Contains(Actor))
	{
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("ActorPool Release rejected: actor %s is not managed by this pool"),
			*GetNameSafe(Actor));
		return false;
	}

	// 错误 World：Actor 已不属于本子系统所在的 World。
	if (Actor->GetWorld() != GetWorld())
	{
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("ActorPool Release rejected: actor %s belongs to another world"),
			*GetNameSafe(Actor));
		return false;
	}

	ManagedActors.Remove(Actor);

	FShooterActorPool& Pool = FindOrAddPool(Actor->GetClass());
	if (Pool.PooledActors.Num() >= Pool.Capacity)
	{
		// 容量已满：不入池，直接销毁，保持池占用有界。
		Actor->Destroy();
		UE_LOG(
			LogShootGame,
			Verbose,
			TEXT("ActorPool Release destroyed overflow: Class=%s Actor=%s"),
			*GetNameSafe(Actor->GetClass()),
			*GetNameSafe(Actor));
		return true;
	}

	ApplyReleasedState(Actor);
	Pool.PooledActors.Add(Actor);
	UE_LOG(
		LogShootGame,
		Verbose,
		TEXT("ActorPool Release pooled: Class=%s Actor=%s Pooled=%d/%d"),
		*GetNameSafe(Actor->GetClass()),
		*GetNameSafe(Actor),
		Pool.PooledActors.Num(),
		Pool.Capacity);
	return true;
}

int32 UShooterActorPoolSubsystem::Prewarm(UClass* ActorClass, int32 Count)
{
	if (!IsPoolAuthority() || !IsPoolableClass(ActorClass) || Count <= 0)
	{
		return 0;
	}

	FShooterActorPool& Pool = FindOrAddPool(ActorClass);
	int32 Warmed = 0;
	for (int32 Index = 0; Index < Count && Pool.PooledActors.Num() < Pool.Capacity; ++Index)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Spawned = GetWorld()->SpawnActor<AActor>(ActorClass, FTransform::Identity, SpawnParams);
		if (!Spawned)
		{
			break;
		}

		ApplyReleasedState(Spawned);
		Pool.PooledActors.Add(Spawned);
		++Warmed;
	}

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("ActorPool Prewarm: Class=%s Warmed=%d Pooled=%d"),
		*GetNameSafe(ActorClass),
		Warmed,
		Pool.PooledActors.Num());
	return Warmed;
}

void UShooterActorPoolSubsystem::SetClassCapacity(UClass* ActorClass, int32 Capacity)
{
	if (!ActorClass)
	{
		return;
	}

	FShooterActorPool& Pool = FindOrAddPool(ActorClass);
	Pool.Capacity = Capacity > 0 ? Capacity : DefaultClassCapacity;
}

int32 UShooterActorPoolSubsystem::GetClassCapacity(UClass* ActorClass) const
{
	const FShooterActorPool* Pool = Pools.Find(ActorClass);
	return Pool ? Pool->Capacity : DefaultClassCapacity;
}

bool UShooterActorPoolSubsystem::RegisterExisting(AActor* Actor)
{
	if (!IsPoolAuthority() || !IsValid(Actor))
	{
		return false;
	}

	// 重复注册 fail closed。
	if (ManagedActors.Contains(Actor))
	{
		return false;
	}

	for (const TPair<TObjectPtr<UClass>, FShooterActorPool>& Pair : Pools)
	{
		if (Pair.Value.PooledActors.Contains(Actor))
		{
			return false;
		}
	}

	ManagedActors.Add(Actor);
	return true;
}

int32 UShooterActorPoolSubsystem::GetPooledCount(UClass* ActorClass) const
{
	const FShooterActorPool* Pool = Pools.Find(ActorClass);
	return Pool ? Pool->PooledActors.Num() : 0;
}

bool UShooterActorPoolSubsystem::IsManaged(AActor* Actor) const
{
	return IsValid(Actor) && ManagedActors.Contains(Actor);
}

UShooterActorPoolSubsystem::FShooterActorPool& UShooterActorPoolSubsystem::FindOrAddPool(UClass* ActorClass)
{
	return Pools.FindOrAdd(ActorClass);
}

void UShooterActorPoolSubsystem::ApplyAcquiredState(
	AActor* Actor,
	const FTransform& SpawnTransform,
	const FActorSpawnParameters& SpawnParameters)
{
	Actor->SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::ResetPhysics);
	Actor->SetOwner(SpawnParameters.Owner);
	Actor->SetInstigator(SpawnParameters.Instigator);
	Actor->SetActorHiddenInGame(false);
	Actor->SetActorEnableCollision(true);
	Actor->SetActorTickEnabled(true);

	if (IShooterPoolableActor* Poolable = Cast<IShooterPoolableActor>(Actor))
	{
		Poolable->OnAcquiredFromPool();
	}
}

void UShooterActorPoolSubsystem::ApplyReleasedState(AActor* Actor)
{
	// 回调先于通用隐藏执行，让实现者在仍可见时完成领域清理。
	if (IShooterPoolableActor* Poolable = Cast<IShooterPoolableActor>(Actor))
	{
		Poolable->OnReleasedToPool();
	}

	Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Actor->SetActorHiddenInGame(true);
	Actor->SetActorEnableCollision(false);
	Actor->SetActorTickEnabled(false);
	Actor->SetOwner(nullptr);
	Actor->SetInstigator(nullptr);
}
