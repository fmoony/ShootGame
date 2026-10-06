// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterBulletCounterUI.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Characters/ShooterCharacter.h"
#include "Weapons/ShooterWeapon.h"
#include "ShootGame.h"
#endif

void UShooterBulletCounterUI::UpdateBulletCounter(int32 MagazineSize, int32 BulletCount, int32 ReserveAmmo)
{
	BP_UpdateBulletCounter(MagazineSize, BulletCount, ReserveAmmo);
#if WITH_DEV_AUTOMATION_TESTS
	DisplayedMagazineForTest = BulletCount;
	DisplayedReserveForTest = ReserveAmmo;
	const AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwningPlayerPawn());
	const AShooterWeapon* Weapon = Character ? Character->GetCurrentWeaponActor() : nullptr;
	const bool bPredicted = Weapon && Weapon->GetPendingPredictedShots() > 0 && BulletCount < Weapon->GetBulletCount();
	if (bPredicted)
	{
		++PredictedUpdateCountForTest;
	}
	UE_LOG(LogShootGame, Display, TEXT("HUD_AMMO_UPDATE Mag=%d Reserve=%d Predicted=%d Weapon=%s"),
		BulletCount, ReserveAmmo, bPredicted ? 1 : 0, *GetNameSafe(Weapon));
#endif
}
