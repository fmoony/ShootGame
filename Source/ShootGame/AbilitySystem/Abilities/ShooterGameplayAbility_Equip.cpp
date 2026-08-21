// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterGameplayAbility_Equip.h"

#include "GameplayTagContainer.h"
#include "ShooterCharacter.h"
#include "ShooterGameplayTags.h"
#include "ShootGame.h"

bool UShooterGameplayAbility_Equip::HasInputEquipNextTag() const
{
	return GetAssetTags().HasTag(ShooterGameplayTags::Input_Equip_Next);
}

bool UShooterGameplayAbility_Equip::IsBlockedByStateDead() const
{
	return ActivationBlockedTags.HasTag(ShooterGameplayTags::State_Dead);
}

bool UShooterGameplayAbility_Equip::IsBlockedByStateEquipping() const
{
	return ActivationBlockedTags.HasTag(ShooterGameplayTags::State_Equipping);
}

bool UShooterGameplayAbility_Equip::OwnsStateEquippingWhileActive() const
{
	return ActivationOwnedTags.HasTag(ShooterGameplayTags::State_Equipping);
}

bool UShooterGameplayAbility_Equip::CanRetriggerInstancedAbility() const
{
	return bRetriggerInstancedAbility;
}

UShooterGameplayAbility_Equip::UShooterGameplayAbility_Equip()
{
	// 同一 Avatar 同生命周期内只保留一个实例；网络执行只发生在服务器。
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	// Input.Equip.Next 是 ASC 输入查找与 Ability Spec 之间的稳定映射。
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(ShooterGameplayTags::Input_Equip_Next);
	SetAssetTags(AssetTags);
	// 死亡与装备中状态阻塞激活；State.Equipping 在激活期间由 GAS 自动挂到拥有者 ASC。
	// 5C 会在激活时显式取消 Fire / Reload，因此这里不把 State.Firing / State.Reloading 设为阻塞。
	ActivationBlockedTags.AddTag(ShooterGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(ShooterGameplayTags::State_Equipping);
	ActivationOwnedTags.AddTag(ShooterGameplayTags::State_Equipping);
}

bool UShooterGameplayAbility_Equip::CanActivateAbility(
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

	// ServerOnly 激活前会先在拥有者客户端做一次本地预检；
	// 客户端只要求 Avatar 存在，完整校验统一留给服务器。
	const AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (AvatarActor && !AvatarActor->HasAuthority())
	{
		return true;
	}

	// 5A 阶段尚未接入 Inventory 事务；服务器只拒绝没有合法玩家 Avatar 的激活。
	return Cast<AShooterCharacter>(AvatarActor) != nullptr;
}

void UShooterGameplayAbility_Equip::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 纵深防御：ServerOnly 能力仍显式确认服务器权威。
	if (!HasAuthority(&ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 5A 只验证 Ability 可以被服务器激活并正确结束；ActiveWeapon / CurrentWeapon 提交由 5C 接入。
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("GA_Equip activated (5A shell): Avatar=%s Owner=%s"),
		*GetNameSafe(GetShooterAvatarActor()),
		*GetNameSafe(ActorInfo ? ActorInfo->OwnerActor.Get() : nullptr));
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
