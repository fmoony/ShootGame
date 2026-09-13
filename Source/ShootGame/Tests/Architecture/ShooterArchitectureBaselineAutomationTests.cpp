// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimInstance.h"
#include "Characters/Aim/ShooterAimPresentationComponent.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Characters/Animation/ShooterFirstPersonAnimInstance.h"
#include "Characters/ShooterCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "AI/ShooterNPC.h"
#include "AbilitySystem/ShooterAttributeSet.h"
#include "Tests/Architecture/ShooterArchitectureTestTypes.h"
#include "Tests/Equipment/ShooterWeaponPresentationTestTypes.h"
#include "UObject/UnrealType.h"
#include "Weapons/Pickup/ShooterPickup.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/Subsystems/ShooterWeaponRuntimeSubsystem.h"
#include "Tests/Weapon/ShooterWeaponTestTableTypes.h"

namespace ShooterArchitectureBaselineAutomationTests
{
	UWorld* CreateArchitectureTestWorld()
	{
		// CreateWorld 总会创建并初始化 PersistentLevel / WorldSettings；
	// 测试只补一个 WorldContext，不能再次调用 InitializeNewWorld。
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World || !GEngine)
	{
		return World;
	}

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	return World;
	}

	void DestroyArchitectureTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
}

/**
 * R0/R1 行为冻结：用真实 UWorld 驱动角色武器授予路径。
 * R1 后玩家武器只有 Inventory 授予入口一条创建路径，且授予后仍立即装备。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterArchitectureWeaponGrantSurfaceTest,
	"ShootGame.Architecture.Baseline.WeaponGrantSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterArchitectureWeaponGrantSurfaceTest::RunTest(const FString& Parameters)
{
	using namespace ShooterArchitectureBaselineAutomationTests;

	UWorld* World = CreateArchitectureTestWorld();
	if (!TestNotNull(TEXT("Architecture test world created"), World))
	{
		return false;
	}

	AShooterArchitectureTestCharacter* Character = World->SpawnActor<AShooterArchitectureTestCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Architecture test character spawned"), Character))
	{
		DestroyArchitectureTestWorld(World);
		return false;
	}

	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	if (!TestNotNull(TEXT("Architecture test character owns InventoryComponent"), Inventory))
	{
		DestroyArchitectureTestWorld(World);
		return false;
	}

	// S3 授予链 = Runtime.Acquire + Inventory.AddWeapon；立即装备 = Equipment.EquipWeapon（重放 Pickup 的最小行为）。
	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	UShooterWeaponRuntimeSubsystem* Runtime = World->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
	if (!TestNotNull(TEXT("Architecture test character owns EquipmentComponent"), Equipment) ||
		!TestNotNull(TEXT("Architecture test world owns the weapon runtime"), Runtime))
	{
		DestroyArchitectureTestWorld(World);
		return false;
	}

	// 授予前先登记 WeaponId 行；同一 WeaponId 被两次租用，用于验证重复类型被拒绝。
	const FName WeaponRowName = AddTestWeaponRow(GetOrInjectRuntimeTestTable(World), MakeTestWeaponRow(
			AShooterArchitectureTestWeapon::StaticClass(),
			/*MagazineSize*/ 10,
			/*InitialReserveAmmo*/ -1));
	Runtime->InitializeWeaponRuntimeForTest();

	AShooterWeapon* GrantedWeapon = Runtime->AcquireWeapon(WeaponRowName, Character, nullptr);
	const EShooterInventoryAddResult FirstGrantResult = GrantedWeapon
		? Inventory->AddWeapon(GrantedWeapon)
		: EShooterInventoryAddResult::InvalidWeapon;
	TestEqual(TEXT("Inventory grants the first weapon"), static_cast<int32>(FirstGrantResult), static_cast<int32>(EShooterInventoryAddResult::Added));
	TestTrue(TEXT("Equipment commits the granted weapon"), Equipment->EquipWeapon(GrantedWeapon));

	AShooterWeapon* CurrentWeapon = Character->GetCurrentWeaponActor();
	TestNotNull(TEXT("Granted weapon is equipped"), CurrentWeapon);
	if (CurrentWeapon)
	{
		TestTrue(TEXT("Granted weapon is owned by the character"), CurrentWeapon->GetOwner() == Character);
		TestFalse(TEXT("Granted weapon carries its WeaponId identity"), CurrentWeapon->GetWeaponId().IsNone());
		TestTrue(TEXT("CurrentWeaponActor matches the Inventory entry actor"),
			Inventory->ContainsWeapon(CurrentWeapon));
	}

	TestEqual(TEXT("Grant creates exactly one Inventory entry"), Inventory->GetWeaponCount(), 1);

	// 重复授予同一武器种类不能产生第二把武器：由 Inventory 的 DuplicateWeapon 拒绝。
	AShooterWeapon* DuplicateWeapon = Runtime->AcquireWeapon(WeaponRowName, Character, nullptr);
	if (TestNotNull(TEXT("Duplicate weapon acquires from the runtime"), DuplicateWeapon))
	{
		TestEqual(TEXT("Duplicate weapon grant is rejected"), static_cast<int32>(Inventory->AddWeapon(DuplicateWeapon)),
			static_cast<int32>(EShooterInventoryAddResult::DuplicateWeapon));
		Runtime->ReleaseWeapon(DuplicateWeapon);
	}
	AShooterWeapon* CurrentWeaponAfterSecondGrant = Character->GetCurrentWeaponActor();
	TestTrue(TEXT("Duplicate weapon class grant keeps the same CurrentWeapon"), CurrentWeaponAfterSecondGrant == CurrentWeapon);
	TestEqual(TEXT("Duplicate grant keeps exactly one Inventory entry"), Inventory->GetWeaponCount(), 1);
	DestroyArchitectureTestWorld(World);
	return true;
}

