// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterAbilitySystemComponent.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/ShooterGameplayTags.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "ShootGame.h"

namespace
{
	/** 取 Spec 上真正执行的 Ability 实例；未实例化时回落到 CDO。 */
	const UShooterGameplayAbility* GetShooterAbilityForSpec(const FGameplayAbilitySpec& Spec)
	{
		const UGameplayAbility* Instance = Spec.GetPrimaryInstance();
		return Cast<UShooterGameplayAbility>(Instance ? Instance : Spec.Ability.Get());
	}
}

UShooterAbilitySystemComponent::UShooterAbilitySystemComponent()
{
	// 短暂阻塞：动作马上结束，单次按下沿值得保留到窗口结束。
	// 硬失败（本机节拍未 Ready、武器无效）不在这里登记，因而永远不会进入短期缓冲。
	TransientInputBlockedTags.AddTag(ShooterGameplayTags::State_Reloading);
	TransientInputBlockedTags.AddTag(ShooterGameplayTags::State_Equipping);
}

void UShooterAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
{
	FGameplayAbilitySpec* Spec = FindAbilitySpecFromInputTag(InputTag);
	if (!Spec)
	{
		UE_LOG(LogShootGame, Verbose, TEXT("AbilityInputTagPressed: no ability spec for InputTag=%s Owner=%s"),
			*InputTag.ToString(), *GetNameSafe(GetOwnerActor()));
		return;
	}

	// 非本机拥有者视图（服务器上的 NPC）没有每帧输入解释入口，保持既有的「按下即尝试一次」直通语义。
	// 采集集合只属于本机拥有者视图，这里不写，避免产生无人清理的残留。
	if (!ShouldProcessLocalAbilityInput())
	{
		AbilitySpecInputPressed(*Spec);
		if (!Spec->IsActive())
		{
			TryActivateAbility(Spec->Handle, true);
		}
		return;
	}

	// 采集层：只记录本帧按下沿与持续按住，不尝试激活、不写 Spec.InputPressed、不建立输入缓冲。
	PressedInputTags.AddUnique(InputTag);
	HeldInputTags.AddUnique(InputTag);
}

void UShooterAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag& InputTag)
{
	FGameplayAbilitySpec* Spec = FindAbilitySpecFromInputTag(InputTag);
	if (!Spec)
	{
		UE_LOG(LogShootGame, Verbose, TEXT("AbilityInputTagReleased: no ability spec for InputTag=%s Owner=%s"),
			*InputTag.ToString(), *GetNameSafe(GetOwnerActor()));
		return;
	}

	// 非本机拥有者视图同样直通：没有每帧输入解释入口，立即执行标准释放。
	if (!ShouldProcessLocalAbilityInput())
	{
		ReleaseInputTag(*Spec);
		return;
	}

	// 采集层：只记录本帧松开并退出持续按住；释放业务在 ProcessAbilityInput 里统一执行。
	ReleasedInputTags.AddUnique(InputTag);
	HeldInputTags.RemoveSingleSwap(InputTag);
}

