// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterGameplayTags.h"

namespace ShooterGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Input_Fire, "Input.Fire", "开火输入：本地输入与 AI 意图进入 ASC 的唯一稳定标签。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Input_Reload, "Input.Reload", "换弹输入：本地输入进入 ASC 的唯一稳定标签。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Input_Equip, "Input.Equip", "切枪 Ability 的分类与统一取消标签。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Input_Equip_Next, "Input.Equip.Next", "切换到下一个武器槽位的输入标签。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Input_Equip_Previous, "Input.Equip.Previous", "切换到上一个武器槽位的输入标签。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputBehavior_HeldRepeat, "InputBehavior.HeldRepeat",
		"输入行为：Spec.InputPressed 仍为 true 时，短暂阻塞解除后允许该 Spec 重新尝试激活；"
		"该 Spec 不登记也不消费短期 Buffered Press。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Dead, "State.Dead", "拥有者死亡状态：阻塞 Fire / Reload / Equip 激活，并在死亡时触发取消。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Firing, "State.Firing", "GA_Fire 活动期间挂载的开火事务标签。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Reloading, "State.Reloading", "GA_Reload 活动期间挂载的换弹事务标签。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Equipping, "State.Equipping", "GA_Equip 活动期间挂载的装备事务标签。");
}
