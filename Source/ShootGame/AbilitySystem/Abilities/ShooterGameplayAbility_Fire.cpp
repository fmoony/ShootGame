// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterGameplayAbility_Fire.h"

#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "ShooterCharacter.h"
#include "ShooterGameplayTags.h"
#include "ShooterNPC.h"
#include "ShooterWeapon.h"
#include "ShooterWeaponHolder.h"
#include "ShootGame.h"

bool UShooterGameplayAbility_Fire::HasInputFireTag() const
{
	return GetAssetTags().HasTag(ShooterGameplayTags::Input_Fire);
}

bool UShooterGameplayAbility_Fire::IsBlockedByStateDead() const
{
	return ActivationBlockedTags.HasTag(ShooterGameplayTags::State_Dead);
}

bool UShooterGameplayAbility_Fire::IsBlockedByStateReloading() const
{
	return ActivationBlockedTags.HasTag(ShooterGameplayTags::State_Reloading);
}

bool UShooterGameplayAbility_Fire::IsBlockedByStateEquipping() const
{
	return ActivationBlockedTags.HasTag(ShooterGameplayTags::State_Equipping);
}

bool UShooterGameplayAbility_Fire::OwnsStateFiringWhileActive() const
{
	return ActivationOwnedTags.HasTag(ShooterGameplayTags::State_Firing);
}

bool UShooterGameplayAbility_Fire::CanRetriggerInstancedAbility() const
{
	return bRetriggerInstancedAbility;
}

UShooterGameplayAbility_Fire::UShooterGameplayAbility_Fire()
{
	// 同一 Avatar 同生命周期内只保留一个实例；网络执行只发生在服务器。
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	// Input.Fire 是 ASC 输入查找与 Ability Spec 之间的稳定映射。
	// 资产标签使用 UE 5.6 新 API 在构造函数内写入 CDO。
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(ShooterGameplayTags::Input_Fire);
	SetAssetTags(AssetTags);
	// 死亡状态阻塞激活；State.Firing 在激活期间由 GAS 自动挂到拥有者 ASC。
	ActivationBlockedTags.AddTag(ShooterGameplayTags::State_Dead);
	// 换弹与装备事务期间同样阻塞开火，保证三者互斥。
	ActivationBlockedTags.AddTag(ShooterGameplayTags::State_Reloading);
	ActivationBlockedTags.AddTag(ShooterGameplayTags::State_Equipping);
	ActivationOwnedTags.AddTag(ShooterGameplayTags::State_Firing);
}

bool UShooterGameplayAbility_Fire::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(
		Handle,
		ActorInfo,
		SourceTags,
		TargetTags,
		OptionalRelevantTags))
	{
		return false;
	}

	const AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!AvatarActor)
	{
		return false;
	}

	// ServerOnly 激活前会先在拥有者客户端做一次本地预检。
	// 客户端只检查 Avatar 存在，武器、弹药与死亡等完整校验统一留给服务器，
	// 避免复制时序差异在本地误拒绝合法输入。
	if (!AvatarActor->HasAuthority())
	{
		return true;
	}

	// 服务器完整校验：Avatar 必须是 ASC 当前 Avatar、玩家 / NPC 未死亡、
	// 当前 WeaponActor 有效且属于该 Avatar，并且存在可消耗弹药。
	const UAbilitySystemComponent* AbilitySystemComponent =
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const IShooterWeaponHolder* WeaponHolder =
		Cast<IShooterWeaponHolder>(AvatarActor);
	const AShooterWeapon* Weapon = WeaponHolder
		? WeaponHolder->GetCurrentWeapon()
		: nullptr;

	const AShooterCharacter* Character = Cast<AShooterCharacter>(AvatarActor);
	const AShooterNPC* Npc = Cast<AShooterNPC>(AvatarActor);
	const bool bHolderAlive = Character
		? !Character->IsDead()
		: Npc
			? !Npc->IsDead()
			: false;

	return AbilitySystemComponent &&
		AbilitySystemComponent->GetAvatarActor() == AvatarActor &&
		bHolderAlive &&
		IsValid(Weapon) &&
		Weapon->GetOwner() == AvatarActor &&
		!Weapon->IsHidden() &&
		Weapon->CanConsumeAmmo();
}

