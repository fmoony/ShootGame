// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "Misc/AutomationTest.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Tests/Weapon/ShooterWeaponRuntimeTestTypes.h"
#include "Weapons/Data/ShooterWeaponConfigRow.h"
#include "Weapons/Data/ShooterWeaponTable.h"
#include "Weapons/Animation/ShooterAnimNotify_WeaponMagazine.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/Subsystems/ShooterWeaponRuntimeSubsystem.h"

namespace ShooterWeaponMagazinePresentationAutomationTests
{
	const FName MagazineSocketName(TEXT("MagazineSocket"));
	const TCHAR* RifleMagazineMeshPath = TEXT("/Game/Weapons/Rifle/Meshes/SM_Rifle_Magazine.SM_Rifle_Magazine");
	const TCHAR* RifleReloadSequencePath = TEXT("/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Reload.MM_Rifle_Reload");
	const TCHAR* ShooterCharacterClassPath = TEXT("/Game/Shooter/Blueprints/Characters/BP_ShooterCharacter.BP_ShooterCharacter_C");

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

/** Rifle 的 TP Reload Sequence 是弹匣表现唯一时间源，并同时包含 Detach / Insert Notify。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterWeaponMagazineNotifyConfigurationTest,
	"ShootGame.Weapon.Magazine.RifleReloadNotifyConfiguration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponMagazineNotifyConfigurationTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponMagazinePresentationAutomationTests;

	const UAnimSequence* ReloadSequence = LoadObject<UAnimSequence>(nullptr, RifleReloadSequencePath);
	if (!TestNotNull(TEXT("Rifle reload sequence for Magazine Notify resolves"), ReloadSequence))
	{
		return false;
	}

	bool bHasDetachNotify = false;
	bool bHasInsertNotify = false;
	for (const FAnimNotifyEvent& NotifyEvent : ReloadSequence->Notifies)
	{
		const UShooterAnimNotify_WeaponMagazine* MagazineNotify = Cast<UShooterAnimNotify_WeaponMagazine>(NotifyEvent.Notify);
		if (!MagazineNotify)
		{
			continue;
		}

		bHasDetachNotify |= MagazineNotify->Stage == EShooterMagazinePresentationStage::Detach;
		bHasInsertNotify |= MagazineNotify->Stage == EShooterMagazinePresentationStage::Insert;
	}

	TestTrue(TEXT("Rifle reload sequence contains Magazine Detach Notify"), bHasDetachNotify);
	TestTrue(TEXT("Rifle reload sequence contains Magazine Insert Notify"), bHasInsertNotify);
	return bHasDetachNotify && bHasInsertNotify;
}

/** Pistol 使用独立 TP Sequence，必须且只能包含顺序正确的两个弹匣阶段。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterPistolMagazineNotifyConfigurationTest,
	"ShootGame.Weapon.Magazine.PistolReloadNotifyConfiguration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterPistolMagazineNotifyConfigurationTest::RunTest(const FString& Parameters)
{
	const UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/Characters/Mannequins/Anims/Pistol/MM_Pistol_Reload.MM_Pistol_Reload"));
	if (!TestNotNull(TEXT("Pistol reload sequence resolves"), Sequence))
	{
		return false;
	}

	int32 DetachCount = 0;
	int32 InsertCount = 0;
	float DetachTime = -1.0f;
	float InsertTime = -1.0f;
	for (const FAnimNotifyEvent& Event : Sequence->Notifies)
	{
		const UShooterAnimNotify_WeaponMagazine* Notify = Cast<UShooterAnimNotify_WeaponMagazine>(Event.Notify);
		if (!Notify)
		{
			continue;
		}
		if (Notify->Stage == EShooterMagazinePresentationStage::Detach)
		{
			++DetachCount;
			DetachTime = Event.GetTriggerTime();
		}
		else
		{
			++InsertCount;
			InsertTime = Event.GetTriggerTime();
		}
	}
	TestEqual(TEXT("Pistol has exactly one Detach Notify"), DetachCount, 1);
	TestEqual(TEXT("Pistol has exactly one Insert Notify"), InsertCount, 1);
	TestTrue(TEXT("Pistol Magazine Notify order is valid"),
		DetachTime > 0.0f && InsertTime > DetachTime && InsertTime < Sequence->GetPlayLength());
	return true;
}

/** 用正式四枪行配置租用实体，验证实际网格、抓握与恢复；不注入替代武器配置。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterProductionMagazinePresentationTest,
	"ShootGame.Weapon.Magazine.ProductionWeaponsPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterProductionMagazinePresentationTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponMagazinePresentationAutomationTests;
	UWorld* World = CreateTestWorld();
	if (!TestNotNull(TEXT("Production Magazine world exists"), World))
	{
		return false;
	}
	UShooterWeaponRuntimeSubsystem* Runtime = World->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
	if (!Runtime || !TestTrue(TEXT("Production weapon runtime initializes"), Runtime->InitializeWeaponRuntime()))
	{
		DestroyTestWorld(World);
		return false;
	}
	UClass* CharacterClass = StaticLoadClass(AShooterCharacter::StaticClass(), nullptr, ShooterCharacterClassPath);
	AShooterCharacter* Character = CharacterClass
		? Cast<AShooterCharacter>(World->SpawnActor(CharacterClass))
		: nullptr;
	if (!TestNotNull(TEXT("Production Magazine character exists"), Character))
	{
		DestroyTestWorld(World);
		return false;
	}

	for (const FName WeaponId : {FName(TEXT("Rifle")), FName(TEXT("Pistol")),
		 FName(TEXT("AWP")), FName(TEXT("GrenadeLauncher"))})
	{
		AddInfo(FString::Printf(TEXT("Magazine production weapon: %s"), *WeaponId.ToString()));
		const FShooterWeaponConfigRow* Row = Runtime->FindRuntimeConfig(WeaponId);
		AShooterWeapon* Weapon = Runtime->AcquireWeapon(WeaponId, Character, Character);
		if (!TestNotNull(TEXT("Production row exists"), Row) || !TestNotNull(TEXT("Production weapon acquired"), Weapon))
		{
			DestroyTestWorld(World);
			return false;
		}
		const FShooterWeaponConfigRow Captured = Weapon->CaptureWeaponConfigRow();
		TestTrue(TEXT("FP Grip survives production row application"),
			Captured.FirstPersonMagazineGripTransform.Equals(Row->FirstPersonMagazineGripTransform));
		TestTrue(TEXT("TP Grip survives production row application"),
			Captured.ThirdPersonMagazineGripTransform.Equals(Row->ThirdPersonMagazineGripTransform));
		TestTrue(TEXT("FP unit scale"), Row->FirstPersonMagazineGripTransform.GetScale3D().Equals(FVector::OneVector));
		TestTrue(TEXT("TP unit scale"), Row->ThirdPersonMagazineGripTransform.GetScale3D().Equals(FVector::OneVector));
		const int32 AmmoBefore = Weapon->GetBulletCount();
		const int32 ReserveBefore = Weapon->GetReserveAmmo();
		UStaticMeshComponent* FPProxy = Weapon->GetFirstPersonMagazineProxy();
		UStaticMeshComponent* TPProxy = Weapon->GetThirdPersonMagazineProxy();
		TestTrue(TEXT("Production proxies start hidden"), FPProxy->bHiddenInGame && TPProxy->bHiddenInGame);
		TestTrue(TEXT("FP uses MagazineMesh"), FPProxy->GetStaticMesh() == Row->MagazineMesh.LoadSynchronous());
		TestEqual(TEXT("TP proxy uses production MagazineMesh"), TPProxy->GetStaticMesh(), FPProxy->GetStaticMesh());

		for (USkeletalMeshComponent* Mesh : {Weapon->GetFirstPersonMesh(), Weapon->GetThirdPersonMesh()})
		{
			TestTrue(TEXT("Mesh has MagazineSocket"), Mesh->DoesSocketExist(MagazineSocketName));
			const FName Bone = Mesh->GetSocketBoneName(MagazineSocketName);
			TestTrue(TEXT("Socket parent bone is valid"), !Bone.IsNone() && Mesh->GetBoneIndex(Bone) != INDEX_NONE);
		}
		Weapon->ShowMagazineProxyInPlace();
		TestTrue(TEXT("Production Show displays both proxies"), FPProxy->IsVisible() && TPProxy->IsVisible());
		for (USkeletalMeshComponent* Mesh : {Weapon->GetFirstPersonMesh(), Weapon->GetThirdPersonMesh()})
		{
			TestTrue(TEXT("Show hides production Magazine bone"),
				Mesh->IsBoneHiddenByName(Mesh->GetSocketBoneName(MagazineSocketName)));
		}
		Weapon->ResetMagazinePresentation();
		TestTrue(TEXT("FP Detach succeeds"), Weapon->DetachFirstPersonMagazineProxy(Character->GetFirstPersonMesh()));
		TestTrue(TEXT("Production repeated FP Detach is safe"),
			Weapon->DetachFirstPersonMagazineProxy(Character->GetFirstPersonMesh()));
		TestTrue(TEXT("Production FP uses configured local Grip"),
			FPProxy->GetRelativeTransform().Equals(Row->FirstPersonMagazineGripTransform));
		TestTrue(TEXT("FP proxy attaches to FP hand_l"),
			FPProxy->GetAttachParent() == Character->GetFirstPersonMesh() &&
			FPProxy->GetAttachSocketName() == FName(TEXT("hand_l")));
		TestTrue(TEXT("FP Detach hides source Magazine bone"), Weapon->GetFirstPersonMesh()->IsBoneHiddenByName(
			Weapon->GetFirstPersonMesh()->GetSocketBoneName(MagazineSocketName)));
		TestFalse(TEXT("FP Detach leaves TP proxy hidden"), TPProxy->IsVisible());
		TestTrue(TEXT("Production FP Insert succeeds"), Weapon->InsertFirstPersonMagazineProxy());
		TestTrue(TEXT("Production TP Detach succeeds"), Weapon->DetachThirdPersonMagazineProxy(Character->GetMesh()));
		TestTrue(TEXT("Repeated TP Detach is safe"), Weapon->DetachThirdPersonMagazineProxy(Character->GetMesh()));
		TestTrue(TEXT("Production TP uses configured local Grip"),
			TPProxy->GetRelativeTransform().Equals(Row->ThirdPersonMagazineGripTransform));
		TestTrue(TEXT("TP proxy attaches to TP hand_l"), TPProxy->GetAttachParent() == Character->GetMesh() &&
			TPProxy->GetAttachSocketName() == FName(TEXT("hand_l")));
		TestTrue(TEXT("TP Detach hides source Magazine bone"), Weapon->GetThirdPersonMesh()->IsBoneHiddenByName(
			Weapon->GetThirdPersonMesh()->GetSocketBoneName(MagazineSocketName)));
		TestFalse(TEXT("TP Detach leaves FP proxy hidden"), FPProxy->IsVisible());
		TestTrue(TEXT("Production TP Insert succeeds"), Weapon->InsertThirdPersonMagazineProxy());
		Weapon->ResetMagazinePresentation();
		Weapon->ResetMagazinePresentation();
		for (UStaticMeshComponent* Proxy : {FPProxy, TPProxy})
		{
			TestTrue(TEXT("Reset hides proxy and restores root attachment with Identity"),
				Proxy->bHiddenInGame && !Proxy->IsVisible() && Proxy->GetAttachParent() == Weapon->GetRootComponent() &&
				Proxy->GetRelativeTransform().Equals(FTransform::Identity));
		}
		for (USkeletalMeshComponent* Mesh : {Weapon->GetFirstPersonMesh(), Weapon->GetThirdPersonMesh()})
		{
			TestFalse(TEXT("Reset unhides production Magazine bone"),
				Mesh->IsBoneHiddenByName(Mesh->GetSocketBoneName(MagazineSocketName)));
		}
		TestEqual(TEXT("Production Magazine presentation preserves Ammo"), Weapon->GetBulletCount(), AmmoBefore);
		TestEqual(TEXT("Presentation preserves ReserveAmmo"), Weapon->GetReserveAmmo(), ReserveBefore);
		Runtime->ReleaseWeapon(Weapon);
	}
	DestroyTestWorld(World);
	return true;
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
	const FTransform ExpectedFirstPersonGrip(FRotator(5.0f, 10.0f, 15.0f), FVector(1.0f, 2.0f, 3.0f), FVector::OneVector);
	const FTransform ExpectedThirdPersonGrip(FRotator(-8.0f, 20.0f, -12.0f), FVector(-2.0f, 1.5f, 0.5f), FVector::OneVector);
	TestRow.FirstPersonMagazineGripTransform = ExpectedFirstPersonGrip;
	TestRow.ThirdPersonMagazineGripTransform = ExpectedThirdPersonGrip;
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
	TestTrue(TEXT("Runtime snapshot keeps first-person MagazineGripTransform"),
		RuntimeRow->FirstPersonMagazineGripTransform.Equals(ExpectedFirstPersonGrip, 0.001f));
	TestTrue(TEXT("Runtime snapshot keeps third-person MagazineGripTransform"),
		RuntimeRow->ThirdPersonMagazineGripTransform.Equals(ExpectedThirdPersonGrip, 0.001f));

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
	TestTrue(TEXT("CaptureWeaponConfigRow preserves first-person MagazineGripTransform"),
		CapturedRow.FirstPersonMagazineGripTransform.Equals(ExpectedFirstPersonGrip, 0.001f));
	TestTrue(TEXT("CaptureWeaponConfigRow preserves third-person MagazineGripTransform"),
		CapturedRow.ThirdPersonMagazineGripTransform.Equals(ExpectedThirdPersonGrip, 0.001f));

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
	const FTransform ExpectedFirstPersonGrip(FRotator(7.0f, -13.0f, 21.0f), FVector(2.0f, -1.0f, 3.0f), FVector::OneVector);
	const FTransform ExpectedThirdPersonGrip(FRotator(-11.0f, 17.0f, 9.0f), FVector(-1.5f, 2.5f, 0.75f), FVector::OneVector);
	Weapon->SetMagazineGripTransformsForAutomationTest(ExpectedFirstPersonGrip, ExpectedThirdPersonGrip);
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

	UClass* ShooterCharacterClass = StaticLoadClass(AShooterCharacter::StaticClass(), nullptr, ShooterCharacterClassPath);
	const FVector CharacterSpawnLocation(100.0f, 0.0f, 0.0f);
	const FRotator CharacterSpawnRotation = FRotator::ZeroRotator;
	AActor* SpawnedCharacter = ShooterCharacterClass
		? World->SpawnActor(ShooterCharacterClass, &CharacterSpawnLocation, &CharacterSpawnRotation)
		: nullptr;
	AShooterCharacter* PresentationCharacter = static_cast<AShooterCharacter*>(SpawnedCharacter);
	TestNotNull(TEXT("Magazine hand test character exists"), PresentationCharacter);
	if (!PresentationCharacter || !PresentationCharacter->GetMesh() || !PresentationCharacter->GetFirstPersonMesh())
	{
		DestroyTestWorld(World);
		return false;
	}

	TestNotNull(TEXT("Magazine hand test character third-person mesh resolves"),
		PresentationCharacter->GetMesh()->GetSkeletalMeshAsset());
	TestNotNull(TEXT("Magazine hand test character first-person mesh resolves"),
		PresentationCharacter->GetFirstPersonMesh()->GetSkeletalMeshAsset());
	TestTrue(TEXT("Third-person Character mesh contains hand_l"),
		PresentationCharacter->GetMesh()->GetBoneIndex(FName(TEXT("hand_l"))) != INDEX_NONE);
	TestTrue(TEXT("First-person Character mesh contains hand_l"),
		PresentationCharacter->GetFirstPersonMesh()->GetBoneIndex(FName(TEXT("hand_l"))) != INDEX_NONE);

	Weapon->ResetMagazinePresentation();
	const FTransform FirstPersonHandTransform =
		PresentationCharacter->GetFirstPersonMesh()->GetSocketTransform(FName(TEXT("hand_l")), RTS_World);
	TestTrue(TEXT("First-person detach succeeds"),
		Weapon->DetachFirstPersonMagazineProxy(PresentationCharacter->GetFirstPersonMesh()));
	TestTrue(TEXT("First-person proxy attaches to Character hand_l"),
		FirstPersonProxy->GetAttachParent() == PresentationCharacter->GetFirstPersonMesh() &&
		FirstPersonProxy->GetAttachSocketName() == FName(TEXT("hand_l")));
	TestTrue(TEXT("First-person detach applies final hand_l relative GripTransform"),
		FirstPersonProxy->GetRelativeTransform().Equals(ExpectedFirstPersonGrip, 0.01f));
	TestTrue(TEXT("First-person detach applies GripTransform in hand_l space"),
		FirstPersonProxy->GetComponentTransform().Equals(ExpectedFirstPersonGrip * FirstPersonHandTransform, 0.01f));
	TestTrue(TEXT("First-person detach hides source Magazine bone"),
		Weapon->GetFirstPersonMesh()->IsBoneHiddenByName(FirstPersonParentBone));
	TestTrue(TEXT("Third-person side is unchanged during first-person detach"),
		!ThirdPersonProxy->IsVisible() && !Weapon->GetThirdPersonMesh()->IsBoneHiddenByName(ThirdPersonParentBone));

	TestTrue(TEXT("First-person insert succeeds"), Weapon->InsertFirstPersonMagazineProxy());
	TestFalse(TEXT("First-person insert unhides source Magazine bone"),
		Weapon->GetFirstPersonMesh()->IsBoneHiddenByName(FirstPersonParentBone));
	TestTrue(TEXT("First-person insert hides proxy"),
		FirstPersonProxy->bHiddenInGame && !FirstPersonProxy->IsVisible());
	TestTrue(TEXT("First-person insert restores proxy to Weapon Root"),
		FirstPersonProxy->GetAttachParent() == Weapon->GetRootComponent() &&
		FirstPersonProxy->GetRelativeTransform().Equals(FTransform::Identity, 0.01f));

	Weapon->ResetMagazinePresentation();
	const FTransform ThirdPersonHandTransform =
		PresentationCharacter->GetMesh()->GetSocketTransform(FName(TEXT("hand_l")), RTS_World);
	TestTrue(TEXT("Third-person detach succeeds"),
		Weapon->DetachThirdPersonMagazineProxy(PresentationCharacter->GetMesh()));
	TestTrue(TEXT("Third-person proxy attaches to Character hand_l"),
		ThirdPersonProxy->GetAttachParent() == PresentationCharacter->GetMesh() &&
		ThirdPersonProxy->GetAttachSocketName() == FName(TEXT("hand_l")));
	TestTrue(TEXT("Third-person detach applies final hand_l relative GripTransform"),
		ThirdPersonProxy->GetRelativeTransform().Equals(ExpectedThirdPersonGrip, 0.01f));
	TestTrue(TEXT("Third-person detach applies GripTransform in hand_l space"),
		ThirdPersonProxy->GetComponentTransform().Equals(ExpectedThirdPersonGrip * ThirdPersonHandTransform, 0.01f));
	TestTrue(TEXT("Third-person detach hides source Magazine bone"),
		Weapon->GetThirdPersonMesh()->IsBoneHiddenByName(ThirdPersonParentBone));

	Weapon->ResetMagazinePresentation();
	TestTrue(TEXT("Reset clears first-person hand attachment"),
		FirstPersonProxy->GetAttachParent() == Weapon->GetRootComponent());
	TestTrue(TEXT("Reset clears third-person hand attachment"),
		ThirdPersonProxy->GetAttachParent() == Weapon->GetRootComponent());
	TestFalse(TEXT("Reset restores first-person source Magazine bone"),
		Weapon->GetFirstPersonMesh()->IsBoneHiddenByName(FirstPersonParentBone));
	TestFalse(TEXT("Reset restores third-person source Magazine bone"),
		Weapon->GetThirdPersonMesh()->IsBoneHiddenByName(ThirdPersonParentBone));
	TestEqual(TEXT("Side presentation keeps MagazineAmmo unchanged"), Weapon->GetBulletCount(), 7);
	TestEqual(TEXT("Side presentation keeps ReserveAmmo unchanged"), Weapon->GetReserveAmmo(), 13);

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
	const FProperty* FirstPersonGripProperty =
		FindFProperty<FProperty>(AShooterWeapon::StaticClass(), TEXT("FirstPersonMagazineGripTransform"));
	const FProperty* ThirdPersonGripProperty =
		FindFProperty<FProperty>(AShooterWeapon::StaticClass(), TEXT("ThirdPersonMagazineGripTransform"));
	TestTrue(TEXT("First-person MagazineGripTransform property exists"), FirstPersonGripProperty != nullptr);
	TestTrue(TEXT("Third-person MagazineGripTransform property exists"), ThirdPersonGripProperty != nullptr);
	TestFalse(TEXT("First-person MagazineGripTransform is not replicated"),
		FirstPersonGripProperty && FirstPersonGripProperty->HasAnyPropertyFlags(CPF_Net));
	TestFalse(TEXT("Third-person MagazineGripTransform is not replicated"),
		ThirdPersonGripProperty && ThirdPersonGripProperty->HasAnyPropertyFlags(CPF_Net));
	const UFunction* ShowFunction = AShooterWeapon::StaticClass()->FindFunctionByName(TEXT("ShowMagazineProxyInPlace"));
	const UFunction* DetachFirstPersonFunction = AShooterWeapon::StaticClass()->FindFunctionByName(TEXT("DetachFirstPersonMagazineProxy"));
	const UFunction* InsertFirstPersonFunction = AShooterWeapon::StaticClass()->FindFunctionByName(TEXT("InsertFirstPersonMagazineProxy"));
	const UFunction* DetachThirdPersonFunction = AShooterWeapon::StaticClass()->FindFunctionByName(TEXT("DetachThirdPersonMagazineProxy"));
	const UFunction* InsertThirdPersonFunction = AShooterWeapon::StaticClass()->FindFunctionByName(TEXT("InsertThirdPersonMagazineProxy"));
	const UFunction* ResetFunction = AShooterWeapon::StaticClass()->FindFunctionByName(TEXT("ResetMagazinePresentation"));
	TestFalse(TEXT("ShowMagazineProxyInPlace is not an RPC"),
		ShowFunction && ShowFunction->HasAnyFunctionFlags(FUNC_Net));
	TestFalse(TEXT("ResetMagazinePresentation is not an RPC"),
		ResetFunction && ResetFunction->HasAnyFunctionFlags(FUNC_Net));
	TestFalse(TEXT("DetachFirstPersonMagazineProxy is not an RPC"),
		DetachFirstPersonFunction && DetachFirstPersonFunction->HasAnyFunctionFlags(FUNC_Net));
	TestFalse(TEXT("InsertFirstPersonMagazineProxy is not an RPC"),
		InsertFirstPersonFunction && InsertFirstPersonFunction->HasAnyFunctionFlags(FUNC_Net));
	TestFalse(TEXT("DetachThirdPersonMagazineProxy is not an RPC"),
		DetachThirdPersonFunction && DetachThirdPersonFunction->HasAnyFunctionFlags(FUNC_Net));
	TestFalse(TEXT("InsertThirdPersonMagazineProxy is not an RPC"),
		InsertThirdPersonFunction && InsertThirdPersonFunction->HasAnyFunctionFlags(FUNC_Net));

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
