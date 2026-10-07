// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterGameplayAbility_Fire.h"

#include "AbilitySystemComponent.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "GameplayTagContainer.h"
#include "Characters/ShooterCharacter.h"
#include "AbilitySystem/ShooterGameplayTags.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/Interfaces/ShooterWeaponHolder.h"
#include "ShootGame.h"

UShooterGameplayAbility_Fire::UShooterGameplayAbility_Fire()
{
	// 同一 Avatar 同生命周期内只保留一个实例；拥有者与服务器各自持有一份实例。
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	// UE 5.6 的 UGameplayAbility 构造函数把该标志默认置为 true。
	// GA_Fire 必须保留服务器对权威 Ability 何时结束的决定权，因此显式关闭.
	bServerRespectsRemoteAbilityCancellation = false;

	// Input.Fire 是 ASC 输入查找与 Ability Spec 之间的稳定映射。
	// 资产标签使用 UE 5.6 新 API 在构造函数内写入 CDO。
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(ShooterGameplayTags::Input_Fire);
	SetAssetTags(AssetTags);
	// 死亡状态阻塞激活；State.Firing 在激活期间由 GAS 自动挂到拥有者 ASC。
	ActivationBlockedTags.AddTag(ShooterGameplayTags::State_Dead);
	// 换弹与装备事务期间同样阻塞开火，保证三者互斥。
	ActivationBlockedTags.AddTag(ShooterGameplayTags::State_Reloading);
	ActivationBlockedTags.AddTag(ShooterGameplayTags::State_Equipping);
	ActivationOwnedTags.AddTag(ShooterGameplayTags::State_Firing);
}

// ============================ 生命周期入口 ============================

bool UShooterGameplayAbility_Fire::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	const AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!AvatarActor)
	{
		return false;
	}

	// 两侧的 activation 资格刻意不同：客户端只读本地确定性条件，权威端执行完整校验。
	return AvatarActor->HasAuthority()
		? CanAuthorityStartFire(Handle, ActorInfo, AvatarActor, SourceTags, TargetTags, OptionalRelevantTags)
		: CanLocallyStartFire(Handle, ActorInfo, AvatarActor, SourceTags, TargetTags, OptionalRelevantTags);
}

bool UShooterGameplayAbility_Fire::CanLocallyStartFire(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const AActor* AvatarActor,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	const UAbilitySystemComponent* AbilitySystemComponent = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!AbilitySystemComponent || AbilitySystemComponent->GetAvatarActor() != AvatarActor)
	{
		return false;
	}

	// 只有本机拥有者视图才允许预测；表现对象未就绪时仍允许请求服务器。
	if (!ActorInfo->IsLocallyControlledPlayer())
	{
		return false;
	}

	// 本次 Action 的武器身份来自 Spec 的 SourceObject，不来自 CurrentWeaponActor：
	// 客户端对"这次请求属于哪把枪"的判定必须与服务器读取的是同一个事实。
	AShooterWeapon* RequestedWeapon = ResolveRequestedWeaponForActivation(Handle, ActorInfo);
	if (!IsValid(RequestedWeapon) || RequestedWeapon->IsHidden())
	{
		return false;
	}

	// 本地视图与请求身份必须一致：输入层按"当前武器对应的那一份 Spec"选 Spec，
	// 因此这里不一致只可能是同帧内的上下文变化，属于硬失败。
	if (RequestedWeapon != GetCurrentWeaponForAvatar(const_cast<AActor*>(AvatarActor)))
	{
		return false;
	}

	// 本地开火节拍：两种模式共用。半自动用它保证"一次按下沿最多形成一次动作边界"，
	// 全自动用它把按住输入节流成"每 RefireRate 才提交一次 GA_Fire"，
	// 从而不会每帧向服务器发送必然失败的请求。
	if (!RequestedWeapon->IsLocalFireCooldownReady())
	{
		return false;
	}

	// 全自动是「按住持续」语义：已经松开的按下沿不得再形成一次动作边界。
	if (RequestedWeapon->IsFullAuto() && !IsInputHeld(Handle, ActorInfo))
	{
		return false;
	}

	// 最后交给 Super 的 ActivationBlockedTags 门控（State.Reloading / State.Equipping / State.Dead）。
	// 上面的本地确定性条件必须先于本步：它们是硬失败，不产生 Tag 阻塞，因此不会被输入缓冲稍后补枪；
	// 而 Tag 阻塞只把这次输入推迟到窗口内，不再永久吞掉它（输入保留由 UShooterAbilitySystemComponent 负责）。
	return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

