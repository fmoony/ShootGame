// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterAbilitySystemComponent.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/ShooterGameplayAbility.h"
#include "AbilitySystem/ShooterGameplayTags.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"
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
	// 短暂阻塞：动作马上结束，输入值得保留到窗口结束。
	TransientInputBlockedTags.AddTag(ShooterGameplayTags::State_Reloading);
	TransientInputBlockedTags.AddTag(ShooterGameplayTags::State_Equipping);

	// 硬阻塞：生命周期已失效，任何待消费意图都必须立即作废。
	FatalInputBlockedTags.AddTag(ShooterGameplayTags::State_Dead);
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

	// 先把按下状态写进 Spec，再尝试激活。
	// 对 LocalPredicted Ability：客户端 TryActivateAbility 会走 CallServerTryActivateAbility；
	// 服务器端则直接进入 InternalTryActivateAbility。
	AbilitySpecInputPressed(*Spec);

	// 新的按下是新的意图：重置按住型输入的有限重试计数。
	HeldReArmAttempts = 0;

	if (Spec->IsActive())
	{
		return;
	}

	if (!ShouldBufferLocalInput())
	{
		// 服务器上的远端玩家 ASC 与 NPC 不参与输入缓冲：保持既有的「失败即结束」语义。
		TryActivateAbility(Spec->Handle, true);
		return;
	}

	// 本次按下期间的所有激活失败都归因到这个 Ability，失败分类见 HandleAbilityFailed。
	PendingPressAbilityClass = Spec->Ability ? Spec->Ability->GetClass() : nullptr;
	PendingPressInputTag = InputTag;
	bPressInFlight = true;
	const bool bActivated = TryActivateAbility(Spec->Handle, true);
	bPressInFlight = false;
	PendingPressAbilityClass = nullptr;
	PendingPressInputTag = FGameplayTag();

	if (bActivated)
	{
		// 这次按下已经形成本地动作边界：同一个输入 Tag 的旧按下沿失效。
		RemoveBufferedInput(InputTag);
	}
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

	// 真实松开：先经可靠 Server RPC 把松开转发到服务器，再分发给本地活动 Ability 实例。
	ReleaseInputTag(*Spec);

	// 松开不立即丢弃按下沿：半自动仍可能在窗口内形成一次本地动作边界，条目过期时自然移除。
	// 但按住型输入的再武装必须以「仍处于按住」为前提，因此这里只清理重试状态。
	HeldReArmAttempts = 0;
	ClearHeldRetryTimer();
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

void UShooterAbilitySystemComponent::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor)
{
	// Avatar 变化（重生 / 换 Pawn）后，旧生命周期的输入意图不得延续。
	ClearBufferedInputs();

	Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);

	// 失败回调与标签监听按实例绑定；ActorInfo 重建时只保留一份。
	AbilityFailedCallbacks.RemoveAll(this);
	AbilityFailedCallbacks.AddUObject(this, &UShooterAbilitySystemComponent::HandleAbilityFailed);
	RegisterInputTagWatchers();
}

void UShooterAbilitySystemComponent::ClearActorInfo()
{
	ClearBufferedInputs();
	UnregisterInputTagWatchers();
	AbilityFailedCallbacks.RemoveAll(this);

	Super::ClearActorInfo();
}

void UShooterAbilitySystemComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearBufferedInputs();
	UnregisterInputTagWatchers();
	AbilityFailedCallbacks.RemoveAll(this);

	Super::EndPlay(EndPlayReason);
}

void UShooterAbilitySystemComponent::HandleAbilityFailed(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureTags)
{
	if (!bPressInFlight || !Ability || !ShouldBufferLocalInput())
	{
		return;
	}

	if (PendingPressAbilityClass && Ability->GetClass() != PendingPressAbilityClass)
	{
		return;
	}

	// 只有项目 Ability 参与输入缓冲：它们才能声明输入语义与上下文。
	const UShooterGameplayAbility* ShooterAbility = Cast<UShooterGameplayAbility>(Ability);
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
		// 含硬阻塞（例如 State.Dead）或未登记标签：不保留。
		return;
	}

	RegisterBufferedInput(PendingPressInputTag, ShooterAbility->GetInputBufferContext());
}

