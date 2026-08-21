// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Abilities/GameplayAbility.h"
#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "ShooterCharacter.h"
#include "ShooterGameplayAbility_Reload.h"
#include "ShooterGameplayTags.h"
#include "ShooterInventoryComponent.h"
#include "ShooterInventoryTypes.h"
#include "ShooterPlayerState.h"
#include "ShooterWeapon.h"
#include "UObject/UnrealType.h"

namespace ShooterAbilityReloadBehaviorAutomationTests
{
	bool TestServerOnlyContract(FAutomationTestBase& Test)
	{
		const UShooterGameplayAbility_Reload* ReloadDefaults =
			GetDefault<UShooterGameplayAbility_Reload>();
		if (!Test.TestNotNull(TEXT("GA_Reload has defaults"), ReloadDefaults))
		{
			return false;
		}

		Test.TestEqual(
			TEXT("GA_Reload executes only on server"),
			static_cast<int32>(ReloadDefaults->GetNetExecutionPolicy()),
			static_cast<int32>(EGameplayAbilityNetExecutionPolicy::ServerOnly));
		Test.TestEqual(
			TEXT("GA_Reload is InstancedPerActor"),
			static_cast<int32>(ReloadDefaults->GetInstancingPolicy()),
			static_cast<int32>(EGameplayAbilityInstancingPolicy::InstancedPerActor));
		Test.TestFalse(
			TEXT("GA_Reload does not retrigger an already active instance"),
			ReloadDefaults->CanRetriggerInstancedAbility());
		Test.TestTrue(
			TEXT("GA_Reload is bound to Input.Reload"),
			ReloadDefaults->HasInputReloadTag());
		Test.TestTrue(
			TEXT("GA_Reload owns State.Reloading while active"),
			ReloadDefaults->OwnsStateReloadingWhileActive());
		return true;
	}

	bool TestAmmoAuthorityContract(FAutomationTestBase& Test)
	{
		const UShooterGameplayAbility_Reload* ReloadDefaults =
			GetDefault<UShooterGameplayAbility_Reload>();
		if (Test.TestNotNull(TEXT("GA_Reload has defaults"), ReloadDefaults))
		{
			// Ability 不得新增第二份 Ammo 权威：没有 Cost GE，弹药只由 Inventory.ReloadMagazine 修改。
			Test.TestNull(
				TEXT("GA_Reload has no cost GameplayEffect"),
				ReloadDefaults->GetCostGameplayEffect());
		}

		// 客户端不能远程调用 Inventory Reload 事务入口。
		Test.TestNull(
			TEXT("Inventory ReloadMagazine is not a remote-callable UFUNCTION"),
			UShooterInventoryComponent::StaticClass()->FindFunctionByName(
				TEXT("ReloadMagazine")));

		const AShooterWeapon* WeaponDefaults = GetDefault<AShooterWeapon>();
		if (Test.TestNotNull(TEXT("AShooterWeapon has defaults"), WeaponDefaults))
		{
			Test.TestTrue(
				TEXT("Weapon ReloadDuration is the server transaction clock and positive"),
				WeaponDefaults->GetReloadDuration() > 0.0f);
		}
		return true;
	}

	bool TestRejectFullMagazineDataContract(FAutomationTestBase& Test)
	{
		FShooterWeaponInventoryList Inventory;
		FShooterWeaponInstanceData InstanceData;
		InstanceData.InstanceId = FGuid::NewGuid();
		InstanceData.DefinitionId = FPrimaryAssetId(
			FPrimaryAssetType(TEXT("ShooterTest")),
			FName(TEXT("Weapon")));
		InstanceData.MagazineAmmo = 30;
		InstanceData.ReserveAmmo = 20;
		InstanceData.SlotIndex = 0;

		Test.TestTrue(TEXT("Full magazine instance is added"), Inventory.AddItem(InstanceData));
		int32 TransferredAmmo = INDEX_NONE;
		Test.TestFalse(
			TEXT("Full magazine rejects the data transaction"),
			Inventory.ReloadMagazine(InstanceData.InstanceId, 30, TransferredAmmo));
		Test.TestEqual(TEXT("Full magazine transfers zero"), TransferredAmmo, 0);
		Test.TestEqual(
			TEXT("Full magazine keeps MagazineAmmo"),
			Inventory.FindItem(InstanceData.InstanceId)->InstanceData.MagazineAmmo,
			30);
		Test.TestEqual(
			TEXT("Full magazine keeps ReserveAmmo"),
			Inventory.FindItem(InstanceData.InstanceId)->InstanceData.ReserveAmmo,
			20);

		// 服务器激活侧完整拒绝由 ShooterNetworkTestCoordinator 在真实会话中验证。
		return true;
	}

