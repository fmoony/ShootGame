// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/ShooterProjectile.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

namespace ShooterWeaponAutomationTests
{
	bool TestWeaponConfiguration(
		FAutomationTestBase& Test,
		const TCHAR* WeaponName,
		const TCHAR* WeaponClassPath)
	{
		UClass* WeaponClass = LoadClass<AShooterWeapon>(nullptr, WeaponClassPath);
		if (!Test.TestNotNull(FString::Printf(TEXT("%s class can be loaded"), WeaponName), WeaponClass))
		{
			return false;
		}

		const AShooterWeapon* WeaponDefaults = WeaponClass->GetDefaultObject<AShooterWeapon>();
		if (!Test.TestNotNull(FString::Printf(TEXT("%s has defaults"), WeaponName), WeaponDefaults))
		{
			return false;
		}

		Test.TestTrue(
			FString::Printf(TEXT("%s replicates"), WeaponName),
			WeaponDefaults->GetIsReplicated());
		Test.TestTrue(
			FString::Printf(TEXT("%s magazine size is positive"), WeaponName),
			WeaponDefaults->GetMagazineSize() > 0);

		const FProperty* CurrentBulletsProperty =
			FindFProperty<FProperty>(WeaponClass, TEXT("CurrentBullets"));
		if (!Test.TestNotNull(
			FString::Printf(TEXT("%s exposes CurrentBullets"), WeaponName),
			CurrentBulletsProperty))
		{
			return false;
		}
		Test.TestTrue(
			FString::Printf(TEXT("%s CurrentBullets is replicated"), WeaponName),
			CurrentBulletsProperty->HasAnyPropertyFlags(CPF_Net));
		Test.TestEqual(
			FString::Printf(TEXT("%s CurrentBullets uses OnRep_CurrentBullets"), WeaponName),
			CurrentBulletsProperty->RepNotifyFunc,
			FName(TEXT("OnRep_CurrentBullets")));

		const FFloatProperty* RefireRateProperty = FindFProperty<FFloatProperty>(WeaponClass, TEXT("RefireRate"));
		if (!Test.TestNotNull(FString::Printf(TEXT("%s exposes RefireRate"), WeaponName), RefireRateProperty))
		{
			return false;
		}

		const float RefireRate = RefireRateProperty->GetPropertyValue_InContainer(WeaponDefaults);
		Test.TestTrue(
			FString::Printf(TEXT("%s refire rate is positive"), WeaponName),
			RefireRate > 0.0f);

		const FObjectPropertyBase* MuzzleFlashProperty =
			FindFProperty<FObjectPropertyBase>(WeaponClass, TEXT("MuzzleFlash"));
		Test.TestNotNull(
			FString::Printf(TEXT("%s has muzzle flash configured"), WeaponName),
			MuzzleFlashProperty
				? MuzzleFlashProperty->GetObjectPropertyValue_InContainer(WeaponDefaults)
				: nullptr);

		const FObjectPropertyBase* FireSoundProperty =
			FindFProperty<FObjectPropertyBase>(WeaponClass, TEXT("FireSound"));
		Test.TestNotNull(
			FString::Printf(TEXT("%s has fire sound configured"), WeaponName),
			FireSoundProperty
				? FireSoundProperty->GetObjectPropertyValue_InContainer(WeaponDefaults)
				: nullptr);

		const FClassProperty* ProjectileClassProperty =
			FindFProperty<FClassProperty>(WeaponClass, TEXT("ProjectileClass"));
		const UClass* ProjectileClass = ProjectileClassProperty
			? Cast<UClass>(ProjectileClassProperty->GetObjectPropertyValue_InContainer(WeaponDefaults))
			: nullptr;
		if (!Test.TestNotNull(
			FString::Printf(TEXT("%s has projectile class configured"), WeaponName),
			ProjectileClass))
		{
			return false;
		}

		const AShooterProjectile* ProjectileDefaults =
			ProjectileClass->GetDefaultObject<AShooterProjectile>();
		if (!Test.TestNotNull(
			FString::Printf(TEXT("%s projectile has defaults"), WeaponName),
			ProjectileDefaults))
		{
			return false;
		}

		Test.TestTrue(
			FString::Printf(TEXT("%s projectile replicates"), WeaponName),
			ProjectileDefaults->GetIsReplicated());
		Test.TestTrue(
			FString::Printf(TEXT("%s projectile replicates movement"), WeaponName),
			ProjectileDefaults->IsReplicatingMovement());

		return true;
	}

	bool TestCharacterReplication(FAutomationTestBase& Test)
	{
		const FProperty* CurrentHPProperty =
			FindFProperty<FProperty>(AShooterCharacter::StaticClass(), TEXT("CurrentHP"));
		if (!Test.TestNotNull(TEXT("Character exposes CurrentHP"), CurrentHPProperty))
		{
			return false;
		}

		Test.TestTrue(
			TEXT("CurrentHP is replicated"),
			CurrentHPProperty->HasAnyPropertyFlags(CPF_Net));
		Test.TestEqual(
			TEXT("CurrentHP uses OnRep_CurrentHP"),
			CurrentHPProperty->RepNotifyFunc,
			FName(TEXT("OnRep_CurrentHP")));

		const FProperty* IsDeadProperty =
			FindFProperty<FProperty>(AShooterCharacter::StaticClass(), TEXT("bIsDead"));
		if (!Test.TestNotNull(TEXT("Character exposes bIsDead"), IsDeadProperty))
		{
			return false;
		}

		Test.TestTrue(
			TEXT("bIsDead is replicated"),
			IsDeadProperty->HasAnyPropertyFlags(CPF_Net));
		Test.TestEqual(
			TEXT("bIsDead uses OnRep_IsDead"),
			IsDeadProperty->RepNotifyFunc,
			FName(TEXT("OnRep_IsDead")));

		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponConfigurationTest,
	"ShootGame.Weapon.Configuration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponConfigurationTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponAutomationTests;

	bool bSucceeded = true;
	bSucceeded &= TestWeaponConfiguration(
		*this,
		TEXT("Rifle"),
		TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C"));
	bSucceeded &= TestWeaponConfiguration(
		*this,
		TEXT("Pistol"),
		TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Pistol.BP_ShooterWeapon_Pistol_C"));
	bSucceeded &= TestCharacterReplication(*this);

	return bSucceeded;
}

#endif
