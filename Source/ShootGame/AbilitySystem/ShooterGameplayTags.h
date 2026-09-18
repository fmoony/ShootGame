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

	/** 输入标签：所有切枪 Ability 共用的分类与取消标签。 */
	SHOOTGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Equip);

	/** 输入标签：按 SlotIndex 升序切换武器。 */
	SHOOTGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Equip_Next);

	/** 输入标签：按 SlotIndex 降序切换武器。 */
	SHOOTGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Equip_Previous);

	/**
	 * 输入行为标签：Spec 声明「按住可恢复」。
	 *
	 * 语义：Spec.InputPressed 仍为 true 时，短暂动作阻塞解除后允许该 Spec 重新尝试激活。
	 * 它挂在 FGameplayAbilitySpec 的动态 Spec 标签上（随 Spec 复制），由宿主按当前输入语义同步：
	 * 同一个 GA_Fire 在连发武器上是 HeldRepeat，在单发武器上不是。
	 * ASC 只认这个标签与 Spec.InputPressed，不判断武器或具体 Ability。
	 */
	SHOOTGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputBehavior_HeldRepeat);

	/** 状态标签：拥有者已死亡，GA_Fire / GA_Reload / GA_Equip 拒绝激活并取消现有事务。 */
	SHOOTGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);

	/** 状态标签：GA_Fire 活动期间挂在 ASC 上的开火事务标签。 */
	SHOOTGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Firing);

	/** 状态标签：GA_Reload 活动期间挂在 ASC 上的换弹事务标签。 */
	SHOOTGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Reloading);

	/** 状态标签：GA_Equip 活动期间挂在 ASC 上的装备事务标签。 */
	SHOOTGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Equipping);
}
