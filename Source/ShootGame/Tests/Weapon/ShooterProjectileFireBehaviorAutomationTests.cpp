// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Weapons/Projectile/ShooterProjectile.h"
#include "Weapons/Projectile/ShooterProjectileFireBehavior.h"
#include "Weapons/ShooterWeapon.h"
#include "Tests/Equipment/ShooterWeaponPresentationTestTypes.h"

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
		UClass* PistolBulletClass = LoadObject<UClass>(nullptr,
			TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterProjectile_Bullet_Pistol.BP_ShooterProjectile_Bullet_Pistol_C"));
		Test.TestNotNull(TEXT("Pistol bullet class loaded"), PistolBulletClass);
		return PistolBulletClass;
	}
}

/**
 * 休眠定义守卫：Projectile FireBehavior 仍按上下文生成弹丸。
 * 服务器上下文恰好生成一个上下文声明的弹丸类；缺 WeaponActor / 缺弹丸类 / 非服务器全部 fail closed。
 * 上下文只携带弹丸类这一条武器派生参数，不再携带整行武器配置快照。
 * 该行为当前未接入产品路径（WeaponActor 直接生成自己的 ProjectileClass），本用例只守定义本身。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterProjectileFireBehaviorSpawnTest,
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

	AShooterInventoryOrderTestWeapon* Weapon = World->SpawnActor<AShooterInventoryOrderTestWeapon>(FVector::ZeroVector,
			FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Fire behavior weapon spawned"), Weapon))
	{
		DestroyFireBehaviorTestWorld(World);
		return false;
	}

	// 行为要求 Instigator；用具体化测试角色充当持有者。
	APawn* InstigatorPawn = World->SpawnActor<AShooterWeaponPresentationTestCharacter>(FVector::ZeroVector,
		FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Fire behavior instigator spawned"), InstigatorPawn))
	{
		DestroyFireBehaviorTestWorld(World);
		return false;
	}

	UShooterProjectileFireBehavior* Behavior = NewObject<UShooterProjectileFireBehavior>(GetTransientPackage());

	FShooterWeaponFireContext Context;
	Context.WeaponActor = nullptr;
	Context.Instigator = InstigatorPawn;
	Context.MuzzleTransform = FTransform(FRotator::ZeroRotator, FVector(10.0f, 0.0f, 80.0f));
	Context.ProjectileClass = PistolBulletClass;

	// 缺 WeaponActor：不生成。
	Behavior->ExecuteFire(Context);
	TestEqual(TEXT("Null weapon actor spawns nothing"), CountProjectiles(World, PistolBulletClass), 0);

	Context.WeaponActor = Weapon;

	// 非服务器 Role：纵深防御 fail closed。
	Weapon->SetRole(ROLE_SimulatedProxy);
	Behavior->ExecuteFire(Context);
	TestEqual(TEXT("Non-authority weapon spawns nothing"), CountProjectiles(World, PistolBulletClass), 0);
	Weapon->SetRole(ROLE_Authority);

	// 上下文未携带弹丸类：不生成。
	Context.ProjectileClass = nullptr;
	Behavior->ExecuteFire(Context);
	TestEqual(TEXT("Missing projectile class spawns nothing"), CountProjectiles(World, PistolBulletClass), 0);

	// 合法服务器上下文：恰好一个弹丸，且使用上下文声明的类与上下文变换。
	Context.ProjectileClass = PistolBulletClass;
	Behavior->ExecuteFire(Context);
	TestEqual(TEXT("Valid server context spawns exactly one projectile"), CountProjectiles(World, PistolBulletClass), 1);

	for (TActorIterator<AShooterProjectile> It(World, PistolBulletClass); It; ++It)
	{
		TestEqual(TEXT("Projectile uses the context transform"), It->GetActorLocation(), FVector(10.0f, 0.0f, 80.0f));
	}

	DestroyFireBehaviorTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
