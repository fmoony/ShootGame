// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/ShooterGameplayAbility.h"
#include "ShooterAbilitySystemComponent.generated.h"

struct FGameplayAbilitySpec;
struct FGameplayTag;
struct FGameplayTagContainer;
class UGameplayAbility;

/**
 * ShootGame 项目通用 ASC：输入控制流分采集与解释两层，只产生通用输入意图，不产生任何 Gameplay 结果。
 *
 * 采集层（输入回调，只记事）：
 * - PressedInputTags：本帧 Press edge；
 * - ReleasedInputTags：本帧 Release edge；
 * - HeldInputTags：持续按住（Level 状态）。
 * 回调不解释 Gameplay：不尝试激活、不写 FGameplayAbilitySpec::InputPressed、不建立输入缓冲。
 *
 * 解释层（ProcessAbilityInput，唯一入口）：本机拥有者每帧一次，从上到下按序处理
 * 1. Buffered Press：Semi 单次按下沿被「短暂动作阻塞」拒绝后保留的固定窗口，
 *    窗口内每帧最多尝试一次；本地失败不删除也不续期，成功 / 过期 / 上下文变化才删除；
 * 2. Held：按住持续语义（ActivationPolicy == WhileInputActive），本帧新按下不从这里出发；
 * 3. Pressed：本帧按下沿，两种策略都尝试一次（同帧点击也要形成动作边界）；
 * 4. Released：真实松开走标准释放路径（可靠 ServerSetInputReleased + AbilitySpecInputReleased）。
 * 三个分支按 InputTag 互斥，同一 Tag 每帧最多一次激活尝试。
 *
 * 归因：只有 Press edge 尝试的失败才可能登记 Buffered Press。唯一的激活点
 * （TryActivateInputTag）负责设置 / 清除 CurrentPressAttemptInputTag，
 * HandleAbilityFailed 只按它做「这次失败是否值得进入 Semi 缓冲」的分类。
 *
 * 策略来源：由 Ability 通过 GetActivationPolicy(ActorInfo) 按当前 Gameplay Context 动态返回
 * （显式传 ActorInfo，不读未激活实例的 CurrentActorInfo，也不依赖任何外部同步的派生状态）。
 *
 * 边界：
 * - 不判断「现在能不能开火 / 换弹」，那是 Ability 与 GameplayTags 的职责；
 * - 不读 Weapon / Ammo / Inventory / Projectile，也不做 Ammo / Projectile 预测；
 * - 不调度 timer，不注册 blocker 监听，不记录服务器拒绝状态。
 */
