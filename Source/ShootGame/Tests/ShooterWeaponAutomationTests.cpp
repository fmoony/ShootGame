// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Characters/Animation/ShooterFirstPersonAnimInstance.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/UnrealType.h"
#include "ShooterCharacter.h"
#include "ShooterGameMode.h"
#include "ShooterGameState.h"
#include "ShooterPlayerState.h"
#include "ShooterProjectile.h"
#include "ShooterWeapon.h"
#include "Weapons/ShooterAnimNotify_WeaponSound.h"

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
		Test.TestTrue(
			FString::Printf(TEXT("%s first-person mesh has configured muzzle socket"), WeaponName),
			WeaponDefaults->GetFirstPersonMesh() &&
			WeaponDefaults->GetFirstPersonMesh()->DoesSocketExist(WeaponDefaults->GetMuzzleSocketName()));
		Test.TestTrue(
			FString::Printf(TEXT("%s third-person mesh has authoritative muzzle socket"), WeaponName),
			WeaponDefaults->HasThirdPersonMuzzleSocket());

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

		// 换弹音效配置：三个阶段都由武器蓝图声明资源，Notify 只在本端触发播放。
		const TCHAR* ReloadSoundProperties[] = {
			TEXT("ReloadMagazineOutSound"),
			TEXT("ReloadMagazineInSound"),
			TEXT("ReloadCockingSound"),
		};
		for (const TCHAR* PropertyName : ReloadSoundProperties)
		{
			const FObjectPropertyBase* ReloadSoundProperty =
				FindFProperty<FObjectPropertyBase>(WeaponClass, PropertyName);
			Test.TestNotNull(
				FString::Printf(TEXT("%s has %s configured"), WeaponName, PropertyName),
				ReloadSoundProperty
					? ReloadSoundProperty->GetObjectPropertyValue_InContainer(WeaponDefaults)
					: nullptr);
		}

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

	bool TestMatchStateReplication(FAutomationTestBase& Test)
	{
		const AShooterGameMode* GameModeDefaults =
			AShooterGameMode::StaticClass()->GetDefaultObject<AShooterGameMode>();
		if (!Test.TestNotNull(TEXT("Shooter GameMode has defaults"), GameModeDefaults))
		{
			return false;
		}

		Test.TestEqual(
			TEXT("Shooter GameMode uses replicated ShooterGameState"),
			GameModeDefaults->GameStateClass.Get(),
			AShooterGameState::StaticClass());
		Test.TestEqual(
			TEXT("Shooter GameMode uses ShooterPlayerState"),
			GameModeDefaults->PlayerStateClass.Get(),
			AShooterPlayerState::StaticClass());

		const FProperty* TeamScoresProperty =
			FindFProperty<FProperty>(AShooterGameState::StaticClass(), TEXT("TeamScores"));
		if (!Test.TestNotNull(TEXT("ShooterGameState exposes TeamScores"), TeamScoresProperty))
		{
			return false;
		}
		Test.TestTrue(TEXT("TeamScores is replicated"), TeamScoresProperty->HasAnyPropertyFlags(CPF_Net));
		Test.TestEqual(
			TEXT("TeamScores uses OnRep_TeamScores"),
			TeamScoresProperty->RepNotifyFunc,
			FName(TEXT("OnRep_TeamScores")));

		struct FReplicatedPlayerProperty
		{
			const TCHAR* Name;
			const TCHAR* RepNotify;
		};
		const FReplicatedPlayerProperty PlayerProperties[] = {
			{TEXT("TeamId"), TEXT("OnRep_TeamId")},
			{TEXT("Kills"), TEXT("OnRep_CombatStats")},
			{TEXT("Deaths"), TEXT("OnRep_CombatStats")},
		};

		for (const FReplicatedPlayerProperty& Expected : PlayerProperties)
		{
			const FProperty* Property = FindFProperty<FProperty>(
				AShooterPlayerState::StaticClass(),
				Expected.Name);
			if (!Test.TestNotNull(
				FString::Printf(TEXT("ShooterPlayerState exposes %s"), Expected.Name),
				Property))
			{
				return false;
			}
			Test.TestTrue(
				FString::Printf(TEXT("%s is replicated"), Expected.Name),
				Property->HasAnyPropertyFlags(CPF_Net));
			Test.TestEqual(
				FString::Printf(TEXT("%s uses %s"), Expected.Name, Expected.RepNotify),
				Property->RepNotifyFunc,
				FName(Expected.RepNotify));
		}

		return true;
	}

	bool TestAnimationConfiguration(FAutomationTestBase& Test)
	{
		const AShooterCharacter* CharacterDefaults =
			AShooterCharacter::StaticClass()->GetDefaultObject<AShooterCharacter>();
		if (!Test.TestNotNull(TEXT("Shooter character has defaults"), CharacterDefaults))
		{
			return false;
		}

		Test.TestTrue(
			TEXT("First-person mesh is visible only to its owner"),
			CharacterDefaults->GetFirstPersonMesh()->bOnlyOwnerSee);
		Test.TestTrue(
			TEXT("Third-person mesh is hidden from its owner"),
			CharacterDefaults->GetMesh()->bOwnerNoSee);

		const TCHAR* FirstPersonAnimClassPaths[] = {
			TEXT("/Game/Shooter/Animation/FirstPerson/ABP_FP_Weapon.ABP_FP_Weapon_C"),
			TEXT("/Game/Shooter/Animation/FirstPerson/ABP_FP_Pistol.ABP_FP_Pistol_C"),
		};
		for (const TCHAR* AnimClassPath : FirstPersonAnimClassPaths)
		{
			const UClass* AnimClass = LoadClass<UAnimInstance>(nullptr, AnimClassPath);
			if (!Test.TestNotNull(
				FString::Printf(TEXT("First-person AnimBP can be loaded: %s"), AnimClassPath),
				AnimClass))
			{
				return false;
			}
			Test.TestTrue(
				FString::Printf(TEXT("First-person AnimBP derives from UShooterFirstPersonAnimInstance: %s"), AnimClassPath),
				AnimClass->IsChildOf(UShooterFirstPersonAnimInstance::StaticClass()));
		}

		const TCHAR* ThirdPersonAnimClassPaths[] = {
			TEXT("/Game/Shooter/Animation/ThirdPerson/ABP_TP_Rifle.ABP_TP_Rifle_C"),
			TEXT("/Game/Shooter/Animation/ThirdPerson/ABP_TP_Pistol.ABP_TP_Pistol_C"),
		};
		for (const TCHAR* AnimClassPath : ThirdPersonAnimClassPaths)
		{
			const UClass* AnimClass = LoadClass<UAnimInstance>(nullptr, AnimClassPath);
			if (!Test.TestNotNull(
				FString::Printf(TEXT("Third-person AnimBP can be loaded: %s"), AnimClassPath),
				AnimClass))
			{
				return false;
			}
			Test.TestTrue(
				FString::Printf(TEXT("Third-person AnimBP derives from UShooterThirdPersonAnimInstance: %s"), AnimClassPath),
				AnimClass->IsChildOf(UShooterThirdPersonAnimInstance::StaticClass()));
		}

		// B3 数据契约：AnimBP 的 AimOffset 俯仰输入不再依赖外部反射写入，
		// Character 提供 AO 就绪形式的 GetAimPitchN（sin 俯仰，与旧 PitchN 语义一致）。
		const UFunction* AimPitchNFunction = AShooterCharacter::StaticClass()
			? AShooterCharacter::StaticClass()->FindFunctionByName(FName(TEXT("GetAimPitchN")))
			: nullptr;
		Test.TestNotNull(
			TEXT("AShooterCharacter exposes GetAimPitchN data contract"),
			AimPitchNFunction);
		Test.TestTrue(
			TEXT("GetAimPitchN is BlueprintCallable"),
			AimPitchNFunction && AimPitchNFunction->HasAnyFunctionFlags(FUNC_BlueprintCallable));

		return true;
	}

	bool TestRifleReloadAnimationConfiguration(FAutomationTestBase& Test)
	{
		const UClass* RifleClass = LoadClass<AShooterWeapon>(
			nullptr,
			TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C"));
		if (!Test.TestNotNull(TEXT("Rifle class can be loaded for reload animation validation"), RifleClass))
		{
			return false;
		}

		const AShooterWeapon* RifleDefaults = RifleClass->GetDefaultObject<AShooterWeapon>();
		const UAnimSequence* ReloadSequence = LoadObject<UAnimSequence>(
			nullptr,
			TEXT("/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Reload.MM_Rifle_Reload"));
		const UAnimBlueprint* RifleAnimBlueprint = LoadObject<UAnimBlueprint>(
			nullptr,
			TEXT("/Game/Shooter/Animation/ThirdPerson/ABP_TP_Rifle.ABP_TP_Rifle"));
		if (!Test.TestNotNull(TEXT("Rifle reload sequence can be loaded"), ReloadSequence) ||
			!Test.TestNotNull(TEXT("Rifle defaults can be loaded for reload animation validation"), RifleDefaults) ||
			!Test.TestNotNull(TEXT("Rifle third-person AnimBP can be loaded"), RifleAnimBlueprint))
		{
			return false;
		}

		Test.TestTrue(
			*FString::Printf(
				TEXT("Rifle authoritative ReloadDuration (%.3fs) covers reload sequence (%.3fs)"),
				RifleDefaults->GetReloadDuration(),
				ReloadSequence->GetPlayLength()),
			RifleDefaults->GetReloadDuration() + 0.01f >= ReloadSequence->GetPlayLength());

		Test.TestTrue(
			TEXT("Rifle reload sequence uses the third-person AnimBP target skeleton"),
			RifleAnimBlueprint->TargetSkeleton &&
			ReloadSequence->GetSkeleton() == RifleAnimBlueprint->TargetSkeleton);

		return true;
	}

	bool TestReloadSequenceSoundNotifies(FAutomationTestBase& Test)
	{
		// 换弹音效由序列内 WeaponSound Notify 在各端本地触发；
		// 序列资产无法用 MCP 只读校验，这里直接检查 Notifies 数组补上验证缺口。
		struct FReloadSequencePath
		{
			const TCHAR* Name;
			const TCHAR* Path;
		};
		const FReloadSequencePath Sequences[] = {
			{TEXT("Rifle"), TEXT("/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Reload.MM_Rifle_Reload")},
			{TEXT("Pistol"), TEXT("/Game/Characters/Mannequins/Anims/Pistol/MM_Pistol_Reload.MM_Pistol_Reload")},
		};

		for (const FReloadSequencePath& Sequence : Sequences)
		{
			const UAnimSequence* ReloadSequence = LoadObject<UAnimSequence>(nullptr, Sequence.Path);
			if (!Test.TestNotNull(
				FString::Printf(TEXT("%s reload sequence can be loaded"), Sequence.Name),
				ReloadSequence))
			{
				return false;
			}

			bool bStageSeen[static_cast<int32>(EShooterReloadSoundStage::Cocking) + 1] = {false};
			for (const FAnimNotifyEvent& NotifyEvent : ReloadSequence->Notifies)
			{
				// NotifyState 与其他类型的 Notify 一并遍历，只统计 WeaponSound 实例。
				const UShooterAnimNotify_WeaponSound* WeaponSoundNotify =
					Cast<UShooterAnimNotify_WeaponSound>(NotifyEvent.Notify);
				if (WeaponSoundNotify)
				{
					bStageSeen[static_cast<int32>(WeaponSoundNotify->Stage)] = true;
				}
			}

			const TCHAR* StageNames[] = {TEXT("MagazineOut"), TEXT("MagazineIn"), TEXT("Cocking")};
			for (int32 StageIndex = 0; StageIndex < UE_ARRAY_COUNT(StageNames); ++StageIndex)
			{
				Test.TestTrue(
					FString::Printf(
						TEXT("%s reload sequence covers %s weapon sound notify"),
						Sequence.Name, StageNames[StageIndex]),
					bStageSeen[StageIndex]);
			}
		}

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
		TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C"));
	bSucceeded &= TestWeaponConfiguration(
		*this,
		TEXT("Pistol"),
		TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Pistol.BP_ShooterWeapon_Pistol_C"));
	bSucceeded &= TestCharacterReplication(*this);
	bSucceeded &= TestMatchStateReplication(*this);
	bSucceeded &= TestAnimationConfiguration(*this);
	bSucceeded &= TestRifleReloadAnimationConfiguration(*this);
	bSucceeded &= TestReloadSequenceSoundNotifies(*this);

	return bSucceeded;
}

#endif