bool UShooterGameplayAbility_Fire::CanAuthorityStartFire(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const AActor* AvatarActor,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return RecordAuthorityRejectAndReturnFalse();
	}

	// 服务器完整校验：Avatar 必须是 ASC 当前 Avatar、State.Dead 未挂载、
	// 本次请求的 RequestedWeapon 有效且属于该 Avatar、可见、有弹药，
	// 并且**仍然是服务器认可的当前装备**——否则这次 Fire Action 已经失效，只能拒绝。
	const UAbilitySystemComponent* AbilitySystemComponent = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const AShooterWeapon* RequestedWeapon = ResolveRequestedWeaponForActivation(Handle, ActorInfo);
	const AShooterWeapon* CurrentWeapon = GetCurrentWeaponForAvatar(const_cast<AActor*>(AvatarActor));
	if (!AbilitySystemComponent || AbilitySystemComponent->GetAvatarActor() != AvatarActor ||
		!IsAuthorityFireContextValid(AvatarActor, AbilitySystemComponent, RequestedWeapon, CurrentWeapon))
	{
#if WITH_DEV_AUTOMATION_TESTS
		if (IsValid(RequestedWeapon) && RequestedWeapon != CurrentWeapon)
		{
			// 取证：客户端请求的武器已经不是当前装备——这正是"旧请求不得被重新解释成新武器"的判据。
			UE_LOG(LogShootGame, Display, TEXT("FIRE_WEAPON_CONTEXT_STALE_REJECT Requested=%s Current=%s"),
				*GetNameSafe(RequestedWeapon), *GetNameSafe(CurrentWeapon));
		}
#endif
		return RecordAuthorityRejectAndReturnFalse();
	}

	// 全自动的按住语义在权威端同样成立：监听主机（本机也有输入采集层）读真实按住事实，
	// Dedicated Server 上的远端请求则由引擎按请求置位 Spec 镜像。已经松开的按下沿不得形成权威 Shot。
	if (RequestedWeapon->IsFullAuto() && !IsInputHeld(Handle, ActorInfo))
	{
		return RecordAuthorityRejectAndReturnFalse();
	}

	// 权威射速是服务器自己的判据，绝不采信客户端上报的射击时间。
	// 不满足时这次 Activation 直接失败（GAS 会下发明确的 Reject），
	// 不会出现"接受 Ability 但最终没有开枪"的中间态。
	if (!RequestedWeapon->CanCommitAuthorityShot())
	{
#if WITH_DEV_AUTOMATION_TESTS
		// 取证：这一发被权威射速门控拒绝时，距离上一次权威提交差了多少。
		LogAuthorityCadenceForTest(TEXT("FIRE_CADENCE_REFIRE_REJECT"), RequestedWeapon, 0);
#endif
		return RecordAuthorityRejectAndReturnFalse();
	}

	return true;
}

AShooterWeapon* UShooterGameplayAbility_Fire::ResolveRequestedWeaponFromSpec(const FGameplayAbilitySpec& Spec)
{
	// SourceObject 是授予时写入的武器身份；它不是武器（例如历史遗留的宿主对象）时返回 nullptr，
	// 由调用方显式失败，绝不回落到"当前武器"猜一个。
	return Cast<AShooterWeapon>(Spec.SourceObject.Get());
}

AShooterWeapon* UShooterGameplayAbility_Fire::ResolveRequestedWeaponForActivation(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo) const
{
	const UAbilitySystemComponent* AbilitySystemComponent = ActorInfo
		? ActorInfo->AbilitySystemComponent.Get()
		: nullptr;
	const FGameplayAbilitySpec* Spec = AbilitySystemComponent
		? AbilitySystemComponent->FindAbilitySpecFromHandle(Handle)
		: nullptr;
	return Spec ? ResolveRequestedWeaponFromSpec(*Spec) : nullptr;
}

EShooterAbilityActivationPolicy UShooterGameplayAbility_Fire::GetActivationPolicyForSpec(
	const FGameplayAbilitySpec& Spec, const FGameplayAbilityActorInfo* ActorInfo) const
{
	// 连发语义来自"这一份 Spec 对应的武器"：同一个 GA_Fire 类同时服务 Rifle 与 AWP，
	// 输入边沿必须按本次选中的 Spec 判定，不能读当前武器猜。
	const AShooterWeapon* Weapon = ResolveRequestedWeaponFromSpec(Spec);
	if (!Weapon)
	{
		return Super::GetActivationPolicyForSpec(Spec, ActorInfo);
	}

	return Weapon->IsFullAuto()
		? EShooterAbilityActivationPolicy::WhileInputActive
		: EShooterAbilityActivationPolicy::OnInputTriggered;
}

bool UShooterGameplayAbility_Fire::DoesSpecMatchInputContext(const FGameplayAbilitySpec& Spec,
	const FGameplayAbilityActorInfo* ActorInfo) const
{
	// 只有"SourceObject 就是当前武器"的那一份 Spec 参与输入：
	// AbilitySpecHandle 因此成为网络上的武器动作身份，服务器不需要任何新增字段即可复现它。
	const AShooterWeapon* RequestedWeapon = ResolveRequestedWeaponFromSpec(Spec);
	if (!RequestedWeapon)
	{
		return false;
	}

	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	return RequestedWeapon == GetCurrentWeaponForAvatar(AvatarActor);
}

bool UShooterGameplayAbility_Fire::IsAuthorityFireContextValid(
	const AActor* AvatarActor,
	const UAbilitySystemComponent* AbilitySystemComponent,
	const AShooterWeapon* RequestedWeapon,
	const AShooterWeapon* CurrentWeapon) const
{
	// 死亡判断统一使用 ASC Tag，不再 Cast Character/NPC 读各自 IsDead()。
	const bool bDead = AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Dead);

	// RequestedWeapon 是本次 Fire Action 的武器身份，CurrentWeapon 只是"它是否仍然合法"的判据：
	// 两者必须相等，服务器绝不把一次旧请求改写成当前武器的 Shot。
	return AvatarActor && !bDead && IsValid(RequestedWeapon) && RequestedWeapon->GetOwner() == AvatarActor &&
		!RequestedWeapon->IsHidden() && RequestedWeapon->CanConsumeAmmo() && RequestedWeapon == CurrentWeapon;
}

void UShooterGameplayAbility_Fire::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 分流的唯一依据：权威侧与拥有者本地玩家视图。
	// 监听主机与 Standalone 同时为真，因此下面两条路径都会执行，这是有意为之；
	// Dedicated Server 上的 NPC 与远端玩家都不成立本地视图。
	const bool bAuthoritySide = HasAuthority(&ActivationInfo);
	const bool bOwnerLocalView = ActorInfo != nullptr && ActorInfo->IsLocallyControlledPlayer();

	if (!bAuthoritySide && !bOwnerLocalView)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
		return;
	}

	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	// 本次 Shot 的武器身份来自 Spec 的 SourceObject；CurrentWeaponActor 只在校验阶段使用。
	// CanActivateAbility 与本函数在同一次同步调用栈内完成，中间不存在可让身份失效的窗口。
	AShooterWeapon* Weapon = ResolveRequestedWeaponForActivation(Handle, ActorInfo);

	// 一次 Activation 的身份：服务这一次 Shot 的本地表现与裁决结清。
	EffectivePredictionKey = 0;
	CachedWeapon = Weapon;

