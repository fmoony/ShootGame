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
