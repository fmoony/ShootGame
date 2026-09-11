// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "Weapons/Definitions/ShooterWeaponDefinition.h"
#include "Weapons/ShooterProjectile.h"
#include "Weapons/ShooterProjectileFireBehavior.h"
#include "Weapons/ShooterWeapon.h"
#include "../Equipment/ShooterWeaponPresentationTestTypes.h"

namespace ShooterProjectileFireBehaviorAutomationTests
{
	UWorld* CreateFireBehaviorTestWorld()
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

	void DestroyFireBehaviorTestWorld(UWorld* World)
	{
		if (!World || !GEngine)
		{
			return;
		}

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	int32 CountProjectiles(UWorld* World, UClass* ProjectileClass)
	{
		int32 Count = 0;
		for (TActorIterator<AShooterProjectile> It(World, ProjectileClass); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	UClass* LoadPistolBulletClass(FAutomationTestBase& Test)
	{
		UClass* PistolBulletClass = LoadObject<UClass>(
			nullptr,
			TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterProjectile_Bullet_Pistol.BP_ShooterProjectile_Bullet_Pistol_C"));
		Test.TestNotNull(TEXT("Pistol bullet class loaded"), PistolBulletClass);
		return PistolBulletClass;
	}
}

/**
 * A3 验证：Projectile FireBehavior 是弹丸生成唯一正式边界。
 * 服务器上下文恰好生成一个配置类的弹丸；缺 WeaponActor / 缺弹丸类 / 非服务器全部 fail closed。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterProjectileFireBehaviorSpawnTest,
	"ShootGame.Weapon.FireBehavior.ProjectileSpawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterProjectileFireBehaviorSpawnTest::RunTest(const FString& Parameters)
{
	using namespace ShooterProjectileFireBehaviorAutomationTests;

	UWorld* World = CreateFireBehaviorTestWorld();
	if (!TestNotNull(TEXT("Fire behavior world created"), World))
	{
		return false;
	}

	UClass* PistolBulletClass = LoadPistolBulletClass(*this);
	if (!PistolBulletClass)
	{
		DestroyFireBehaviorTestWorld(World);
		return false;
	}

	AShooterInventoryOrderTestWeapon* Weapon =
		World->SpawnActor<AShooterInventoryOrderTestWeapon>(
			FVector::ZeroVector,
			FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Fire behavior weapon spawned"), Weapon))
	{
		DestroyFireBehaviorTestWorld(World);
		return false;
	}

	// 行为要求 Instigator；用具体化测试角色充当持有者。
	APawn* InstigatorPawn = World->SpawnActor<AShooterWeaponPresentationTestCharacter>(
		FVector::ZeroVector,
		FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Fire behavior instigator spawned"), InstigatorPawn))
	{
		DestroyFireBehaviorTestWorld(World);
		return false;
	}

	UShooterProjectileFireBehavior* Behavior =
		NewObject<UShooterProjectileFireBehavior>(GetTransientPackage());
	Behavior->ProjectileClass = PistolBulletClass;

	FShooterWeaponFireContext Context;
	Context.WeaponActor = nullptr;
	Context.Instigator = InstigatorPawn;
	Context.MuzzleTransform = FTransform(FRotator::ZeroRotator, FVector(10.0f, 0.0f, 80.0f));

	// 缺 WeaponActor：不生成。
	Behavior->ExecuteFire(Context);
	TestEqual(TEXT("Null weapon actor spawns nothing"), CountProjectiles(World, PistolBulletClass), 0);

	Context.WeaponActor = Weapon;

	// 非服务器 Role：纵深防御 fail closed。
	Weapon->SetRole(ROLE_SimulatedProxy);
	Behavior->ExecuteFire(Context);
	TestEqual(TEXT("Non-authority weapon spawns nothing"), CountProjectiles(World, PistolBulletClass), 0);
	Weapon->SetRole(ROLE_Authority);

	// 缺弹丸类：不生成。
	TSubclassOf<AShooterProjectile> SavedClass = Behavior->ProjectileClass;
	Behavior->ProjectileClass = nullptr;
	Behavior->ExecuteFire(Context);
	TestEqual(TEXT("Missing projectile class spawns nothing"), CountProjectiles(World, PistolBulletClass), 0);
	Behavior->ProjectileClass = SavedClass;

	// 合法服务器上下文：恰好一个弹丸，且使用行为配置的类与上下文变换。
	Behavior->ExecuteFire(Context);
	TestEqual(TEXT("Valid server context spawns exactly one projectile"), CountProjectiles(World, PistolBulletClass), 1);

	for (TActorIterator<AShooterProjectile> It(World, PistolBulletClass); It; ++It)
	{
		TestEqual(
			TEXT("Projectile uses the context transform"),
			It->GetActorLocation(),
			FVector(10.0f, 0.0f, 80.0f));
	}

	DestroyFireBehaviorTestWorld(World);
	return true;
}

/**
 * A3 验证：WeaponActor 的开火行为解析完全由 Definition 驱动。
 * Definition 授予的武器解析到 Definition 内的行为实例（含其弹丸类）；
 * 适配入口（伪造 DefinitionId）授予的武器解析不到正式行为，回落旧路径。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterProjectileFireBehaviorDefinitionWiringTest,
	"ShootGame.Weapon.FireBehavior.DefinitionWiring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterProjectileFireBehaviorDefinitionWiringTest::RunTest(const FString& Parameters)
{
	using namespace ShooterProjectileFireBehaviorAutomationTests;

	const FPrimaryAssetId TestAutoId(
		UShooterWeaponDefinition::GetWeaponDefinitionAssetType(),
		FName(TEXT("WD_TestAuto")));
	UShooterWeaponDefinition* TestAuto =
		UShooterWeaponDefinition::ResolveDefinitionSync(TestAutoId);
	if (!TestNotNull(
		TEXT("WD_TestAuto resolves (run ShootGame.Tools.WeaponDefinition.CreateTestAsset first)"),
		TestAuto))
	{
		return false;
	}
	if (!TestTrue(TEXT("WD_TestAuto configures a fire behavior"), TestAuto->FireBehavior != nullptr))
	{
		return false;
	}

	UWorld* World = CreateFireBehaviorTestWorld();
	if (!TestNotNull(TEXT("Definition wiring world created"), World))
	{
		return false;
	}

	AShooterWeaponPresentationTestCharacter* Character =
		World->SpawnActor<AShooterWeaponPresentationTestCharacter>(
			FVector::ZeroVector,
			FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Wiring test character spawned"), Character))
	{
		DestroyFireBehaviorTestWorld(World);
		return false;
	}
	if (AWorldSettings* WorldSettings = World->GetWorldSettings())
	{
		WorldSettings->NotifyBeginPlay();
	}

	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	if (!TestNotNull(TEXT("Character owns inventory"), Inventory))
	{
		DestroyFireBehaviorTestWorld(World);
		return false;
	}

	// Definition 授予：解析到 Definition 的行为实例。
	FGuid DefinitionGrantedId;
	TestEqual(
		TEXT("WD_TestAuto grant succeeds"),
		static_cast<int32>(Inventory->TryAddWeaponDefinition(TestAuto, DefinitionGrantedId)),
		static_cast<int32>(EShooterInventoryAddResult::Added));
	AShooterWeapon* DefinitionWeapon = Inventory->FindWeaponActor(DefinitionGrantedId);
	if (TestNotNull(TEXT("Definition weapon actor exists"), DefinitionWeapon))
	{
		TestTrue(
			TEXT("Weapon resolves the definition fire behavior"),
			DefinitionWeapon->ResolveFireBehavior() == TestAuto->FireBehavior.Get());

		const UShooterProjectileFireBehavior* ProjectileBehavior =
			Cast<UShooterProjectileFireBehavior>(DefinitionWeapon->ResolveFireBehavior());
		TestTrue(
			TEXT("Definition behavior is the projectile behavior"),
			ProjectileBehavior != nullptr);
		UClass* PistolBulletClass = LoadPistolBulletClass(*this);
		TestTrue(
			TEXT("Behavior projectile class matches the definition configuration"),
			ProjectileBehavior && PistolBulletClass &&
			ProjectileBehavior->ProjectileClass == PistolBulletClass);
	}

	// 内存 Definition（ID 不在 AssetManager）授予：兼容路径，解析不到正式行为。
	FGuid CompatGrantedId;
	TestEqual(
		TEXT("Compat grant succeeds"),
		static_cast<int32>(Inventory->TryAddWeaponDefinition(
			MakeShooterTestWeaponDefinition(TEXT("WD_FireBehaviorCompat"), AShooterInventoryOrderTestWeapon::StaticClass()),
			CompatGrantedId)),
		static_cast<int32>(EShooterInventoryAddResult::Added));
	AShooterWeapon* CompatWeapon = Inventory->FindWeaponActor(CompatGrantedId);
	if (TestNotNull(TEXT("Compat weapon actor exists"), CompatWeapon))
	{
		TestNull(
			TEXT("Compat-granted weapon resolves no formal behavior"),
			CompatWeapon->ResolveFireBehavior());
	}

	DestroyFireBehaviorTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
