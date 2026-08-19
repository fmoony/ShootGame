// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterGameplayTags.h"

namespace ShooterGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Input_Fire,
		"Input.Fire",
		"开火输入：本地输入与 AI 意图进入 ASC 的唯一稳定标签。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		State_Dead,
		"State.Dead",
		"拥有者死亡状态：阻塞 GA_Fire 激活，并在死亡时触发取消。");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		State_Firing,
		"State.Firing",
		"GA_Fire 活动期间挂载的开火事务标签。");
}