UCLASS(ClassGroup=(Shooter))
class SHOOTGAME_API UShooterAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UShooterAbilitySystemComponent();

	/** 采集本帧 Press edge 与持续按住；非本机视图（服务器侧 NPC）保持既有的直通语义。 */
	void AbilityInputTagPressed(const FGameplayTag& InputTag);

	/** 采集本帧 Release edge 并退出持续按住；非本机视图保持既有的直通语义。 */
	void AbilityInputTagReleased(const FGameplayTag& InputTag);

	/**
	 * 唯一的 Gameplay 输入解释入口：由本机 PlayerController 的 PostProcessInput 驱动，
	 * 即本帧全部输入回调完成之后立即执行一次。
	 */
	void ProcessAbilityInput();

	/** 返回匹配输入 Tag 的第一个 Ability Spec；没有匹配时返回 nullptr。 */
	FGameplayAbilitySpec* FindAbilitySpecFromInputTag(const FGameplayTag& InputTag);

	/** 返回指定 Ability 类的 Spec 数量；宿主用它验证授予幂等性。 */
	int32 GetAbilitySpecCountForClass(TSubclassOf<UGameplayAbility> AbilityClass) const;

	/** 返回指定 Ability 类当前处于活动状态的 Spec 数量；网络测试用它验证单激活约束。 */
	int32 GetActiveAbilityCountForClass(TSubclassOf<UGameplayAbility> AbilityClass) const;

	/** 按输入 Tag 取消所有匹配 Ability；死亡、切枪、断线等清理链共用。 */
	void CancelAbilitiesByTag(const FGameplayTag& InputTag);

	/** 查询指定 Spec 的当前输入激活策略；无法解析 Ability 时按单次按下沿处理。 */
	EShooterAbilityActivationPolicy GetInputActivationPolicy(const FGameplayAbilitySpec& Spec) const;

	/**
	 * 作废所有旧输入意图：清本帧按下沿 / 松开沿 / 持续按住采集、清待消费按下沿，
	 * 并直接复位 Spec.InputPressed。
	 *
	 * 生命周期边界表达的是「旧输入意图作废」，不是「玩家产生了一次真实 Release」：
	 * 因此不派发 UGameplayAbility::InputReleased，也不发送任何 Release RPC。
	 * Avatar 变化 / ClearActorInfo / EndPlay 由本类自行调用；死亡由宿主显式调用。
	 */
	void InvalidateInputIntents();

	// 引擎把这三个函数声明为 public，覆写不得降低可见性；
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

	/** 登记 / 覆盖一条按下沿。 */
	void RegisterBufferedInput(const FGameplayTag& InputTag, const UObject* Context);

	/** 是否为本机玩家自己的 ASC；只有它参与输入解释。 */
	bool ShouldProcessLocalAbilityInput() const;

	/** 条目上下文是否仍然成立。 */
	bool IsBufferedInputContextStillValid(const FShooterBufferedInput& Entry, const FGameplayAbilitySpec& Spec) const;

	/** 标准释放路径：可靠 RPC + 本地释放；真实松开与消费后的补释放共用。 */
	void ReleaseInputTag(FGameplayAbilitySpec& Spec);

	/**
	 * 唯一的激活执行点：只负责设置 / 清除归因游标并调用引擎的 TryActivateAbility。
	 * 各输入分支的后续语义（消费窗口、补释放、间隔判定）留在分支自己身上，不由本函数承担。
	 */
	bool TryActivateInputTag(const FGameplayTag& InputTag, FGameplayAbilitySpec& Spec, bool bPressEdge);

	/** 输入标记的开发构建可搜索输出；Shipping 不输出。 */
	void LogInputBufferMarker(const TCHAR* Marker, const FGameplayTag& InputTag, const TCHAR* Reason = nullptr,
		float WorldTime = 0.0f) const;

	/** 单次按下沿的保留窗口；绝对世界时间过期，重试不续期。 */
	UPROPERTY(EditDefaultsOnly, Category = "Shooter|Input")
	float InputBufferWindowSeconds = 0.15f;

	/** 只有这些标签造成的阻塞才被视为「动作马上结束」，可进入短期输入缓冲。 */
	UPROPERTY(EditDefaultsOnly, Category = "Shooter|Input")
	FGameplayTagContainer TransientInputBlockedTags;

	/** 本帧 Press edge 采集；帧末清空。 */
	TArray<FGameplayTag> PressedInputTags;

	/** 本帧 Release edge 采集；帧末清空。 */
	TArray<FGameplayTag> ReleasedInputTags;

	/** 持续按住采集（Level）；只由 Press / Release 回调与 InvalidateInputIntents 改变。 */
	TArray<FGameplayTag> HeldInputTags;

	/** 待消费的按下沿；每个输入 Tag 最多一条。 */
	TMap<FGameplayTag, FShooterBufferedInput> BufferedInputs;

	/** 归因游标：只在一次 Press edge 尝试期间有效，其余分支的尝试不设置它。 */
	FGameplayTag CurrentPressAttemptInputTag;

#if WITH_DEV_AUTOMATION_TESTS
public:
	/** 测试观察接口：当前待消费的按下沿数量。 */
	int32 GetBufferedInputCountForTest() const { return BufferedInputs.Num(); }

	/** 测试观察接口：当前持续按住的采集数量。 */
	int32 GetHeldInputTagCountForTest() const { return HeldInputTags.Num(); }

	/** 测试观察接口：立即执行一次输入处理（ProcessAbilityInput）。 */
	void ProcessAbilityInputForTest();

	/** 测试观察接口：让所有待消费按下沿立即过期（不推进世界时间）。 */
	void ExpireBufferedInputsForTest();
#endif
};
