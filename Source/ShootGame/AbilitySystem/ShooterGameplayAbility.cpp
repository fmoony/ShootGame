// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterGameplayAbility.h"

AActor* UShooterGameplayAbility::GetShooterAvatarActor() const
{
	// 引擎 API 在 CDO 上调用会触发 ensure；安全入口先排除 CDO 场景。
	if (!IsInstantiated())
	{
		return nullptr;
	}

	// 尚未激活的实例（例如测试夹具直接 NewObject 出来的实例）没有 CurrentActorInfo，
	// GetAvatarActorFromActorInfo() 会触发 ensure；诊断与只读查询必须按"无 Avatar"返回。
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	return ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
}

bool UShooterGameplayAbility::IsAvatarAuthoritative() const
{
	const AActor* AvatarActor = GetShooterAvatarActor();
	return IsValid(AvatarActor) && AvatarActor->HasAuthority();
}

bool UShooterGameplayAbility::IsBlockedByOwnActiveTags(const FGameplayTagContainer& BlockingTags) const
{
	// 阻塞来自本 Ability 自己的 ActivationOwnedTags 时，说明是同一个动作仍在进行
	// （例如换弹中再按换弹）；这种重复按下没有意义，不应进入输入缓冲。
	return ActivationOwnedTags.HasAny(BlockingTags);
}