#if WITH_DEV_AUTOMATION_TESTS
	// 取证：这一次 Activation 在**本端**实际使用的武器。
	// 拥有端写预测武器，权威端写提交武器；两端样本按同一个 PredictionKey 对齐后，
	// 就能判定"客户端用 A 发起的请求是否被服务器重新解释成 B 的 Shot"。
	RecordWeaponContextForTest(ActivationInfo.GetActivationPredictionKey().Current, Weapon, bAuthoritySide);
#endif

	if (bOwnerLocalView && IsValid(Weapon))
	{
		// 拥有端（远端客户端与监听主机）：本地表现只在这里产生，且一次 Activation 至多一发。
		if (bAuthoritySide)
		{
			// 监听主机：权威提交在同一调用栈里同步发生，本地不需要记录也不需要等待裁决。
			TryPredictOwnerShot(*Weapon);
		}
		else
		{
			BeginOwnerActivationIdentity(ActivationInfo, *Weapon);
			const FOwnerShotAttemptResult Attempt = TryPredictOwnerShot(*Weapon);
			if (FPredictedShotRecord* Record = PendingShotRecords.Find(EffectivePredictionKey))
			{
				Record->bBudgetConsumed = Attempt.bBudgetConsumed;
				Record->bFeedbackPlayed = Attempt.bFeedbackPlayed;
			}
		}

		// 本地节拍推进与"是否预测表现"无关：请求已经发出，节流必须生效。
		// 这也是"本地弹药预算为 0 时仍然每秒只请求 RefireRate 次"的唯一保证。
		// PredictionKey 只用于开发构建的节拍取证，把本地样本与这一发对齐。
		Weapon->AdvanceLocalFireCooldown(EffectivePredictionKey);
	}

	if (bAuthoritySide)
	{
#if WITH_DEV_AUTOMATION_TESTS
		// 取证：服务器真正处理这次 Activation 的时刻（与随后的提交在同一调用栈内）。
		const int32 AcceptedKey = ActivationInfo.GetActivationPredictionKey().Current;
		LogAuthorityCadenceForTest(TEXT("FIRE_CADENCE_SERVER_ACCEPT"), Weapon, AcceptedKey);
#endif
		// 权威路径只做一件事：提交且只提交这一发。
		// CanActivateAbility 与 ActivateAbility 在同一次同步调用栈内完成，
		// 中间不存在可以让上下文失效的窗口，因此校验只在 CanActivateAbility 做一次。
		if (!IsValid(Weapon) || !Weapon->CommitSingleShot(ActivationInfo.GetActivationPredictionKey().Current))
		{
			// 不可能路径：CanActivateAbility 已经保证武器有效、有弹药且射速就绪。
			// 这里绝不产生"接受 Activation 但最终没有开枪"的静默结果。
			UE_LOG(LogShootGame, Error, TEXT("GA_Fire authority commit failed: Avatar=%s Weapon=%s"),
				*GetNameSafe(AvatarActor), *GetNameSafe(Weapon));
			EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
			return;
		}

		if (IsValid(Weapon))
		{
			UE_LOG(LogShootGame, Display, TEXT("GA_Fire committed one shot: Avatar=%s Weapon=%s Ammo=%d"),
				*GetNameSafe(AvatarActor), *GetNameSafe(Weapon), Weapon->GetBulletCount());
		}
	}

	// 一次 Activation 到此结束：Ability 绝不留在活动状态等待下一发，
	// 连发由输入层在"仍按住 + 本地节拍就绪"时再次激活新的 GA_Fire 表达。
	// 不复制结束：远端客户端的本地实例已经结束，服务器端 Ability 的结束也不需要通知任何人。
	EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility*/ false, /*bWasCancelled*/ false);
}

void UShooterGameplayAbility_Fire::InputReleased(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	// 正常路径下本 Ability 已经随那一发结束，引擎不会在非活动实例上调到这里。
	// 保守保留：若服务器实例因任何原因仍活动，松开必须把它收口。
	EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}

void UShooterGameplayAbility_Fire::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 幂等清理：Reject、释放、切枪、换弹、死亡与断线都可能在同一次 Activation 上到达。
	// Shot 记录刻意不在这里清理：它的职责是活过本地结束，等待这一发的裁决。
	PruneShotRecords();

#if WITH_DEV_AUTOMATION_TESTS
	// 取证：本地实例的结束时刻。下一发样本用它算出"Ability End → 再激活"的间隔，
	// 用来判定输入层在结束时是否额外损失了一帧。
	if (ActorInfo && ActorInfo->IsLocallyControlledPlayer() && CachedWeapon.IsValid())
	{
		CachedWeapon->RecordLocalFireEndForAutomationTest();
	}
#endif

	if (CachedWeapon.IsValid())
	{
		if (bWasCancelled && ActorInfo && ActorInfo->IsLocallyControlledPlayer())
		{
			SuppressWeaponConfirmedFeedback(CachedWeapon.Get());
		}
	}

	CachedWeapon.Reset();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

	UE_LOG(LogShootGame, Display, TEXT("GA_Fire ended: Cancelled=%s Avatar=%s Key=%d"),
		bWasCancelled ? TEXT("true") : TEXT("false"), *GetNameSafe(GetShooterAvatarActor()), EffectivePredictionKey);
}