void UShooterAbilitySystemComponent::ProcessAbilityInput()
{
	if (!ShouldProcessLocalAbilityInput())
	{
		// 非本机拥有者视图不参与输入解释：只清本机采集残留。
		// 权威端自己的 Spec.InputPressed 属于引擎 RPC 真值，不得由这里复位。
		PressedInputTags.Reset();
		ReleasedInputTags.Reset();
		HeldInputTags.Reset();
		BufferedInputs.Reset();
		return;
	}

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;

	// 短期按下沿（只有 OnInputTriggered 会有条目）：
	// 固定窗口内每帧最多尝试一次；本地失败既不删除也不续期，成功 / 过期 / 上下文变化才删除。
	// 遍历用键快照：成功路径会在遍历中移除条目。
	TArray<FGameplayTag> BufferedInputTags;
	BufferedInputs.GetKeys(BufferedInputTags);
	for (const FGameplayTag& InputTag : BufferedInputTags)
	{
		if (PressedInputTags.Contains(InputTag))
		{
			// 同一 Tag 出现新的 Press edge：旧窗口立即作废。
			// 新边若再被短暂阻塞，由失败分类登记一个新的固定窗口。
			BufferedInputs.Remove(InputTag);
			continue;
		}

		const FShooterBufferedInput* Entry = BufferedInputs.Find(InputTag);
		if (!Entry)
		{
			continue;
		}

		FGameplayAbilitySpec* Spec = FindAbilitySpecFromInputTag(InputTag);
		if (!Spec)
		{
			BufferedInputs.Remove(InputTag);
			continue;
		}

		if (Now > Entry->ExpireTime)
		{
			// 过期：不激活、不表现、不请求，失败不续期。
			BufferedInputs.Remove(InputTag);
			LogInputBufferMarker(TEXT("INPUT_BUFFER_DROPPED"), InputTag, TEXT("Expired"), Now);
			continue;
		}

		if (!IsBufferedInputContextStillValid(*Entry, *Spec))
		{
			// 上下文变化（例如换枪提交、武器归还池）后不得在新上下文上消费旧输入。
			BufferedInputs.Remove(InputTag);
			LogInputBufferMarker(TEXT("INPUT_BUFFER_DROPPED"), InputTag, TEXT("ContextChanged"), Now);
			continue;
		}

		const bool bActivated = TryActivateInputTag(InputTag, *Spec, /*bPressEdge*/ false);
		if (!bActivated)
		{
			// 本地失败：窗口保留到绝对过期时间，既不移除也不续期。
			LogInputBufferMarker(TEXT("INPUT_BUFFER_RETRY"), InputTag, TEXT("Rejected"), Now);
			continue;
		}

		// 已经形成本地动作边界：消费该窗口。
		BufferedInputs.Remove(InputTag);
		if (!Spec->InputPressed)
		{
			// 按下沿在消费前已经松开：立即走标准释放路径，
			// 避免残留活动 Ability 与不会结束的本地预测实例。
			ReleaseInputTag(*Spec);
		}

		LogInputBufferMarker(TEXT("INPUT_BUFFER_CONSUMED"), InputTag, TEXT("Activated"), Now);
	}

	// 按住持续（WhileInputActive）：意图只由 Held 集合表达。
	// 本帧新按下不从本分支出发，避免同一 Tag 在一帧内产生两种来源的尝试。
	for (const FGameplayTag& InputTag : HeldInputTags)
	{
		if (PressedInputTags.Contains(InputTag))
		{
			continue;
		}

		FGameplayAbilitySpec* Spec = FindAbilitySpecFromInputTag(InputTag);
		if (!Spec || !Spec->Ability || Spec->IsActive())
		{
			continue;
		}

		if (GetInputActivationPolicy(*Spec) != EShooterAbilityActivationPolicy::WhileInputActive)
		{
			continue;
		}

		const bool bActivated = TryActivateInputTag(InputTag, *Spec, /*bPressEdge*/ false);
		LogInputBufferMarker(TEXT("INPUT_HELD_RETRY"), InputTag, bActivated ? TEXT("Activated") : TEXT("Rejected"), Now);
	}

	// 本帧按下沿：两种策略都至少尝试一次，
	// 因为「按下与松开落在同一帧」的点击也必须形成一次本地动作边界。
	for (const FGameplayTag& InputTag : PressedInputTags)
	{
		FGameplayAbilitySpec* Spec = FindAbilitySpecFromInputTag(InputTag);
		if (!Spec || !Spec->Ability)
		{
			continue;
		}

		// 引擎标准输入入口：写 Spec.InputPressed，并在 Ability 已活动时把按下事件转发给活动实例。
		// 本次请求的「是否按住」语义由它承载，权威端的全自动门控依赖该值。
		AbilitySpecInputPressed(*Spec);

		if (Spec->IsActive())
		{
			// 已激活的 Ability 只完成输入事件转发，不再尝试激活。
			continue;
		}

		TryActivateInputTag(InputTag, *Spec, /*bPressEdge*/ true);
	}

	// 本帧松开：真实松开仍然走可靠 ServerSetInputReleased + AbilitySpecInputReleased。
	for (const FGameplayTag& InputTag : ReleasedInputTags)
	{
		if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromInputTag(InputTag))
		{
			ReleaseInputTag(*Spec);
		}
	}

	// 只清帧级采集；Held 集合由 Press / Release 回调与 InvalidateInputIntents 改变。
	PressedInputTags.Reset();
	ReleasedInputTags.Reset();
}

