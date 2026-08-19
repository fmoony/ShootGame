// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

namespace ShooterGameplayTags
{
	/** 输入标签：Enhanced Input / AI 意图与 GA_Fire Ability Spec 之间的稳定映射。 */
	SHOOTGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Fire);

	/** 状态标签：拥有者已死亡，GA_Fire 拒绝激活并取消现有开火。 */
	SHOOTGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);

	/** 状态标签：GA_Fire 活动期间挂在 ASC 上的开火事务标签。 */
	SHOOTGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Firing);
}
