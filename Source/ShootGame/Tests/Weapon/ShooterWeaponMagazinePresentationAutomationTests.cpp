// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Tests/Weapon/ShooterWeaponRuntimeTestTypes.h"
#include "Weapons/Data/ShooterWeaponConfigRow.h"
#include "Weapons/Data/ShooterWeaponTable.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/Subsystems/ShooterWeaponRuntimeSubsystem.h"

namespace ShooterWeaponMagazinePresentationAutomationTests
{
	const FName MagazineSocketName(TEXT("MagazineSocket"));
	const TCHAR* RifleMagazineMeshPath = TEXT("/Game/Weapons/Rifle/Meshes/SM_Rifle_Magazine.SM_Rifle_Magazine");

	UWorld* CreateTestWorld()
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

	void DestroyTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	const FShooterWeaponConfigRow* ResolveRifleRow(FAutomationTestBase& Test)
	{
		const UDataTable* Table = ShooterWeaponTable::ResolveWeaponTable();
		if (!Test.TestNotNull(TEXT("Production weapon table resolves"), Table))
		{
			return nullptr;
		}

		const FShooterWeaponConfigRow* Row = ShooterWeaponTable::FindWeaponRow(Table, FName(TEXT("Rifle")));
		Test.TestNotNull(TEXT("Production Rifle row resolves"), Row);
		return Row;
	}
}

