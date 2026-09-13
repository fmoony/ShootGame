// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "Weapons/Subsystems/ShooterWeaponRuntimeSubsystem.h"
#include "Tests/Weapon/ShooterWeaponTestTableTypes.h"
#include "Weapons/ShooterWeapon.h"
#include "Tests/Equipment/ShooterWeaponPresentationTestTypes.h"

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

	AShooterWeaponPresentationTestCharacter* SpawnLifecycleTestCharacter(FAutomationTestBase& Test, UWorld* World)
	{
		AShooterWeaponPresentationTestCharacter* Character = World->SpawnActor<AShooterWeaponPresentationTestCharacter>(
				FVector::ZeroVector, FRotator::ZeroRotator);
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponLifecycleTransitionTest, "ShootGame.Weapon.Lifecycle.Transitions",
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

	AShooterWeapon* Weapon = World->SpawnActor<AShooterWeaponLifecycleTestWeapon>(FVector::ZeroVector,
		FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Lifecycle weapon spawned"), Weapon))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	// 初始状态：未租用 -> InPool，激活被拒绝。
	// 注意：可见性不属于状态机契约（池归还 / Inventory 授予 / 表现收敛各自负责），
	// 这里只断言 InPool 的身份前置条件。
	TestEqual(TEXT("Fresh weapon starts InPool"), static_cast<int32>(Weapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::InPool));
	TestTrue(TEXT("Fresh weapon has no WeaponId yet"), Weapon->GetWeaponId().IsNone());
	TestTrue(TEXT("InPool weapon has no owner"), Weapon->GetOwner() == nullptr);
	Weapon->ActivateWeapon();
	TestEqual(TEXT("InPool activation rejected"), static_cast<int32>(Weapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::InPool));

	// 租用（设置 Owner + 租用回调）-> Holstered；装备事务 -> Equipping；激活 -> Equipped。
	Weapon->SetOwner(Character);
	Weapon->OnAcquiredFromWeaponPool();
	TestEqual(TEXT("Lease moves to Holstered"), static_cast<int32>(Weapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::Holstered));

	Weapon->BeginEquipTransaction();
	TestEqual(TEXT("Equip transaction enters Equipping"), static_cast<int32>(Weapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::Equipping));

	Weapon->ActivateWeapon();
	TestEqual(TEXT("Activation completes to Equipped"), static_cast<int32>(Weapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::Equipped));
	TestFalse(TEXT("Equipped weapon is visible"), Weapon->IsHidden());

	// 重复激活幂等。
	Weapon->ActivateWeapon();
	TestEqual(TEXT("Repeated activation stays Equipped"), static_cast<int32>(Weapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::Equipped));

	// 卸下 -> Holstered；重复卸下幂等。
	Weapon->DeactivateWeapon();
	TestEqual(TEXT("Deactivation returns to Holstered"), static_cast<int32>(Weapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::Holstered));
	TestTrue(TEXT("Holstered weapon is hidden"), Weapon->IsHidden());
	Weapon->DeactivateWeapon();
	TestEqual(TEXT("Repeated deactivation stays Holstered"), static_cast<int32>(Weapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::Holstered));

	// 归还回调 -> InPool。
	Weapon->OnReleasedToWeaponPool();
	TestEqual(TEXT("Release callback returns to InPool"), static_cast<int32>(Weapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::InPool));

	DestroyLifecycleTestWorld(World);
	return true;
}

/** 池归还完整清理：停 Timer、清绑定、隐藏、Owner 缓存清空；复用同一实例且不继承旧绑定。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponLifecyclePoolReleaseTest,
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

	UShooterWeaponRuntimeSubsystem* Runtime = World->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
	if (!TestNotNull(TEXT("Weapon runtime subsystem exists"), Runtime))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	const FName LifecycleRowName = AddTestWeaponRow(GetOrInjectRuntimeTestTable(World),
		MakeTestWeaponRow(AShooterWeaponLifecycleTestWeapon::StaticClass()));
	Runtime->InitializeWeaponRuntimeForTest();

	AShooterWeaponLifecycleTestWeapon* Weapon = Cast<AShooterWeaponLifecycleTestWeapon>(
		Runtime->AcquireWeapon(LifecycleRowName, Character, nullptr));
	if (!TestNotNull(TEXT("Weapon acquired from runtime pool"), Weapon))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	// 租用后进入装备态，布置待清理的 Timer。
	Weapon->BeginEquipTransaction();
	Weapon->ActivateWeapon();
	Weapon->ArmRefireTimerForTest();
	TestTrue(TEXT("Refire timer is active before release"), Weapon->IsRefireTimerActiveForTest());

	TestTrue(TEXT("Weapon releases to pool"), Runtime->ReleaseWeapon(Weapon));

	TestEqual(TEXT("Released weapon is InPool"), static_cast<int32>(Weapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::InPool));
	TestTrue(TEXT("Released weapon is hidden"), Weapon->IsHidden());
	TestFalse(TEXT("Refire timer is cleared on release"), Weapon->IsRefireTimerActiveForTest());
	TestFalse(TEXT("Runtime no longer leases released weapon"), Runtime->IsLeased(Weapon));
	TestEqual(TEXT("Runtime bucket holds one weapon"), Runtime->GetAvailableCount(LifecycleRowName), 1);

	// 复用同一实例：不继承旧绑定，并重新绑定到新 Owner（池取出回调负责 Owner/Instigator 重绑）。
	AShooterWeaponPresentationTestCharacter* SecondCharacter =
		World->SpawnActor<AShooterWeaponPresentationTestCharacter>(FVector(200.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Second lifecycle character spawned"), SecondCharacter))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	AShooterWeaponLifecycleTestWeapon* Reused = Cast<AShooterWeaponLifecycleTestWeapon>(
		Runtime->AcquireWeapon(LifecycleRowName, SecondCharacter, SecondCharacter));
	TestTrue(TEXT("Reacquire returns the same weapon"), Reused == Weapon);
	if (Reused)
	{
		// 租用后停在 Holstered：WeaponId 与静态配置保留，Owner 重新绑定。
		TestEqual(TEXT("Reused weapon waits at Holstered"), static_cast<int32>(Reused->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::Holstered));
		TestTrue(TEXT("Reused weapon rebinds new owner"), Reused->GetOwner() == SecondCharacter);
		TestTrue(TEXT("Reused weapon rebinds owner cache"), Reused->HasWeaponOwnerCacheForTest());
	}

	DestroyLifecycleTestWorld(World);
	return true;
}

/** 客户端 Owner 复制切换必须解除旧 Pawn 委托，避免旧 Pawn 销毁作用到已复用武器。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponLifecycleClientOwnerRebindTest,
	"ShootGame.Weapon.Lifecycle.ClientOwnerRebindClearsPreviousDelegate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponLifecycleClientOwnerRebindTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponLifecycleAutomationTests;

	UWorld* World = CreateLifecycleTestWorld();
	if (!TestNotNull(TEXT("Client owner rebind world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* FirstCharacter = SpawnLifecycleTestCharacter(*this, World);
	AShooterWeaponPresentationTestCharacter* SecondCharacter =
		World->SpawnActor<AShooterWeaponPresentationTestCharacter>(FVector(200.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	AShooterWeaponLifecycleTestWeapon* Weapon = World->SpawnActor<AShooterWeaponLifecycleTestWeapon>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!FirstCharacter || !TestNotNull(TEXT("Second client owner character spawned"), SecondCharacter) ||
		!TestNotNull(TEXT("Client owner rebind weapon spawned"), Weapon))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	// 模拟客户端先收到 Owner=A，再收到池化释放的 Owner=nullptr。
	Weapon->SimulateOwnerReplicationForTest(FirstCharacter);
	TestTrue(TEXT("First replicated owner is cached"), Weapon->HasCachedOwnerActorForTest(FirstCharacter));

	Weapon->SimulateOwnerReplicationForTest(nullptr);
	TestFalse(TEXT("Owner clear removes the first owner cache"), Weapon->HasCachedOwnerActorForTest(FirstCharacter));
	TestFalse(TEXT("Owner clear removes the holder cache"), Weapon->HasWeaponOwnerCacheForTest());

	// 同一 Actor 随后复用给 B；A 的延迟销毁通知不得再作用到该武器。
	Weapon->SimulateOwnerReplicationForTest(SecondCharacter);
	TestTrue(TEXT("Reused weapon caches the second owner"), Weapon->HasCachedOwnerActorForTest(SecondCharacter));

	FirstCharacter->OnDestroyed.Broadcast(FirstCharacter);
	TestFalse(TEXT("Old owner destruction does not destroy the reused weapon"), Weapon->IsActorBeingDestroyed());
	TestTrue(TEXT("Old owner destruction keeps the new owner"), Weapon->GetOwner() == SecondCharacter);
	TestTrue(TEXT("Old owner destruction keeps the new owner cache"), Weapon->HasCachedOwnerActorForTest(SecondCharacter));

	DestroyLifecycleTestWorld(World);
	return true;
}

/** 切枪语义：Inventory 内切枪只发生 Equipped <-> Holstered，不进入 InPool、不进池。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponLifecycleSwitchKeepsHolsteredTest,
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
	if (!TestNotNull(TEXT("Character owns inventory"), Inventory) || !TestNotNull(TEXT("Character owns equipment"), Equipment))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	EShooterInventoryAddResult PrimaryAddResult = EShooterInventoryAddResult::NotAuthoritative;
	AShooterWeapon* PrimaryWeapon = GrantTestWeapon(World, Inventory, AShooterInventoryOrderTestWeapon::StaticClass(),
		/*MagazineSize*/ 10,
		/*InitialReserveAmmo*/ -1,
		&PrimaryAddResult);
	TestEqual(TEXT("Primary weapon granted"), static_cast<int32>(PrimaryAddResult),
		static_cast<int32>(EShooterInventoryAddResult::Added));
	if (!TestNotNull(TEXT("Primary weapon actor exists"), PrimaryWeapon))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	EShooterInventoryAddResult SecondaryAddResult = EShooterInventoryAddResult::NotAuthoritative;
	AShooterWeapon* SecondaryWeapon = GrantTestWeapon(World, Inventory,
		AShooterWeaponPresentationTestWeaponSecondary::StaticClass(),
		/*MagazineSize*/ 10,
		/*InitialReserveAmmo*/ -1,
		&SecondaryAddResult);
	TestEqual(TEXT("Secondary weapon granted"), static_cast<int32>(SecondaryAddResult),
		static_cast<int32>(EShooterInventoryAddResult::Added));
	if (!TestNotNull(TEXT("Secondary weapon actor exists"), SecondaryWeapon))
	{
		DestroyLifecycleTestWorld(World);
		return false;
	}

	TestTrue(TEXT("Primary equipped"), Equipment->EquipWeapon(PrimaryWeapon));
	TestEqual(TEXT("Primary is Equipped"), static_cast<int32>(PrimaryWeapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::Equipped));

	// 切枪：旧武器回到 Holstered（仍持有、仍归 Inventory），不进池、不销毁。
	TestTrue(TEXT("Secondary equipped"), Equipment->EquipWeapon(SecondaryWeapon));
	TestEqual(TEXT("Switched-out primary is Holstered"), static_cast<int32>(PrimaryWeapon->GetLifecycleState()), static_cast<int32>(EShooterWeaponLifecycleState::Holstered));
	TestTrue(TEXT("Switched-out primary keeps its WeaponId"), !PrimaryWeapon->GetWeaponId().IsNone());
	TestTrue(TEXT("Switched-out primary stays in Inventory"), Inventory->ContainsWeapon(PrimaryWeapon));

	UShooterWeaponRuntimeSubsystem* Runtime = World->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
	TestFalse(TEXT("Switch keeps primary out of pool"), Runtime && Runtime->IsPooled(PrimaryWeapon));
	TestTrue(TEXT("Switch keeps primary leased"), Runtime && Runtime->IsLeased(PrimaryWeapon));

	// 同武器重复提交幂等，状态不变。
	TestTrue(TEXT("Re-equip secondary is idempotent"), Equipment->EquipWeapon(SecondaryWeapon));
	TestEqual(TEXT("Re-equipped secondary stays Equipped"),
		static_cast<int32>(SecondaryWeapon ? SecondaryWeapon->GetLifecycleState() : EShooterWeaponLifecycleState::InPool),
		static_cast<int32>(EShooterWeaponLifecycleState::Equipped));

	DestroyLifecycleTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
