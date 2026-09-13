// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Characters/Animation/ShooterFirstPersonAnimInstance.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/UnrealType.h"
#include "Characters/ShooterCharacter.h"
#include "GameFramework/GameMode/ShooterGameMode.h"
#include "GameFramework/GameState/ShooterGameState.h"
#include "GameFramework/PlayerState/ShooterPlayerState.h"
#include "Weapons/Projectile/ShooterProjectile.h"
#include "AI/ShooterNPC.h"
#include "Weapons/Pickup/ShooterPickup.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/Data/ShooterWeaponConfigRow.h"
#include "Weapons/Data/ShooterWeaponTable.h"
#include "Weapons/Animation/ShooterAnimNotify_WeaponSound.h"

namespace ShooterWeaponAutomationTests
{
	/** 四个正式武器行：行名与显示名一致，配置只来自 DT_WeaponData。 */
	struct FProductionWeaponRow
	{
		const TCHAR* Name;
		const TCHAR* RowName;
	};

	const FProductionWeaponRow ProductionWeaponRows[] = {
		{TEXT("Rifle"), TEXT("Rifle")},
		{TEXT("Pistol"), TEXT("Pistol")},
		{TEXT("AWP"), TEXT("AWP")},
		{TEXT("GrenadeLauncher"), TEXT("GrenadeLauncher")},
	};