void UShooterGameplayAbility_Fire::OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	// Spec 生命周期结束 = 这个实例以后不可能再收到任何裁决：
	// 它自己留下的本地预测债务必须在这里作废，不能依赖"迟到的 ClientActivateAbilityFailed"、
	// "Owner 一定变化"、"Weapon 一定在客户端执行 Pool 回调"或"Ammo 复制最终碰巧纠正"。
	//
	// UE5.6 引擎顺序（AbilitySystemComponent_Abilities.cpp:598-675 / GameplayAbilityTypes.cpp:233-255）：
	//   1. 活动实例先 EndAbility(..., bReplicateEndAbility=false, bWasCancelled=false)；
	//   2. 再调 PrimaryInstance->OnRemoveAbility(ActorInfo, Spec) —— 本函数；
	//   3. 最后才 PrimaryInstance->MarkAsGarbage()。
	// 因此这里 Spec.Handle / Spec.SourceObject / PendingShotRecords / CachedWeapon 全部仍然可读可写。
	// 服务器与拥有者客户端都会走到这里：服务器清 ClearAbility，客户端走 Spec 复制删除（PreReplicatedRemove）。
	RetireOwnedPredictionDebt(Spec);

	Super::OnRemoveAbility(ActorInfo, Spec);
}

void UShooterGameplayAbility_Fire::RetireOwnedPredictionDebt(const FGameplayAbilitySpec& Spec)
{
	// 清理范围严格限定为"本实例自己的未结记录"：
	// 每把武器各有一份 Spec 与独立实例，记录天然按实例隔离，因此旧 Spec H1 的迟到移除
	// 不可能碰到同一个 pooled WeaponActor 上 H2 新生命周期的预测状态。
	// 这里也刻意不调用 ResetAmmoPrediction()：那会清掉整把武器的预算，属于武器生命周期，不属于本 Spec。
	AShooterWeapon* RequestedWeapon = ResolveRequestedWeaponFromSpec(Spec);
	int32 RetiredCount = 0;
	int32 RefundedCount = 0;
	int32 GenerationSkippedCount = 0;

	if (RequestedWeapon)
	{
		// 无论是否有未结记录都登记"本 Spec 的清算跑过"：清理可能因为武器侧生命周期
		// 已经先一步结清（Owner 清空 → Invalidate + ResetAmmoPrediction）而无可清理。
		RequestedWeapon->RecordSpecRemovalCleanupForTest();
	}

	for (auto It = PendingShotRecords.CreateIterator(); It; ++It)
	{
		FPredictedShotRecord& Record = It->Value;
		const int32 PredictionKey = It.Key();

		// 候选解绑对象：记录所属武器（绑定就是在它身上建立的）以及本次 Spec 的 SourceObject。
		if (AShooterWeapon* RecordWeapon = Record.Weapon.Get())
		{
			RecordWeapon->UnbindPredictionAbilityIfBoundTo(this);
		}

		if (Record.bResolved)
		{
			It.RemoveCurrent();
			continue;
		}

		// 语义是"这一发以后不可能再有裁决"，不是"服务器拒绝了这一发"：走与 Reject 相同的结清实现，
		// 但按 Retired 终结原因走——不补播确认表现、不碰服务器弹药、不发任何 RPC、不计入任何 Reject 统计。
		// 退款与代次跳过只认结清返回的事实，绝不在这里按 bBudgetConsumed 反推。
		++RetiredCount;
		const FShotResolution Resolution = ResolveShotRecord(Record, PredictionKey, EShooterShotOutcome::Retired);
		if (Resolution.bBudgetRefunded)
		{
			++RefundedCount;
		}
		if (Resolution.bGenerationSkipped)
		{
			++GenerationSkippedCount;
		}
		It.RemoveCurrent();
	}

	if (RequestedWeapon)
	{
		// 仅当武器当前绑定到本实例时才解绑：pooled 复用后 H2 的绑定不得被旧 H1 的迟到移除清掉。
		RequestedWeapon->UnbindPredictionAbilityIfBoundTo(this);
	}

	// 实例级残留一并作废（幂等：EndAbility 可能已经清过一次）。
	EffectivePredictionKey = 0;
	CachedWeapon.Reset();

#if WITH_DEV_AUTOMATION_TESTS
	// 累加本身幂等：第二次进入时 map 已空，三项计数都是 0。
	SpecRemovalRetiredRecordCountForTest += RetiredCount;
	SpecRemovalRefundedCountForTest += RefundedCount;
	SpecRemovalGenerationSkippedCountForTest += GenerationSkippedCount;
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("FIRE_SPEC_REMOVE_CLEANUP Weapon=%s Retired=%d Refunded=%d GenerationSkipped=%d Remain=%d"),
		*GetNameSafe(RequestedWeapon),
		RetiredCount,
		RefundedCount,
		GenerationSkippedCount,
		PendingShotRecords.Num());
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("FIRE_SPEC_REMOVE_CLEANUP_TOTAL TotalRetired=%d TotalRefunded=%d TotalGenerationSkipped=%d"),
		SpecRemovalRetiredRecordCountForTest,
		SpecRemovalRefundedCountForTest,
		SpecRemovalGenerationSkippedCountForTest);
#endif

	PendingShotRecords.Reset();
}

// ============================ Owner Prediction Path ============================

void UShooterGameplayAbility_Fire::BeginOwnerActivationIdentity(const FGameplayAbilityActivationInfo& ActivationInfo,
	AShooterWeapon& Weapon)
{
	FPredictionKey ActivationPredictionKey = ActivationInfo.GetActivationPredictionKey();
	EffectivePredictionKey = ActivationPredictionKey.Current;

	Weapon.BindPredictionAbility(this);
	RegisterShotRecord(EffectivePredictionKey, &Weapon);

	// 引擎 Reject 通道：Rejected 委托按 PredictionKey 绑定，与 Ability 实例是否活动无关，
	// 因此即使本地实例早已结束，迟到的 Reject 仍然能退还被拒的那一发。
	if (ActivationPredictionKey.IsValidKey())
	{
		ActivationPredictionKey.NewRejectedDelegate().BindUObject(
			this, &UShooterGameplayAbility_Fire::HandlePredictedShotRejected, EffectivePredictionKey);
	}
}