FGameplayAbilitySpec* UShooterAbilitySystemComponent::FindAbilitySpecFromInputTag(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return nullptr;
	}

	for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (!Spec.Ability)
		{
			continue;
		}

		// AssetTags 来自 Ability CDO，表示 Ability 类型自身的固定标签；
		// DynamicSpecSourceTags 属于具体 AbilitySpec，可在 GiveAbility 时为本次授予追加标签。
		if (Spec.Ability->GetAssetTags().HasTagExact(InputTag) || Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			return &Spec;
		}
	}

	return nullptr;
}

int32 UShooterAbilitySystemComponent::GetAbilitySpecCountForClass(TSubclassOf<UGameplayAbility> AbilityClass) const
{
	if (!AbilityClass)
	{
		return 0;
	}

	int32 Count = 0;
	for (const FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->IsA(AbilityClass))
		{
			++Count;
		}
	}

	return Count;
}

int32 UShooterAbilitySystemComponent::GetActiveAbilityCountForClass(TSubclassOf<UGameplayAbility> AbilityClass) const
{
	if (!AbilityClass)
	{
		return 0;
	}

	int32 Count = 0;
	for (const FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (Spec.IsActive() && Spec.Ability && Spec.Ability->IsA(AbilityClass))
		{
			++Count;
		}
	}

	return Count;
}

void UShooterAbilitySystemComponent::CancelAbilitiesByTag(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return;
	}

	FGameplayTagContainer CancelTags;
	CancelTags.AddTag(InputTag);
	CancelAbilities(&CancelTags);
}

EShooterAbilityActivationPolicy UShooterAbilitySystemComponent::GetInputActivationPolicy(const FGameplayAbilitySpec& Spec) const
{
	const UShooterGameplayAbility* ShooterAbility = GetShooterAbilityForSpec(Spec);
	if (!ShooterAbility)
	{
		return EShooterAbilityActivationPolicy::OnInputTriggered;
	}

	// 显式传本 ASC 当前的 ActorInfo：未激活（或重生后尚未再次激活）的 Ability 实例上没有
	// CurrentActorInfo，策略查询不得读实例缓存，因此由这里提供权威的运行时上下文。
	return ShooterAbility->GetActivationPolicy(AbilityActorInfo.Get());
}

void UShooterAbilitySystemComponent::InvalidateInputIntents()
{
	// 生命周期边界表达的是「旧输入意图作废」，不是「玩家产生了一次真实 Release」：
	// Spec.InputPressed 是引擎标记为 NotReplicated 的本地真值，直接复位即可维护引擎不变量，
	// 因此这里不派发 UGameplayAbility::InputReleased，也不发送任何 Release RPC。
	// 采集状态、宽容窗口与输入真值必须一起作废，否则会留下「仍然按着」的残留意图。
	PressedInputTags.Reset();
	ReleasedInputTags.Reset();
	HeldInputTags.Reset();
	BufferedInputs.Reset();

	for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		Spec.InputPressed = false;
	}
}

void UShooterAbilitySystemComponent::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor)
{
	// Avatar 变化（重生 / 换 Pawn）后，旧生命周期的输入意图不得延续。
	InvalidateInputIntents();

	Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);

	// 失败回调按实例绑定；ActorInfo 重建时只保留一份。
	AbilityFailedCallbacks.RemoveAll(this);
	AbilityFailedCallbacks.AddUObject(this, &UShooterAbilitySystemComponent::HandleAbilityFailed);
}