	/**
	 * 单表纠偏后的武器配置校验：配置一律从 DT_WeaponData 行读取，
	 * 不再读取 WeaponActor 蓝图默认值（蓝图 CDO 已不再承载可表格化配置）。
	 */
	bool TestWeaponConfiguration(FAutomationTestBase& Test, const TCHAR* WeaponName, const TCHAR* WeaponRowName)
	{
		const FShooterWeaponConfigRow* Row = ShooterWeaponTable::FindWeaponRow(ShooterWeaponTable::ResolveWeaponTable(),
			FName(WeaponRowName));
		if (!Test.TestNotNull(FString::Printf(TEXT("%s weapon row %s resolves"), WeaponName, WeaponRowName), Row))
		{
			return false;
		}

		Test.TestTrue(FString::Printf(TEXT("%s row is valid for grant"), WeaponName), ShooterWeaponTable::IsRowValidForGrant(Row));

		UClass* WeaponClass = Row->WeaponActorClass;
		if (!Test.TestNotNull(FString::Printf(TEXT("%s row WeaponActorClass can be loaded"), WeaponName), WeaponClass))
		{
			return false;
		}

		const AShooterWeapon* WeaponDefaults = WeaponClass->GetDefaultObject<AShooterWeapon>();
		if (!Test.TestNotNull(FString::Printf(TEXT("%s has defaults"), WeaponName), WeaponDefaults))
		{
			return false;
		}

		Test.TestTrue(FString::Printf(TEXT("%s replicates"), WeaponName), WeaponDefaults->GetIsReplicated());
		Test.TestTrue(FString::Printf(TEXT("%s magazine size is positive"), WeaponName), Row->MagazineSize > 0);
		Test.TestTrue(FString::Printf(TEXT("%s refire rate is positive"), WeaponName), Row->RefireRate > 0.0f);

		// 网格与 Socket 都来自行：第一/第三人称网格必须真实拥有行配置的 Muzzle socket。
		const USkeletalMesh* FirstPersonMesh = Row->FirstPersonMesh.LoadSynchronous();
		const USkeletalMesh* ThirdPersonMesh = Row->ThirdPersonMesh.LoadSynchronous();
		Test.TestTrue(FString::Printf(TEXT("%s first-person mesh has configured muzzle socket"), WeaponName),
			FirstPersonMesh && FirstPersonMesh->FindSocket(Row->MuzzleSocketName) != nullptr);
		Test.TestTrue(FString::Printf(TEXT("%s third-person mesh has authoritative muzzle socket"), WeaponName),
			ThirdPersonMesh && ThirdPersonMesh->FindSocket(Row->MuzzleSocketName) != nullptr);

		// 弹药权威在 WeaponActor（S2）：弹匣与备弹必须复制且带 OnRep 推 HUD。
		const FProperty* MagazineAmmoProperty = FindFProperty<FProperty>(WeaponClass, TEXT("MagazineAmmo"));
		const FProperty* ReserveAmmoProperty = FindFProperty<FProperty>(WeaponClass, TEXT("ReserveAmmo"));
		if (!Test.TestNotNull(FString::Printf(TEXT("%s exposes MagazineAmmo"), WeaponName).GetCharArray().GetData(),
			MagazineAmmoProperty) || !Test.TestNotNull(
				FString::Printf(TEXT("%s exposes ReserveAmmo"), WeaponName).GetCharArray().GetData(),
				ReserveAmmoProperty))
		{
			return false;
		}
		Test.TestTrue(FString::Printf(TEXT("%s MagazineAmmo is replicated"), WeaponName).GetCharArray().GetData(),
			MagazineAmmoProperty->HasAnyPropertyFlags(CPF_Net));
		Test.TestEqual(
			FString::Printf(TEXT("%s MagazineAmmo uses OnRep_MagazineAmmo"), WeaponName).GetCharArray().GetData(),
			MagazineAmmoProperty->RepNotifyFunc,
			FName(TEXT("OnRep_MagazineAmmo")));
		Test.TestTrue(FString::Printf(TEXT("%s ReserveAmmo is replicated"), WeaponName).GetCharArray().GetData(),
			ReserveAmmoProperty->HasAnyPropertyFlags(CPF_Net));
		Test.TestEqual(
			FString::Printf(TEXT("%s ReserveAmmo uses OnRep_ReserveAmmo"), WeaponName).GetCharArray().GetData(),
			ReserveAmmoProperty->RepNotifyFunc,
			FName(TEXT("OnRep_ReserveAmmo")));

		Test.TestNotNull(FString::Printf(TEXT("%s has fire sound configured"), WeaponName), Row->FireSound.Get());

		// 换弹音效配置：三个阶段都由武器模板行声明资源，Notify 只在本端触发播放。
		const TObjectPtr<USoundBase>* ReloadSounds[] = {
			&Row->ReloadMagazineOutSound,
			&Row->ReloadMagazineInSound,
			&Row->ReloadCockingSound,
		};
		const TCHAR* ReloadSoundNames[] = {
			TEXT("ReloadMagazineOutSound"),
			TEXT("ReloadMagazineInSound"),
			TEXT("ReloadCockingSound"),
		};
		for (int32 SoundIndex = 0; SoundIndex < UE_ARRAY_COUNT(ReloadSounds); ++SoundIndex)
		{
			Test.TestNotNull(FString::Printf(TEXT("%s has %s configured"), WeaponName, ReloadSoundNames[SoundIndex]),
				ReloadSounds[SoundIndex]->Get());
		}

		const UClass* ProjectileClass = Row->ProjectileClass.Get();
		if (!Test.TestNotNull(FString::Printf(TEXT("%s has projectile class configured"), WeaponName), ProjectileClass))
		{
			return false;
		}

		const AShooterProjectile* ProjectileDefaults = ProjectileClass->GetDefaultObject<AShooterProjectile>();
		if (!Test.TestNotNull(FString::Printf(TEXT("%s projectile has defaults"), WeaponName), ProjectileDefaults))
		{
			return false;
		}

		Test.TestTrue(FString::Printf(TEXT("%s projectile replicates"), WeaponName),
			ProjectileDefaults->GetIsReplicated());
		Test.TestTrue(FString::Printf(TEXT("%s projectile replicates movement"), WeaponName),
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

		Test.TestTrue(TEXT("CurrentHP is replicated"), CurrentHPProperty->HasAnyPropertyFlags(CPF_Net));
		Test.TestEqual(TEXT("CurrentHP uses OnRep_CurrentHP"), CurrentHPProperty->RepNotifyFunc,
			FName(TEXT("OnRep_CurrentHP")));

		const FProperty* IsDeadProperty = FindFProperty<FProperty>(AShooterCharacter::StaticClass(), TEXT("bIsDead"));
		if (!Test.TestNotNull(TEXT("Character exposes bIsDead"), IsDeadProperty))
		{
			return false;
		}

		Test.TestTrue(TEXT("bIsDead is replicated"), IsDeadProperty->HasAnyPropertyFlags(CPF_Net));
		Test.TestEqual(TEXT("bIsDead uses OnRep_IsDead"), IsDeadProperty->RepNotifyFunc, FName(TEXT("OnRep_IsDead")));

		return true;
	}

	bool TestMatchStateReplication(FAutomationTestBase& Test)
	{
		const AShooterGameMode* GameModeDefaults = AShooterGameMode::StaticClass()->GetDefaultObject<AShooterGameMode>();
		if (!Test.TestNotNull(TEXT("Shooter GameMode has defaults"), GameModeDefaults))
		{
			return false;
		}

		Test.TestEqual(TEXT("Shooter GameMode uses replicated ShooterGameState"),
			GameModeDefaults->GameStateClass.Get(), AShooterGameState::StaticClass());
		Test.TestEqual(TEXT("Shooter GameMode uses ShooterPlayerState"), GameModeDefaults->PlayerStateClass.Get(),
			AShooterPlayerState::StaticClass());

		const FProperty* TeamScoresProperty =
			FindFProperty<FProperty>(AShooterGameState::StaticClass(), TEXT("TeamScores"));
		if (!Test.TestNotNull(TEXT("ShooterGameState exposes TeamScores"), TeamScoresProperty))
		{
			return false;
		}
		Test.TestTrue(TEXT("TeamScores is replicated"), TeamScoresProperty->HasAnyPropertyFlags(CPF_Net));
		Test.TestEqual(TEXT("TeamScores uses OnRep_TeamScores"), TeamScoresProperty->RepNotifyFunc,
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
			const FProperty* Property = FindFProperty<FProperty>(AShooterPlayerState::StaticClass(), Expected.Name);
			if (!Test.TestNotNull(FString::Printf(TEXT("ShooterPlayerState exposes %s"), Expected.Name), Property))
			{
				return false;
			}
			Test.TestTrue(FString::Printf(TEXT("%s is replicated"), Expected.Name),
				Property->HasAnyPropertyFlags(CPF_Net));
			Test.TestEqual(FString::Printf(TEXT("%s uses %s"), Expected.Name, Expected.RepNotify),
				Property->RepNotifyFunc, FName(Expected.RepNotify));
		}

		return true;
	}

	bool TestAnimationConfiguration(FAutomationTestBase& Test)
	{
		const AShooterCharacter* CharacterDefaults = AShooterCharacter::StaticClass()->GetDefaultObject<AShooterCharacter>();
		if (!Test.TestNotNull(TEXT("Shooter character has defaults"), CharacterDefaults))
		{
			return false;
		}

		Test.TestTrue(TEXT("First-person mesh is visible only to its owner"),
			CharacterDefaults->GetFirstPersonMesh()->bOnlyOwnerSee);
		Test.TestTrue(TEXT("Third-person mesh is hidden from its owner"), CharacterDefaults->GetMesh()->bOwnerNoSee);

		const TCHAR* FirstPersonAnimClassPaths[] = {
			TEXT("/Game/Shooter/Animation/FirstPerson/ABP_FP_Weapon.ABP_FP_Weapon_C"),
			TEXT("/Game/Shooter/Animation/FirstPerson/ABP_FP_Pistol.ABP_FP_Pistol_C"),
		};
		for (const TCHAR* AnimClassPath : FirstPersonAnimClassPaths)
		{
			const UClass* AnimClass = LoadClass<UAnimInstance>(nullptr, AnimClassPath);
			if (!Test.TestNotNull(FString::Printf(TEXT("First-person AnimBP can be loaded: %s"), AnimClassPath), AnimClass))
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
			if (!Test.TestNotNull(FString::Printf(TEXT("Third-person AnimBP can be loaded: %s"), AnimClassPath), AnimClass))
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
		Test.TestNotNull(TEXT("AShooterCharacter exposes GetAimPitchN data contract"), AimPitchNFunction);
		Test.TestTrue(TEXT("GetAimPitchN is BlueprintCallable"),
			AimPitchNFunction && AimPitchNFunction->HasAnyFunctionFlags(FUNC_BlueprintCallable));

		return true;
	}

	bool TestRifleReloadAnimationConfiguration(FAutomationTestBase& Test)
	{
		// 权威 ReloadDuration 只来自 Rifle 模板行；蓝图默认值不再承载可表格化时序。
		const FShooterWeaponConfigRow* RifleRow = ShooterWeaponTable::FindWeaponRow(
			ShooterWeaponTable::ResolveWeaponTable(), FName(TEXT("Rifle")));
		const UAnimSequence* ReloadSequence = LoadObject<UAnimSequence>(nullptr,
			TEXT("/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Reload.MM_Rifle_Reload"));
		const UAnimBlueprint* RifleAnimBlueprint = LoadObject<UAnimBlueprint>(nullptr,
			TEXT("/Game/Shooter/Animation/ThirdPerson/ABP_TP_Rifle.ABP_TP_Rifle"));
		if (!Test.TestNotNull(TEXT("Rifle reload sequence can be loaded"), ReloadSequence) ||
			!Test.TestNotNull(TEXT("Rifle weapon row can be resolved for reload animation validation"), RifleRow) ||
			!Test.TestNotNull(TEXT("Rifle third-person AnimBP can be loaded"), RifleAnimBlueprint))
		{
			return false;
		}

		Test.TestTrue(*FString::Printf(
				TEXT("Rifle authoritative ReloadDuration (%.3fs) covers reload sequence (%.3fs)"),
				RifleRow->ReloadDuration, ReloadSequence->GetPlayLength()),
			RifleRow->ReloadDuration + 0.01f >= ReloadSequence->GetPlayLength());

		Test.TestTrue(TEXT("Rifle reload sequence uses the third-person AnimBP target skeleton"),
			RifleAnimBlueprint->TargetSkeleton && ReloadSequence->GetSkeleton() == RifleAnimBlueprint->TargetSkeleton);

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
			if (!Test.TestNotNull(FString::Printf(TEXT("%s reload sequence can be loaded"), Sequence.Name), ReloadSequence))
			{
				return false;
			}

			bool bStageSeen[static_cast<int32>(EShooterReloadSoundStage::Cocking) + 1] = {false};
			for (const FAnimNotifyEvent& NotifyEvent : ReloadSequence->Notifies)
			{
				// NotifyState 与其他类型的 Notify 一并遍历，只统计 WeaponSound 实例。
				const UShooterAnimNotify_WeaponSound* WeaponSoundNotify = Cast<UShooterAnimNotify_WeaponSound>(NotifyEvent.Notify);
				if (WeaponSoundNotify)
				{
					bStageSeen[static_cast<int32>(WeaponSoundNotify->Stage)] = true;
				}
			}

			const TCHAR* StageNames[] = {TEXT("MagazineOut"), TEXT("MagazineIn"), TEXT("Cocking")};
			for (int32 StageIndex = 0; StageIndex < UE_ARRAY_COUNT(StageNames); ++StageIndex)
			{
				Test.TestTrue(FString::Printf(TEXT("%s reload sequence covers %s weapon sound notify"),
						Sequence.Name, StageNames[StageIndex]), bStageSeen[StageIndex]);
			}
		}

		return true;
	}

	bool TestPickupWeaponIdAssets(FAutomationTestBase& Test)
	{
		// S4 资产迁移结果：四个正式 Pickup BP 只保存 WeaponId，NPC BP 保存 WeaponId=Rifle。
		for (const FProductionWeaponRow& Weapon : ProductionWeaponRows)
		{
			const FString BlueprintPath = FString::Printf(
			TEXT("/Game/Shooter/Blueprints/Pickups/BP_ShooterPickup_%s.BP_ShooterPickup_%s_C"),
			Weapon.Name,
			Weapon.Name);
			UClass* PickupClass = LoadClass<AShooterPickup>(nullptr, *BlueprintPath);
			const AShooterPickup* PickupDefaults = PickupClass
				? PickupClass->GetDefaultObject<AShooterPickup>()
				: nullptr;
			const FNameProperty* WeaponIdProperty = PickupDefaults
				? FindFProperty<FNameProperty>(PickupClass, TEXT("WeaponId"))
				: nullptr;
			Test.TestNotNull(FString::Printf(TEXT("%s Pickup BP exposes WeaponId"), Weapon.Name), WeaponIdProperty);
			Test.TestEqual(FString::Printf(TEXT("%s Pickup BP WeaponId matches its data row"), Weapon.Name),
				WeaponIdProperty ? WeaponIdProperty->GetPropertyValue_InContainer(PickupDefaults).ToString() : FString(),
				FString(Weapon.RowName));
		}

		UClass* NpcClass = LoadClass<AShooterNPC>(nullptr,
				TEXT("/Game/Shooter/Blueprints/AI/BP_ShooterNPC.BP_ShooterNPC_C"));
		const AShooterNPC* NpcDefaults = NpcClass ? NpcClass->GetDefaultObject<AShooterNPC>() : nullptr;
		const FNameProperty* NpcWeaponIdProperty = NpcDefaults
			? FindFProperty<FNameProperty>(NpcClass, TEXT("WeaponId"))
			: nullptr;
		Test.TestNotNull(TEXT("NPC BP exposes WeaponId"), NpcWeaponIdProperty);
		Test.TestEqual(TEXT("NPC BP WeaponId is Rifle"),
			NpcWeaponIdProperty ? NpcWeaponIdProperty->GetPropertyValue_InContainer(NpcDefaults).ToString() : FString(),
			FString(TEXT("Rifle")));

		// 每个正式 WeaponId 的启动预热数量必须大于 0。
		const UDataTable* WeaponTable = ShooterWeaponTable::ResolveWeaponTable();
		if (Test.TestNotNull(TEXT("DT_WeaponData can be resolved for pool size validation"), WeaponTable))
		{
			for (const FProductionWeaponRow& Weapon : ProductionWeaponRows)
			{
				const FShooterWeaponConfigRow* Row = ShooterWeaponTable::FindWeaponRow(WeaponTable, FName(Weapon.RowName));
				Test.TestTrue(FString::Printf(TEXT("%s InitialPoolSize is positive"), Weapon.Name),
					Row && Row->InitialPoolSize > 0);
			}
		}

		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponConfigurationTest, "ShootGame.Weapon.Configuration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponConfigurationTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponAutomationTests;

	bool bSucceeded = true;
	for (const FProductionWeaponRow& Weapon : ProductionWeaponRows)
	{
		bSucceeded &= TestWeaponConfiguration(*this, Weapon.Name, Weapon.RowName);
	}
	bSucceeded &= TestCharacterReplication(*this);
	bSucceeded &= TestMatchStateReplication(*this);
	bSucceeded &= TestAnimationConfiguration(*this);
	bSucceeded &= TestRifleReloadAnimationConfiguration(*this);
	bSucceeded &= TestReloadSequenceSoundNotifies(*this);
	bSucceeded &= TestPickupWeaponIdAssets(*this);

	return bSucceeded;
}

#endif
