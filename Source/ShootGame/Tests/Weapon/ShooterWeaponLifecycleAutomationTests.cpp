// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "Pool/ShooterActorPoolSubsystem.h"
#include "Weapons/ShooterWeapon.h"
#include "../Equipment/ShooterWeaponPresentationTestTypes.h"

namespace ShooterWeaponLifecycleAutomationTests
{
	UWorld* CreateLifecycleTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!World || !GEngine)
		{
			return World;
		}

		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		return World;
	}

	void DestroyLifecycleTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	AShooterWeaponPresentationTestCharacter* SpawnLifecycleTestCharacter(
		FAutomationTestBase& Test,
		UWorld* World)
	{
		AShooterWeaponPresentationTestCharacter* Character =
			World->SpawnActor<AShooterWeaponPresentationTestCharacter>(
				FVector::ZeroVector,
				FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("Lifecycle test character spawned"), Character))
		{
			return nullptr;
		}

		if (AWorldSettings* WorldSettings = World->GetWorldSettings())
		{
			WorldSettings->NotifyBeginPlay();
		}
		return Character;
	}
}

/** 状态转换与拒绝：InPool 拒绝装备、装备态拒绝改写绑定、幂等转换不重复触发。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponLifecycleTransitionTest,
	"ShootGame.Weapon.Lifecycle.Transitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponLifecycleTransitionTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponLifecycleAutomationTests;

	UWorld* World = CreateLifecycleTestWorld();
	if (!TestNotNull(TEXT("Lifecycle transition world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnLifecycleTestCharacter(*this, World);
	if (!Character)
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	AShooterWeapon* Weapon = World->SpawnActor<AShooterWeaponLifecycleTestWeapon>(
		FVector::ZeroVector,
		FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Lifecycle weapon spawned"), Weapon))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	// 初始状态：未绑定 -> InPool，激活被拒绝。
	// 注意：可见性不属于状态机契约（池归还 / Inventory 授予 / 表现收敛各自负责），
	// 这里只断言 InPool 的身份前置条件。
	TestEqual(TEXT("Fresh weapon starts InPool"), static_cast<int32>(Weapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::InPool));
	TestFalse(TEXT("InPool weapon has no instance binding"), Weapon->GetBoundInstanceId().IsValid());
	TestTrue(TEXT("InPool weapon has no owner"), Weapon->GetOwner() == nullptr);
	Weapon->ActivateWeapon();
	TestEqual(TEXT("InPool activation rejected"), static_cast<int32>(Weapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::InPool));

	// 绑定 -> Holstered；装备事务 -> Equipping；激活 -> Equipped。
	const FGuid InstanceId = FGuid::NewGuid();
	Weapon->SetBoundInstanceId(InstanceId);
	TestEqual(TEXT("Binding moves to Holstered"), static_cast<int32>(Weapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::Holstered));
	TestEqual(TEXT("Binding recorded"), Weapon->GetBoundInstanceId(), InstanceId);

	Weapon->BeginEquipTransaction();
	TestEqual(TEXT("Equip transaction enters Equipping"), static_cast<int32>(Weapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::Equipping));

	// Equipping 状态下改写绑定被拒绝。
	Weapon->SetBoundInstanceId(FGuid::NewGuid());
	TestEqual(TEXT("Rewriting binding in Equipping rejected"), Weapon->GetBoundInstanceId(), InstanceId);

	Weapon->ActivateWeapon();
	TestEqual(TEXT("Activation completes to Equipped"), static_cast<int32>(Weapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::Equipped));
	TestFalse(TEXT("Equipped weapon is visible"), Weapon->IsHidden());

	// 重复激活幂等。
	Weapon->ActivateWeapon();
	TestEqual(TEXT("Repeated activation stays Equipped"), static_cast<int32>(Weapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::Equipped));

	// Equipped 状态下改写绑定被拒绝。
	Weapon->SetBoundInstanceId(FGuid::NewGuid());
	TestEqual(TEXT("Rewriting binding in Equipped rejected"), Weapon->GetBoundInstanceId(), InstanceId);

	// 卸下 -> Holstered；重复卸下幂等。
	Weapon->DeactivateWeapon();
	TestEqual(TEXT("Deactivation returns to Holstered"), static_cast<int32>(Weapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::Holstered));
	TestTrue(TEXT("Holstered weapon is hidden"), Weapon->IsHidden());
	Weapon->DeactivateWeapon();
	TestEqual(TEXT("Repeated deactivation stays Holstered"), static_cast<int32>(Weapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::Holstered));

	// 解绑 -> InPool。
	Weapon->SetBoundInstanceId(FGuid());
	TestEqual(TEXT("Unbinding returns to InPool"), static_cast<int32>(Weapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::InPool));
	TestFalse(TEXT("Unbound instance id"), Weapon->GetBoundInstanceId().IsValid());

	DestroyLifecycleTestWorld(World);
	return true;
}

/** 池归还完整清理：停 Timer、清绑定、隐藏、Owner 缓存清空；复用同一实例且不继承旧绑定。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponLifecyclePoolReleaseTest,
	"ShootGame.Weapon.Lifecycle.PoolReleaseClearsState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponLifecyclePoolReleaseTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponLifecycleAutomationTests;

	UWorld* World = CreateLifecycleTestWorld();
	if (!TestNotNull(TEXT("Pool release world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnLifecycleTestCharacter(*this, World);
	if (!Character)
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	UShooterActorPoolSubsystem* Pool = World->GetSubsystem<UShooterActorPoolSubsystem>();
	if (!TestNotNull(TEXT("Pool subsystem exists"), Pool))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	FActorSpawnParameters AcquireParams;
	AcquireParams.Owner = Character;
	AShooterWeaponLifecycleTestWeapon* Weapon = Cast<AShooterWeaponLifecycleTestWeapon>(Pool->Acquire(
		AShooterWeaponLifecycleTestWeapon::StaticClass(),
		FTransform::Identity,
		AcquireParams));
	if (!TestNotNull(TEXT("Weapon acquired from pool"), Weapon))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	// 绑定并进入装备态，布置待清理的 Timer。
	const FGuid InstanceId = FGuid::NewGuid();
	Weapon->SetBoundInstanceId(InstanceId);
	Weapon->BeginEquipTransaction();
	Weapon->ActivateWeapon();
	Weapon->ArmRefireTimerForTest();
	TestTrue(TEXT("Refire timer is active before release"), Weapon->IsRefireTimerActiveForTest());

	TestTrue(TEXT("Weapon releases to pool"), Pool->Release(Weapon));

	TestEqual(TEXT("Released weapon is InPool"), static_cast<int32>(Weapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::InPool));
	TestFalse(TEXT("Released weapon has no binding"), Weapon->GetBoundInstanceId().IsValid());
	TestTrue(TEXT("Released weapon is hidden"), Weapon->IsHidden());
	TestFalse(TEXT("Refire timer is cleared on release"), Weapon->IsRefireTimerActiveForTest());
	TestFalse(TEXT("Pool manages released weapon"), Pool->IsManaged(Weapon));
	TestEqual(TEXT("Pool holds one weapon"), Pool->GetPooledCount(AShooterWeaponLifecycleTestWeapon::StaticClass()), 1);

	// 复用同一实例：不继承旧绑定，并重新绑定到新 Owner（池取出回调负责 Owner/Instigator 重绑）。
	AShooterWeaponPresentationTestCharacter* SecondCharacter =
		World->SpawnActor<AShooterWeaponPresentationTestCharacter>(
			FVector(200.0f, 0.0f, 0.0f),
			FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Second lifecycle character spawned"), SecondCharacter))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	FActorSpawnParameters ReacquireParams;
	ReacquireParams.Owner = SecondCharacter;
	ReacquireParams.Instigator = SecondCharacter;
	AShooterWeaponLifecycleTestWeapon* Reused = Cast<AShooterWeaponLifecycleTestWeapon>(Pool->Acquire(
		AShooterWeaponLifecycleTestWeapon::StaticClass(),
		FTransform::Identity,
		ReacquireParams));
	TestTrue(TEXT("Reacquire returns the same weapon"), Reused == Weapon);
	if (Reused)
	{
		TestFalse(TEXT("Reused weapon does not inherit old binding"), Reused->GetBoundInstanceId().IsValid());
		TestEqual(TEXT("Reused weapon waits at InPool"), static_cast<int32>(Reused->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::InPool));
		TestTrue(TEXT("Reused weapon rebinds new owner"), Reused->GetOwner() == SecondCharacter);
		TestTrue(TEXT("Reused weapon rebinds owner cache"), Reused->HasWeaponOwnerCacheForTest());
	}

	DestroyLifecycleTestWorld(World);
	return true;
}

/** 切枪语义：Inventory 内切枪只发生 Equipped <-> Holstered，不进入 InPool、不进池。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponLifecycleSwitchKeepsHolsteredTest,
	"ShootGame.Weapon.Lifecycle.SwitchKeepsHolstered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponLifecycleSwitchKeepsHolsteredTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponLifecycleAutomationTests;

	UWorld* World = CreateLifecycleTestWorld();
	if (!TestNotNull(TEXT("Switch world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character = SpawnLifecycleTestCharacter(*this, World);
	if (!Character)
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	if (!TestNotNull(TEXT("Character owns inventory"), Inventory) ||
		!TestNotNull(TEXT("Character owns equipment"), Equipment))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	FGuid PrimaryId;
	FGuid SecondaryId;
	TestEqual(
		TEXT("Primary weapon granted"),
		static_cast<int32>(Inventory->TryAddWeaponDefinition(
			MakeShooterTestWeaponDefinition(TEXT("WD_LifecycleSwitchPrimary"), AShooterInventoryOrderTestWeapon::StaticClass()),
			PrimaryId)),
		static_cast<int32>(EShooterInventoryAddResult::Added));
	TestEqual(
		TEXT("Secondary weapon granted"),
		static_cast<int32>(Inventory->TryAddWeaponDefinition(
			MakeShooterTestWeaponDefinition(TEXT("WD_LifecycleSwitchSecondary"), AShooterWeaponPresentationTestWeaponSecondary::StaticClass()),
			SecondaryId)),
		static_cast<int32>(EShooterInventoryAddResult::Added));

	TestTrue(TEXT("Primary equipped"), Equipment->EquipWeapon(PrimaryId));
	AShooterWeapon* PrimaryWeapon = Inventory->FindWeaponActor(PrimaryId);
	if (!TestNotNull(TEXT("Primary weapon actor exists"), PrimaryWeapon))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}
	TestEqual(TEXT("Primary is Equipped"), static_cast<int32>(PrimaryWeapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::Equipped));

	// 切枪：旧武器回到 Holstered（仍绑定、仍归 Inventory），不进池、不销毁。
	TestTrue(TEXT("Secondary equipped"), Equipment->EquipWeapon(SecondaryId));
	TestEqual(TEXT("Switched-out primary is Holstered"), static_cast<int32>(PrimaryWeapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::Holstered));
	TestTrue(TEXT("Switched-out primary keeps binding"), PrimaryWeapon->GetBoundInstanceId() == PrimaryId);
	TestTrue(TEXT("Switched-out primary stays registered"), Inventory->FindWeaponActor(PrimaryId) == PrimaryWeapon);

	UShooterActorPoolSubsystem* Pool = World->GetSubsystem<UShooterActorPoolSubsystem>();
	TestTrue(TEXT("Switch does not pool weapons"), Pool && Pool->GetPooledCount(AShooterInventoryOrderTestWeapon::StaticClass()) == 0);
	TestFalse(TEXT("Switch keeps primary out of pool"), Pool && Pool->IsManaged(PrimaryWeapon));

	// 同武器重复提交幂等，状态不变。
	TestTrue(TEXT("Re-equip secondary is idempotent"), Equipment->EquipWeapon(SecondaryId));
	AShooterWeapon* SecondaryWeapon = Inventory->FindWeaponActor(SecondaryId);
	TestEqual(
		TEXT("Re-equipped secondary stays Equipped"),
		static_cast<int32>(SecondaryWeapon ? SecondaryWeapon->GetLifecycleState() : EShooterWeaponLifecycleState::InPool),
		static_cast<int32>(EShooterWeaponLifecycleState::Equipped));

	DestroyLifecycleTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