void UShooterAbilitySystemComponent::ClearActorInfo()
{
	InvalidateInputIntents();
	AbilityFailedCallbacks.RemoveAll(this);

	Super::ClearActorInfo();
}

void UShooterAbilitySystemComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	InvalidateInputIntents();
	AbilityFailedCallbacks.RemoveAll(this);

	Super::EndPlay(EndPlayReason);
}

void UShooterAbilitySystemComponent::HandleAbilityFailed(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureTags)
{
	// 只有 Press edge 尝试的失败才可能进入 Semi 缓冲：Held 与 Buffered 分支的尝试不设置归因游标，
	// 因此它们既不能创建窗口，也不能刷新窗口的过期时间。
	if (!CurrentPressAttemptInputTag.IsValid() || !Ability || !ShouldProcessLocalAbilityInput())
	{
		return;
	}

	FGameplayAbilitySpec* Spec = FindAbilitySpecFromInputTag(CurrentPressAttemptInputTag);
	if (!Spec || !Spec->Ability)
	{
		return;
	}

	// 失败回调必须属于本次 Press 尝试的 Ability：引擎对 InstancedPerActor 传主实例，
	// 也会在「本地 / 服务器执行策略不允许」的早退路径上传 CDO，因此按类比对。
	if (Ability->GetClass() != Spec->Ability->GetClass())
	{
		return;
	}

	// 只有项目 Ability 参与短期按下沿：它们才能声明输入上下文。
	const UShooterGameplayAbility* ShooterAbility = GetShooterAbilityForSpec(*Spec);
	if (!ShooterAbility)
	{
		return;
	}

	// 失败原因只能从引擎追加的具体阻塞标签推导：
	// GAS 的 Tag 门控会把「本机 OwnedTags ∩ ActivationBlockedTags」追加进失败容器，
	// 而通用失败标签在本项目没有配置，不能依赖。
	FGameplayTagContainer OwnedTags;
	GetOwnedGameplayTags(OwnedTags);
	FGameplayTagContainer BlockingTags;
	BlockingTags.AppendMatchingTags(OwnedTags, FailureTags);
	if (BlockingTags.IsEmpty())
	{
		// 不是被自身标签挡住：本地节拍未 Ready、武器无效、自定义否决等都是硬失败，不保留。
		return;
	}

	if (ShooterAbility->IsBlockedByOwnActiveTags(BlockingTags))
	{
		// 阻塞来自该动作自身（例如换弹中再按换弹）：重复按下没有意义，不保留。
		return;
	}

	if (!TransientInputBlockedTags.HasAll(BlockingTags))
	{
		// 含未登记的阻塞标签（例如 State.Dead）：不保留。
		return;
	}

	// WhileInputActive 不登记短期按下沿：它的持续意图只由 Held 集合表达，Release 即终止；
	// 阻塞解除后的重试由 ProcessAbilityInput 的 Held 分支负责，不需要第二条意图表达。
	if (GetInputActivationPolicy(*Spec) == EShooterAbilityActivationPolicy::WhileInputActive)
	{
		return;
	}

	RegisterBufferedInput(CurrentPressAttemptInputTag, ShooterAbility->GetInputBufferContext());
}

void UShooterAbilitySystemComponent::RegisterBufferedInput(const FGameplayTag& InputTag, const UObject* Context)
{
	const UWorld* World = GetWorld();
	if (!World || !InputTag.IsValid())
	{
		return;
	}

	// 同一个输入 Tag 只保留最新的一条按下沿；新的按下是新意图，允许重置窗口。
	FShooterBufferedInput& Entry = BufferedInputs.FindOrAdd(InputTag);
	Entry.InputTag = InputTag;
	Entry.Context = Context;
	Entry.bHasContext = Context != nullptr;
	Entry.ExpireTime = World->GetTimeSeconds() + FMath::Max(InputBufferWindowSeconds, 0.0f);

	UE_LOG(LogShootGame, Verbose, TEXT("Input buffered: InputTag=%s Window=%.3f Owner=%s"),
		*InputTag.ToString(), InputBufferWindowSeconds, *GetNameSafe(GetOwnerActor()));

	// 开发构建输出可搜索标记，供网络场景与自动化定位输入行为。
	LogInputBufferMarker(TEXT("INPUT_BUFFER_REGISTERED"), InputTag, TEXT("Registered"), World->GetTimeSeconds());
}