	bool TestRejectNoReserveDataContract(FAutomationTestBase& Test)
	{
		FShooterWeaponInventoryList Inventory;
		FShooterWeaponInstanceData InstanceData;
		InstanceData.InstanceId = FGuid::NewGuid();
		InstanceData.DefinitionId = FPrimaryAssetId(
			FPrimaryAssetType(TEXT("ShooterTest")),
			FName(TEXT("Weapon")));
		InstanceData.MagazineAmmo = 5;
		InstanceData.ReserveAmmo = 0;
		InstanceData.SlotIndex = 0;

		Test.TestTrue(TEXT("No-reserve instance is added"), Inventory.AddItem(InstanceData));
		int32 TransferredAmmo = INDEX_NONE;
		Test.TestFalse(
			TEXT("No reserve rejects the data transaction"),
			Inventory.ReloadMagazine(InstanceData.InstanceId, 30, TransferredAmmo));
		Test.TestEqual(TEXT("No reserve transfers zero"), TransferredAmmo, 0);
		Test.TestEqual(
			TEXT("No reserve keeps MagazineAmmo"),
			Inventory.FindItem(InstanceData.InstanceId)->InstanceData.MagazineAmmo,
			5);
		Test.TestEqual(
			TEXT("No reserve keeps ReserveAmmo"),
			Inventory.FindItem(InstanceData.InstanceId)->InstanceData.ReserveAmmo,
			0);
		return true;
	}

