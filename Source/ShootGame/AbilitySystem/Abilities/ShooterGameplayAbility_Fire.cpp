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

	// 本地动作边界必须有可用的表现目标（当前武器有效且未隐藏），
	// 否则会出现「请求已发出、本地却无表现目标」的分裂。
	// 判据只读本地已知的武器对象，不读 Ammo / Reloading / Equipping / Dead 等可能过期的复制状态。
	const AShooterWeapon* LocalWeapon = GetCurrentWeaponForAvatar(const_cast<AActor*>(AvatarActor));
	if (!IsValid(LocalWeapon) || LocalWeapon->IsHidden())
	{
		return false;
	}

	// 本地开火节拍：两种模式共用。半自动用它保证"一次按下沿最多形成一次动作边界"，
	// 全自动用它把按住输入节流成"每 RefireRate 才提交一次 GA_Fire"，
	// 从而不会每帧向服务器发送必然失败的请求。
	if (!LocalWeapon->IsLocalFireCooldownReady())
	{
		return false;
	}

	// 全自动是「按住持续」语义：已经松开的按下沿不得再形成一次动作边界。
	if (LocalWeapon->IsFullAuto() && !IsInputHeld(Handle, ActorInfo))
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
	// 当前 WeaponActor 有效且属于该 Avatar、可见，并且存在可消耗弹药。
	const UAbilitySystemComponent* AbilitySystemComponent = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const AShooterWeapon* Weapon = GetCurrentWeaponForAvatar(const_cast<AActor*>(AvatarActor));
	if (!AbilitySystemComponent || AbilitySystemComponent->GetAvatarActor() != AvatarActor ||
		!IsAuthorityFireContextValid(AvatarActor, AbilitySystemComponent, Weapon))
	{
		return RecordAuthorityRejectAndReturnFalse();
	}

	// 全自动的按住语义在权威端同样成立：远端请求由引擎在进入本校验前按请求置位 InputPressed，
	// 主机本地视图则直接反映真实按键；已经松开的按下沿不得形成一次权威 Shot。
	if (Weapon->IsFullAuto() && !IsInputHeld(Handle, ActorInfo))
	{
		return RecordAuthorityRejectAndReturnFalse();
	}

	// 权威射速是服务器自己的判据，绝不采信客户端上报的射击时间。
	// 不满足时这次 Activation 直接失败（GAS 会下发明确的 Reject），
	// 不会出现"接受 Ability 但最终没有开枪"的中间态。
	if (!Weapon->CanCommitAuthorityShot())
	{
#if WITH_DEV_AUTOMATION_TESTS
		// 取证：这一发被权威射速门控拒绝时，距离上一次权威提交差了多少。
		LogAuthorityCadenceForTest(TEXT("FIRE_CADENCE_REFIRE_REJECT"), Weapon, 0);
#endif
		return RecordAuthorityRejectAndReturnFalse();
	}

	return true;
}

bool UShooterGameplayAbility_Fire::IsAuthorityFireContextValid(const AActor* AvatarActor,
	const UAbilitySystemComponent* AbilitySystemComponent, const AShooterWeapon* Weapon) const
{
	// 死亡判断统一使用 ASC Tag，不再 Cast Character/NPC 读各自 IsDead()。
	const bool bDead = AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Dead);

	return AvatarActor && !bDead && IsValid(Weapon) && Weapon->GetOwner() == AvatarActor && !Weapon->IsHidden() && Weapon->CanConsumeAmmo();
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
	AShooterWeapon* Weapon = GetCurrentWeaponForAvatar(AvatarActor);

	// 一次 Activation 的身份：服务这一次 Shot 的本地表现与裁决结清。
	EffectivePredictionKey = 0;
	CachedWeapon = Weapon;

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
		// 服务器接受时由 Committed 裁决补播一次拥有端表现。
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

	// 武器转发过来的裁决必须来自这条记录自己的武器；引擎 Reject 通道不带武器，跳过该比对。
	if (SourceWeapon && Record->Weapon.Get() != SourceWeapon)
	{
		return;
	}

#if WITH_DEV_AUTOMATION_TESTS
	// 明确区分两条 Reject 通道：武器转发的裁决带武器指针，引擎 ClientActivateAbilityFailed 不带。
	bLastResolvedShotRejectedByEngineForTest = !bCommitted && SourceWeapon == nullptr;