void UShooterGameplayAbility_Fire::RegisterShotRecord(int32 PredictionKey, AShooterWeapon* Weapon)
{
	if (PredictionKey <= 0 || !Weapon || !Weapon->IsAmmoPredictionContext())
	{
		// 本机权威视图（监听主机 / Standalone）与 NPC 不维护预测记录：
		// 它们的权威结果同步可得，也不会有裁决通知送回来。
		return;
	}

	RegisterShotRecordCore(PredictionKey, Weapon);
}

void UShooterGameplayAbility_Fire::RegisterShotRecordCore(int32 PredictionKey, AShooterWeapon* Weapon)
{
	if (PredictionKey <= 0 || !Weapon)
	{
		return;
	}

	PruneShotRecords();

	FPredictedShotRecord& Record = PendingShotRecords.FindOrAdd(PredictionKey);
	Record.Weapon = Weapon;
	Record.WeaponPredictionGeneration = Weapon->GetAmmoPredictionGeneration();
	Record.bBudgetConsumed = false;
	Record.bFeedbackPlayed = false;
	Record.bFeedbackSuppressed = false;
	Record.bResolved = false;

	Weapon->BeginAmmoDisplayActivation(PredictionKey);
}

void UShooterGameplayAbility_Fire::PruneShotRecords()
{
	for (auto It = PendingShotRecords.CreateIterator(); It; ++It)
	{
		const AShooterWeapon* Weapon = It->Value.Weapon.Get();
		if (It->Value.bResolved || !Weapon || It->Value.WeaponPredictionGeneration != Weapon->GetAmmoPredictionGeneration())
		{
			It.RemoveCurrent();
		}
	}
}

UShooterGameplayAbility_Fire::FOwnerShotAttemptResult UShooterGameplayAbility_Fire::TryPredictOwnerShot(AShooterWeapon& Weapon)
{
	FOwnerShotAttemptResult Result;

	// 上下文：表现目标必须成立（武器有效、未隐藏、仍是当前装备），否则本次不提前表现。
	if (!IsOwnerFireContextValid())
	{
		return Result;
	}

	// 预测弹药预算：只有预测型 Owner 客户端会真正扣预算；
	// 权威端（Listen Host / Standalone）由 Weapon 侧直接放行，它们不维护 Pending。
	if (!Weapon.TryConsumePredictedAmmo())
	{
		// 预算不足：这一次不提前表现、不增加 Pending，但请求照旧到达服务器并接受独立裁决。
		// Server Accept / Reject 只负责结果与状态结算，不补播过去的 Owner 瞬时表现。
		return Result;
	}
	Result.bBudgetConsumed = true;

	// 本地预测表现与 Pending 增加同步发生：两者必须描述同一发。
	Weapon.RecordAmmoDisplayPredictedShot(EffectivePredictionKey);
	Result.bFeedbackPlayed = PlayOwnerShotFeedback(Weapon);
	return Result;
}

bool UShooterGameplayAbility_Fire::PlayOwnerShotFeedback(AShooterWeapon& Weapon)
{
	if (!Weapon.PlayOwnerPredictedShotFeedback())
	{
		return false;
	}

	LogFirePredictionMarker(TEXT("FIRE_PREDICTED_OWNER"), &Weapon);
	return true;
}

bool UShooterGameplayAbility_Fire::IsOwnerFireContextValid()
{
	return IsOwnerFireContextValidForContext(GetShooterAvatarActor(), CachedWeapon.Get(), GetAbilitySystemComponentFromActorInfo());
}

bool UShooterGameplayAbility_Fire::IsOwnerFireContextValidForContext(AActor* AvatarActor,
	const AShooterWeapon* Weapon, const UAbilitySystemComponent* AbilitySystemComponent)
{
	// 唯一判据是"表现目标是否成立"：武器有效、未隐藏、仍是当前装备。
	//
	// 刻意不读取 Ammo / Reloading / Equipping / Dead：这些复制状态可能过期，
	// 用它们二次否决首次 Owner 表现，就会让服务器已经接受并生成弹丸的那一发永久没有反馈。
	// 弹药预算不足只影响"是否提前表现"，不影响请求与服务器裁决。
	// 参数刻意不使用，测试依赖「这些状态存在也不参与门控」这一事实，见头文件说明。
	(void)AbilitySystemComponent;
	return AvatarActor && IsValid(Weapon) && !Weapon->IsHidden() && GetCurrentWeaponForAvatar(AvatarActor) == Weapon;
}

void UShooterGameplayAbility_Fire::HandleAuthorityShotVerdict(AShooterWeapon* SourceWeapon, int32 ActivationKey, bool bCommitted)
{
	FPredictedShotRecord* Record = PendingShotRecords.Find(ActivationKey);
	if (!Record || Record->bResolved)
	{
		// 迟到或重复的裁决：幂等忽略，绝不二次退还或二次补播。
		return;
	}

#if WITH_DEV_AUTOMATION_TESTS
	// 取证：本条裁决的发送方（引擎 Reject 通道不带武器，保持上一次的值）。
	if (SourceWeapon)
	{
		LastVerdictSourceWeaponForTest = SourceWeapon;
	}
#endif

	// 武器转发过来的裁决必须来自这条记录自己的武器；引擎 Reject 通道不带武器，跳过该比对。
	if (SourceWeapon && Record->Weapon.Get() != SourceWeapon)
	{
#if WITH_DEV_AUTOMATION_TESTS
		// 取证：这次裁决来自另一把武器——本次请求的 Weapon Context 在服务器侧被重新解释。
		// 该记录因此永远不会被结清，调用方可以据此观察到 Pending / 记录 / HUD 的残留。
		++WeaponContextMismatchVerdictCountForTest;
		LogFirePredictionMarker(TEXT("FIRE_VERDICT_WEAPON_CONTEXT_MISMATCH"), SourceWeapon);
#endif
		return;
	}

#if WITH_DEV_AUTOMATION_TESTS
	// 明确区分两条 Reject 通道：武器转发的裁决带武器指针，引擎 ClientActivateAbilityFailed 不带。
	bLastResolvedShotRejectedByEngineForTest = !bCommitted && SourceWeapon == nullptr;
#endif
	ResolveShotRecord(*Record, ActivationKey, bCommitted ? EShooterShotOutcome::Committed : EShooterShotOutcome::Rejected);
}