/** MagazineMesh 应在行配置进入运行时快照并回写 Actor 后保持同一资源引用。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponMagazineConfigRoundTripTest, "ShootGame.Weapon.Magazine.ConfigRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponMagazineConfigRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponMagazinePresentationAutomationTests;

	UWorld* World = CreateTestWorld();
	if (!TestNotNull(TEXT("Magazine config test world created"), World))
	{
		return false;
	}

	const FShooterWeaponConfigRow* RifleRow = ResolveRifleRow(*this);
	UStaticMesh* MagazineMesh = LoadObject<UStaticMesh>(nullptr, RifleMagazineMeshPath);
	if (!TestNotNull(TEXT("Rifle MagazineMesh asset resolves"), MagazineMesh) || !RifleRow)
	{
		DestroyTestWorld(World);
		return false;
	}

	TestEqual(TEXT("Rifle row uses /Game/Weapons magazine"),
		RifleRow->MagazineMesh.ToSoftObjectPath().ToString(), FString(RifleMagazineMeshPath));

	UShooterWeaponRuntimeSubsystem* Runtime = World->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
	if (!TestNotNull(TEXT("Magazine config runtime subsystem exists"), Runtime))
	{
		DestroyTestWorld(World);
		return false;
	}

	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
	Table->RowStruct = FShooterWeaponConfigRow::StaticStruct();
	FShooterWeaponConfigRow TestRow = *RifleRow;
	TestRow.WeaponActorClass = AShooterRuntimePoolTestWeapon::StaticClass();
	TestRow.InitialPoolSize = 1;
	TestRow.MagazineMesh = MagazineMesh;
	Table->AddRow(FName(TEXT("MagazineRoundTrip")), TestRow);
	Runtime->SetWeaponTableOverride(Table);
	World->BeginPlay();

	const FShooterWeaponConfigRow* RuntimeRow = Runtime->FindRuntimeConfig(TEXT("MagazineRoundTrip"));
	if (!TestNotNull(TEXT("Runtime snapshot keeps MagazineMesh"), RuntimeRow))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("Runtime snapshot keeps MagazineMesh reference"), RuntimeRow->MagazineMesh.Get(), MagazineMesh);

	AActor* Owner = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	AShooterWeapon* Weapon = Runtime->AcquireWeapon(TEXT("MagazineRoundTrip"), Owner, nullptr);
	if (!TestNotNull(TEXT("Magazine round-trip weapon acquired"), Weapon))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestTrue(TEXT("First-person proxy receives MagazineMesh"),
		Weapon->GetFirstPersonMagazineProxy()->GetStaticMesh() == MagazineMesh);
	TestTrue(TEXT("Third-person proxy receives MagazineMesh"),
		Weapon->GetThirdPersonMagazineProxy()->GetStaticMesh() == MagazineMesh);
	const FShooterWeaponConfigRow CapturedRow = Weapon->CaptureWeaponConfigRow();
	TestEqual(TEXT("CaptureWeaponConfigRow preserves MagazineMesh"), CapturedRow.MagazineMesh.Get(), MagazineMesh);

	DestroyTestWorld(World);
	return true;
}

/** 代理的视角过滤、Socket 父骨骼解析、Show/Reset、Ammo 不变和生命周期复位。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponMagazinePresentationTest,
	"ShootGame.Weapon.Magazine.PresentationShowReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponMagazinePresentationTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponMagazinePresentationAutomationTests;

	UWorld* World = CreateTestWorld();
	if (!TestNotNull(TEXT("Magazine presentation test world created"), World))
	{
		return false;
	}

	const FShooterWeaponConfigRow* RifleRow = ResolveRifleRow(*this);
	if (!RifleRow)
	{
		DestroyTestWorld(World);
		return false;
	}

	AShooterRuntimePoolTestWeapon* Weapon = World->SpawnActor<AShooterRuntimePoolTestWeapon>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Magazine presentation weapon spawned"), Weapon))
	{
		DestroyTestWorld(World);
		return false;
	}

	USkeletalMesh* FirstPersonMesh = RifleRow->FirstPersonMesh.LoadSynchronous();
	USkeletalMesh* ThirdPersonMesh = RifleRow->ThirdPersonMesh.LoadSynchronous();
	// 表资产写回由 ConfigRoundTrip 单独验证；表现测试直接加载已验收的资源，避免表未保存时掩盖代理行为。
	UStaticMesh* MagazineMesh = LoadObject<UStaticMesh>(nullptr, RifleMagazineMeshPath);
	Weapon->GetFirstPersonMesh()->SetSkeletalMeshAsset(FirstPersonMesh);
	Weapon->GetThirdPersonMesh()->SetSkeletalMeshAsset(ThirdPersonMesh);
	Weapon->GetFirstPersonMagazineProxy()->SetStaticMesh(MagazineMesh);
	Weapon->GetThirdPersonMagazineProxy()->SetStaticMesh(MagazineMesh);
	Weapon->SetAmmoForAutomationTest(7, 13);

	UStaticMeshComponent* FirstPersonProxy = Weapon->GetFirstPersonMagazineProxy();
	UStaticMeshComponent* ThirdPersonProxy = Weapon->GetThirdPersonMagazineProxy();
	TestNotNull(TEXT("First-person magazine proxy exists"), FirstPersonProxy);
	TestNotNull(TEXT("Third-person magazine proxy exists"), ThirdPersonProxy);
	if (!FirstPersonProxy || !ThirdPersonProxy || !FirstPersonMesh || !ThirdPersonMesh || !MagazineMesh)
	{
		DestroyTestWorld(World);
		return false;
	}

	TestTrue(TEXT("First-person proxy starts hidden"),
		FirstPersonProxy->bHiddenInGame && !FirstPersonProxy->IsVisible());
	TestTrue(TEXT("Third-person proxy starts hidden"),
		ThirdPersonProxy->bHiddenInGame && !ThirdPersonProxy->IsVisible());
	TestTrue(TEXT("First-person proxy is owner-only"),
		FirstPersonProxy->bOnlyOwnerSee && !FirstPersonProxy->bOwnerNoSee);
	TestTrue(TEXT("Third-person proxy is owner-hidden"),
		ThirdPersonProxy->bOwnerNoSee && !ThirdPersonProxy->bOnlyOwnerSee);
	TestEqual(TEXT("First-person proxy has no collision"),
		FirstPersonProxy->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestEqual(TEXT("Third-person proxy has no collision"),
		ThirdPersonProxy->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestFalse(TEXT("First-person proxy does not replicate"), FirstPersonProxy->GetIsReplicated());
	TestFalse(TEXT("Third-person proxy does not replicate"), ThirdPersonProxy->GetIsReplicated());

	const FName FirstPersonParentBone = Weapon->GetFirstPersonMesh()->GetSocketBoneName(MagazineSocketName);
	const FName ThirdPersonParentBone = Weapon->GetThirdPersonMesh()->GetSocketBoneName(MagazineSocketName);
	TestTrue(TEXT("First-person MagazineSocket resolves a parent bone"), !FirstPersonParentBone.IsNone());
	TestTrue(TEXT("Third-person MagazineSocket resolves a parent bone"), !ThirdPersonParentBone.IsNone());
	const FTransform FirstPersonSocketTransform =
		Weapon->GetFirstPersonMesh()->GetSocketTransform(MagazineSocketName, RTS_World);
	const FTransform ThirdPersonSocketTransform =
		Weapon->GetThirdPersonMesh()->GetSocketTransform(MagazineSocketName, RTS_World);

	Weapon->ShowMagazineProxyInPlace();
	TestTrue(TEXT("First-person proxy is visible after Show"),
		!FirstPersonProxy->bHiddenInGame && FirstPersonProxy->IsVisible());
	TestTrue(TEXT("Third-person proxy is visible after Show"),
		!ThirdPersonProxy->bHiddenInGame && ThirdPersonProxy->IsVisible());
	TestTrue(TEXT("First-person proxy aligns before hiding its bone"),
		FirstPersonProxy->GetComponentTransform().Equals(FirstPersonSocketTransform, 0.01f));
	TestTrue(TEXT("Third-person proxy aligns before hiding its bone"),
		ThirdPersonProxy->GetComponentTransform().Equals(ThirdPersonSocketTransform, 0.01f));
	TestTrue(TEXT("First-person MagazineSocket parent bone is hidden"),
		Weapon->GetFirstPersonMesh()->IsBoneHiddenByName(FirstPersonParentBone));
	TestTrue(TEXT("Third-person MagazineSocket parent bone is hidden"),
		Weapon->GetThirdPersonMesh()->IsBoneHiddenByName(ThirdPersonParentBone));
	TestEqual(TEXT("Show does not change MagazineAmmo"), Weapon->GetBulletCount(), 7);
	TestEqual(TEXT("Show does not change ReserveAmmo"), Weapon->GetReserveAmmo(), 13);

	Weapon->ResetMagazinePresentation();
	TestFalse(TEXT("Reset unhides first-person parent bone"),
		Weapon->GetFirstPersonMesh()->IsBoneHiddenByName(FirstPersonParentBone));
	TestFalse(TEXT("Reset unhides third-person parent bone"),
		Weapon->GetThirdPersonMesh()->IsBoneHiddenByName(ThirdPersonParentBone));
	TestTrue(TEXT("Reset hides first-person proxy"), FirstPersonProxy->bHiddenInGame && !FirstPersonProxy->IsVisible());
	TestTrue(TEXT("Reset hides third-person proxy"), ThirdPersonProxy->bHiddenInGame && !ThirdPersonProxy->IsVisible());
	TestTrue(TEXT("Reset restores first-person proxy to Weapon Root"),
		FirstPersonProxy->GetAttachParent() == Weapon->GetRootComponent() &&
		FirstPersonProxy->GetRelativeTransform().Equals(FTransform::Identity, 0.01f));
	TestTrue(TEXT("Reset restores third-person proxy to Weapon Root"),
		ThirdPersonProxy->GetAttachParent() == Weapon->GetRootComponent() &&
		ThirdPersonProxy->GetRelativeTransform().Equals(FTransform::Identity, 0.01f));
	TestEqual(TEXT("Reset does not change MagazineAmmo"), Weapon->GetBulletCount(), 7);
	TestEqual(TEXT("Reset does not change ReserveAmmo"), Weapon->GetReserveAmmo(), 13);

	// 幂等复位：重复调用不重新显示、不卡在临时附着或隐藏骨骼状态。
	Weapon->ResetMagazinePresentation();
	TestTrue(TEXT("Repeated Reset keeps first-person proxy hidden"),
		FirstPersonProxy->bHiddenInGame && !FirstPersonProxy->IsVisible());
	TestTrue(TEXT("Repeated Reset keeps third-person proxy hidden"),
		ThirdPersonProxy->bHiddenInGame && !ThirdPersonProxy->IsVisible());

	// 生命周期清理即使武器当前处于 InPool 也必须复位，不依赖 Deactivate 的状态转换分支。
	Weapon->ShowMagazineProxyInPlace();
	Weapon->DeactivateWeapon();
	TestTrue(TEXT("Deactivate resets magazine proxy"),
		FirstPersonProxy->bHiddenInGame && ThirdPersonProxy->bHiddenInGame);

	const FProperty* FirstPersonProxyProperty =
		FindFProperty<FProperty>(AShooterWeapon::StaticClass(), TEXT("FirstPersonMagazineProxy"));
	const FProperty* ThirdPersonProxyProperty =
		FindFProperty<FProperty>(AShooterWeapon::StaticClass(), TEXT("ThirdPersonMagazineProxy"));
	TestTrue(TEXT("First-person proxy property exists"), FirstPersonProxyProperty != nullptr);
	TestTrue(TEXT("Third-person proxy property exists"), ThirdPersonProxyProperty != nullptr);
	TestFalse(TEXT("First-person proxy property is not replicated"),
		FirstPersonProxyProperty && FirstPersonProxyProperty->HasAnyPropertyFlags(CPF_Net));
	TestFalse(TEXT("Third-person proxy property is not replicated"),
		ThirdPersonProxyProperty && ThirdPersonProxyProperty->HasAnyPropertyFlags(CPF_Net));
	const UFunction* ShowFunction = AShooterWeapon::StaticClass()->FindFunctionByName(TEXT("ShowMagazineProxyInPlace"));
	const UFunction* ResetFunction = AShooterWeapon::StaticClass()->FindFunctionByName(TEXT("ResetMagazinePresentation"));
	TestFalse(TEXT("ShowMagazineProxyInPlace is not an RPC"),
		ShowFunction && ShowFunction->HasAnyFunctionFlags(FUNC_Net));
	TestFalse(TEXT("ResetMagazinePresentation is not an RPC"),
		ResetFunction && ResetFunction->HasAnyFunctionFlags(FUNC_Net));

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