void UShooterAbilitySystemComponent::HandleTransientBlockedTagChanged(FGameplayTag Tag, int32 NewCount)
{
	// 只在阻塞解除时安排处理；不在标签回调栈内直接重入激活。
	if (NewCount > 0)
	{
		return;
	}

	LogInputBufferMarker(TEXT("INPUT_BUFFER_TAG_CLEARED"), Tag, TEXT("BlockEnded"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f);
	RequestBufferedInputProcessing();
}

void UShooterAbilitySystemComponent::HandleFatalBlockedTagChanged(FGameplayTag /*Tag*/, int32 NewCount)
{
	if (NewCount <= 0)
	{
		return;
	}

	// 生命周期已失效：任何待消费意图立即作废。
	ClearBufferedInputs();
}

void UShooterAbilitySystemComponent::RequestBufferedInputProcessing()
{
	if (bBufferProcessScheduled)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 合并到当前 GAS 清理调用栈退出后的安全时点：下一 Tick 只处理一次。
	bBufferProcessScheduled = true;
	World->GetTimerManager().SetTimerForNextTick(this, &UShooterAbilitySystemComponent::ProcessBufferedInputs);
}

void UShooterAbilitySystemComponent::ProcessBufferedInputs()
{
	bBufferProcessScheduled = false;

	if (!ShouldBufferLocalInput())
	{
		ClearBufferedInputs();
		return;
	}

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;

	// 1. 消费仍未过期的按下沿：一次输入只对应一次尝试。
	while (BufferedInputs.Num() > 0)
	{
		const FShooterBufferedInput Entry = BufferedInputs[0];
		BufferedInputs.RemoveAt(0);

		if (Now > Entry.ExpireTime)
		{
			// 过期：不激活、不表现、不请求，也不重建条目。
			LogInputBufferMarker(TEXT("INPUT_BUFFER_DROPPED"), Entry.InputTag, TEXT("Expired"), Now);
			continue;
		}

		FGameplayAbilitySpec* Spec = FindAbilitySpecFromInputTag(Entry.InputTag);
		if (!Spec || !IsBufferedInputContextStillValid(Entry, *Spec))
		{
			LogInputBufferMarker(TEXT("INPUT_BUFFER_DROPPED"), Entry.InputTag, TEXT("ContextChanged"), Now);
			continue;
		}

		const bool bActivated = ConsumeBufferedInput(*Spec);
		LogInputBufferMarker(TEXT("INPUT_BUFFER_CONSUMED"), Entry.InputTag,
			bActivated ? TEXT("Activated") : TEXT("Rejected"), Now);

		// 按住型输入仍未被激活：允许有限次数的后续再尝试
		// （典型场景：客户端本地换弹窗口先结束，服务器还在换弹并 Reject）。
		const UShooterGameplayAbility* ShooterAbility = GetShooterAbilityForSpec(*Spec);
		const bool bSustained = ShooterAbility && ShooterAbility->IsSustainedInputAbility();
		if (bSustained && Spec->InputPressed && !Spec->IsActive())
		{
			ScheduleHeldRetryCheck();
		}
	}

	// 2. 按住型输入再武装：覆盖「Ability 已被取消，但玩家一直按住」的场景。
	ReArmSustainedInputs();
}

bool UShooterAbilitySystemComponent::ConsumeBufferedInput(FGameplayAbilitySpec& Spec)
{
	if (Spec.IsActive())
	{
		return false;
	}

	// 按下沿在消费时是否仍按住，直接读引擎的 Spec 状态，不维护第二份布尔。
	const bool bStillHeld = Spec.InputPressed;

	// 延迟消费不再登记新条目：这里的失败不会重建、不会续期。
	const bool bActivated = TryActivateAbility(Spec.Handle, true);

	if (bActivated && !bStillHeld)
	{
		// 按下沿在消费前已经松开：立即走标准释放路径，
		// 避免残留活动 Ability 与不会结束的本地预测实例。
		ReleaseInputTag(Spec);
	}

	return bActivated;
}

void UShooterAbilitySystemComponent::ReArmSustainedInputs()
{
	if (!ShouldBufferLocalInput() || HeldReArmAttempts >= MaxHeldRetryPerPress)
	{
		return;
	}

	for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (Spec.IsActive() || !Spec.InputPressed)
		{
			continue;
		}

		TryReArmSustainedInput(Spec);
	}
}

bool UShooterAbilitySystemComponent::TryReArmSustainedInput(FGameplayAbilitySpec& Spec)
{
	const UShooterGameplayAbility* ShooterAbility = GetShooterAbilityForSpec(Spec);
	if (!ShooterAbility || !ShooterAbility->IsSustainedInputAbility())
	{
		return false;
	}

	++HeldReArmAttempts;
	const bool bActivated = TryActivateAbility(Spec.Handle, true);

	if (Spec.InputPressed && !Spec.IsActive())
	{
		ScheduleHeldRetryCheck();
	}

	return bActivated;
}

void UShooterAbilitySystemComponent::ScheduleHeldRetryCheck()
{
	UWorld* World = GetWorld();
	if (!World || HeldRetryTimer.IsValid() || HeldReArmAttempts >= MaxHeldRetryPerPress)
	{
		return;
	}

	World->GetTimerManager().SetTimer(HeldRetryTimer, this, &UShooterAbilitySystemComponent::HandleHeldRetryCheck,
		FMath::Max(HeldRetryIntervalSeconds, 0.01f), /*bLoop*/ false);
}

