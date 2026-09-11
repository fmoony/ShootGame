// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterBulletCounterUI.h"

void UShooterBulletCounterUI::UpdateBulletCounter(int32 MagazineSize, int32 BulletCount, int32 ReserveAmmo)
{
	BP_UpdateBulletCounter(MagazineSize, BulletCount, ReserveAmmo);
}