#endif
	ResolveShotRecord(*Record, ActivationKey, bCommitted);
}

void UShooterGameplayAbility_Fire::HandlePredictedShotRejected(int32 PredictionKey)
{
	HandleAuthorityShotVerdict(nullptr, PredictionKey, /*bCommitted*/ false);
}

void UShooterGameplayAbility_Fire::ResolveShotRecord(FPredictedShotRecord& Record, int32 PredictionKey, bool bCommitted)
{
	AShooterWeapon* Weapon = Record.Weapon.Get();
	if (!Weapon || Record.WeaponPredictionGeneration != Weapon->GetAmmoPredictionGeneration())
	{
		// 旧武器 / 旧租用的裁决：不得修改新上下文的 Pending，也不得在新武器上补播。
		Record.bResolved = true;
		return;
	}

	if (bCommitted)
	{
		if (Record.bBudgetConsumed)
		{
			// 已经提前占用本地预算：这一发已经被服务器真实提交，结清预算。
			Weapon->ConfirmPredictedAmmo(1);
		}

		if (!Record.bFeedbackPlayed)
		{
			// 拥有端此前没有真的播出表现（本地预算为 0，或表现通道当时不可用）：补播恰好一次。
			if (!Record.bFeedbackSuppressed && Weapon->PlayOwnerConfirmedShotFeedback())
			{
#if WITH_DEV_AUTOMATION_TESTS
				++ConfirmedBackfillRequestCountForTest;
#endif
				LogFirePredictionMarker(TEXT("FIRE_CONFIRMED_BACKFILL_OWNER"), Weapon);
			}
			else
			{
				UE_LOG(LogShootGame, Display, TEXT("FIRE_CONFIRMED_FEEDBACK_SUPPRESSED Key=%d Reason=Context"), PredictionKey);
			}
		}
	}
	else
	{
		// Reject：只退还被拒的那一发。已经播出的瞬时表现按契约不回滚，也不补播。
		if (Record.bBudgetConsumed)
		{
			Weapon->RefundPredictedAmmo(1);
		}
		// HUD 显示扣减按同一个 PredictionKey 结清。
		Weapon->RejectAmmoDisplayActivation(PredictionKey);
		UE_LOG(LogShootGame, Display, TEXT("FIRE_SHOT_REJECT_REFUND Key=%d Budget=%d Feedback=%d"),
			PredictionKey, Record.bBudgetConsumed ? 1 : 0, Record.bFeedbackPlayed ? 1 : 0);
	}

	Record.bResolved = true;

#if WITH_DEV_AUTOMATION_TESTS
	LastResolvedShotKeyForTest = PredictionKey;
	bLastResolvedShotCommittedForTest = bCommitted;
#endif
	UE_LOG(LogShootGame, Display, TEXT("FIRE_SHOT_VERDICT_RESOLVED Key=%d Committed=%d Budget=%d Feedback=%d"),
		PredictionKey, bCommitted ? 1 : 0, Record.bBudgetConsumed ? 1 : 0, Record.bFeedbackPlayed ? 1 : 0);
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
	// 同一个 GA_Fire 同时服务单发与连发，因此策略是动态 Gameplay Context 查询：
	// 由当前武器决定，不依赖任何由装备路径同步的派生状态，也没有「同步落后于武器」的窗口。
	// 显式使用传入的 ActorInfo：未激活的实例上没有 CurrentActorInfo，不能读实例缓存。
	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	const AShooterWeapon* Weapon = GetCurrentWeaponForAvatar(AvatarActor);
	return Weapon && Weapon->IsFullAuto()
		? EShooterAbilityActivationPolicy::WhileInputActive
		: EShooterAbilityActivationPolicy::OnInputTriggered;
}

bool UShooterGameplayAbility_Fire::IsInputHeld(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo) const
{
	const UAbilitySystemComponent* AbilitySystemComponent = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const FGameplayAbilitySpec* Spec = AbilitySystemComponent
		? AbilitySystemComponent->FindAbilitySpecFromHandle(Handle)
		: nullptr;
	return Spec != nullptr && Spec->InputPressed;
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
