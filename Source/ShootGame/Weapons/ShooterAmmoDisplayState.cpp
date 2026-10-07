// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterAmmoDisplayState.h"

bool FShooterAmmoDisplaySnapshot::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << Revision << Magazine << Reserve;
	bOutSuccess = !Ar.IsError();
	return true;
}

void FShooterAmmoDisplayState::ReceiveSnapshot(const FShooterAmmoDisplaySnapshot& Snapshot)
{
	if (Snapshot.Revision == 0 || Snapshot.Revision <= AppliedSnapshot.Revision)
	{
		return;
	}
	if (!PredictedByActivation.IsEmpty())
	{
		// 属性可先于最终裁决到达，尚不能从这个值判断包含了哪轮预测。
		if (Snapshot.Revision > DeferredSnapshot.Revision)
		{
			DeferredSnapshot = Snapshot;
		}
		return;
	}
	AppliedSnapshot = Snapshot;
	bHasBaseline = true;
}

void FShooterAmmoDisplayState::BeginActivation(int32 Key, const FShooterAmmoDisplaySnapshot& InitialSnapshot)
{
	ReceiveSnapshot(InitialSnapshot);
	if (!bHasBaseline)
	{
		// 首次快照尚未就绪时冻结当前镜像，不随之后的独立属性变化重复扣减。
		AppliedSnapshot = InitialSnapshot;
		bHasBaseline = true;
	}
	PredictedByActivation.FindOrAdd(Key);
}

void FShooterAmmoDisplayState::PredictShot(int32 Key)
{
	if (int32* Shots = PredictedByActivation.Find(Key))
	{
		++*Shots;
	}
}

void FShooterAmmoDisplayState::RetireActivation(int32 Key)
{
	PredictedByActivation.Remove(Key);
	if (PredictedByActivation.IsEmpty())
	{
		ReceiveSnapshot(DeferredSnapshot);
	}
}

bool FShooterAmmoDisplayState::SettleActivation(int32 Key, const FShooterAmmoDisplaySnapshot& Snapshot)
{
	// 旧裁决不仅不能覆盖基线，也不能清掉复用 key 后建立的新显示记录。
	if (Snapshot.Revision < AppliedSnapshot.Revision || PredictedByActivation.Remove(Key) == 0)
	{
		return false;
	}
	// 同一可靠武器通道的最终快照按发送顺序到达；先扣除本轮叠加，再采用该时点真值。
	if (Snapshot.Revision > AppliedSnapshot.Revision)
	{
		AppliedSnapshot = Snapshot;
		bHasBaseline = true;
	}
	if (PredictedByActivation.IsEmpty())
	{
		ReceiveSnapshot(DeferredSnapshot);
	}
	return true;
}

void FShooterAmmoDisplayState::Reset()
{
	AppliedSnapshot = FShooterAmmoDisplaySnapshot();
	DeferredSnapshot = FShooterAmmoDisplaySnapshot();
	PredictedByActivation.Reset();
	bHasBaseline = false;
}

int32 FShooterAmmoDisplayState::GetMagazine(int32 FallbackMagazine) const
{
	int32 Deduction = 0;
	for (const TPair<int32, int32>& Pair : PredictedByActivation)
	{
		Deduction += Pair.Value;
	}
	return FMath::Max(0, (bHasBaseline ? AppliedSnapshot.Magazine : FallbackMagazine) - Deduction);
}

int32 FShooterAmmoDisplayState::GetReserve(int32 FallbackReserve) const
{
	return FMath::Max(0, bHasBaseline ? AppliedSnapshot.Reserve : FallbackReserve);
}
