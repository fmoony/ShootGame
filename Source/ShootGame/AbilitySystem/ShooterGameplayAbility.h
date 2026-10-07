// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "ShooterGameplayAbility.generated.h"

struct FGameplayAbilitySpec;

/**
 * 输入激活策略：Ability 自己声明「如何响应输入」。
 *
 * - OnInputTriggered：单次按下沿语义。Press 当场尝试一次激活；失败可按输入缓冲规则短期保留。
 * - WhileInputActive：按住持续语义。持续意图由 ASC 的输入采集层表达（Held Input Tag，或本帧的按下沿），
 *   未激活时由 ASC 的输入处理时点重试一次；Release 即终止，不需要任何由外部同步的派生状态。
 *   只有没有采集层的一端（Dedicated Server 上的远端 ASC）才回落到 FGameplayAbilitySpec::InputPressed。
 */
UENUM()
enum class EShooterAbilityActivationPolicy : uint8
{
	OnInputTriggered,
	WhileInputActive
};

/**
 * ShootGame 项目通用 Ability 最小基类。
 * 只在出现第二个真实共享需求时扩展；不向基类塞入武器、Inventory、UI 或 Projectile 逻辑。
 */
UCLASS(Abstract)
class SHOOTGAME_API UShooterGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	/** 安全取得当前 Avatar Actor；AbilityActorInfo 尚未初始化时为 nullptr。 */
	AActor* GetShooterAvatarActor() const;

	/** 判断当前执行端是否拥有权威 Avatar；客户端预检可据此把完整校验留给服务器。 */
	bool IsAvatarAuthoritative() const;

	/**
	 * 输入激活策略；由 ASC 在输入处理时查询，默认单次按下沿。
	 *
	 * 显式传入 ActorInfo：未激活（或重生后尚未再次激活）的 Ability 实例上没有 CurrentActorInfo
	 * （它只在 PreActivate 里设置），因此策略查询不得读实例缓存，也不得依赖 CDO 的运行时状态。
	 * 策略允许按当前 Gameplay Context 动态返回（例如按当前武器的连发语义）。
	 */
	virtual EShooterAbilityActivationPolicy GetActivationPolicy(const FGameplayAbilityActorInfo* ActorInfo) const;

	/**
	 * 按 Spec 查询输入激活策略；同一个输入 Tag 存在多份同类 Spec 时，
	 * 策略必须来自"这一份 Spec 对应的 Gameplay Context"，而不是当前武器。
	 * 默认忽略 Spec，回落到 GetActivationPolicy。
	 */
	virtual EShooterAbilityActivationPolicy GetActivationPolicyForSpec(const FGameplayAbilitySpec& Spec,
		const FGameplayAbilityActorInfo* ActorInfo) const;

	/**
	 * 该 Spec 是否参与本次输入：同一个输入 Tag 存在多份同类 Spec 时用它选出唯一一份。
	 * 默认对所有 Spec 返回 true（只有一份 Spec 的动作无需区分）。
	 */
	virtual bool DoesSpecMatchInputContext(const FGameplayAbilitySpec& Spec, const FGameplayAbilityActorInfo* ActorInfo) const;

	/**
	 * 输入 Buffer 的上下文对象；默认无上下文。
	 * 上下文变化后，同一输入 Tag 的待消费按下沿不得继续生效。
	 */
	virtual const UObject* GetInputBufferContext() const { return nullptr; }

	/**
	 * 输入意图的唯一判定入口：该输入 Tag 在本解释时点是否仍然成立（按住，或本帧的按下沿）。
	 *
	 * 输入事实与"哪一份 Spec 响应它"是两层，因此这里刻意不读某个 Spec 的镜像：
	 * - 本机拥有者视图（远端客户端 / 监听主机）读 ASC 的输入采集层。按住是输入事实，
	 *   与"这一份 Spec 曾经收到过 Press"无关；切枪后新武器从未收到过 Press，但玩家确实一直按着，
	 *   全自动必须能据此继续响应。同一帧内"按下 + 松开"的点击也仍然成立，因为那次按下沿是真实的。
	 * - 没有采集层的一端（Dedicated Server 上的远端玩家与 NPC 的 ASC）读引擎按激活请求与
	 *   可靠释放维护的 FGameplayAbilitySpec::InputPressed 镜像，它是那一端唯一的输入真值。
	 */
	bool IsInputTagHeld(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTag& InputTag) const;

	/** 这些阻塞标签是否由本 Ability 自己的活动状态提供（例如换弹中再按换弹）。 */
	bool IsBlockedByOwnActiveTags(const FGameplayTagContainer& BlockingTags) const;
};
