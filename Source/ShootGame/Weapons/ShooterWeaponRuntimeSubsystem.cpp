// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterWeaponRuntimeSubsystem.h"

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "ShootGame.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/ShooterWeaponTable.h"

bool UShooterWeaponRuntimeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// 只在真实游戏 World 存在；编辑器预览 World 不创建。
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UShooterWeaponRuntimeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// World 准备开始玩法、Actor BeginPlay 前：冻结配置快照并完成服务器预热。
	InitializeWeaponRuntime();
}

void UShooterWeaponRuntimeSubsystem::Deinitialize()
{
	// World teardown 的幂等清理边界：显式销毁全部池内与租出 Actor，不留悬空对象。
	for (TPair<FName, FShooterWeaponRuntimeBucket>& Pair : Buckets)
	{
		for (AShooterWeapon* Pooled : Pair.Value.AvailableActors)
		{
			if (IsValid(Pooled))
			{
				Pooled->Destroy();
			}
		}
		Pair.Value.AvailableActors.Empty();

		for (AShooterWeapon* Leased : Pair.Value.LeasedActors)
		{
			if (IsValid(Leased))
			{
				Leased->Destroy();
			}
		}
		Pair.Value.LeasedActors.Empty();
	}
	Buckets.Empty();
	bRuntimeInitialized = false;

	Super::Deinitialize();
}

bool UShooterWeaponRuntimeSubsystem::IsRuntimeAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

bool UShooterWeaponRuntimeSubsystem::IsRuntimeRowValid(const FShooterWeaponConfigRow* Row) const
{
	// 复用正式授予约束（ActorClass 是 AShooterWeapon 子类 + 弹匣/备弹合法），
	// 再叠加可生成与池预热数量约束：abstract 类与 InitialPoolSize<=0 的行不建 Bucket。
	return ShooterWeaponTable::IsRowValidForGrant(Row) &&
		!Row->WeaponActorClass->HasAnyClassFlags(CLASS_Abstract) &&
		Row->InitialPoolSize > 0;
}

bool UShooterWeaponRuntimeSubsystem::InitializeWeaponRuntime()
{
	// 启动导入只执行一次：运行时禁止重新读取 DataTable（重构方案 11 非目标）。
	if (bRuntimeInitialized)
	{
		return true;
	}

	UDataTable* Table = WeaponTableOverride ? WeaponTableOverride.Get()
		: ShooterWeaponTable::ResolveWeaponTable();

	// 表缺失：RuntimeSubsystem 初始化失败，正式测试将直接失败。
	if (!Table)
	{
		UE_LOG(
			LogShootGame,
			Error,
			TEXT("WeaponRuntime initialization failed: weapon table missing"));
		return false;
	}

	// 结构错误：同表缺失处理，不允许用错误行结构解释武器配置。
	if (Table->GetRowStruct() != FShooterWeaponConfigRow::StaticStruct())
	{
		UE_LOG(
			LogShootGame,
			Error,
			TEXT("WeaponRuntime initialization failed: table %s does not use FShooterWeaponConfigRow"),
			*GetNameSafe(Table));
		return false;
	}

	const bool bAuthority = IsRuntimeAuthority();
	int32 BucketCount = 0;

	for (const FName& RowName : Table->GetRowNames())
	{
		const FShooterWeaponConfigRow* Row = ShooterWeaponTable::FindWeaponRow(Table, RowName);
		if (!IsRuntimeRowValid(Row))
		{
			// 单行非法：该 WeaponId 不建立 Bucket，输出一次 Error。
			UE_LOG(
				LogShootGame,
				Error,
				TEXT("WeaponRuntime skipped invalid row: WeaponId=%s Table=%s"),
				*RowName.ToString(),
				*GetNameSafe(Table));
			continue;
		}

		FShooterWeaponRuntimeBucket& Bucket = Buckets.Add(RowName);
		Bucket.WeaponId = RowName;
		// 行值复制形成不可变快照：此后运行时不再访问 DataTable 或缓存 Row 指针。
		Bucket.RuntimeConfig = *Row;
		++BucketCount;

		if (bAuthority)
		{
			for (int32 Index = 0; Index < Row->InitialPoolSize; ++Index)
			{
				AShooterWeapon* Weapon = SpawnPoolWeaponActor(Bucket);
				if (!Weapon)
				{
					// 预创建数量不足：该 WeaponId 不允许半可用池，销毁已创建实体并撤销 Bucket。
					for (AShooterWeapon* Created : Bucket.AvailableActors)
					{
						if (IsValid(Created))
						{
							Created->Destroy();
						}
					}
					Bucket.AvailableActors.Empty();
					Buckets.Remove(RowName);
					--BucketCount;
					UE_LOG(
						LogShootGame,
						Error,
						TEXT("WeaponRuntime prewarm failed: WeaponId=%s Expected=%d Created=%d"),
						*RowName.ToString(),
						Row->InitialPoolSize,
						Index);
					break;
				}

				Bucket.AvailableActors.Add(Weapon);
			}
		}
	}

	bRuntimeInitialized = BucketCount > 0;
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("WeaponRuntime initialized: Table=%s Buckets=%d Authority=%s"),
		*GetNameSafe(Table),
		BucketCount,
		bAuthority ? TEXT("true") : TEXT("false"));
	return bRuntimeInitialized;
}

