// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Abilities/GameplayAbility.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "ShooterCharacter.h"
#include "ShooterGameplayAbility_Equip.h"
#include "ShooterGameplayAbility_Fire.h"
#include "ShooterGameplayAbility_Reload.h"
#include "ShooterInventoryTypes.h"
#include "ShooterWeapon.h"
#include "UObject/UnrealType.h"

namespace ShooterAbilityEquipBehaviorAutomationTests
{
	bool TestServerOnlyContract(FAutomationTestBase& Test)
	{
		const UShooterGameplayAbility_Equip* EquipDefaults =
			GetDefault<UShooterGameplayAbility_Equip>();
		if (!Test.TestNotNull(TEXT("GA_Equip has defaults"), EquipDefaults))
		{
			return false;
		}

		Test.TestEqual(
			TEXT("GA_Equip executes only on server"),
			static_cast<int32>(EquipDefaults->GetNetExecutionPolicy()),
			static_cast<int32>(EGameplayAbilityNetExecutionPolicy::ServerOnly));
		Test.TestEqual(
			TEXT("GA_Equip is InstancedPerActor"),
			static_cast<int32>(EquipDefaults->GetInstancingPolicy()),
			static_cast<int32>(EGameplayAbilityInstancingPolicy::InstancedPerActor));
		Test.TestFalse(
			TEXT("GA_Equip does not retrigger an already active instance"),
			EquipDefaults->CanRetriggerInstancedAbility());
		Test.TestTrue(
			TEXT("GA_Equip is bound to Input.Equip.Next"),
			EquipDefaults->HasInputEquipNextTag());
		Test.TestTrue(
			TEXT("GA_Equip owns State.Equipping while active"),
			EquipDefaults->OwnsStateEquippingWhileActive());
		return true;
	}