void UShooterGameplayAbility_Fire::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 纵深防御：ServerOnly 能力仍显式确认服务器权威，客户端绕过输入层也无法执行。
	if (!HasAuthority(&ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	AShooterCharacter* Character = Cast<AShooterCharacter>(AvatarActor);
	AShooterNPC* Npc = Cast<AShooterNPC>(AvatarActor);
	AShooterWeapon* Weapon = GetCurrentWeaponForAvatar(AvatarActor);
	const bool bHolderAlive = Character
		? !Character->IsDead()
		: Npc
			? !Npc->IsDead()
			: false;
	if ((!Character && !Npc) || !IsValid(Weapon) || !bHolderAlive ||
		Weapon->GetOwner() != AvatarActor || Weapon->IsHidden() ||
		!Weapon->CanConsumeAmmo())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	CachedWeapon = Weapon;
	Weapon->OnOutOfAmmo.AddUObject(
		this,
		&UShooterGameplayAbility_Fire::HandleWeaponOutOfAmmo);
	Weapon->StartFiring();

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("GA_Fire activated: Avatar=%s Weapon=%s Ammo=%d"),
		*GetNameSafe(Character),
		*GetNameSafe(Weapon),
		Weapon->GetBulletCount());
}

void UShooterGameplayAbility_Fire::InputReleased(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	// 客户端本地没有 ServerOnly Ability 实例；本方法只会在服务器收到释放 RPC 后执行。
	if (HasAuthority(&ActivationInfo))
	{
		StopWeaponAndEndAbility();
	}
}

void UShooterGameplayAbility_Fire::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 幂等清理：多条取消链（释放、死亡、切枪、断线、弹药耗尽）可能同时到达。
	if (CachedWeapon.IsValid())
	{
		CachedWeapon->OnOutOfAmmo.RemoveAll(this);
		CachedWeapon->StopFiring();
		CachedWeapon.Reset();
	}

	Super::EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		bReplicateEndAbility,
		bWasCancelled);

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("GA_Fire ended: Cancelled=%s Avatar=%s"),
		bWasCancelled ? TEXT("true") : TEXT("false"),
		*GetNameSafe(GetShooterAvatarActor()));
}

void UShooterGameplayAbility_Fire::StopWeaponAndEndAbility()
{
	if (CachedWeapon.IsValid())
	{
		CachedWeapon->StopFiring();
	}

	EndAbility(
		GetCurrentAbilitySpecHandle(),
		GetCurrentActorInfo(),
		GetCurrentActivationInfo(),
		true,
		false);
}

void UShooterGameplayAbility_Fire::HandleWeaponOutOfAmmo(AShooterWeapon* Weapon)
{
	if (Weapon != CachedWeapon.Get())
	{
		return;
	}

	// Weapon 已自行 StopFiring；这里只结束 Ability 并移除 State.Firing。
	EndAbility(
		GetCurrentAbilitySpecHandle(),
		GetCurrentActorInfo(),
		GetCurrentActivationInfo(),
		true,
		false);
}

AShooterCharacter* UShooterGameplayAbility_Fire::GetShooterCharacter() const
{
	return Cast<AShooterCharacter>(GetShooterAvatarActor());
}

AShooterNPC* UShooterGameplayAbility_Fire::GetShooterNPC() const
{
	return Cast<AShooterNPC>(GetShooterAvatarActor());
}

AShooterWeapon* UShooterGameplayAbility_Fire::GetCurrentWeaponForAvatar(
	AActor* AvatarActor) const
{
	// 玩家与 NPC 共用 IShooterWeaponHolder 的最小只读 CurrentWeapon 接口。
	const IShooterWeaponHolder* WeaponHolder =
		Cast<IShooterWeaponHolder>(AvatarActor);
	return WeaponHolder ? WeaponHolder->GetCurrentWeapon() : nullptr;
}
