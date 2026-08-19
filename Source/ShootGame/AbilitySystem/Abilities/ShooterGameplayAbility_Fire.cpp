// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterGameplayAbility_Fire.h"

#include "GameplayTagContainer.h"
#include "ShooterCharacter.h"
#include "ShooterGameplayTags.h"
#include "ShooterNPC.h"
#include "ShootGame.h"

bool UShooterGameplayAbility_Fire::HasInputFireTag() const
{
	return GetAssetTags().HasTag(ShooterGameplayTags::Input_Fire);
}

bool UShooterGameplayAbility_Fire::IsBlockedByStateDead() const
{
	return ActivationBlockedTags.HasTag(ShooterGameplayTags::State_Dead);
}

bool UShooterGameplayAbility_Fire::OwnsStateFiringWhileActive() const
{
	return ActivationOwnedTags.HasTag(ShooterGameplayTags::State_Firing);
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

	// ServerOnly 激活前会先在拥有者客户端做一次本地预检；
	// 客户端只要求 Avatar 存在，武器、弹药与死亡等完整校验统一留给服务器。
	const AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (AvatarActor && !AvatarActor->HasAuthority())
	{
		return true;
	}

	// 4A 阶段尚未接入武器执行；服务器只拒绝没有合法 Avatar 的激活。
	return Cast<AShooterCharacter>(AvatarActor) || Cast<AShooterNPC>(AvatarActor);
}

void UShooterGameplayAbility_Fire::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 4A 只验证 Ability 可以被服务器激活并正确结束；开火闭环由 4B 接入。
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("GA_Fire activated (4A shell): Avatar=%s Owner=%s"),
		*GetNameSafe(GetShooterAvatarActor()),
		*GetNameSafe(ActorInfo ? ActorInfo->OwnerActor.Get() : nullptr));
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

AShooterCharacter* UShooterGameplayAbility_Fire::GetShooterCharacter() const
{
	return Cast<AShooterCharacter>(GetShooterAvatarActor());
}

AShooterNPC* UShooterGameplayAbility_Fire::GetShooterNPC() const
{
	return Cast<AShooterNPC>(GetShooterAvatarActor());
}
