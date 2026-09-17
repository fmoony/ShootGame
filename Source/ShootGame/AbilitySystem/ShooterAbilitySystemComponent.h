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
 * ShootGame 项目通用 ASC。
 *
 * 职责边界：
 * - 把输入 Tag 稳定映射到 Ability Spec（按下 / 松开）；
 * - 保留「短暂动作阻塞」期间的一次按下沿，并在安全时点重新尝试（输入缓冲）；
 * - 不判断「现在能不能开火 / 换弹」——那是 Ability 与 GameplayTags 的职责；
 * - 不读 Weapon / Ammo / Inventory / Projectile，也不产生任何 Gameplay 结果。
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

	/** 短暂阻塞标签计数归零：只登记待处理，调度到 Tag 回调栈退出后的安全时点。 */
	void HandleTransientBlockedTagChanged(FGameplayTag Tag, int32 NewCount);

	/** 硬阻塞标签出现：立即作废所有待消费意图。 */
	void HandleFatalBlockedTagChanged(FGameplayTag Tag, int32 NewCount);

	/** 请求在下一 Tick 处理输入缓冲；同帧多次请求只执行一次。 */
	void RequestBufferedInputProcessing();

	/** 安全时点：消费未过期的按下沿，并对按住型输入做有限再武装。 */
	void ProcessBufferedInputs();

	/** 消费一条按下沿；返回是否真的形成了本地动作边界。 */
	bool ConsumeBufferedInput(FGameplayAbilitySpec& Spec);

	/** 按住型输入再武装：只在输入仍按住、Ability 未激活时尝试。 */
	void ReArmSustainedInputs();

	/** 按住型输入再武装的实际尝试；返回是否发起了激活。 */
	bool TryReArmSustainedInput(FGameplayAbilitySpec& Spec);

	/** 安排一次按住型输入的有限重试检查。 */
	void ScheduleHeldRetryCheck();

	/** 按住型输入的有限重试检查回调。 */
	void HandleHeldRetryCheck();

	/** 登记 / 覆盖一条按下沿。 */
	void RegisterBufferedInput(const FGameplayTag& InputTag, const UObject* Context);

	/** 移除指定输入 Tag 的按下沿；返回是否移除过。 */
	bool RemoveBufferedInput(const FGameplayTag& InputTag);

	/** 清空所有待消费意图与按住型重试状态。 */
	void ClearBufferedInputs();

	/** 停止按住型重试定时器。 */
	void ClearHeldRetryTimer();

	/** 是否为本机玩家自己的 ASC；只有它参与输入缓冲。 */
	bool ShouldBufferLocalInput() const;

	/** 条目上下文是否仍然成立。 */
	bool IsBufferedInputContextStillValid(const FShooterBufferedInput& Entry, const FGameplayAbilitySpec& Spec) const;

	/** 标准释放路径：可靠 RPC + 本地释放；真实松开与消费后的补释放共用。 */
	void ReleaseInputTag(FGameplayAbilitySpec& Spec);

	/** 注册 / 注销短暂阻塞与硬阻塞标签监听。 */
	void RegisterInputTagWatchers();
	void UnregisterInputTagWatchers();

	/** 输入缓冲的开发构建可搜索标记；Shipping 不输出。 */
	void LogInputBufferMarker(const TCHAR* Marker, const FGameplayTag& InputTag, const TCHAR* Reason = nullptr,
		float WorldTime = 0.0f) const;

	/** 单次按下沿的保留窗口；绝对世界时间过期，重试不续期。 */
	UPROPERTY(EditDefaultsOnly, Category = "Shooter|Input")
	float InputBufferWindowSeconds = 0.15f;

	/** 只有这些标签造成的阻塞才被视为「动作马上结束」，可进入输入缓冲。 */
	UPROPERTY(EditDefaultsOnly, Category = "Shooter|Input")
	FGameplayTagContainer TransientInputBlockedTags;

	/** 这些标签出现时，生命周期已失效，所有待消费意图立即作废。 */
	UPROPERTY(EditDefaultsOnly, Category = "Shooter|Input")
	FGameplayTagContainer FatalInputBlockedTags;

	/** 按住型输入在一个按下周期内允许的有限再尝试次数。 */
	UPROPERTY(EditDefaultsOnly, Category = "Shooter|Input")
	int32 MaxHeldRetryPerPress = 2;

	/** 按住型输入有限再尝试的间隔（秒）。 */
	UPROPERTY(EditDefaultsOnly, Category = "Shooter|Input")
	float HeldRetryIntervalSeconds = 0.2f;

	/** 待消费的按下沿；每个输入 Tag 最多一条。 */
	TArray<FShooterBufferedInput> BufferedInputs;

	/** 本次按下期间期待的 Ability 类；用于把失败回调归因到这次按下。 */
	TObjectPtr<UClass> PendingPressAbilityClass = nullptr;

	/** 本次按下期间使用的输入 Tag。 */
	FGameplayTag PendingPressInputTag;

	/** 是否正在处理一次按下；只有此时才登记新的按下沿。 */
	bool bPressInFlight = false;

	/** 是否已经安排下一 Tick 的输入缓冲处理。 */
	bool bBufferProcessScheduled = false;

	/** 按住型输入在本次按下周期内已尝试过的再武装次数。 */
	int32 HeldReArmAttempts = 0;

	/** 按住型输入的有限重试 Timer。 */
	FTimerHandle HeldRetryTimer;

	/** 短暂阻塞标签事件绑定。 */
	TMap<FGameplayTag, FDelegateHandle> TransientTagDelegateHandles;

	/** 硬阻塞标签事件绑定。 */
	TMap<FGameplayTag, FDelegateHandle> FatalTagDelegateHandles;

#if WITH_DEV_AUTOMATION_TESTS
public:
	/** 测试观察接口：当前待消费的按下沿数量。 */
	int32 GetBufferedInputCountForTest() const { return BufferedInputs.Num(); }

	/** 测试观察接口：指定输入 Tag 是否存在待消费的按下沿。 */
	bool HasBufferedInputForTest(const FGameplayTag& InputTag) const { return BufferedInputs.ContainsByPredicate(
		[&InputTag](const FShooterBufferedInput& Entry) { return Entry.InputTag == InputTag; }); }

	/** 测试观察接口：立即执行一次安全时点处理。 */
	void ProcessBufferedInputsForTest();

	/** 测试观察接口：让所有待消费按下沿立即过期（不推进世界时间）。 */
	void ExpireBufferedInputsForTest();

	/** 测试观察接口：本次按下周期已尝试的按住型再武装次数。 */
	int32 GetHeldReArmAttemptsForTest() const { return HeldReArmAttempts; }
#endif
};
