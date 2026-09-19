// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "ShooterAbilitySystemComponent.generated.h"

struct FGameplayAbilitySpec;
struct FGameplayTag;
struct FGameplayTagContainer;
class UGameplayAbility;

/**
 * ShootGame 项目通用 ASC：只负责通用输入意图，不产生任何 Gameplay 结果。
 *
 * 职责：
 * - Press / Release：把输入 Tag 稳定映射到 Ability Spec，并把按下与松开转发给对应 Ability；
 * - Held：直接读 FGameplayAbilitySpec::InputPressed，不维护第二份按住状态；
 * - 短期 Buffered Press：没有 HeldRepeat 的 Spec 被「短暂动作阻塞」拒绝时保留一次按下沿，
 *   阻塞解除后消费一次；
 * - Deferred Retry：带 InputBehavior.HeldRepeat 的 Spec 在阻塞解除后按仍按住的状态重试一次。
 *
 * 两种输入意图来源互斥（冻结语义）：
 * - 无 HeldRepeat 的 Spec（Semi）：pending intent 只由短期 Buffered Press Edge 表达；
 * - 带 HeldRepeat 的 Spec（FullAuto）：pending intent 只由 Spec.InputPressed 表达，
 *   既不登记也不消费 Buffered Press，Release 即终止。
 *
 * 边界：
 * - 不判断「现在能不能开火 / 换弹」，那是 Ability 与 GameplayTags 的职责；
 * - 不读 Weapon / Ammo / Inventory / Projectile，也不做 Ammo / Projectile 预测；
 * - 重试只发生在「短暂阻塞解除」这一次安全时点，被拒后不再自行排程，不存在 Reject → Retry 循环。
 */
UCLASS(ClassGroup=(Shooter))
class SHOOTGAME_API UShooterAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UShooterAbilitySystemComponent();

	/** 按下输入 Tag：记录 Spec.InputPressed，并尝试激活匹配的 Ability。 */
	void AbilityInputTagPressed(const FGameplayTag& InputTag);

	/** 松开输入 Tag：非权威端通过可靠 Server RPC 转发到服务器，服务器转发给活动 Ability。 */
	void AbilityInputTagReleased(const FGameplayTag& InputTag);

	/** 返回匹配输入 Tag 的第一个 Ability Spec；没有匹配时返回 nullptr。 */
	FGameplayAbilitySpec* FindAbilitySpecFromInputTag(const FGameplayTag& InputTag);

	/** 返回指定 Ability 类的 Spec 数量；宿主用它验证授予幂等性。 */
	int32 GetAbilitySpecCountForClass(TSubclassOf<UGameplayAbility> AbilityClass) const;

	/** 返回指定 Ability 类当前处于活动状态的 Spec 数量；网络测试用它验证单激活约束。 */
	int32 GetActiveAbilityCountForClass(TSubclassOf<UGameplayAbility> AbilityClass) const;

	/** 按输入 Tag 取消所有匹配 Ability；死亡、切枪、断线等清理链共用。 */
	void CancelAbilitiesByTag(const FGameplayTag& InputTag);

	/**
	 * 同步通用输入行为标签 InputBehavior.HeldRepeat。
	 *
	 * 由宿主在当前输入语义变化时调用（例如当前武器在连发 / 单发之间切换）。
	 * 同一个 Ability 可以服务两种语义，因此行为标签写在 Spec 上而不是 Ability 上；
	 * 标签随 Spec 复制给拥有者，本函数负责标脏。
	 * 带该标签的 Spec 不参与短期 Buffered Press：不登记按下沿，也不消费历史残留条目。
	 */
	void SetHeldRepeatInputBehavior(const FGameplayTag& InputTag, bool bEnabled);

	/**
	 * 作废所有待消费的输入意图。
	 *
	 * 生命周期边界（Avatar 变化 / ClearActorInfo / EndPlay）由本类自行调用；
	 * 死亡这类宿主可见的明确边界由宿主显式调用——旧输入绝不跨生命周期消费。
	 */
	void ClearBufferedInputs();

	// 引擎把这两个函数声明为 public，覆写不得降低可见性；
	// 这里承担「输入意图不跨生命周期泄漏」的清理责任。
	virtual void InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor) override;
	virtual void ClearActorInfo() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	/** 一条待消费的按下沿；不保存「是否已松开」，该状态读 FGameplayAbilitySpec::InputPressed。 */
	struct FShooterBufferedInput
	{
		FGameplayTag InputTag;

		/** 按下时的上下文对象（GA_Fire 使用当前武器）；上下文变化后不得消费。 */
		TWeakObjectPtr<const UObject> Context;

		/** 按下时是否约定了上下文；未约定时不做上下文校验。 */
		bool bHasContext = false;

		/** 绝对过期时间；同一条目不得因重试而续期。 */
		float ExpireTime = 0.0f;
	};

