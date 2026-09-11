// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ShooterActorPoolSubsystem.generated.h"

class AActor;

/**
 * World 级通用 Actor 池（实施计划 4.4 / 7.B1）。
 *
 * 职责边界：
 * - 按 ActorClass 分池，只提供 Acquire / Release / Prewarm / Capacity / RegisterExisting；
 * - 池负责通用复位：变换、Owner、Instigator、可见性、碰撞、Tick、Detach；
 * - 领域内绑定与清理由 IShooterPoolableActor 两个回调承担；
 * - 第一位正式消费者是 WeaponActor（B2）；Projectile 池化延后；
 * - 只在服务器/权威端驱动；客户端自主 Acquire 第一版不支持；
 * - 不做 Dormancy、跨 World 全局池或动态容量回收算法。
 */
UCLASS()
class SHOOTGAME_API UShooterActorPoolSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

	/**
	 * 取出或生成一个指定类的池化 Actor。
	 * 池命中时复位通用状态（变换 / Owner / Instigator / 可见性 / 碰撞 / Tick）并回调 OnAcquiredFromPool；
	 * 池未命中时按参数 SpawnActor。失败（非法类 / 非权威端 / 生成失败）返回 nullptr。
	 */
	AActor* Acquire(
		UClass* ActorClass,
		const FTransform& SpawnTransform,
		const FActorSpawnParameters& SpawnParameters);

	/**
	 * 归还 Actor 到其类池。
	 * 回调 OnReleasedToPool 后执行通用隐藏 / 碰撞关闭 / Tick 关闭 / Detach / Owner 清空。
	 * 容量已满时直接销毁。重复归还、非本池管理、错误 World、PendingKill、非权威端全部 fail closed 返回 false。
	 */
	bool Release(AActor* Actor);

	/** 预热：生成 Count 个 Actor 直接入池；返回实际入池数量。 */
	int32 Prewarm(UClass* ActorClass, int32 Count);

	/** 设置指定类的容量上限（<=0 视为默认容量）。 */
	void SetClassCapacity(UClass* ActorClass, int32 Capacity);

	/** 返回指定类的容量上限。 */
	int32 GetClassCapacity(UClass* ActorClass) const;

	/** 把一个已存在、非池出生的 Actor 纳入池管理（迁移期/测试用）；重复注册 fail closed。 */
	bool RegisterExisting(AActor* Actor);

	/** 观测：指定类当前池内数量。 */
	int32 GetPooledCount(UClass* ActorClass) const;

	/** 观测：指定 Actor 是否正被本池管理（在池外使用中）。 */
	bool IsManaged(AActor* Actor) const;

	/**
	 * 观测：指定 Actor 当前是否停留在某个类池内（已归还、未复用）。
	 * 用于区分「池化待复用」与「真正遗留」的世界实体，例如断线清理检查。
	 */
	bool IsPooled(const AActor* Actor) const;

private:
	struct FShooterActorPool
	{
		TArray<TObjectPtr<AActor>> PooledActors;
		int32 Capacity = 32;
	};

	FShooterActorPool& FindOrAddPool(UClass* ActorClass);

	/** 通用获取复位：变换 / Owner / Instigator / 可见性 / 碰撞 / Tick / 回调。 */
	void ApplyAcquiredState(AActor* Actor, const FTransform& SpawnTransform, const FActorSpawnParameters& SpawnParameters);

	/** 通用归还清理：回调 / 隐藏 / 碰撞 / Tick / Detach / Owner 清空。 */
	void ApplyReleasedState(AActor* Actor);

	/** 当前世界是否允许驱动池（权威端）。 */
	bool IsPoolAuthority() const;

	/** 类池容器；键为 ActorClass，跨 Class 不串池。 */
	TMap<TObjectPtr<UClass>, FShooterActorPool> Pools;

	/** 当前被本池管理且在池外使用中的 Actor。 */
	TSet<TObjectPtr<AActor>> ManagedActors;

	/** 默认每类容量上限。 */
	static constexpr int32 DefaultClassCapacity = 32;
};
