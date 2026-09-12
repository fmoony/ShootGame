// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Weapons/ShooterWeaponConfigRow.h"
#include "ShooterWeaponRuntimeSubsystem.generated.h"

class AShooterWeapon;
class APawn;
class UDataTable;

/**
 * 单个 WeaponId 的运行时配置快照与实体池。
 *
 * AvailableActors 中的 Actor 处于归还复位状态（隐藏、无 Owner、InPool）；
 * LeasedActors 中的 Actor 正被某个持有者租用。
 */
USTRUCT()
struct FShooterWeaponRuntimeBucket
{
	GENERATED_BODY()

	/** 武器种类身份；与 DT_WeaponData 行名一致，启动导入时确定。 */
	FName WeaponId;

	/** 启动时冻结的该 WeaponId 完整配置快照；运行时不再访问 DataTable。 */
	UPROPERTY()
	FShooterWeaponConfigRow RuntimeConfig;

	/** 可租用：已完成归还复位、等待下一次租用。 */
	UPROPERTY()
	TArray<TObjectPtr<AShooterWeapon>> AvailableActors;

	/** 已租出：正被某个持有者使用。 */
	UPROPERTY()
	TArray<TObjectPtr<AShooterWeapon>> LeasedActors;
};

/**
 * World 级武器运行时子系统：启动配置快照 + WeaponId 预热池（重构方案 4.1）。
 *
 * 职责边界：
 * - OnWorldBeginPlay 一次性读取 DT_WeaponData、校验并按行值复制形成不可变 RuntimeConfig；
 * - 服务器按 InitialPoolSize 预创建复制型 WeaponActor，写入永久 WeaponId 并应用静态配置；
 * - 可用池耗尽时只使用冻结快照弹性 Spawn，不重新查询 DataTable，不设最大容量；
 * - AcquireWeapon 成功直接返回 AShooterWeapon*，不产生池句柄或中间身份对象；
 * - 客户端不自主创建权威 WeaponActor，只建立快照供复制到达的 WeaponId 恢复静态表现；
 * - 不做 Dormancy、跨 World 池或运行时重读表。
 */
UCLASS()
class SHOOTGAME_API UShooterWeaponRuntimeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/**
	 * 启动导入：加载武器表、校验全部行并复制为 RuntimeConfig 快照；
	 * 权威端随后按 InitialPoolSize 预创建 Actor。
	 * 表缺失或结构错误时返回 false（RuntimeSubsystem 初始化失败）；已初始化时幂等返回。
	 */
	bool InitializeWeaponRuntime();

	/**
	 * 测试注入瞬态武器表；必须在 World BeginPlay 之前调用才能影响启动导入。
	 * 传 nullptr 恢复使用固定 DT_WeaponData。
	 */
	void SetWeaponTableOverride(UDataTable* InWeaponTable);

	/** 返回测试注入的武器表；未注入时返回 nullptr。 */
	UDataTable* GetWeaponTableOverride() const { return WeaponTableOverride; }

	/**
	 * 测试专用：保留全部租出中的 Actor，销毁未租出实体并按当前注入表重建快照与预热。
	 * 生产路径运行时禁止重读表；本入口只服务自动化测试在追加行后重建快照。
	 */
	void InitializeWeaponRuntimeForTest();

	/**
	 * 从指定 WeaponId 池取出一个已配置好的 WeaponActor 并绑定租用归属。
	 * 池命中复用既有 Actor；耗尽时按冻结快照弹性 Spawn。
	 * 失败（非权威端 / 未知 WeaponId / 弹性 Spawn 失败）返回 nullptr，调用方不得提交任何数据。
	 */
	AShooterWeapon* AcquireWeapon(FName WeaponId, AActor* Owner, APawn* Instigator);

	/**
	 * 归还租用中的 WeaponActor 到其 WeaponId 池。
	 * 归还执行租用复位（停 Timer / 解委托 / 回 InPool / 隐藏 / 清 Owner），静态配置不重复应用。
	 * 非权威端、未知 WeaponId、未租用或无效 Actor 全部 fail closed 返回 false。
	 */
	bool ReleaseWeapon(AShooterWeapon* Weapon);

	/** 查询启动快照中的 WeaponId 配置；未初始化或未知 WeaponId 返回 nullptr。 */
	const FShooterWeaponConfigRow* FindRuntimeConfig(FName WeaponId) const;

	/** 该 WeaponId 是否具备有效启动配置。 */
	bool HasWeaponId(FName WeaponId) const;

	/** 观测：指定 WeaponId 当前可租用数量（测试与诊断）。 */
	int32 GetAvailableCount(FName WeaponId) const;

	/** 观测：指定 WeaponId 当前租出数量（测试与诊断）。 */
	int32 GetLeasedCount(FName WeaponId) const;

	/** 观测：指定 Actor 是否正被本池租出（测试与诊断）。 */
	bool IsLeased(const AShooterWeapon* Weapon) const;

	/** 观测：指定 Actor 是否停留在本池某个 Bucket 内（已归还、待复用；测试与诊断）。 */
	bool IsPooled(const AShooterWeapon* Weapon) const;

	/** 观测：启动导入是否已成功执行。 */
	bool IsRuntimeInitialized() const { return bRuntimeInitialized; }

private:
	FShooterWeaponRuntimeBucket* FindBucket(FName WeaponId);
	const FShooterWeaponRuntimeBucket* FindBucket(FName WeaponId) const;

	/** 预创建与弹性 Spawn 共用的 Actor 生成：写入 WeaponId、应用静态配置并完成归还复位。 */
	AShooterWeapon* SpawnPoolWeaponActor(FShooterWeaponRuntimeBucket& Bucket);

	/** 归还复位：领域回调 + 通用隐藏 / 关碰撞 / 关 Tick / Detach / 清 Owner。 */
	void ApplyReleasedActorState(AShooterWeapon* Weapon);

	/** 当前 World 是否允许驱动池（权威端）。 */
	bool IsRuntimeAuthority() const;

	/** 启动行校验：正式授予约束 + InitialPoolSize > 0；不满足则该行不建 Bucket。 */
	bool IsRuntimeRowValid(const FShooterWeaponConfigRow* Row) const;

	/** 启动快照与全部池实体（权威端）；键为 WeaponId。 */
	UPROPERTY()
	TMap<FName, FShooterWeaponRuntimeBucket> Buckets;

	/** 测试注入的瞬态武器表；保持强引用防止 GC 回收。 */
	UPROPERTY(Transient)
	TObjectPtr<UDataTable> WeaponTableOverride;

	/** 启动导入是否已成功执行。 */
	bool bRuntimeInitialized = false;
};
