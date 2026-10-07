// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterGameplayAbility.h"

#include "AbilitySystem/ShooterAbilitySystemComponent.h"
#include "GameplayTagContainer.h"

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

EShooterAbilityActivationPolicy UShooterGameplayAbility::GetActivationPolicy(const FGameplayAbilityActorInfo* ActorInfo) const
{
	// 默认单次按下沿；是否按住持续由具体 Ability 按当前 Gameplay Context 决定。
	(void)ActorInfo;
	return EShooterAbilityActivationPolicy::OnInputTriggered;
}

EShooterAbilityActivationPolicy UShooterGameplayAbility::GetActivationPolicyForSpec(const FGameplayAbilitySpec& Spec,
	const FGameplayAbilityActorInfo* ActorInfo) const
{
	// 默认与 Spec 无关：只有"同一个输入 Tag 对应多份 Spec"的 Ability 才需要覆写。
	(void)Spec;
	return GetActivationPolicy(ActorInfo);
}

bool UShooterGameplayAbility::DoesSpecMatchInputContext(const FGameplayAbilitySpec& Spec, const FGameplayAbilityActorInfo* ActorInfo) const
{
	// 默认匹配：只有"同一个输入 Tag 对应多份 Spec"的 Ability 才需要覆写。
	(void)Spec;
	(void)ActorInfo;
	return true;
}

bool UShooterGameplayAbility::IsInputTagHeld(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTag& InputTag) const
{
	if (!InputTag.IsValid())
	{
		return false;
	}

	const UShooterAbilitySystemComponent* ShooterAbilitySystemComponent = Cast<UShooterAbilitySystemComponent>(
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr);

	// 本机拥有者视图（远端客户端与监听主机）持有输入采集层：输入意图是否成立由采集层决定。
	// 切枪后新武器的 Spec 从未收到过 Press，但玩家确实一直按着，因此必须读采集层；
	// 读 Spec 镜像会把"切枪"误判成"已经松开"。
	if (ShooterAbilitySystemComponent && ShooterAbilitySystemComponent->HasLocalInputCollection())
	{
		// 两个事实都来自采集层，缺一不可：
		// - 仍然按住（Level）：切枪后的全自动据此继续响应；
		// - 本帧存在按下沿（Edge）：按下与松开落在同一帧的"点击"在解释时点已经退出 Held 采集，
		//   但那一次 Press edge 同样必须形成一次动作边界。
		// 这不是"伪造一次新的 Press edge"：这里只读取玩家真实产生的那一次边沿，
		// 既不写回 PressedInputTags，也不让任何单发 Ability 因此多打一枪。
		return ShooterAbilitySystemComponent->IsInputTagHeld(InputTag) ||
			ShooterAbilitySystemComponent->IsInputTagPressedThisFrame(InputTag);
	}

	// 没有采集层的一端（Dedicated Server 上的远端玩家与 NPC 的 ASC）：引擎按每次激活请求与
	// 可靠释放维护 Spec 上的镜像，它是那一端唯一可用的输入真值，权威端不得自行推断按住。
	const UAbilitySystemComponent* AbilitySystemComponent = ActorInfo
		? ActorInfo->AbilitySystemComponent.Get()
		: nullptr;
	const FGameplayAbilitySpec* Spec = AbilitySystemComponent
		? AbilitySystemComponent->FindAbilitySpecFromHandle(Handle)
		: nullptr;
	return Spec != nullptr && Spec->InputPressed;
}

bool UShooterGameplayAbility::IsBlockedByOwnActiveTags(const FGameplayTagContainer& BlockingTags) const
{
	// 阻塞来自本 Ability 自己的 ActivationOwnedTags 时，说明是同一个动作仍在进行
	// （例如换弹中再按换弹）；这种重复按下没有意义，不应进入输入缓冲。
	return ActivationOwnedTags.HasAny(BlockingTags);
}