bool UShooterAbilitySystemComponent::ShouldProcessLocalAbilityInput() const
{
	// 只有本机玩家自己的 ASC 参与输入解释：
	// 服务器上的远端玩家 ASC 与 NPC 保持既有语义，避免改变权威侧行为。
	const FGameplayAbilityActorInfo* ActorInfo = AbilityActorInfo.Get();
	return ActorInfo && ActorInfo->IsLocallyControlledPlayer();
}

bool UShooterAbilitySystemComponent::IsBufferedInputContextStillValid(const FShooterBufferedInput& Entry,
	const FGameplayAbilitySpec& Spec) const
{
	if (!Entry.bHasContext)
	{
		// 按下时没有约定上下文：不做上下文校验。
		return true;
	}

	const UShooterGameplayAbility* ShooterAbility = GetShooterAbilityForSpec(Spec);
	const UObject* CurrentContext = ShooterAbility ? ShooterAbility->GetInputBufferContext() : nullptr;

	// 上下文变化（例如换枪提交、武器归还池）后不得在新上下文上消费旧输入。
	return Entry.Context.Get() == CurrentContext;
}

void UShooterAbilitySystemComponent::ReleaseInputTag(FGameplayAbilitySpec& Spec)
{
	// 非权威端先经可靠 Server RPC 转发松开；服务器收到后由 AbilitySpecInputReleased 分发。
	if (!IsOwnerActorAuthoritative())
	{
		ServerSetInputReleased(Spec.Handle);
	}

	AbilitySpecInputReleased(Spec);
}

bool UShooterAbilitySystemComponent::TryActivateInputTag(const FGameplayTag& InputTag, FGameplayAbilitySpec& Spec, bool bPressEdge)
{
	// 归因游标：只有 Press edge 尝试期间有效。HandleAbilityFailed 只按它判定
	// 「这次失败是否值得进入 Semi 缓冲」，因此其余分支的失败不可能创建窗口。
	CurrentPressAttemptInputTag = bPressEdge ? InputTag : FGameplayTag();
	const bool bActivated = TryActivateAbility(Spec.Handle, true);
	CurrentPressAttemptInputTag = FGameplayTag();
	return bActivated;
}

void UShooterAbilitySystemComponent::LogInputBufferMarker(const TCHAR* Marker, const FGameplayTag& InputTag,
	const TCHAR* Reason, float WorldTime) const
{
#if WITH_DEV_AUTOMATION_TESTS
	// 与 FIRE_* 标记一致：只在开发构建输出，Shipping 不增加日志带宽。
	UE_LOG(LogShootGame, Display, TEXT("%s InputTag=%s Reason=%s WorldTime=%.3f Owner=%s"),
		Marker, *InputTag.ToString(), Reason ? Reason : TEXT("-"), WorldTime, *GetNameSafe(GetOwnerActor()));
#else
	(void)Marker;
	(void)InputTag;
	(void)Reason;
	(void)WorldTime;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
void UShooterAbilitySystemComponent::ProcessAbilityInputForTest()
{
	ProcessAbilityInput();
}

void UShooterAbilitySystemComponent::ExpireBufferedInputsForTest()
{
	const UWorld* World = GetWorld();
	const float ExpiredTime = World ? World->GetTimeSeconds() - 1.0f : -1.0f;
	for (TPair<FGameplayTag, FShooterBufferedInput>& Pair : BufferedInputs)
	{
		Pair.Value.ExpireTime = ExpiredTime;
	}
}
#endif