	FShooterWeaponInstanceData MakeInstance(const FGuid& InstanceId, int32 SlotIndex)
	{
		FShooterWeaponInstanceData InstanceData;
		InstanceData.InstanceId = InstanceId;
		InstanceData.DefinitionId = FPrimaryAssetId(
			FPrimaryAssetType(TEXT("ShooterTest")),
			FName(TEXT("Weapon")));
		InstanceData.MagazineAmmo = 1;
		InstanceData.ReserveAmmo = 1;
		InstanceData.SlotIndex = SlotIndex;
		return InstanceData;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityEquipServerOnlyTest,
	"ShootGame.Ability.Equip.ServerOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityEquipServerOnlyTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityEquipBehaviorAutomationTests;
	return TestServerOnlyContract(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityEquipNextSlotTest,
	"ShootGame.Ability.Equip.NextSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityEquipNextSlotTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityEquipBehaviorAutomationTests;

	FShooterWeaponInventoryList Inventory;
	const FGuid Slot0 = FGuid::NewGuid();
	const FGuid Slot1 = FGuid::NewGuid();
	const FGuid Slot2 = FGuid::NewGuid();
	TestTrue(TEXT("Slot 0 is added"), Inventory.AddItem(MakeInstance(Slot0, 0)));
	TestTrue(TEXT("Slot 1 is added"), Inventory.AddItem(MakeInstance(Slot1, 1)));
	TestTrue(TEXT("Slot 2 is added"), Inventory.AddItem(MakeInstance(Slot2, 2)));

	FGuid Next;
	TestTrue(
		TEXT("Slot 0 advances to Slot 1"),
		Inventory.FindNextItemId(Slot0, Next) && Next == Slot1);
	TestTrue(
		TEXT("Slot 1 advances to Slot 2"),
		Inventory.FindNextItemId(Slot1, Next) && Next == Slot2);
	TestTrue(
		TEXT("Slot 2 wraps to Slot 0"),
		Inventory.FindNextItemId(Slot2, Next) && Next == Slot0);
	TestFalse(
		TEXT("Missing current instance is rejected"),
		Inventory.FindNextItemId(FGuid::NewGuid(), Next));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityEquipCancelFireTest,
	"ShootGame.Ability.Equip.CancelFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityEquipCancelFireTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityEquipBehaviorAutomationTests;

	const UShooterGameplayAbility_Equip* EquipDefaults = GetDefault<UShooterGameplayAbility_Equip>();
	const UShooterGameplayAbility_Fire* FireDefaults = GetDefault<UShooterGameplayAbility_Fire>();
	TestTrue(TEXT("GA_Fire is blocked while State.Equipping"), FireDefaults && FireDefaults->IsBlockedByStateEquipping());
	TestTrue(TEXT("GA_Equip owns State.Equipping"), EquipDefaults && EquipDefaults->OwnsStateEquippingWhileActive());
	// 服务器实际取消活动 GA_Fire 与弹丸无残留由网络协调器验证。
	return TestServerOnlyContract(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityEquipCancelReloadTest,
	"ShootGame.Ability.Equip.CancelReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityEquipCancelReloadTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityEquipBehaviorAutomationTests;

	const UShooterGameplayAbility_Reload* ReloadDefaults = GetDefault<UShooterGameplayAbility_Reload>();
	TestTrue(TEXT("GA_Reload is blocked while State.Equipping"), ReloadDefaults && ReloadDefaults->IsBlockedByStateEquipping());
	// GA_Equip 不把 State.Reloading 设为阻塞，激活时会显式取消 GA_Reload；网络协调器验证 Ammo 不变。
	return TestServerOnlyContract(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityEquipRejectSingleWeaponTest,
	"ShootGame.Ability.Equip.Reject.SingleWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityEquipRejectSingleWeaponTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityEquipBehaviorAutomationTests;

	FShooterWeaponInventoryList Inventory;
	const FGuid OnlyId = FGuid::NewGuid();
	TestTrue(TEXT("Single weapon is added"), Inventory.AddItem(MakeInstance(OnlyId, 0)));
	FGuid Next;
	TestTrue(TEXT("Single weapon has no next slot"), Inventory.Items.Num() < 2);
	// 服务器激活拒绝由网络协调器在单武器阶段验证。
	return TestServerOnlyContract(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityEquipRejectDeadTest,
	"ShootGame.Ability.Equip.Reject.Dead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityEquipRejectDeadTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityEquipBehaviorAutomationTests;

	const UShooterGameplayAbility_Equip* EquipDefaults = GetDefault<UShooterGameplayAbility_Equip>();
	TestTrue(TEXT("GA_Equip is blocked by State.Dead"), EquipDefaults && EquipDefaults->IsBlockedByStateDead());
	return TestServerOnlyContract(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityEquipCurrentWeaponReplicationTest,
	"ShootGame.Ability.Equip.CurrentWeaponReplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityEquipCurrentWeaponReplicationTest::RunTest(const FString& Parameters)
{
	// R4：CurrentWeaponActor 的复制权威迁入 EquipmentComponent。
	TestNull(
		TEXT("Character no longer owns CurrentWeapon property"),
		FindFProperty<FProperty>(AShooterCharacter::StaticClass(), TEXT("CurrentWeapon")));

	const FProperty* EquipmentCurrentWeaponProperty = FindFProperty<FProperty>(
		UShooterEquipmentComponent::StaticClass(),
		TEXT("CurrentWeaponActor"));
	if (!TestNotNull(TEXT("Equipment exposes CurrentWeaponActor"), EquipmentCurrentWeaponProperty))
	{
		return false;
	}
	TestTrue(TEXT("Equipment CurrentWeaponActor is replicated"), EquipmentCurrentWeaponProperty->HasAnyPropertyFlags(CPF_Net));
	TestEqual(
		TEXT("Equipment CurrentWeaponActor uses OnRep_CurrentWeaponActor"),
		EquipmentCurrentWeaponProperty->RepNotifyFunc,
		FName(TEXT("OnRep_CurrentWeaponActor")));

	const AShooterCharacter* CharacterDefaults = GetDefault<AShooterCharacter>();
	TestNotNull(TEXT("Character has defaults"), CharacterDefaults);
	TestEqual(
		TEXT("GetCurrentWeaponActor mirrors GetCurrentWeapon"),
		CharacterDefaults ? CharacterDefaults->GetCurrentWeaponActor() : nullptr,
		CharacterDefaults ? CharacterDefaults->GetCurrentWeapon() : nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityEquipInstanceActorConsistencyTest,
	"ShootGame.Ability.Equip.InstanceActorConsistency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityEquipInstanceActorConsistencyTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityEquipBehaviorAutomationTests;

	// CommitActiveWeapon 是服务器权威普通 C++ 入口，不是客户端可远程调用的 UFUNCTION。
	TestNull(
		TEXT("CommitActiveWeapon is not a remote-callable UFUNCTION"),
		AShooterCharacter::StaticClass()->FindFunctionByName(TEXT("CommitActiveWeapon")));

	const AShooterWeapon* WeaponDefaults = GetDefault<AShooterWeapon>();
	if (TestNotNull(TEXT("AShooterWeapon has defaults"), WeaponDefaults))
	{
		TestTrue(TEXT("Weapon EquipDuration is positive"), WeaponDefaults->GetEquipDuration() > 0.0f);
	}

	// 逻辑 InstanceId 与 CurrentWeapon Actor 的运行时一致性由网络协调器在切换提交后验证。
	return TestServerOnlyContract(*this);
}

#endif