void UShooterAbilitySystemComponent::HandleHeldRetryCheck()
{
	HeldRetryTimer.Invalidate();

	if (HeldReArmAttempts >= MaxHeldRetryPerPress)
	{
		return;
	}

	ReArmSustainedInputs();
}

void UShooterAbilitySystemComponent::RegisterBufferedInput(const FGameplayTag& InputTag, const UObject* Context)
{
	const UWorld* World = GetWorld();
	if (!World || !InputTag.IsValid())
	{
		return;
	}

	const float ExpireTime = World->GetTimeSeconds() + FMath::Max(InputBufferWindowSeconds, 0.0f);

	// 同一个输入 Tag 只保留最新的一条按下沿；新的按下是新意图，允许重置窗口。
	for (FShooterBufferedInput& Entry : BufferedInputs)
	{
		if (Entry.InputTag == InputTag)
		{
			Entry.Context = Context;
			Entry.bHasContext = Context != nullptr;
			Entry.ExpireTime = ExpireTime;
			return;
		}
	}

	FShooterBufferedInput NewEntry;
	NewEntry.InputTag = InputTag;
	NewEntry.Context = Context;
	NewEntry.bHasContext = Context != nullptr;
	NewEntry.ExpireTime = ExpireTime;
	BufferedInputs.Add(NewEntry);

	UE_LOG(LogShootGame, Verbose, TEXT("Input buffered: InputTag=%s Window=%.3f Owner=%s"),
		*InputTag.ToString(), InputBufferWindowSeconds, *GetNameSafe(GetOwnerActor()));

	// 开发构建输出可搜索标记，供网络场景与自动化定位输入缓冲行为。
	LogInputBufferMarker(TEXT("INPUT_BUFFER_REGISTERED"), InputTag, TEXT("Registered"), World->GetTimeSeconds());
}

bool UShooterAbilitySystemComponent::RemoveBufferedInput(const FGameplayTag& InputTag)
{
	const int32 RemovedCount = BufferedInputs.RemoveAll(
		[&InputTag](const FShooterBufferedInput& Entry) { return Entry.InputTag == InputTag; });
	return RemovedCount > 0;
}

void UShooterAbilitySystemComponent::ClearBufferedInputs()
{
	BufferedInputs.Reset();
	HeldReArmAttempts = 0;
	ClearHeldRetryTimer();
}

void UShooterAbilitySystemComponent::ClearHeldRetryTimer()
{
	if (!HeldRetryTimer.IsValid())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HeldRetryTimer);
	}
	HeldRetryTimer.Invalidate();
}

bool UShooterAbilitySystemComponent::ShouldBufferLocalInput() const
{
	// 只有本机玩家自己的 ASC 参与输入缓冲：
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

void UShooterAbilitySystemComponent::RegisterInputTagWatchers()
{
	UnregisterInputTagWatchers();

	for (const FGameplayTag& Tag : TransientInputBlockedTags)
	{
		const FDelegateHandle Handle = RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &UShooterAbilitySystemComponent::HandleTransientBlockedTagChanged);
		TransientTagDelegateHandles.Add(Tag, Handle);
	}

	for (const FGameplayTag& Tag : FatalInputBlockedTags)
	{
		const FDelegateHandle Handle = RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &UShooterAbilitySystemComponent::HandleFatalBlockedTagChanged);
		FatalTagDelegateHandles.Add(Tag, Handle);
	}
}

void UShooterAbilitySystemComponent::UnregisterInputTagWatchers()
{
	for (const TPair<FGameplayTag, FDelegateHandle>& Pair : TransientTagDelegateHandles)
	{
		UnregisterGameplayTagEvent(Pair.Value, Pair.Key, EGameplayTagEventType::NewOrRemoved);
	}
	TransientTagDelegateHandles.Reset();

	for (const TPair<FGameplayTag, FDelegateHandle>& Pair : FatalTagDelegateHandles)
	{
		UnregisterGameplayTagEvent(Pair.Value, Pair.Key, EGameplayTagEventType::NewOrRemoved);
	}
	FatalTagDelegateHandles.Reset();
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
void UShooterAbilitySystemComponent::ProcessBufferedInputsForTest()
{
	ProcessBufferedInputs();
}

void UShooterAbilitySystemComponent::ExpireBufferedInputsForTest()
{
	const UWorld* World = GetWorld();
	const float ExpiredTime = World ? World->GetTimeSeconds() - 1.0f : -1.0f;
	for (FShooterBufferedInput& Entry : BufferedInputs)
	{
		Entry.ExpireTime = ExpiredTime;
	}
}
#endif
