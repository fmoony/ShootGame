// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/ShooterGameplayAbility.h"
#include "AbilitySystem/ShooterGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "ShooterInputBufferTestTypes.generated.h"

/**
 * 输入缓冲自动化测试使用的 Ability 类型。
 *
 * 这些类型只存在于测试代码路径，不代表任何生产 Ability 的语义；
 * 它们复用生产的 UShooterAbilitySystemComponent 输入路径与 GAS Tag 门控，
 * 用来验证「短暂阻塞保留一次按下沿」「硬失败不保留」「按住型输入在阻塞解除后重试一次」等判定。
 */

/** 基础测试 Ability：被 State.Reloading / State.Dead 阻塞，一次按下对应一次本地动作边界。 */
UCLASS(Transient, NotBlueprintable)
class UShooterInputBufferTestAbility : public UShooterGameplayAbility
{
	GENERATED_BODY()

public:
	UShooterInputBufferTestAbility()
	{
		InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
		NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;

		FGameplayTagContainer AssetTags;
		AssetTags.AddTag(ShooterGameplayTags::Input_Fire);
		SetAssetTags(AssetTags);

		// 与生产的 GA_Fire 一致：换弹 / 装备为短暂阻塞，死亡为硬阻塞。
		ActivationBlockedTags.AddTag(ShooterGameplayTags::State_Reloading);
		ActivationBlockedTags.AddTag(ShooterGameplayTags::State_Dead);
	}

	/** 已经形成的本地动作边界次数。 */
	int32 ActivationCountForTest = 0;

	/** 输入缓冲上下文；非空时参与「上下文变化即丢弃」判定。 */
	TWeakObjectPtr<const UObject> InputContextForTest;

	void SetInputContextForTest(const UObject* Context) { InputContextForTest = Context; }

	virtual const UObject* GetInputBufferContext() const override { return InputContextForTest.Get(); }

protected:
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags,
		const FGameplayTagContainer* TargetTags,
		FGameplayTagContainer* OptionalRelevantTags) const override
	{
		// 与生产的 GA_Fire 一致：本地确定性硬失败先于 Tag 门控，且不产生任何失败标签。
		if (!IsLocalInputStateValid(Handle, ActorInfo))
		{
			return false;
		}

		return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
	}

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override
	{
		++ActivationCountForTest;
	}

	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override
	{
		// 与生产的 GA_Fire 一致：松开即结束本地预测实例。
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility*/ false, /*bWasCancelled*/ false);
	}

	/** 本地确定性条件；默认无额外约束。 */
	virtual bool IsLocalInputStateValid(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo) const
	{
		return true;
	}
};

/** 硬失败测试 Ability：CanActivateAbility 恒定拒绝且不产生任何失败标签。 */
UCLASS(Transient, NotBlueprintable)
class UShooterInputBufferHardFailTestAbility : public UShooterInputBufferTestAbility
{
	GENERATED_BODY()

protected:
	virtual bool IsLocalInputStateValid(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo) const override
	{
		return false;
	}
};

/** 被自身活动标签阻塞：State.Reloading 同时出现在它自己的 ActivationOwnedTags 中。 */
UCLASS(Transient, NotBlueprintable)
class UShooterInputBufferSelfBlockedTestAbility : public UShooterInputBufferTestAbility
{
	GENERATED_BODY()

public:
	UShooterInputBufferSelfBlockedTestAbility()
	{
		ActivationOwnedTags.AddTag(ShooterGameplayTags::State_Reloading);
	}
};

/**
 * 按住型（Held）测试 Ability：只有仍处于按住状态才允许启动。
 *
 * 与生产的全自动 GA_Fire 一致：Ability 本身不声明输入语义，
 * 「阻塞解除后仍按住是否允许重试」由 Spec 上的 InputBehavior.HeldRepeat 决定，
 * 测试通过 ASC.SetHeldRepeatInputBehavior 写入（生产由装备入口做同一件事）。
 */
UCLASS(Transient, NotBlueprintable)
class UShooterInputBufferSustainedTestAbility : public UShooterInputBufferTestAbility
{
	GENERATED_BODY()

protected:
	/** 与生产的全自动 GA_Fire 一致：已经松开的按下沿不得补出一次启动。 */
	virtual bool IsLocalInputStateValid(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo) const override
	{
		const UAbilitySystemComponent* AbilitySystemComponent = ActorInfo
			? ActorInfo->AbilitySystemComponent.Get()
			: nullptr;
		const FGameplayAbilitySpec* Spec = AbilitySystemComponent
			? AbilitySystemComponent->FindAbilitySpecFromHandle(Handle)
			: nullptr;
		return Spec != nullptr && Spec->InputPressed;
	}
};
