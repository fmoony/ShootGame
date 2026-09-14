// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Abilities/GameplayAbility.h"
#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Characters/ShooterCharacter.h"
#include "AbilitySystem/Abilities/ShooterGameplayAbility_Equip.h"
#include "AbilitySystem/Abilities/ShooterGameplayAbility_Fire.h"
#include "AbilitySystem/Abilities/ShooterGameplayAbility_Reload.h"
#include "Inventory/ShooterInventoryTypes.h"
#include "Weapons/ShooterWeapon.h"
#include "Tests/Weapon/ShooterWeaponRuntimeTestTypes.h"
#include "UObject/UnrealType.h"

namespace ShooterAbilityEquipBehaviorAutomationTests
{
	/** Equip 数据契约测试用的裸 World 与测试武器。 */
	UWorld* CreateEquipContractWorld(FAutomationTestBase& Test)
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!Test.TestNotNull(TEXT("Equip contract world created"), World) || !GEngine)
		{
			return nullptr;
		}

		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		return World;
	}

	void DestroyEquipContractWorld(UWorld* World)
	{
		if (World && GEngine)
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	}

	AShooterRuntimePoolTestWeapon* SpawnEquipContractWeapon(FAutomationTestBase& Test, UWorld* World, FName WeaponId)
	{
		AShooterRuntimePoolTestWeapon* Weapon = World
			? World->SpawnActor<AShooterRuntimePoolTestWeapon>(FVector::ZeroVector, FRotator::ZeroRotator)
			: nullptr;
		if (!Test.TestNotNull(TEXT("Equip contract weapon spawned"), Weapon))
		{
			return nullptr;
		}

		Weapon->InitializeWeaponIdentity(WeaponId);
		return Weapon;
	}

	bool TestServerOnlyContract(FAutomationTestBase& Test)
	{
		const UShooterGameplayAbility_Equip* EquipDefaults = GetDefault<UShooterGameplayAbility_Equip>();
		if (!Test.TestNotNull(TEXT("GA_Equip has defaults"), EquipDefaults))
		{
			return false;
		}

		Test.TestEqual(TEXT("GA_Equip executes only on server"),
			static_cast<int32>(EquipDefaults->GetNetExecutionPolicy()),
			static_cast<int32>(EGameplayAbilityNetExecutionPolicy::ServerOnly));
		Test.TestEqual(TEXT("GA_Equip is InstancedPerActor"), static_cast<int32>(EquipDefaults->GetInstancingPolicy()),
			static_cast<int32>(EGameplayAbilityInstancingPolicy::InstancedPerActor));
		Test.TestFalse(TEXT("GA_Equip does not retrigger an already active instance"),
			EquipDefaults->CanRetriggerInstancedAbility());
		Test.TestTrue(TEXT("GA_Equip uses the Input.Equip category tag"), EquipDefaults->HasInputEquipTag());
		Test.TestTrue(TEXT("GA_Equip owns State.Equipping while active"),
			EquipDefaults->OwnsStateEquippingWhileActive());
		return true;
	}

	bool TestInputBindingContract(FAutomationTestBase& Test)
	{
		const UClass* CharacterClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Shooter/Blueprints/Characters/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		if (!Test.TestNotNull(TEXT("BP_ShooterCharacter can be loaded"), CharacterClass))
		{
			return false;
		}

		const FObjectProperty* SwitchActionProperty = FindFProperty<FObjectProperty>(CharacterClass,
			TEXT("SwitchWeaponAction"));
		if (!Test.TestNotNull(TEXT("Character exposes SwitchWeaponAction"), SwitchActionProperty))
		{
			return false;
		}

		const AShooterCharacter* CharacterDefaults = CharacterClass->GetDefaultObject<AShooterCharacter>();
		const UInputAction* SwitchAction = CharacterDefaults
			? Cast<UInputAction>(SwitchActionProperty->GetObjectPropertyValue_InContainer(CharacterDefaults))
			: nullptr;
		if (!Test.TestNotNull(TEXT("BP_ShooterCharacter configures SwitchWeaponAction"), SwitchAction))
		{
			return false;
		}

		Test.TestEqual(TEXT("IA_SwapWeapon uses Axis1D"), static_cast<int32>(SwitchAction->ValueType),
			static_cast<int32>(EInputActionValueType::Axis1D));

		const UInputMappingContext* InputMappingContext = LoadObject<UInputMappingContext>(nullptr,
			TEXT("/Game/Shooter/Input/IMC_Weapons.IMC_Weapons"));
		if (!Test.TestNotNull(TEXT("IMC_Weapons can be loaded"), InputMappingContext))
		{
			return false;
		}

		bool bFoundWheelDown = false;
		bool bFoundNegatedWheelUp = false;
		for (const FEnhancedActionKeyMapping& Mapping : InputMappingContext->GetMappings())
		{
			if (Mapping.Action != SwitchAction)
			{
				continue;
			}

			if (Mapping.Key == EKeys::MouseScrollDown)
			{
				bFoundWheelDown = true;
			}
			else if (Mapping.Key == EKeys::MouseScrollUp)
			{
				bFoundNegatedWheelUp = Mapping.Modifiers.ContainsByPredicate(
					[](const TObjectPtr<UInputModifier>& Modifier)
					{
						return Modifier && Modifier->IsA<UInputModifierNegate>();
					});
			}
		}

		Test.TestTrue(TEXT("Mouse wheel down maps to next weapon"), bFoundWheelDown);
		Test.TestTrue(TEXT("Mouse wheel up maps to previous weapon through Negate"), bFoundNegatedWheelUp);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAbilityEquipServerOnlyTest, "ShootGame.Ability.Equip.ServerOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityEquipServerOnlyTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityEquipBehaviorAutomationTests;
	return TestServerOnlyContract(*this) && TestInputBindingContract(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAbilityEquipNextSlotTest, "ShootGame.Ability.Equip.NextSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityEquipNextSlotTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityEquipBehaviorAutomationTests;

	UWorld* World = CreateEquipContractWorld(*this);
	if (!World)
	{
		return false;
	}

	FShooterWeaponInventoryList Inventory;
	AShooterRuntimePoolTestWeapon* Slot0 = SpawnEquipContractWeapon(*this, World, TEXT("EquipSlotWeapon_0"));
	AShooterRuntimePoolTestWeapon* Slot1 = SpawnEquipContractWeapon(*this, World, TEXT("EquipSlotWeapon_1"));
	AShooterRuntimePoolTestWeapon* Slot2 = SpawnEquipContractWeapon(*this, World, TEXT("EquipSlotWeapon_2"));
	if (!Slot0 || !Slot1 || !Slot2)
	{
		DestroyEquipContractWorld(World);
		return false;
	}

	TestTrue(TEXT("Slot 0 is added"), Inventory.AddItem(Slot0, 0));
	TestTrue(TEXT("Slot 1 is added"), Inventory.AddItem(Slot1, 1));
	TestTrue(TEXT("Slot 2 is added"), Inventory.AddItem(Slot2, 2));

	TestTrue(TEXT("Slot 0 advances to Slot 1"), Inventory.FindAdjacentWeapon(Slot0, 1) == Slot1);
	TestTrue(TEXT("Slot 1 advances to Slot 2"), Inventory.FindAdjacentWeapon(Slot1, 1) == Slot2);
	TestTrue(TEXT("Slot 2 wraps to Slot 0"), Inventory.FindAdjacentWeapon(Slot2, 1) == Slot0);
	TestTrue(TEXT("Slot 0 wraps backward to Slot 2"), Inventory.FindAdjacentWeapon(Slot0, -1) == Slot2);
	TestTrue(TEXT("Slot 2 moves backward to Slot 1"), Inventory.FindAdjacentWeapon(Slot2, -1) == Slot1);

	// 未入库的当前武器：明确拒绝。
	AShooterRuntimePoolTestWeapon* Unlisted = SpawnEquipContractWeapon(*this, World, TEXT("EquipSlotWeapon_X"));
	if (TestNotNull(TEXT("Unlisted weapon spawns"), Unlisted))
	{
		TestNull(TEXT("Missing current weapon is rejected"), Inventory.FindAdjacentWeapon(Unlisted, 1));
	}

	DestroyEquipContractWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAbilityEquipCancelFireTest, "ShootGame.Ability.Equip.CancelFire",
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAbilityEquipCancelReloadTest, "ShootGame.Ability.Equip.CancelReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityEquipCancelReloadTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityEquipBehaviorAutomationTests;

	const UShooterGameplayAbility_Reload* ReloadDefaults = GetDefault<UShooterGameplayAbility_Reload>();
	TestTrue(TEXT("GA_Reload is blocked while State.Equipping"), ReloadDefaults && ReloadDefaults->IsBlockedByStateEquipping());
	// GA_Equip 不把 State.Reloading 设为阻塞，激活时会显式取消 GA_Reload；网络协调器验证 Ammo 不变。
	return TestServerOnlyContract(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAbilityEquipRejectSingleWeaponTest,
	"ShootGame.Ability.Equip.Reject.SingleWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityEquipRejectSingleWeaponTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityEquipBehaviorAutomationTests;

	UWorld* World = CreateEquipContractWorld(*this);
	if (World)
	{
		FShooterWeaponInventoryList Inventory;
		AShooterRuntimePoolTestWeapon* Only = SpawnEquipContractWeapon(*this, World, TEXT("EquipSingleWeapon"));
		if (TestNotNull(TEXT("Single weapon spawns"), Only))
		{
			TestTrue(TEXT("Single weapon is added"), Inventory.AddItem(Only, 0));
			TestNull(TEXT("Single weapon has no adjacent slot"), Inventory.FindAdjacentWeapon(Only, 1));
		}
		DestroyEquipContractWorld(World);
	}

	// 服务器激活拒绝由网络协调器在单武器阶段验证。
	return TestServerOnlyContract(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAbilityEquipRejectDeadTest, "ShootGame.Ability.Equip.Reject.Dead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityEquipRejectDeadTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityEquipBehaviorAutomationTests;

	const UShooterGameplayAbility_Equip* EquipDefaults = GetDefault<UShooterGameplayAbility_Equip>();
	TestTrue(TEXT("GA_Equip is blocked by State.Dead"), EquipDefaults && EquipDefaults->IsBlockedByStateDead());
	return TestServerOnlyContract(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAbilityEquipCurrentWeaponReplicationTest,
	"ShootGame.Ability.Equip.CurrentWeaponReplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityEquipCurrentWeaponReplicationTest::RunTest(const FString& Parameters)
{
	// R4：CurrentWeaponActor 的复制权威迁入 EquipmentComponent。
	TestNull(TEXT("Character no longer owns CurrentWeapon property"),
		FindFProperty<FProperty>(AShooterCharacter::StaticClass(), TEXT("CurrentWeapon")));

	const FProperty* EquipmentCurrentWeaponProperty = FindFProperty<FProperty>(
		UShooterEquipmentComponent::StaticClass(), TEXT("CurrentWeaponActor"));
	if (!TestNotNull(TEXT("Equipment exposes CurrentWeaponActor"), EquipmentCurrentWeaponProperty))
	{
		return false;
	}
	TestTrue(TEXT("Equipment CurrentWeaponActor is replicated"), EquipmentCurrentWeaponProperty->HasAnyPropertyFlags(CPF_Net));
	TestEqual(TEXT("Equipment CurrentWeaponActor uses OnRep_CurrentWeaponActor"),
		EquipmentCurrentWeaponProperty->RepNotifyFunc, FName(TEXT("OnRep_CurrentWeaponActor")));

	const AShooterCharacter* CharacterDefaults = GetDefault<AShooterCharacter>();
	TestNotNull(TEXT("Character has defaults"), CharacterDefaults);
	TestEqual(TEXT("GetCurrentWeaponActor mirrors GetCurrentWeapon"),
		CharacterDefaults ? CharacterDefaults->GetCurrentWeaponActor() : nullptr,
		CharacterDefaults ? CharacterDefaults->GetCurrentWeapon() : nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAbilityEquipInstanceActorConsistencyTest,
	"ShootGame.Ability.Equip.InstanceActorConsistency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityEquipInstanceActorConsistencyTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityEquipBehaviorAutomationTests;

	// R8 后装备提交唯一入口是 EquipmentComponent::EquipWeapon（普通 C++ 方法，非 UFUNCTION）；
	// Character 的 CommitActiveWeapon 兼容入口已删除。
	TestNull(TEXT("Character no longer exposes CommitActiveWeapon"),
		AShooterCharacter::StaticClass()->FindFunctionByName(TEXT("CommitActiveWeapon")));
	const AShooterWeapon* WeaponDefaults = GetDefault<AShooterWeapon>();
	if (TestNotNull(TEXT("AShooterWeapon has defaults"), WeaponDefaults))
	{
		TestTrue(TEXT("Weapon EquipDuration is positive"), WeaponDefaults->GetEquipDuration() > 0.0f);
	}

	// CurrentWeaponActor 与 Inventory Entry Actor 的运行时一致性由网络协调器在切换提交后验证。
	return TestServerOnlyContract(*this);
}

#endif