void UShooterGameplayAbility_Fire::HandlePredictedShotRejected(int32 PredictionKey)
{
	HandleAuthorityShotVerdict(nullptr, PredictionKey, /*bCommitted*/ false);
}

const TCHAR* UShooterGameplayAbility_Fire::GetShotOutcomeLogName(EShooterShotOutcome Outcome)
{
	switch (Outcome)
	{
	case EShooterShotOutcome::Committed:
		return TEXT("Committed");
	case EShooterShotOutcome::Rejected:
		return TEXT("Rejected");
	case EShooterShotOutcome::Retired:
		return TEXT("Retired");
	default:
		return TEXT("Unknown");
	}
}

UShooterGameplayAbility_Fire::FShotResolution UShooterGameplayAbility_Fire::ResolveShotRecord(
	FPredictedShotRecord& Record, int32 PredictionKey, EShooterShotOutcome Outcome)
{
	FShotResolution Resolution;
	AShooterWeapon* Weapon = Record.Weapon.Get();
	if (!Weapon || Record.WeaponPredictionGeneration != Weapon->GetAmmoPredictionGeneration())
	{
		// 旧武器 / 旧租用 / 旧 Spec 代次的终结：只丢弃这条记录。
		// 它上面的账目不属于当前上下文，因此不退款、不动当前 Pending、不动当前 HUD 显示预测、
		// 也不解绑当前 Ability；统计上必须报告 GenerationSkipped，而不是 Refunded。
		Record.bResolved = true;
		Resolution.bGenerationSkipped = true;
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("FIRE_SHOT_VERDICT_RESOLVED Key=%d Outcome=GenerationSkipped Budget=%d Feedback=%d"),
			PredictionKey,
			Record.bBudgetConsumed ? 1 : 0,
			Record.bFeedbackPlayed ? 1 : 0);
		return Resolution;
	}

	if (Outcome == EShooterShotOutcome::Committed)
	{
		if (Record.bBudgetConsumed)
		{
			// 已经提前占用本地预算：这一发已经被服务器真实提交，结清预算。
			Weapon->ConfirmPredictedAmmo(1);
		}

		// Owner 瞬时表现只由本地 Shot Attempt 决定。
		// Committed 只结清预测预算、Record、AmmoDisplay 与 HUD，绝不重演历史 Owner cosmetic。
	}
	else
	{
		// Rejected 与 Retired 共用同一结清实现，区别只在来源语义与日志：
		// 前者是服务器明确拒绝，后者是 Spec 生命周期结束导致的本地退休（服务器没有拒绝过这一发）。
		// 已播出的瞬时表现按契约不回滚，也不补播。
		if (Record.bBudgetConsumed)
		{
			Weapon->RefundPredictedAmmo(1);
			// 只有真的调用过退款，调用方才允许把这一条计入 Refunded 统计。
			Resolution.bBudgetRefunded = true;
		}

		if (Outcome == EShooterShotOutcome::Rejected)
		{
			// HUD 显示扣减按同一个 PredictionKey 结清。
			Weapon->RejectAmmoDisplayActivation(PredictionKey);
			UE_LOG(
				LogShootGame,
				Display,
				TEXT("FIRE_SHOT_REJECT_REFUND Key=%d Budget=%d Refunded=%d Feedback=%d"),
				PredictionKey,
				Record.bBudgetConsumed ? 1 : 0,
				Resolution.bBudgetRefunded ? 1 : 0,
				Record.bFeedbackPlayed ? 1 : 0);
		}
		else
		{
			// 退休路径与 Reject 共用同一底层显示结清，但必须能被单独识别：
			// 它绝不打印 REJECT 标记，也绝不增加任何用于衡量"服务器拒绝"的统计。
			Weapon->RetireAmmoDisplayActivation(PredictionKey);
			UE_LOG(
				LogShootGame,
				Display,
				TEXT("FIRE_SHOT_LIFECYCLE_RETIRE Key=%d Budget=%d Refunded=%d Feedback=%d"),
				PredictionKey,
				Record.bBudgetConsumed ? 1 : 0,
				Resolution.bBudgetRefunded ? 1 : 0,
				Record.bFeedbackPlayed ? 1 : 0);
		}
	}

	Record.bResolved = true;

#if WITH_DEV_AUTOMATION_TESTS
	LastResolvedShotKeyForTest = PredictionKey;
	bLastResolvedShotCommittedForTest = Outcome == EShooterShotOutcome::Committed;
#endif
	UE_LOG(LogShootGame, Display, TEXT("FIRE_SHOT_VERDICT_RESOLVED Key=%d Outcome=%s Budget=%d Feedback=%d"),
		PredictionKey, GetShotOutcomeLogName(Outcome), Record.bBudgetConsumed ? 1 : 0, Record.bFeedbackPlayed ? 1 : 0);
	return Resolution;
}

void UShooterGameplayAbility_Fire::InvalidateWeaponPredictionContext(AShooterWeapon* Weapon)
{
	for (TPair<int32, FPredictedShotRecord>& Pair : PendingShotRecords)
	{
		if (Pair.Value.Weapon.Get() == Weapon)
		{
			Pair.Value.bResolved = true;
			Pair.Value.bFeedbackSuppressed = true;
			UE_LOG(LogShootGame, Display, TEXT("FIRE_SHOT_CONTEXT_INVALIDATED Key=%d"), Pair.Key);
		}
	}
}