void UShooterWeaponRuntimeSubsystem::SetWeaponTableOverride(UDataTable* InWeaponTable)
{
	WeaponTableOverride = InWeaponTable;
}

void UShooterWeaponRuntimeSubsystem::InitializeWeaponRuntimeForTest()
{
	// 冻结租出实体：测试已授予的武器跨重导入存活，重建后放回对应 Bucket。
	TArray<TObjectPtr<AShooterWeapon>> LeasedToKeep;
	for (TPair<FName, FShooterWeaponRuntimeBucket>& Pair : Buckets)
	{
		for (AShooterWeapon* Pooled : Pair.Value.AvailableActors)
		{
			if (IsValid(Pooled))
			{
				Pooled->Destroy();
			}
		}

		for (AShooterWeapon* Leased : Pair.Value.LeasedActors)
		{
			if (IsValid(Leased))
			{
				LeasedToKeep.Add(Leased);
			}
		}
	}
	Buckets.Empty();
	bRuntimeInitialized = false;

	InitializeWeaponRuntime();

	for (TObjectPtr<AShooterWeapon>& Leased : LeasedToKeep)
	{
		if (FShooterWeaponRuntimeBucket* Bucket = FindBucket(Leased->GetWeaponId()))
		{
			Bucket->LeasedActors.Add(Leased);
		}
	}
}

AShooterWeapon* UShooterWeaponRuntimeSubsystem::SpawnPoolWeaponActor(FShooterWeaponRuntimeBucket& Bucket)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot;

	AShooterWeapon* Weapon = World->SpawnActor<AShooterWeapon>(
		Bucket.RuntimeConfig.WeaponActorClass,
		FTransform::Identity,
		SpawnParams);
	if (!Weapon)
	{
		return nullptr;
	}

	// 创建即冻结身份与静态配置：此后该 Actor 在本 World 生命周期内永远属于这一 WeaponId。
	Weapon->InitializeWeaponIdentity(Bucket.WeaponId);

	// 预创建与弹性 Spawn 统一从归还复位状态进入池。
	ApplyReleasedActorState(Weapon);
	return Weapon;
}

void UShooterWeaponRuntimeSubsystem::ApplyReleasedActorState(AShooterWeapon* Weapon)
{
	// 领域清理先于通用隐藏执行，让实现者在仍可见时完成清理。
	Weapon->OnReleasedToWeaponPool();

	Weapon->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Weapon->SetActorHiddenInGame(true);
	Weapon->SetActorEnableCollision(false);
	Weapon->SetActorTickEnabled(false);
	Weapon->SetOwner(nullptr);
	Weapon->SetInstigator(nullptr);
}

AShooterWeapon* UShooterWeaponRuntimeSubsystem::AcquireWeapon(FName WeaponId, AActor* Owner, APawn* Instigator)
{
	if (!IsRuntimeAuthority())
	{
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("WeaponRuntime Acquire rejected: non-authority world"));
		return nullptr;
	}

	FShooterWeaponRuntimeBucket* Bucket = FindBucket(WeaponId);
	if (!Bucket)
	{
		// 未知 WeaponId：调用方（Pickup / NPC）不得消费，返回空让上层按 InvalidWeaponId 拒绝。
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("WeaponRuntime Acquire rejected: unknown WeaponId=%s"),
			*WeaponId.ToString());
		return nullptr;
	}

	// 池命中：从可用端弹出并跳过无效残留。
	AShooterWeapon* Weapon = nullptr;
	while (Bucket->AvailableActors.Num() > 0)
	{
		AShooterWeapon* Candidate = Bucket->AvailableActors.Pop();
		if (IsValid(Candidate))
		{
			Weapon = Candidate;
			break;
		}
	}

	if (!Weapon)
	{
		// 可用池耗尽：只使用启动冻结的 RuntimeConfig 弹性 Spawn，禁止重新查询 DataTable。
		Weapon = SpawnPoolWeaponActor(*Bucket);
		if (!Weapon)
		{
			UE_LOG(
				LogShootGame,
				Warning,
				TEXT("WeaponRuntime Acquire failed: elastic spawn failed WeaponId=%s Class=%s"),
				*WeaponId.ToString(),
				*GetNameSafe(Bucket->RuntimeConfig.WeaponActorClass));
			return nullptr;
		}

		UE_LOG(
			LogShootGame,
			Display,
			TEXT("WeaponRuntime elastic spawn: WeaponId=%s Weapon=%s"),
			*WeaponId.ToString(),
			*GetNameSafe(Weapon));
	}

	// 租用归属：Owner / Instigator 先写入，Actor 回调内重新绑定缓存与销毁委托。
	// 隐藏保持到装备表现解除（Holstered 武器不可见）；碰撞与 Tick 恢复通用可用状态。
	Weapon->SetOwner(Owner);
	Weapon->SetInstigator(Instigator);
	Weapon->SetActorEnableCollision(true);
	Weapon->SetActorTickEnabled(true);
	Weapon->OnAcquiredFromWeaponPool();

	Bucket->LeasedActors.Add(Weapon);

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("WeaponRuntime Acquire: WeaponId=%s Weapon=%s Owner=%s Available=%d Leased=%d"),
		*WeaponId.ToString(),
		*GetNameSafe(Weapon),
		*GetNameSafe(Owner),
		Bucket->AvailableActors.Num(),
		Bucket->LeasedActors.Num());
	return Weapon;
}

