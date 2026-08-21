// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterGameplayTags.h"

namespace ShooterGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Input_Fire,
		"Input.Fire",
		"开火输入：本地输入与 AI 意图进入 ASC 的唯一稳定标签。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Input_Reload,
		"Input.Reload",
		"换弹输入：本地输入进入 ASC 的唯一稳定标签。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Input_Equip_Next,
		"Input.Equip.Next",
		"切枪输入：本地输入进入 ASC 的唯一稳定标签。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		State_Dead,
		"State.Dead",
		"拥有者死亡状态：阻塞 Fire / Reload / Equip 激活，并在死亡时触发取消。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		State_Firing,
		"State.Firing",
		"GA_Fire 活动期间挂载的开火事务标签。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		State_Reloading,
		"State.Reloading",
		"GA_Reload 活动期间挂载的换弹事务标签。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		State_Equipping,
		"State.Equipping",
		"GA_Equip 活动期间挂载的装备事务标签。");
}