	bool TestInputBindingContract(FAutomationTestBase& Test)
	{
		const UClass* CharacterClass = LoadClass<AShooterCharacter>(
			nullptr,
			TEXT("/Game/Shooter/Blueprints/Characters/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		if (!Test.TestNotNull(TEXT("BP_ShooterCharacter can be loaded"), CharacterClass))
		{
			return false;
		}

		const FObjectProperty* ReloadActionProperty = FindFProperty<FObjectProperty>(
			CharacterClass,
			TEXT("ReloadAction"));
		if (!Test.TestNotNull(TEXT("Character exposes ReloadAction"), ReloadActionProperty))
		{
			return false;
		}

		const AShooterCharacter* CharacterDefaults =
			CharacterClass->GetDefaultObject<AShooterCharacter>();
		const UInputAction* ReloadAction = CharacterDefaults
			? Cast<UInputAction>(ReloadActionProperty->GetObjectPropertyValue_InContainer(
				CharacterDefaults))
			: nullptr;
		Test.TestNotNull(TEXT("BP_ShooterCharacter configures ReloadAction"), ReloadAction);
		Test.TestTrue(
			TEXT("ReloadAction points at IA_Reload"),
			ReloadAction && ReloadAction->GetName() == TEXT("IA_Reload"));

		UInputMappingContext* InputMappingContext = LoadObject<UInputMappingContext>(
			nullptr,
			TEXT("/Game/Shooter/Input/IMC_Weapons.IMC_Weapons"));
		if (!Test.TestNotNull(TEXT("IMC_Weapons can be loaded"), InputMappingContext))
		{
			return false;
		}

		bool bFoundReloadKey = false;
		for (const FEnhancedActionKeyMapping& Mapping : InputMappingContext->GetMappings())
		{
			if (Mapping.Action && Mapping.Action->GetName() == TEXT("IA_Reload") &&
				Mapping.Key == EKeys::R)
			{
				bFoundReloadKey = true;
				break;
			}
		}
		Test.TestTrue(TEXT("IMC_Weapons maps R to IA_Reload"), bFoundReloadKey);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityReloadServerOnlyTest,
	"ShootGame.Ability.Reload.ServerOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityReloadServerOnlyTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityReloadBehaviorAutomationTests;
	return TestServerOnlyContract(*this) && TestInputBindingContract(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityReloadTransferOnceTest,
	"ShootGame.Ability.Reload.TransferOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityReloadTransferOnceTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityReloadBehaviorAutomationTests;

	// 单次激活只提交一次的真实证据由 ShooterNetworkTestCoordinator 的 ReloadTransfer 观测给出；
	// 这里守住静态边界：单事务 Ability 配置 + Inventory 唯一原子入口 + 服务器时钟配置。
	return TestServerOnlyContract(*this) && TestAmmoAuthorityContract(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityReloadRejectFullMagazineTest,
	"ShootGame.Ability.Reload.Reject.FullMagazine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityReloadRejectFullMagazineTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityReloadBehaviorAutomationTests;
	return TestServerOnlyContract(*this) && TestRejectFullMagazineDataContract(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityReloadRejectNoReserveTest,
	"ShootGame.Ability.Reload.Reject.NoReserve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityReloadRejectNoReserveTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityReloadBehaviorAutomationTests;
	return TestServerOnlyContract(*this) && TestRejectNoReserveDataContract(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityReloadCancelDeathTest,
	"ShootGame.Ability.Reload.Cancel.Death",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityReloadCancelDeathTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityReloadBehaviorAutomationTests;

	const UShooterGameplayAbility_Reload* ReloadDefaults =
		GetDefault<UShooterGameplayAbility_Reload>();
	this->TestTrue(
		TEXT("GA_Reload is blocked by State.Dead"),
		ReloadDefaults && ReloadDefaults->IsBlockedByStateDead());
	// 死亡取消与 Ammo 不变的真实证据由 ShooterNetworkTestCoordinator 在 Dedicated / Listen 中验证。
	return TestServerOnlyContract(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityReloadCancelEquipTest,
	"ShootGame.Ability.Reload.Cancel.Equip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityReloadCancelEquipTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityReloadBehaviorAutomationTests;

	const UShooterGameplayAbility_Reload* ReloadDefaults =
		GetDefault<UShooterGameplayAbility_Reload>();
	this->TestTrue(
		TEXT("GA_Reload is blocked by State.Equipping"),
		ReloadDefaults && ReloadDefaults->IsBlockedByStateEquipping());
	// 切枪取消发生在提交窗口的真实证据由 ShooterNetworkTestCoordinator 验证。
	return TestServerOnlyContract(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityReloadCancelDisconnectTest,
	"ShootGame.Ability.Reload.Cancel.Disconnect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityReloadCancelDisconnectTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityReloadBehaviorAutomationTests;
	// EndPlay 的取消链与断线清理由 DisconnectCleanup 阶段验证；
	// 这里守住 Ability 结束时会释放 OwnedTag 的 GAS 配置面。
	return TestServerOnlyContract(*this) && TestAmmoAuthorityContract(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAbilityReloadRespawnNoCarryTest,
	"ShootGame.Ability.Reload.RespawnNoCarry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityReloadRespawnNoCarryTest::RunTest(const FString& Parameters)
{
	using namespace ShooterAbilityReloadBehaviorAutomationTests;

	const AShooterPlayerState* PlayerStateDefaults = GetDefault<AShooterPlayerState>();
	this->TestEqual(
		TEXT("PlayerState CDO does not carry a granted Reload Spec"),
		PlayerStateDefaults ? PlayerStateDefaults->GetReloadAbilitySpecCount() : INDEX_NONE,
		0);
	// 重生后弹药与活动 Reload 事务不跨生命，由 ShooterNetworkTestCoordinator 的
	// ReloadRespawn 观测在 Dedicated / Listen 会话中验证。
	return TestServerOnlyContract(*this);
}

#endif