bool UShooterWeaponRuntimeSubsystem::ReleaseWeapon(AShooterWeapon* Weapon)
{
	if (!IsRuntimeAuthority())
	{
		return false;
	}

	if (!IsValid(Weapon))
	{
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("WeaponRuntime Release rejected: invalid weapon"));
		return false;
	}

	FShooterWeaponRuntimeBucket* Bucket = FindBucket(Weapon->GetWeaponId());
	if (!Bucket)
	{
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("WeaponRuntime Release rejected: no bucket for WeaponId=%s Weapon=%s"),
			*Weapon->GetWeaponId().ToString(),
			*GetNameSafe(Weapon));
		return false;
	}

	// 重复归还或非本池管理的 Actor 全部 fail closed。
	if (!Bucket->LeasedActors.Contains(Weapon))
	{
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("WeaponRuntime Release rejected: weapon not leased WeaponId=%s Weapon=%s"),
			*Weapon->GetWeaponId().ToString(),
			*GetNameSafe(Weapon));
		return false;
	}

	Bucket->LeasedActors.Remove(Weapon);
	ApplyReleasedActorState(Weapon);
	Bucket->AvailableActors.Add(Weapon);

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("WeaponRuntime Release: WeaponId=%s Weapon=%s Available=%d Leased=%d"),
		*Weapon->GetWeaponId().ToString(),
		*GetNameSafe(Weapon),
		Bucket->AvailableActors.Num(),
		Bucket->LeasedActors.Num());
	return true;
}

const FShooterWeaponConfigRow* UShooterWeaponRuntimeSubsystem::FindRuntimeConfig(FName WeaponId) const
{
	const FShooterWeaponRuntimeBucket* Bucket = FindBucket(WeaponId);
	return Bucket ? &Bucket->RuntimeConfig : nullptr;
}

bool UShooterWeaponRuntimeSubsystem::HasWeaponId(FName WeaponId) const
{
	return FindBucket(WeaponId) != nullptr;
}

int32 UShooterWeaponRuntimeSubsystem::GetAvailableCount(FName WeaponId) const
{
	const FShooterWeaponRuntimeBucket* Bucket = FindBucket(WeaponId);
	return Bucket ? Bucket->AvailableActors.Num() : 0;
}

int32 UShooterWeaponRuntimeSubsystem::GetLeasedCount(FName WeaponId) const
{
	const FShooterWeaponRuntimeBucket* Bucket = FindBucket(WeaponId);
	return Bucket ? Bucket->LeasedActors.Num() : 0;
}

bool UShooterWeaponRuntimeSubsystem::IsLeased(const AShooterWeapon* Weapon) const
{
	if (!IsValid(Weapon))
	{
		return false;
	}

	const FShooterWeaponRuntimeBucket* Bucket = FindBucket(Weapon->GetWeaponId());
	return Bucket && Bucket->LeasedActors.Contains(Weapon);
}

bool UShooterWeaponRuntimeSubsystem::IsPooled(const AShooterWeapon* Weapon) const
{
	if (!IsValid(Weapon))
	{
		return false;
	}

	const FShooterWeaponRuntimeBucket* Bucket = FindBucket(Weapon->GetWeaponId());
	return Bucket && Bucket->AvailableActors.Contains(Weapon);
}

FShooterWeaponRuntimeBucket* UShooterWeaponRuntimeSubsystem::FindBucket(FName WeaponId)
{
	return WeaponId.IsNone() ? nullptr : Buckets.Find(WeaponId);
}

const FShooterWeaponRuntimeBucket* UShooterWeaponRuntimeSubsystem::FindBucket(FName WeaponId) const
{
	return WeaponId.IsNone() ? nullptr : Buckets.Find(WeaponId);
}