void UShooterGameplayAbility_Fire::SuppressWeaponConfirmedFeedback(AShooterWeapon* Weapon)
{
	for (TPair<int32, FPredictedShotRecord>& Pair : PendingShotRecords)
	{
		if (Pair.Value.Weapon.Get() == Weapon)
		{
			Pair.Value.bFeedbackSuppressed = true;
		}
	}
}

// ============================ 共用查询 ============================

AShooterWeapon* UShooterGameplayAbility_Fire::GetCurrentWeaponForAvatar(AActor* AvatarActor) const
{
	// 玩家优先走 EquipmentComponent；NPC 尚无 Equipment 时保留最小 IShooterWeaponHolder 回退。
	if (AShooterCharacter* Character = Cast<AShooterCharacter>(AvatarActor))
	{
		if (UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent())
		{
			AShooterWeapon* Weapon = Equipment->GetCurrentWeaponActor();
			if (IsValid(Weapon) && Weapon->GetOwner() == Character)
			{
				return Weapon;
			}
		}

		return Character->GetCurrentWeapon();
	}

	const IShooterWeaponHolder* WeaponHolder = Cast<IShooterWeaponHolder>(AvatarActor);
	return WeaponHolder ? WeaponHolder->GetCurrentWeapon() : nullptr;
}

const UObject* UShooterGameplayAbility_Fire::GetInputBufferContext() const
{
	// 上下文是「按下时的当前武器」：换枪提交后，旧武器的按下沿不得在新武器上生效。
	return GetCurrentWeaponForAvatar(GetShooterAvatarActor());
}

EShooterAbilityActivationPolicy UShooterGameplayAbility_Fire::GetActivationPolicy(const FGameplayAbilityActorInfo* ActorInfo) const
{
	// 无 Spec 上下文时的回落（CDO / 未激活实例 / 诊断查询）：由当前武器决定。
	// 真实输入路径一律走 GetActivationPolicyForSpec，按本次选中的 Spec 解析。
	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	const AShooterWeapon* Weapon = GetCurrentWeaponForAvatar(AvatarActor);
	return Weapon && Weapon->IsFullAuto()
		? EShooterAbilityActivationPolicy::WhileInputActive
		: EShooterAbilityActivationPolicy::OnInputTriggered;
}

bool UShooterGameplayAbility_Fire::IsInputHeld(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo) const
{
	// 按住真值由基类统一判定：本机拥有者视图读 ASC 的输入采集层（按住是输入事实，
	// 与"哪一份 Spec 曾经收到过 Press"无关，因此切枪后全自动新武器同样成立），
	// 没有采集层的远端权威端读引擎维护的 Spec 镜像。
	// 这里刻意不再直接读 Spec.InputPressed：那会把"切枪"误判成"玩家已经松开"。
	return IsInputTagHeld(Handle, ActorInfo, ShooterGameplayTags::Input_Fire);
}

bool UShooterGameplayAbility_Fire::RecordAuthorityRejectAndReturnFalse() const
{
#if WITH_DEV_AUTOMATION_TESTS
	++AuthorityRejectCountForTest;
#endif
	return false;
}

void UShooterGameplayAbility_Fire::LogFirePredictionMarker(const TCHAR* Marker, const AShooterWeapon* Weapon) const
{
#if WITH_DEV_AUTOMATION_TESTS
	// Shipping 不增加预测测试日志带宽：全部 P1 标记只在开发构建输出。
	const AActor* AvatarActor = GetShooterAvatarActor();
	const APawn* AvatarPawn = Cast<APawn>(AvatarActor);
	const APlayerState* OwnerPlayerState = AvatarPawn ? AvatarPawn->GetPlayerState() : nullptr;
	const int32 PlayerId = OwnerPlayerState ? OwnerPlayerState->GetPlayerId() : INDEX_NONE;
	const int32 NetModeValue = AvatarActor ? static_cast<int32>(AvatarActor->GetNetMode()) : INDEX_NONE;
	const UWorld* World = GetWorld();
	const float LocalTime = World ? World->GetTimeSeconds() : 0.0f;

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("%s PlayerId=%d Weapon=%s PredictionKey=%d Count=%d OwnerLocalTime=%.3f NetMode=%d"),
		Marker,
		PlayerId,
		*GetNameSafe(Weapon),
		EffectivePredictionKey,
		Weapon ? Weapon->GetPredictedOwnerFeedbackCountForAutomationTest() : 0,
		LocalTime,
		NetModeValue);
#endif
}

// ============================ 测试观察接口 ============================

#if WITH_DEV_AUTOMATION_TESTS

bool UShooterGameplayAbility_Fire::HasInputFireTag() const
{
	return GetAssetTags().HasTag(ShooterGameplayTags::Input_Fire);
}

bool UShooterGameplayAbility_Fire::IsBlockedByStateDead() const
{
	return ActivationBlockedTags.HasTag(ShooterGameplayTags::State_Dead);
}

bool UShooterGameplayAbility_Fire::IsBlockedByStateReloading() const
{
	return ActivationBlockedTags.HasTag(ShooterGameplayTags::State_Reloading);
}

bool UShooterGameplayAbility_Fire::IsBlockedByStateEquipping() const
{
	return ActivationBlockedTags.HasTag(ShooterGameplayTags::State_Equipping);
}

bool UShooterGameplayAbility_Fire::OwnsStateFiringWhileActive() const
{
	return ActivationOwnedTags.HasTag(ShooterGameplayTags::State_Firing);
}

bool UShooterGameplayAbility_Fire::CanRetriggerInstancedAbility() const
{
	return bRetriggerInstancedAbility;
}

bool UShooterGameplayAbility_Fire::ServerRespectsRemoteAbilityCancellation() const
{
	return bServerRespectsRemoteAbilityCancellation;
}