private:
	/** 激活失败分类：只把「短暂动作标签阻塞」登记为可消费的按下沿。 */
	void HandleAbilityFailed(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureTags);

	/** 短暂阻塞标签计数归零：登记待处理并调度到 Tag 回调栈退出后的安全时点；同帧只调度一次。 */
	void HandleTransientBlockedTagChanged(FGameplayTag Tag, int32 NewCount);

	/** 延迟意图处理（下一 Tick 的安全时点）：消费未过期的按下沿，再对按住型输入做一次安全重试。 */
	void ProcessDeferredInputIntents();

	/** 短暂阻塞解除后的安全重试：只覆盖仍处于按住且未激活的 HeldRepeat Spec。 */
	void RetryHeldRepeatInputs();

	/** Spec 是否声明了「按住可恢复」输入行为。 */
	static bool HasHeldRepeatInputBehavior(const FGameplayAbilitySpec& Spec);

	/** 登记 / 覆盖一条按下沿。 */
	void RegisterBufferedInput(const FGameplayTag& InputTag, const UObject* Context);

	/** 是否为本机玩家自己的 ASC；只有它参与输入缓冲。 */
	bool ShouldBufferLocalInput() const;

	/** 条目上下文是否仍然成立。 */
	bool IsBufferedInputContextStillValid(const FShooterBufferedInput& Entry, const FGameplayAbilitySpec& Spec) const;

	/** 标准释放路径：可靠 RPC + 本地释放；真实松开与消费后的补释放共用。 */
	void ReleaseInputTag(FGameplayAbilitySpec& Spec);

	/** 注册 / 注销短暂阻塞标签监听。 */
	void RegisterTransientBlockedTagWatchers();
	void UnregisterTransientBlockedTagWatchers();

	/** 输入缓冲的开发构建可搜索标记；Shipping 不输出。 */
	void LogInputBufferMarker(const TCHAR* Marker, const FGameplayTag& InputTag, const TCHAR* Reason = nullptr,
		float WorldTime = 0.0f) const;

	/** 单次按下沿的保留窗口；绝对世界时间过期，重试不续期。 */
	UPROPERTY(EditDefaultsOnly, Category = "Shooter|Input")
	float InputBufferWindowSeconds = 0.15f;

	/** 只有这些标签造成的阻塞才被视为「动作马上结束」，可进入输入缓冲。 */
	UPROPERTY(EditDefaultsOnly, Category = "Shooter|Input")
	FGameplayTagContainer TransientInputBlockedTags;

	/** 待消费的按下沿；每个输入 Tag 最多一条。 */
	TMap<FGameplayTag, FShooterBufferedInput> BufferedInputs;

	/** 本次按下期间期待的 Ability 类；用于把失败回调归因到这次按下。 */
	TObjectPtr<UClass> PendingPressAbilityClass = nullptr;

	/** 本次按下期间使用的输入 Tag。 */
	FGameplayTag PendingPressInputTag;

	/** 是否正在处理一次按下；只有此时才登记新的按下沿。 */
	bool bPressInFlight = false;

	/** 是否已经安排下一 Tick 的输入缓冲处理。 */
	bool bBufferProcessScheduled = false;

	/** 短暂阻塞标签事件绑定。 */
	TMap<FGameplayTag, FDelegateHandle> TransientTagDelegateHandles;

#if WITH_DEV_AUTOMATION_TESTS
public:
	/** 测试观察接口：当前待消费的按下沿数量。 */
	int32 GetBufferedInputCountForTest() const { return BufferedInputs.Num(); }

	/** 测试观察接口：立即执行一次延迟意图处理（ProcessDeferredInputIntents）。 */
	void ProcessDeferredInputIntentsForTest();

	/** 测试观察接口：让所有待消费按下沿立即过期（不推进世界时间）。 */
	void ExpireBufferedInputsForTest();
#endif
};
