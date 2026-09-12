// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterProjectileFireBehavior.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "ShootGame.h"
#include "ShooterProjectile.h"
#include "Weapons/ShooterWeapon.h"

void UShooterProjectileFireBehavior::ExecuteFire(const FShooterWeaponFireContext& Context)
{
	AShooterWeapon* Weapon = Context.WeaponActor;
	// 弹丸类只来自本次开火的武器模板行快照；行未配置弹丸类时 fail closed。
	TSubclassOf<AShooterProjectile> ProjectileClass = Context.Config.ProjectileClass;
	if (!Weapon || !Context.Instigator || !ProjectileClass)
	{
		// 缺少任一必要输入时 fail closed，不产生 Gameplay 结果。
		return;
	}

	// 纵深防御：弹丸是服务器权威对象，非服务器调用不生成。
	if (!Weapon->HasAuthority())
	{
		return;
	}

	UWorld* World = Weapon->GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::OverrideRootScale;
	SpawnParams.Owner = Weapon->GetOwner();
	SpawnParams.Instigator = Context.Instigator;

	AShooterProjectile* Projectile = World->SpawnActor<AShooterProjectile>(
		ProjectileClass,
		Context.MuzzleTransform,
		SpawnParams);
	if (!Projectile)
	{
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("ProjectileFireBehavior failed to spawn projectile: Weapon=%s Class=%s WeaponId=%s"),
			*GetNameSafe(Weapon),
			*GetNameSafe(ProjectileClass.Get()),
			*Context.WeaponId.ToString());
	}
}
