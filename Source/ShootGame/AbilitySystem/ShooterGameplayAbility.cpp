// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterGameplayAbility.h"

AActor* UShooterGameplayAbility::GetShooterAvatarActor() const
{
	// 引擎 API 在 CDO 上调用会触发 ensure；安全入口先排除 CDO 场景。
	if (!IsInstantiated())
	{
		return nullptr;
	}

	return GetAvatarActorFromActorInfo();
}

bool UShooterGameplayAbility::IsAvatarAuthoritative() const
{
	const AActor* AvatarActor = GetShooterAvatarActor();
	return IsValid(AvatarActor) && AvatarActor->HasAuthority();
}
