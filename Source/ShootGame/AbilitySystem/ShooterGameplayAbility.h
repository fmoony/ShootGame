// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "ShooterGameplayAbility.generated.h"

/**
 * 输入激活策略：Ability 自己声明「如何响应输入」。
 *
 * - OnInputTriggered：单次按下沿语义。Press 当场尝试一次激活；失败可按输入缓冲规则短期保留。
 * - WhileInputActive：按住持续语义。持续意图只由 FGameplayAbilitySpec::InputPressed 表达，
 *   未激活时由 ASC 的输入处理时点重试一次；Release 即终止，不需要任何由外部同步的派生状态。
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
	 * 输入 Buffer 的上下文对象；默认无上下文。
	 * 上下文变化后，同一输入 Tag 的待消费按下沿不得继续生效。
	 */
	virtual const UObject* GetInputBufferContext() const { return nullptr; }

	/** 这些阻塞标签是否由本 Ability 自己的活动状态提供（例如换弹中再按换弹）。 */
	bool IsBlockedByOwnActiveTags(const FGameplayTagContainer& BlockingTags) const;
};