/**
 * R0 资产表面快照：五个 AnimBP 必须可加载，且记录当前父类分布。
 * R6.2 资产迁移后：FP_Weapon / FP_Pistol 派生自 UShooterFirstPersonAnimInstance；
 * TP_Rifle / TP_Pistol 派生自 UShooterThirdPersonAnimInstance；ABP_FP_Copy 是 Copy Pose 支撑资产，本轮不迁移。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterArchitectureAnimBPSurfaceTest, "ShootGame.Architecture.Baseline.AnimBPSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterArchitectureAnimBPSurfaceTest::RunTest(const FString& Parameters)
{
	struct FAnimBPSnapshot
	{
		const TCHAR* Path;
		const TCHAR* Name;
		bool bExpectedFirstPersonAnimInstance;
		bool bExpectedThirdPersonAnimInstance;
	};

	const FAnimBPSnapshot Snapshots[] = {
		{TEXT("/Game/Shooter/Animation/FirstPerson/ABP_FP_Weapon.ABP_FP_Weapon_C"), TEXT("ABP_FP_Weapon"), true, false},
		{TEXT("/Game/Shooter/Animation/FirstPerson/ABP_FP_Pistol.ABP_FP_Pistol_C"), TEXT("ABP_FP_Pistol"), true, false},
		{TEXT("/Game/Shooter/Animation/FirstPerson/ABP_FP_Copy.ABP_FP_Copy_C"), TEXT("ABP_FP_Copy"), false, false},
		{TEXT("/Game/Shooter/Animation/ThirdPerson/ABP_TP_Rifle.ABP_TP_Rifle_C"), TEXT("ABP_TP_Rifle"), false, true},
		{TEXT("/Game/Shooter/Animation/ThirdPerson/ABP_TP_Pistol.ABP_TP_Pistol_C"), TEXT("ABP_TP_Pistol"), false, true},
	};

	for (const FAnimBPSnapshot& Snapshot : Snapshots)
	{
		const UClass* AnimClass = LoadClass<UAnimInstance>(nullptr, Snapshot.Path);
		if (!TestNotNull(FString::Printf(TEXT("AnimBP can be loaded: %s"), Snapshot.Name), AnimClass))
		{
			continue;
		}

		TestTrue(FString::Printf(TEXT("%s derives from UAnimInstance"), Snapshot.Name),
			AnimClass->IsChildOf(UAnimInstance::StaticClass()));
		TestEqual(FString::Printf(TEXT("%s FirstPersonAnimInstance parent surface matches snapshot"), Snapshot.Name),
			AnimClass->IsChildOf(UShooterFirstPersonAnimInstance::StaticClass()),
			Snapshot.bExpectedFirstPersonAnimInstance);
		TestEqual(FString::Printf(TEXT("%s ThirdPersonAnimInstance parent surface matches snapshot"), Snapshot.Name),
			AnimClass->IsChildOf(UShooterThirdPersonAnimInstance::StaticClass()),
			Snapshot.bExpectedThirdPersonAnimInstance);
	}

	return true;
}

/**
 * R0/R2/R4 所有权表快照：
 * Aim 由 AimPresentationComponent 承接；CurrentWeaponActor 在 R4 迁入 EquipmentComponent；
 * S3 起实例身份（ActiveWeaponInstanceId / BoundInstanceId）全部删除，Equipment 只存 Actor；S4 后 Pickup / NPC 只保存 WeaponId。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterArchitectureOwnershipSurfaceTest,
	"ShootGame.Architecture.Baseline.OwnershipSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterArchitectureOwnershipSurfaceTest::RunTest(const FString& Parameters)
{
	const AShooterCharacter* CharacterDefaults = AShooterCharacter::StaticClass()->GetDefaultObject<AShooterCharacter>();
	if (!TestNotNull(TEXT("Character has defaults"), CharacterDefaults))
	{
		return false;
	}

	const UShooterAimPresentationComponent* AimPresentationComponent = CharacterDefaults->GetAimPresentationComponent();
	if (!TestNotNull(TEXT("Character creates AimPresentationComponent"), AimPresentationComponent))
	{
		return false;
	}
	TestTrue(TEXT("AimPresentationComponent is replicated"), AimPresentationComponent->GetIsReplicated());

	const UShooterEquipmentComponent* EquipmentComponent = CharacterDefaults->GetEquipmentComponent();
	if (!TestNotNull(TEXT("Character creates EquipmentComponent"), EquipmentComponent))
	{
		return false;
	}
	TestTrue(TEXT("EquipmentComponent is replicated after R4"), EquipmentComponent->GetIsReplicated());

	// R4：Character 不再拥有 CurrentWeapon 复制属性。
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

	// S3：实例身份属性从 Equipment 与 Inventory 全部删除，只保留 CurrentWeaponActor。
	TestNull(TEXT("Equipment no longer owns ActiveWeaponInstanceId property"),
		FindFProperty<FProperty>(UShooterEquipmentComponent::StaticClass(), TEXT("ActiveWeaponInstanceId")));
	TestNull(TEXT("Inventory no longer owns ActiveWeaponInstanceId property"),
		FindFProperty<FProperty>(UShooterInventoryComponent::StaticClass(), TEXT("ActiveWeaponInstanceId")));

	// R2：Character 不再持有 Aim 网络属性和 Server RPC。
	TestNull(TEXT("Character no longer owns PresentationAimTarget property"),
		FindFProperty<FProperty>(AShooterCharacter::StaticClass(), TEXT("PresentationAimTarget")));
	TestNull(TEXT("Character no longer owns presentation aim Server RPC"),
		AShooterCharacter::StaticClass()->FindFunctionByName(TEXT("ServerUpdatePresentationAimTarget")));

	const FProperty* ComponentPresentationAimTargetProperty = FindFProperty<FProperty>(
		UShooterAimPresentationComponent::StaticClass(), TEXT("PresentationAimTarget"));
	if (!TestNotNull(TEXT("AimPresentationComponent exposes PresentationAimTarget"), ComponentPresentationAimTargetProperty))
	{
		return false;
	}
	TestTrue(TEXT("AimPresentationComponent PresentationAimTarget is replicated"),
		ComponentPresentationAimTargetProperty->HasAnyPropertyFlags(CPF_Net));
	TestEqual(TEXT("AimPresentationComponent PresentationAimTarget uses OnRep_PresentationAimTarget"),
		ComponentPresentationAimTargetProperty->RepNotifyFunc, FName(TEXT("OnRep_PresentationAimTarget")));

	const UFunction* AimServerRpc = UShooterAimPresentationComponent::StaticClass()->FindFunctionByName(
		TEXT("ServerUpdatePresentationAimTarget"));
	TestNotNull(TEXT("AimPresentationComponent owns the presentation aim Server RPC"), AimServerRpc);
	TestTrue(TEXT("AimPresentationComponent presentation aim RPC is a server RPC"),
		AimServerRpc && AimServerRpc->HasAnyFunctionFlags(FUNC_NetServer));

	return true;
}

/**
 * R7 Health 读取收敛表面：
 * ASC + ShooterAttributeSet 是唯一 Health / MaxHealth 权威；
 * Character 的 CurrentHP / bIsDead 仍是复制表现镜像，MaxHP 只是服务器配置。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterArchitectureHealthSurfaceTest, "ShootGame.Architecture.Baseline.HealthSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterArchitectureHealthSurfaceTest::RunTest(const FString& Parameters)
{
	const FProperty* HealthProperty = FindFProperty<FProperty>(UShooterAttributeSet::StaticClass(), TEXT("Health"));
	if (!TestNotNull(TEXT("ShooterAttributeSet exposes Health"), HealthProperty))
	{
		return false;
	}
	TestTrue(TEXT("AttributeSet Health is replicated"), HealthProperty->HasAnyPropertyFlags(CPF_Net));

	const FProperty* MaxHealthProperty = FindFProperty<FProperty>(UShooterAttributeSet::StaticClass(),
		TEXT("MaxHealth"));
	if (!TestNotNull(TEXT("ShooterAttributeSet exposes MaxHealth"), MaxHealthProperty))
	{
		return false;
	}
	TestTrue(TEXT("AttributeSet MaxHealth is replicated"), MaxHealthProperty->HasAnyPropertyFlags(CPF_Net));

	const FProperty* CurrentHPProperty = FindFProperty<FProperty>(AShooterCharacter::StaticClass(), TEXT("CurrentHP"));
	if (!TestNotNull(TEXT("Character exposes CurrentHP mirror"), CurrentHPProperty))
	{
		return false;
	}
	TestTrue(TEXT("CurrentHP mirror is BlueprintReadOnly"), CurrentHPProperty->HasAnyPropertyFlags(CPF_BlueprintReadOnly));
	TestTrue(TEXT("CurrentHP mirror is replicated"), CurrentHPProperty->HasAnyPropertyFlags(CPF_Net));
	TestEqual(TEXT("CurrentHP mirror uses OnRep_CurrentHP"), CurrentHPProperty->RepNotifyFunc,
		FName(TEXT("OnRep_CurrentHP")));

	const FProperty* bIsDeadProperty = FindFProperty<FProperty>(AShooterCharacter::StaticClass(), TEXT("bIsDead"));
	if (!TestNotNull(TEXT("Character exposes bIsDead mirror"), bIsDeadProperty))
	{
		return false;
	}
	TestTrue(TEXT("bIsDead mirror is BlueprintReadOnly"), bIsDeadProperty->HasAnyPropertyFlags(CPF_BlueprintReadOnly));
	TestTrue(TEXT("bIsDead mirror is replicated"), bIsDeadProperty->HasAnyPropertyFlags(CPF_Net));

	const FProperty* MaxHPProperty = FindFProperty<FProperty>(AShooterCharacter::StaticClass(), TEXT("MaxHP"));
	if (!TestNotNull(TEXT("Character exposes server-side MaxHP config"), MaxHPProperty))
	{
		return false;
	}
	TestFalse(TEXT("MaxHP config is not a replicated Health authority"), MaxHPProperty->HasAnyPropertyFlags(CPF_Net));

	// 不存在第二套公开 Health 写入口。
	TestNull(TEXT("Character has no public SetCurrentHP"),
		AShooterCharacter::StaticClass()->FindFunctionByName(TEXT("SetCurrentHP")));
	TestNull(TEXT("Character has no public SetMaxHP"),
		AShooterCharacter::StaticClass()->FindFunctionByName(TEXT("SetMaxHP")));
	TestNull(TEXT("NPC has no public SetCurrentHP"),
		AShooterNPC::StaticClass()->FindFunctionByName(TEXT("SetCurrentHP")));

	// R7 读穿透：CDO 无 ASC 时，读穿透必须回退到兼容镜像。
	const AShooterCharacter* CharacterDefaults = AShooterCharacter::StaticClass()->GetDefaultObject<AShooterCharacter>();
	if (TestNotNull(TEXT("Character has defaults"), CharacterDefaults))
	{
		TestEqual(TEXT("GetHealthAttributeValue falls back to CurrentHP without ASC"),
			CharacterDefaults->GetHealthAttributeValue(), CharacterDefaults->GetCurrentHP());
		TestEqual(TEXT("GetMaxHealthAttributeValue falls back to MaxHP without ASC"),
			CharacterDefaults->GetMaxHealthAttributeValue(), CharacterDefaults->GetMaxHP());
	}

	return true;
}

/**
 * S4 身份表面快照：运行时只保留 WeaponId / WeaponActor / SlotIndex / CurrentWeaponActor。
 * RowHandle、RowName、InstanceId 与通用对象池接口必须已经从生产与资产表面删除。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterArchitectureWeaponIdentitySurfaceTest,
	"ShootGame.Architecture.Baseline.WeaponIdentitySurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterArchitectureWeaponIdentitySurfaceTest::RunTest(const FString& Parameters)
{
	// WeaponActor：永久 WeaponId 存在，迁移期 WeaponRowName 已删除。
	TestNull(TEXT("WeaponActor no longer owns WeaponRowName property"),
		FindFProperty<FProperty>(AShooterWeapon::StaticClass(), TEXT("WeaponRowName")));
	TestNotNull(TEXT("WeaponActor owns permanent WeaponId property"),
		FindFProperty<FNameProperty>(AShooterWeapon::StaticClass(), TEXT("WeaponId")));

	// Pickup：不再保存 DataTableRowHandle，只保存 FName WeaponId。
	TestNull(TEXT("Pickup no longer owns WeaponType row handle"),
		FindFProperty<FProperty>(AShooterPickup::StaticClass(), TEXT("WeaponType")));
	const FNameProperty* PickupWeaponIdProperty = FindFProperty<FNameProperty>(AShooterPickup::StaticClass(),
		TEXT("WeaponId"));
	TestNotNull(TEXT("Pickup owns WeaponId property"), PickupWeaponIdProperty);
	TestTrue(TEXT("Pickup WeaponId is editor configurable"),
		PickupWeaponIdProperty && PickupWeaponIdProperty->HasAnyPropertyFlags(CPF_Edit));

	// NPC：与玩家共用 WeaponId 池；迁移期 WeaponRowName / WeaponClass 兼容属性已删除。
	TestNull(TEXT("NPC no longer owns WeaponRowName property"),
		FindFProperty<FProperty>(AShooterNPC::StaticClass(), TEXT("WeaponRowName")));
	TestNull(TEXT("NPC no longer owns WeaponClass compatibility property"),
		FindFProperty<FProperty>(AShooterNPC::StaticClass(), TEXT("WeaponClass")));
	TestNotNull(TEXT("NPC owns WeaponId property"),
		FindFProperty<FNameProperty>(AShooterNPC::StaticClass(), TEXT("WeaponId")));

	// Inventory Entry：只有 WeaponActor 引用与 SlotIndex。
	TestNull(TEXT("Inventory entry no longer owns InstanceId"),
		FindFProperty<FProperty>(FShooterInventoryWeaponEntry::StaticStruct(), TEXT("InstanceId")));
	TestNull(TEXT("Inventory entry no longer owns WeaponRowName"),
		FindFProperty<FProperty>(FShooterInventoryWeaponEntry::StaticStruct(), TEXT("WeaponRowName")));
	TestNotNull(TEXT("Inventory entry owns WeaponActor reference"),
		FindFProperty<FProperty>(FShooterInventoryWeaponEntry::StaticStruct(), TEXT("Weapon")));
	TestNotNull(TEXT("Inventory entry owns SlotIndex"),
		FindFProperty<FProperty>(FShooterInventoryWeaponEntry::StaticStruct(), TEXT("SlotIndex")));

	return true;
}


#endif // WITH_DEV_AUTOMATION_TESTS
