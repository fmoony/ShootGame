// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

namespace ShooterGameplayTags
{
	/** 输入标签：Enhanced Input / AI 意图与 GA_Fire Ability Spec 之间的稳定映射。 */
	SHOOTGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Fire);

	/** 输入标签：换弹输入与 GA_Reload Ability Spec 之间的稳定映射。 */
	SHOOTGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Reload);

	/** 输入标签：切枪输入与 GA_Equip Ability Spec 之间的稳定映射。 */
	SHOOTGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Equip_Next);

	/** 状态标签：拥有者已死亡，GA_Fire / GA_Reload / GA_Equip 拒绝激活并取消现有事务。 */
	SHOOTGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);

	/** 状态标签：GA_Fire 活动期间挂在 ASC 上的开火事务标签。 */
	SHOOTGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Firing);

	/** 状态标签：GA_Reload 活动期间挂在 ASC 上的换弹事务标签。 */
	SHOOTGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Reloading);

	/** 状态标签：GA_Equip 活动期间挂在 ASC 上的装备事务标签。 */
	SHOOTGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Equipping);
}
