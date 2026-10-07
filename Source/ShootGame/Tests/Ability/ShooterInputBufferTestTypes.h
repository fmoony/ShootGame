// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/ShooterGameplayAbility.h"
#include "AbilitySystem/ShooterGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "ShooterInputBufferTestTypes.generated.h"

/**
 * 输入处理自动化测试使用的 Ability 类型。
 *
 * 这些类型只存在于测试代码路径，不代表任何生产 Ability 的语义；
 * 它们复用生产的 UShooterAbilitySystemComponent 输入路径与 GAS Tag 门控，
 * 用来验证「单次按下沿的短期保留」「硬失败不保留」「按住持续在每帧输入处理时重试」等判定。
 */

/** 基础测试 Ability（OnInputTriggered）：被 State.Reloading / State.Dead 阻塞，一次按下对应一次本地动作边界。 */
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
 * 按住持续（WhileInputActive）测试 Ability：只有仍处于按住状态才允许启动。
 *
 * 与生产的全自动 GA_Fire 一致：输入语义由 Ability 自己的 ActivationPolicy 声明，
 * 持续意图只由 ASC 的输入采集层（Held Input Tag）表达，不需要任何由外部同步的派生状态。
 */
UCLASS(Transient, NotBlueprintable)
class UShooterInputBufferSustainedTestAbility : public UShooterInputBufferTestAbility
{
	GENERATED_BODY()

public:
	/** 与生产的全自动 GA_Fire 一致：按住持续，由 ASC 的输入处理时点决定何时重试。 */
	virtual EShooterAbilityActivationPolicy GetActivationPolicy(const FGameplayAbilityActorInfo* ActorInfo) const override
	{
		(void)ActorInfo;
		return EShooterAbilityActivationPolicy::WhileInputActive;
	}

protected:
	/** 与生产的全自动 GA_Fire 一致：已经松开的按下沿不得补出一次启动。 */
	virtual bool IsLocalInputStateValid(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo) const override
	{
		// 与生产的 GA_Fire 同一条判定：按住真值来自输入采集层，不读某一份 Spec 的镜像。
		return IsInputTagHeld(Handle, ActorInfo, ShooterGameplayTags::Input_Fire);
	}
};

/**
 * 单发语义下的按住持续测试 Ability：一次激活只形成一次动作边界并立即结束，
 * 由自己的本地节拍决定何时允许下一次激活——与生产的全自动 GA_Fire 完全同构。
 *
 * 它证明的关键事实是：Ability 每发结束之后，输入解释层的 Held 分支会继续尝试，
 * 但真正的动作边界频率由 Ability 自己的本地节拍决定，而不是输入帧率。
 */
UCLASS(Transient, NotBlueprintable)
class UShooterInputBufferSingleShotCadenceTestAbility : public UShooterInputBufferTestAbility
{
	GENERATED_BODY()

public:
	/** 与生产的全自动 GA_Fire 一致：按住持续，由 ASC 的输入处理时点决定何时重试。 */
	virtual EShooterAbilityActivationPolicy GetActivationPolicy(const FGameplayAbilityActorInfo* ActorInfo) const override
	{
		(void)ActorInfo;
		return EShooterAbilityActivationPolicy::WhileInputActive;
	}

	/** 被本地节拍拒绝的尝试次数：证明"每帧都尝试过"，但都没有形成动作边界。 */
	mutable int32 CadenceRefusedAttemptCountForTest = 0;

	/** 测试专用：把节拍置为就绪 / 未就绪，避免依赖真实世界时间推进。 */
	void ForceCadenceReadyForTest() { bCadenceReadyForTest = true; }
	void CloseCadenceForTest() { bCadenceReadyForTest = false; }

protected:
	virtual bool IsLocalInputStateValid(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo) const override
	{
		if (!IsInputTagHeld(Handle, ActorInfo, ShooterGameplayTags::Input_Fire))
		{
			return false;
		}

		// 与生产的全自动 GA_Fire 一致：按住 + 本地节拍就绪才构成一次动作边界。
		if (!bCadenceReadyForTest)
		{
			++CadenceRefusedAttemptCountForTest;
			return false;
		}

		return true;
	}

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override
	{
		(void)TriggerEventData;
		++ActivationCountForTest;

		// 一次 Activation 只处理一次动作：关掉本地节拍并立即结束，
		// 下一发必须由输入层在下一次节拍就绪后激活一次新的 GA_Fire。
		bCadenceReadyForTest = false;
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility*/ false, /*bWasCancelled*/ false);
	}

private:
	/** 本地开火节拍；初始为就绪。 */
	mutable bool bCadenceReadyForTest = true;
};
