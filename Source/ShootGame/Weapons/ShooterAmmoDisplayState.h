// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterAmmoDisplayState.generated.h"

/** 拥有端显示基线；与弹药事务一致，整个结构作为一个网络值传递。 */
USTRUCT()
struct FShooterAmmoDisplaySnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	uint64 Revision = 0;

	UPROPERTY()
	int32 Magazine = 0;

	UPROPERTY()
	int32 Reserve = 0;

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FShooterAmmoDisplaySnapshot> : public TStructOpsTypeTraitsBase2<FShooterAmmoDisplaySnapshot>
{
	enum { WithNetSerializer = true };
};

/** 纯本地 HUD 叠加层，不修改真实 Ammo 或射击预算，不触发 Gameplay。 */
struct SHOOTGAME_API FShooterAmmoDisplayState
{
	void ReceiveSnapshot(const FShooterAmmoDisplaySnapshot& Snapshot);
	void BeginActivation(int32 Key, const FShooterAmmoDisplaySnapshot& InitialSnapshot);
	void PredictShot(int32 Key);
	/**
	 * 撤销该 Key 的本地显示预测。
	 *
	 * 中性语义：服务器拒绝与本地生命周期退休在显示层的动作完全相同，
	 * 因此这里不区分二者，也不代表"服务器拒绝"。
	 */
	void RetireActivation(int32 Key);
	bool SettleActivation(int32 Key, const FShooterAmmoDisplaySnapshot& Snapshot);
	void Reset();

	int32 GetMagazine(int32 FallbackMagazine) const;
	int32 GetReserve(int32 FallbackReserve) const;
	int32 GetUnsettledCount() const { return PredictedByActivation.Num(); }

private:
	FShooterAmmoDisplaySnapshot AppliedSnapshot;
	FShooterAmmoDisplaySnapshot DeferredSnapshot;
	TMap<int32, int32> PredictedByActivation;
	bool bHasBaseline = false;
};