void UShooterGameplayAbility_Fire::LogAuthorityCadenceForTest(const TCHAR* Marker, const AShooterWeapon* Weapon, int32 ActivationKey) const
{
	// 只读取证：服务器处理这次 Activation 的时刻，以及本武器权威射速时钟当时的状态。
	// 接受路径与 Refire 拒绝路径共用同一格式，便于与拥有端本地样本逐发对齐。
	const UWorld* World = GetWorld();
	const float ServerTime = World ? World->GetTimeSeconds() : 0.0f;
	const float LastShotTime = Weapon ? Weapon->GetTimeOfLastShotForAutomationTest() : 0.0f;
	const float RefireRate = Weapon ? Weapon->GetRefireRate() : 0.0f;
	const bool bHadPreviousShot = Weapon != nullptr && LastShotTime > 0.0f;
	const float SinceLastShot = bHadPreviousShot ? ServerTime - LastShotTime : -1.0f;
	const float Deficit = bHadPreviousShot ? FMath::Max(0.0f, RefireRate - SinceLastShot) : 0.0f;

	UE_LOG(LogShootGame, Display, TEXT("%s Key=%d Server=%.6f SinceLastMs=%.3f DeficitMs=%.3f"),
		Marker, ActivationKey, ServerTime, SinceLastShot * 1000.0f, Deficit * 1000.0f);
}

void UShooterGameplayAbility_Fire::RecordWeaponContextForTest(int32 Key, const AShooterWeapon* Weapon, bool bAuthority)
{
	// 只记录事实：本端这一次 Activation 用了哪把武器。
	// 拥有端与权威端各自持有一份实例，因此两端样本天然按端分开，由调用方按 PredictionKey 对齐。
	constexpr int32 MaxWeaponContextSamples = 1024;
	if (WeaponContextSamplesForTest.Num() >= MaxWeaponContextSamples)
	{
		// 只限制内存：取证窗口由夹具按步骤显式重置，超出上限只丢弃后续样本。
		return;
	}

	FWeaponContextSampleForTest& Sample = WeaponContextSamplesForTest.AddDefaulted_GetRef();
	Sample.PredictionKey = Key;
	Sample.Weapon = const_cast<AShooterWeapon*>(Weapon);
	Sample.WeaponId = Weapon ? Weapon->GetWeaponId() : NAME_None;
	Sample.LocalTime = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f;
	Sample.bAuthority = bAuthority;

	UE_LOG(LogShootGame, Display, TEXT("FIRE_WEAPON_CONTEXT Key=%d Authority=%d Weapon=%s WeaponId=%s LocalTime=%.6f"),
		Key, bAuthority ? 1 : 0, *GetNameSafe(Weapon), *Sample.WeaponId.ToString(), Sample.LocalTime);
}

int32 UShooterGameplayAbility_Fire::GetUnresolvedShotRecordCountForTest() const
{
	int32 Count = 0;
	for (const TPair<int32, FPredictedShotRecord>& Pair : PendingShotRecords)
	{
		const AShooterWeapon* Weapon = Pair.Value.Weapon.Get();
		// 只有"裁决仍可能到达并结清"的记录才算未结清：上下文失效的记录不会再参与结算。
		const bool bContextStillValid = Weapon != nullptr && Pair.Value.WeaponPredictionGeneration == Weapon->GetAmmoPredictionGeneration();
		Count += (!Pair.Value.bResolved && bContextStillValid) ? 1 : 0;
	}
	return Count;
}

bool UShooterGameplayAbility_Fire::IsShotRecordBudgetConsumedForTest(int32 PredictionKey) const
{
	const FPredictedShotRecord* Record = PendingShotRecords.Find(PredictionKey);
	return Record != nullptr && Record->bBudgetConsumed;
}

bool UShooterGameplayAbility_Fire::IsShotRecordFeedbackPlayedForTest(int32 PredictionKey) const
{
	const FPredictedShotRecord* Record = PendingShotRecords.Find(PredictionKey);
	return Record != nullptr && Record->bFeedbackPlayed;
}

bool UShooterGameplayAbility_Fire::IsShotRecordResolvedForTest(int32 PredictionKey) const
{
	const FPredictedShotRecord* Record = PendingShotRecords.Find(PredictionKey);
	return Record != nullptr && Record->bResolved;
}

int32 UShooterGameplayAbility_Fire::GetUnresolvedShotRecordCountForWeaponForTest(const AShooterWeapon* Weapon) const
{
	if (!Weapon)
	{
		return 0;
	}

	int32 Count = 0;
	for (const TPair<int32, FPredictedShotRecord>& Pair : PendingShotRecords)
	{
		const AShooterWeapon* RecordWeapon = Pair.Value.Weapon.Get();
		const bool bContextStillValid = RecordWeapon != nullptr &&
			Pair.Value.WeaponPredictionGeneration == RecordWeapon->GetAmmoPredictionGeneration();
		Count += (!Pair.Value.bResolved && bContextStillValid && RecordWeapon == Weapon) ? 1 : 0;
	}
	return Count;
}

void UShooterGameplayAbility_Fire::HandleAuthorityShotVerdictForTest(int32 PredictionKey, bool bCommitted)
{
	HandleAuthorityShotVerdict(nullptr, PredictionKey, bCommitted);
}

void UShooterGameplayAbility_Fire::RegisterShotRecordForTest(int32 PredictionKey, AShooterWeapon* Weapon,
	bool bBudgetConsumed, bool bFeedbackPlayed)
{
	RegisterShotRecordCore(PredictionKey, Weapon);
	if (FPredictedShotRecord* Record = PendingShotRecords.Find(PredictionKey))
	{
		Record->bBudgetConsumed = bBudgetConsumed;
		Record->bFeedbackPlayed = bFeedbackPlayed;
	}
}

#endif
