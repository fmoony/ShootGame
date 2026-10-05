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
#include "TimerManager.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/Interfaces/ShooterWeaponHolder.h"
#include "ShootGame.h"

UShooterGameplayAbility_Fire::UShooterGameplayAbility_Fire()
{
	// 同一 Avatar 同生命周期内只保留一个实例；拥有者与服务器各自持有一份实例。
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	// UE 5.6 的 UGameplayAbility 构造函数把该标志默认置为 true。
	// GA_Fire 必须保留服务器对权威 Ability 何时结束的决定权，因此显式关闭：
	// 客户端释放只经可靠 ServerSetInputReleased 通知，不新增第二条 StopFire RPC。
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

	if (!LocalWeapon->IsFullAuto())
	{
		// 半自动：距上一次本地有效开火不足 RefireRate 时直接拒绝这次输入，
		// 既不播放开火表现，也不向服务器发送 Fire 请求，更不允许稍后补枪。
		if (!LocalWeapon->IsLocalFireCooldownReady())
		{
			return false;
		}
	}
	else if (!IsInputHeld(Handle, ActorInfo))
	{
		// 全自动是「按住持续」语义：已经松开的按下沿不得在短暂阻塞解除后补出一次连发。
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

	// 全自动的按住语义在权威端同样成立：远端请求的 Spec.InputPressed 由引擎按请求置位，
	// 主机本地视图则直接反映真实按键；已松开的按下沿不得启动权威连发。
	if (Weapon->IsFullAuto() && !IsInputHeld(Handle, ActorInfo))
	{
		return RecordAuthorityRejectAndReturnFalse();
	}

	// 半自动必须显式拒绝冷却期内的重复激活，不再沿用“激活成功但 StartFiring 静默不发射”的语义。
	if (!IsAuthorityCadenceReady(*Weapon))
	{
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

bool UShooterGameplayAbility_Fire::IsAuthorityCadenceReady(const AShooterWeapon& Weapon)
{
	// 全自动允许冷却期激活：服务器沿用剩余冷却后继续权威射击，真实补射时机由权威 RefireTimer 决定。
	// 半自动必须越过权威 RefireTimer，避免“激活成功但 Fire 静默不发射”。
	return Weapon.IsFullAuto() || Weapon.CanStartSemiAutoShotNow();
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
	UAbilitySystemComponent* AbilitySystemComponent = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	AShooterWeapon* Weapon = GetCurrentWeaponForAvatar(AvatarActor);

	// 权威端防御复核：CanActivateAbility 与 ActivateAbility 之间仍存在窗口（远端请求尤其明显），
	// 失效时按 Cancelled 结束，也不计入权威拒绝计数。
	// 正式 Reject 只由服务器 CanActivateAbility 返回 false 后经 GAS 下发，
	// 不在这里手工伪造 ActivationMode::Rejected。
	// 权威上下文失效或节拍不成立时按 Cancelled 结束；与起手资格共用同一组共享谓词。
	if (bAuthoritySide)
	{
		if (Weapon)
		{
			// 先打开本轮结果记录：即使随后的防御复核按 Cancelled 结束，也会以 Processed=0 结清。
			Weapon->BeginAuthorityFireActivation(ActivationInfo.GetActivationPredictionKey().Current);
		}
		const bool bAuthorityContextOk = IsAuthorityFireContextValid(AvatarActor, AbilitySystemComponent, Weapon) &&
			IsAuthorityCadenceReady(*Weapon);
		if (!bAuthorityContextOk)
		{
			if (Weapon)
			{
				Weapon->SettleAuthorityFireActivation();
			}
			EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility*/ true, /*bWasCancelled*/ true);
			return;
		}
	}

	// 双端缓存：权威端控武器，拥有端控表现。
	CachedWeapon = Weapon;

	// 三个按激活复位的本地状态：反馈序号、本次预测发数与迟到 Reject 归因身份。
	// 不复位会让日志的 ShotOrdinal 跨 Burst 累积，也会让退还额度跨激活继承。
	PredictedShotOrdinal = 0;
	PredictedShotsThisActivation = 0;
	EffectivePredictionKey = 0;

	// Owner Local：本地预测表现路径。
	if (bOwnerLocalView && CachedWeapon.IsValid())
	{
		// Activation 对账账本：必须在建立本地预测消费之前登记，并绑定明确结果通道。
		// 拥有端释放输入会在 Reject 到达前结束实例并清空 CachedWeapon，
		// 只有按 PredictionKey 保留的账本才能在迟到 Reject / 迟到服务器结果下正确结清。
		FPredictionKey ActivationPredictionKey = ActivationInfo.GetActivationPredictionKey();
		EffectivePredictionKey = ActivationPredictionKey.Current;
		RegisterActivationLedger(EffectivePredictionKey, CachedWeapon.Get());
		CachedWeapon->BindPredictionAbility(this);
		if (ActivationPredictionKey.IsValidKey())
		{
			ActivationPredictionKey.NewRejectedDelegate().BindUObject(
				this, &UShooterGameplayAbility_Fire::HandlePredictedActivationRejected, EffectivePredictionKey);
		}

		// 明确 Activation Success 只标记"不可能再 Reject"，不代替服务器结果 Settled 结清。
		// PredictionKey CaughtUp 不再参与退款 / 结清判定（它不代表 Accept 或 Reject）。
		if (ConfirmDelegateHandle.IsValid())
		{
			OnConfirmDelegate.Remove(ConfirmDelegateHandle);
		}
		ConfirmDelegateHandle = OnConfirmDelegate.AddUObject(
			this, &UShooterGameplayAbility_Fire::HandleActivationConfirmed, EffectivePredictionKey);

		StartOwnerFirePath(*CachedWeapon.Get());
	}

	// Authority：权威开火路径。
	if (bAuthoritySide)
	{
		StartAuthorityFirePath(*Weapon);
	}
}

void UShooterGameplayAbility_Fire::InputReleased(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	// 拥有者第一人称表现只来自本地预测，服务器 Reject 不回滚也不补播，
	// 因此本地预测实例在释放时即可收口，不再需要等待 Confirm / Reject 决定补播。
	// 清理统一在 EndAbility：停本地循环、停权威武器、Reject 退还都在那里，这里不重复。
	// 预测客户端的释放意图已由可靠 ServerSetInputReleased 承载；
	// GA_Fire 显式不接受客户端直接结束服务器实例，因此不复制第二条结束命令。
	EndAbility(Handle, ActorInfo, ActivationInfo, HasAuthority(&ActivationInfo), false);
}

void UShooterGameplayAbility_Fire::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 幂等清理：Reject、释放、切枪、换弹、死亡、断线与弹药耗尽可能同时到达。
	// 本地表现与权威武器都在这里收口，调用方不需要各自清理。
	StopOwnerFireLoop();

	if (CachedWeapon.IsValid())
	{
		// 权威端无论正常结束、取消、打空还是切换都标记本轮 Settled；客户端通过结果记录收口。
		if (HasAuthority(&ActivationInfo))
		{
			CachedWeapon->SettleAuthorityFireActivation();
		}

		// Reject 退还：迟到 Reject 由 PredictionKey 委托与本分支共同收口，账本保证只退一次。
		// 只处理 Rejected：Cancelled（切枪 / 换弹 / 死亡取消）时服务器可能已经扣过部分弹药，
		// 退款只会反向过冲，因此绝不按 Cancelled 退还。
		if (ActivationInfo.ActivationMode == EGameplayAbilityActivationMode::Rejected)
		{
			ResolveRejectedActivation(ActivationInfo.GetActivationPredictionKey().Current, CachedWeapon.Get(),
				PredictedShotsThisActivation);
		}

		CachedWeapon->OnOutOfAmmo.RemoveAll(this);
	}

	// 权威字段写入门控在 HasAuthority：拥有端只清本地表现。
	if (HasAuthority(&ActivationInfo))
	{
		StopAuthorityFirePath();
	}

	CachedWeapon.Reset();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

	UE_LOG(LogShootGame, Display, TEXT("GA_Fire ended: Cancelled=%s Avatar=%s"),
		bWasCancelled ? TEXT("true") : TEXT("false"), *GetNameSafe(GetShooterAvatarActor()));
}

// ============================ Owner Prediction Path ============================

void UShooterGameplayAbility_Fire::StartOwnerFirePath(AShooterWeapon& Weapon)
{
	// 首拍：半自动恒为一次；全自动必须越过上一 Burst 的本地节拍，
	// 不允许把“快速 Release → Re-Press”惩罚成一个完整 RefireRate。
	if (!Weapon.IsFullAuto() || Weapon.IsLocalFireCooldownReady())
	{
		TryOwnerPredictedShot(Weapon);
	}

	// 本路径唯一的分叉点：是否建立连续预测循环（Semi 的下一发由下一次输入驱动）。
	if (Weapon.IsFullAuto())
	{
		StartOwnerFireLoop(Weapon);
	}
}

void UShooterGameplayAbility_Fire::StartOwnerFireLoop(AShooterWeapon& Weapon)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// RefireRate 允许为 0；下限保护避免同帧死循环。
	const float Interval = FMath::Max(Weapon.GetRefireRate(), 0.01f);
	// 全自动首拍：刚播过就等一个完整 Interval；上一 Burst 的节拍还没结束就只等剩余 cooldown。
	const float FirstDelay = FMath::Max(Weapon.GetLocalFireCooldownRemaining(), 0.01f);
	World->GetTimerManager().SetTimer(PredictedFeedbackTimer, this,
		&UShooterGameplayAbility_Fire::HandleOwnerFireTick, Interval, /*bLoop*/ true, FirstDelay);
	bPredictedFeedbackActive = true;
}

bool UShooterGameplayAbility_Fire::TryOwnerPredictedShot(AShooterWeapon& Weapon)
{
	// 上下文：表现目标必须成立（武器有效、未隐藏、仍是当前装备），否则本次 Shot Attempt 不成立。
	if (!IsOwnerFireContextValid())
	{
		return false;
	}

	// Activation 对账判定：已结清不再产生预测；已被 confirmed backfill 覆盖的 ordinal
	// 只推进本地 cadence，不重复表现、不再次增加 Pending。
	const EShooterOwnerShotAttemptDecision Decision = DecideOwnerShotAttempt(EffectivePredictionKey);
	if (Decision == EShooterOwnerShotAttemptDecision::RefusedResolved)
	{
		return false;
	}
	if (Decision == EShooterOwnerShotAttemptDecision::SkipCovered)
	{
		Weapon.AdvanceLocalFireCooldown();
		return false;
	}

	// 预测弹药预算：只有预测型 Owner 客户端会真正扣预算；
	// 权威端（Listen Host / Standalone）与非拥有者视图由 Weapon 侧直接放行，它们不维护 Pending。
	if (!Weapon.TryConsumePredictedAmmo())
	{
		// 预算不足：本次本地 Shot Attempt 不成立，既不播表现也不推进节拍。
		// 不结束 Ability、不停 FullAuto timer：预算会随服务器结果结清自动恢复。
		return false;
	}

	// 记入本次 activation 的预测发数，供 Reject 退款与服务器结果结清使用。
	++PredictedShotsThisActivation;
	NoteOwnerPredictedShot(EffectivePredictionKey);

	// 本地节拍：这是唯一推进 LocalFireCooldown 的地方，
	// 与 Montage / Niagara / Sound 是否真的播放成功无关。
	Weapon.AdvanceLocalFireCooldown();

	// Owner 表现：本地预测只提交纯表现，不扣弹、不生成弹丸、不写权威字段。
	PlayOwnerShotFeedback(Weapon);
	return true;
}

bool UShooterGameplayAbility_Fire::PlayOwnerShotFeedback(AShooterWeapon& Weapon)
{
	++PredictedShotOrdinal;
	if (!Weapon.PlayOwnerPredictedShotFeedback())
	{
		return false;
	}

	LogFirePredictionMarker(TEXT("FIRE_PREDICTED_OWNER"), &Weapon, PredictedShotOrdinal);
	return true;
}

void UShooterGameplayAbility_Fire::HandleOwnerFireTick()
{
	// 武器被切换、隐藏或生命周期失效：立即结束 Ability，避免本地节拍串到新 Owner。
	if (!IsOwnerFireContextValid())
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(),
			/*bReplicateEndAbility*/ false, /*bWasCancelled*/ true);
		return;
	}

	// 全自动每一拍同样是一次本地 Shot Attempt。
	TryOwnerPredictedShot(*CachedWeapon.Get());
}

void UShooterGameplayAbility_Fire::StopOwnerFireLoop()
{
	if (PredictedFeedbackTimer.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(PredictedFeedbackTimer);
		}
		PredictedFeedbackTimer.Invalidate();
	}

	if (bPredictedFeedbackActive)
	{
		bPredictedFeedbackActive = false;
		LogFirePredictionMarker(TEXT("FIRE_LOCAL_FEEDBACK_STOPPED"), CachedWeapon.Get(), PredictedShotOrdinal);
	}
}

bool UShooterGameplayAbility_Fire::IsOwnerFireContextValid()
{
	return IsOwnerFireContextValidForContext(GetShooterAvatarActor(), CachedWeapon.Get(), GetAbilitySystemComponentFromActorInfo());
}

bool UShooterGameplayAbility_Fire::IsOwnerFireContextValidForContext(AActor* AvatarActor,
	const AShooterWeapon* Weapon, const UAbilitySystemComponent* AbilitySystemComponent)
{
	// 唯一判据是“表现目标是否成立”：武器有效、未隐藏、仍是当前装备。
	//
	// 刻意不读取 Ammo / Reloading / Equipping / Dead：这些复制状态可能过期，
	// 用它们二次否决首次 Owner 表现，就会让服务器已经接受并生成弹丸的那一发永久没有反馈。
	// 空枪连点时的 cosmetic phantom 是本模型明确接受的代价（服务器仍会 Reject）。
	// 参数刻意不使用，测试依赖「这些状态存在也不参与门控」这一事实，见头文件说明。
	(void)AbilitySystemComponent;
	return AvatarActor && IsValid(Weapon) && !Weapon->IsHidden() && GetCurrentWeaponForAvatar(AvatarActor) == Weapon;
}

// ============================ Authority Gameplay Path ============================

void UShooterGameplayAbility_Fire::StartAuthorityFirePath(AShooterWeapon& Weapon)
{
	// 权威每一发在 Weapon 内执行：StartFiring → Fire → ExecuteFireAtTarget → 弹丸与表现；
	// 连发由 Weapon 的 RefireTimer 驱动。本 Ability 只启动事务并接管弹药耗尽收口。
	Weapon.OnOutOfAmmo.AddUObject(this, &UShooterGameplayAbility_Fire::HandleWeaponOutOfAmmo);
	Weapon.StartFiring();

	UE_LOG(LogShootGame, Display, TEXT("GA_Fire activated: Avatar=%s Weapon=%s Ammo=%d"),
		*GetNameSafe(GetShooterAvatarActor()), *GetNameSafe(&Weapon), Weapon.GetBulletCount());
}

void UShooterGameplayAbility_Fire::StopAuthorityFirePath()
{
	if (CachedWeapon.IsValid())
	{
		CachedWeapon->StopFiring();
	}
}

void UShooterGameplayAbility_Fire::HandleWeaponOutOfAmmo(AShooterWeapon* Weapon)
{
	if (Weapon != CachedWeapon.Get())
	{
		return;
	}

	// Weapon 已自行 StopFiring；这里只结束 Ability 并移除 State.Firing。
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
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

void UShooterGameplayAbility_Fire::LogFirePredictionMarker(const TCHAR* Marker, const AShooterWeapon* Weapon, int32 ShotOrdinal) const
{
#if WITH_DEV_AUTOMATION_TESTS
	// Shipping 不增加预测测试日志带宽：全部 P1 标记只在开发构建输出。
	const AActor* AvatarActor = GetShooterAvatarActor();
	const APawn* AvatarPawn = Cast<APawn>(AvatarActor);
	const APlayerState* OwnerPlayerState = AvatarPawn ? AvatarPawn->GetPlayerState() : nullptr;
	const int32 PlayerId = OwnerPlayerState ? OwnerPlayerState->GetPlayerId() : INDEX_NONE;
	const int32 PredictionKey = GetCurrentActivationInfo().GetActivationPredictionKey().Current;
	const int32 NetModeValue = AvatarActor ? static_cast<int32>(AvatarActor->GetNetMode()) : INDEX_NONE;
	const UWorld* World = GetWorld();
	const float LocalTime = World ? World->GetTimeSeconds() : 0.0f;

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("%s PlayerId=%d Weapon=%s PredictionKey=%d ShotOrdinal=%d Count=%d OwnerLocalTime=%.3f NetMode=%d"),
		Marker,
		PlayerId,
		*GetNameSafe(Weapon),
		PredictionKey,
		ShotOrdinal,
		Weapon ? Weapon->GetPredictedOwnerFeedbackCountForAutomationTest() : 0,
		LocalTime,
		NetModeValue);
#endif
}

// ============================ 测试观察接口 ============================

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

void UShooterGameplayAbility_Fire::RegisterActivationLedger(int32 PredictionKey, AShooterWeapon* Weapon)
{
	if (PredictionKey <= 0 || !Weapon)
	{
		return;
	}

	PruneActivationLedgers();
	FPredictedFireActivationLedger& Ledger = PendingActivationLedgers.FindOrAdd(PredictionKey);
	Ledger.Weapon = Weapon;
	Ledger.ActivationSerial = 0;
	Ledger.PredictedShots = 0;
	Ledger.ProcessedSeen = 0;
	Ledger.BackfilledShots = 0;
	Ledger.ReconciledPredictedShots = 0;
	Ledger.HighestBackfilledOrdinal = 0;
	Ledger.NextLocalAttemptOrdinal = 1;
	Ledger.bServerConfirmed = false;
	Ledger.bResolved = false;
}

UShooterGameplayAbility_Fire::EShooterOwnerShotAttemptDecision UShooterGameplayAbility_Fire::DecideOwnerShotAttempt(int32 PredictionKey)
{
	FPredictedFireActivationLedger* Ledger = PendingActivationLedgers.Find(PredictionKey);
	if (!Ledger)
	{
		// 无账本（例如 PredictionKey 无效）：退回旧行为，不改变本地预测边界。
		return EShooterOwnerShotAttemptDecision::Predict;
	}
	if (Ledger->bResolved)
	{
		return EShooterOwnerShotAttemptDecision::RefusedResolved;
	}
	if (Ledger->NextLocalAttemptOrdinal <= Ledger->HighestBackfilledOrdinal)
	{
		// 服务器已提交并由 confirmed backfill 覆盖过该 ordinal：本地节拍追上来时不得双播。
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("FIRE_PREDICTED_SKIPPED_BACKFILL_COVERED Key=%d Ordinal=%d HighestBackfilled=%d"),
			PredictionKey,
			Ledger->NextLocalAttemptOrdinal,
			Ledger->HighestBackfilledOrdinal);
		++Ledger->NextLocalAttemptOrdinal;
		return EShooterOwnerShotAttemptDecision::SkipCovered;
	}
	return EShooterOwnerShotAttemptDecision::Predict;
}

void UShooterGameplayAbility_Fire::NoteOwnerPredictedShot(int32 PredictionKey)
{
	FPredictedFireActivationLedger* Ledger = PendingActivationLedgers.Find(PredictionKey);
	if (Ledger)
	{
		++Ledger->PredictedShots;
		++Ledger->NextLocalAttemptOrdinal;
	}
}

void UShooterGameplayAbility_Fire::HandleAuthorityFireActivationResult(int32 ActivationKey, int32 ActivationSerial,
	int32 ProcessedShots, bool bSettled)
{
	FPredictedFireActivationLedger* Ledger = PendingActivationLedgers.Find(ActivationKey);
	if (!Ledger || Ledger->bResolved)
	{
		return;
	}

	if (Ledger->ActivationSerial == 0)
	{
		Ledger->ActivationSerial = ActivationSerial;
	}
	else if (ActivationSerial != 0 && Ledger->ActivationSerial != ActivationSerial)
	{
		// 不同轮次复用同一 PredictionKey：忽略旧槽内容。
		return;
	}

	Ledger->ProcessedSeen = FMath::Max(Ledger->ProcessedSeen, ProcessedShots);
	ApplyActivationResultToLedger(ActivationKey, bSettled);
}

void UShooterGameplayAbility_Fire::ApplyActivationResultToLedger(int32 PredictionKey, bool bSettled)
{
	FPredictedFireActivationLedger* Ledger = PendingActivationLedgers.Find(PredictionKey);
	if (!Ledger || Ledger->bResolved)
	{
		return;
	}

	AShooterWeapon* Weapon = Ledger->Weapon.Get();

	// 1) 服务器真实提交、但 Owner 没有提前表现的 Shot：按差值补播一次 confirmed feedback。
	const int32 CoveredBefore = Ledger->PredictedShots + Ledger->BackfilledShots;
	const int32 RequiredBackfill = FMath::Max(0, Ledger->ProcessedSeen - CoveredBefore);
	if (RequiredBackfill > 0)
	{
		if (Weapon)
		{
			for (int32 Index = 0; Index < RequiredBackfill; ++Index)
			{
				Weapon->PlayOwnerConfirmedShotFeedback();
			}
		}
		Ledger->BackfilledShots += RequiredBackfill;
		Ledger->HighestBackfilledOrdinal = FMath::Max(Ledger->HighestBackfilledOrdinal, Ledger->ProcessedSeen);
		UE_LOG(LogShootGame, Display, TEXT("FIRE_CONFIRMED_BACKFILL Key=%d Amount=%d Backfilled=%d Processed=%d"),
			PredictionKey, RequiredBackfill, Ledger->BackfilledShots, Ledger->ProcessedSeen);
	}

	// 2) 已确认的预测发数从 Pending 结清：这些枪已经被 Owner 提前表现覆盖，不重复播放。
	const int32 AcceptedPredicted = FMath::Max(0, Ledger->ProcessedSeen - Ledger->BackfilledShots);
	const int32 NewlyReconciled = FMath::Max(0, AcceptedPredicted - Ledger->ReconciledPredictedShots);
	if (NewlyReconciled > 0)
	{
		if (Weapon)
		{
			Weapon->ConfirmPredictedAmmo(NewlyReconciled);
		}
		Ledger->ReconciledPredictedShots += NewlyReconciled;
	}

	// 3) Settled：补齐最后的缺表现，并清掉服务器最终没有执行的预测（phantom）。
	if (bSettled)
	{
		const int32 CoveredNow = Ledger->PredictedShots + Ledger->BackfilledShots;
		const int32 RemainingBackfill = FMath::Max(0, Ledger->ProcessedSeen - CoveredNow);
		if (RemainingBackfill > 0)
		{
			if (Weapon)
			{
				for (int32 Index = 0; Index < RemainingBackfill; ++Index)
				{
					Weapon->PlayOwnerConfirmedShotFeedback();
				}
			}
			Ledger->BackfilledShots += RemainingBackfill;
			Ledger->HighestBackfilledOrdinal = FMath::Max(Ledger->HighestBackfilledOrdinal, Ledger->ProcessedSeen);
		}

		const int32 RemainingPredicted = FMath::Max(0, Ledger->PredictedShots - Ledger->ReconciledPredictedShots);
		if (RemainingPredicted > 0)
		{
			if (Weapon)
			{
				Weapon->DiscardPhantomPredictedAmmo(RemainingPredicted);
			}
			Ledger->ReconciledPredictedShots += RemainingPredicted;
			UE_LOG(LogShootGame, Display, TEXT("FIRE_ACTIVATION_PHANTOM_CLEAR Key=%d Amount=%d"), PredictionKey, RemainingPredicted);
		}

		Ledger->bResolved = true;
#if WITH_DEV_AUTOMATION_TESTS
		LastSettledActivationKeyForTest = PredictionKey;
		LastSettledActivationPredictedShotsForTest = Ledger->PredictedShots;
		LastSettledActivationProcessedShotsForTest = Ledger->ProcessedSeen;
		LastSettledActivationBackfilledShotsForTest = Ledger->BackfilledShots;
#endif
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("FIRE_ACTIVATION_SETTLED Key=%d Processed=%d Predicted=%d Backfilled=%d Reconciled=%d"),
			PredictionKey,
			Ledger->ProcessedSeen,
			Ledger->PredictedShots,
			Ledger->BackfilledShots,
			Ledger->ReconciledPredictedShots);
	}
}

void UShooterGameplayAbility_Fire::HandleActivationConfirmed(UGameplayAbility* Ability, int32 PredictionKey)
{
	FPredictedFireActivationLedger* Ledger = PendingActivationLedgers.Find(PredictionKey);
	if (Ledger)
	{
		// 明确 Success：只保证此后不会再有 Reject，不代替服务器结果 Settled 结清。
		Ledger->bServerConfirmed = true;
	}
}

void UShooterGameplayAbility_Fire::HandlePredictedActivationRejected(int32 PredictionKey)
{
	// 委托在 ClientActivateAbilityFailed 里先于实例匹配与 K2_EndAbility 广播；
	// 即使实例已经结束、CachedWeapon 已清空，也能按账本退还。
	ResolveRejectedActivation(PredictionKey, nullptr, 0);
}

void UShooterGameplayAbility_Fire::ResolveRejectedActivation(int32 PredictionKey, AShooterWeapon* FallbackWeapon, int32 FallbackShots)
{
	if (FPredictedFireActivationLedger* Ledger = PendingActivationLedgers.Find(PredictionKey))
	{
		if (Ledger->bResolved)
		{
			// Reject 委托与 EndAbility 都可能到达，本轮只允许结清一次。
			return;
		}

		if (Ledger->bServerConfirmed)
		{
			// 明确 Success 与 Reject 互斥；若异常顺序出现，保留账本等待服务器结果结清。
			return;
		}

		const int32 Remaining = FMath::Max(0, Ledger->PredictedShots - Ledger->ReconciledPredictedShots);
		if (Remaining > 0)
		{
			if (AShooterWeapon* Weapon = Ledger->Weapon.Get())
			{
				Weapon->RefundPredictedAmmo(Remaining);
			}
			Ledger->ReconciledPredictedShots += Remaining;
		}
		Ledger->bResolved = true;
		UE_LOG(LogShootGame, Display, TEXT("FIRE_ACTIVATION_REJECT_REFUND Key=%d Refund=%d"), PredictionKey, Remaining);
		return;
	}

	// 没有登记账本（例如 PredictionKey 无效）：保留 EndAbility 原有退还语义。
	if (FallbackWeapon && FallbackShots > 0)
	{
		FallbackWeapon->RefundPredictedAmmo(FallbackShots);
		UE_LOG(LogShootGame, Display, TEXT("FIRE_PREDICTION_REFUND Key=%d Amount=%d Weapon=%s Source=Fallback"),
			PredictionKey, FallbackShots, *GetNameSafe(FallbackWeapon));
	}
}

void UShooterGameplayAbility_Fire::PruneActivationLedgers()
{
	for (auto It = PendingActivationLedgers.CreateIterator(); It; ++It)
	{
		if (It->Value.bResolved)
		{
			It.RemoveCurrent();
		}
	}

	// 异常通道（结果长期不达）下不要无限累积：超过 4 条未结清账本按"服务器信息丢失"清理，
	// 只清 Pending、不补播；正常单 Activation 流程不会触发该上限。
	constexpr int32 MaxUnresolvedLedgers = 4;
	while (PendingActivationLedgers.Num() > MaxUnresolvedLedgers)
	{
		auto It = PendingActivationLedgers.CreateIterator();
		if (!It)
		{
			break;
		}
		FPredictedFireActivationLedger& Ledger = It->Value;
		const int32 Remaining = FMath::Max(0, Ledger.PredictedShots - Ledger.ReconciledPredictedShots);
		if (Remaining > 0)
		{
			if (AShooterWeapon* Weapon = Ledger.Weapon.Get())
			{
				Weapon->DiscardPhantomPredictedAmmo(Remaining);
			}
			Ledger.ReconciledPredictedShots += Remaining;
		}
		UE_LOG(LogShootGame, Warning, TEXT("FIRE_ACTIVATION_LEDGER_DROPPED Key=%d Reason=Overflow"), It->Key);
		It.RemoveCurrent();
	}
}

#if WITH_DEV_AUTOMATION_TESTS

int32 UShooterGameplayAbility_Fire::GetLastSettledActivationKeyForTest() const
{
	return LastSettledActivationKeyForTest;
}

int32 UShooterGameplayAbility_Fire::GetLastSettledActivationPredictedShotsForTest() const
{
	return LastSettledActivationPredictedShotsForTest;
}

int32 UShooterGameplayAbility_Fire::GetLastSettledActivationProcessedShotsForTest() const
{
	return LastSettledActivationProcessedShotsForTest;
}

int32 UShooterGameplayAbility_Fire::GetLastSettledActivationBackfilledShotsForTest() const
{
	return LastSettledActivationBackfilledShotsForTest;
}

int32 UShooterGameplayAbility_Fire::GetUnresolvedActivationLedgerCountForTest() const
{
	int32 Count = 0;
	for (const TPair<int32, FPredictedFireActivationLedger>& Pair : PendingActivationLedgers)
	{
		Count += Pair.Value.bResolved ? 0 : 1;
	}
	return Count;
}

int32 UShooterGameplayAbility_Fire::GetActivationLedgerPredictedShotsForTest(int32 PredictionKey) const
{
	const FPredictedFireActivationLedger* Ledger = PendingActivationLedgers.Find(PredictionKey);
	return Ledger ? Ledger->PredictedShots : INDEX_NONE;
}

int32 UShooterGameplayAbility_Fire::GetActivationLedgerProcessedShotsForTest(int32 PredictionKey) const
{
	const FPredictedFireActivationLedger* Ledger = PendingActivationLedgers.Find(PredictionKey);
	return Ledger ? Ledger->ProcessedSeen : INDEX_NONE;
}

int32 UShooterGameplayAbility_Fire::GetActivationLedgerBackfilledShotsForTest(int32 PredictionKey) const
{
	const FPredictedFireActivationLedger* Ledger = PendingActivationLedgers.Find(PredictionKey);
	return Ledger ? Ledger->BackfilledShots : INDEX_NONE;
}

int32 UShooterGameplayAbility_Fire::GetActivationLedgerReconciledShotsForTest(int32 PredictionKey) const
{
	const FPredictedFireActivationLedger* Ledger = PendingActivationLedgers.Find(PredictionKey);
	return Ledger ? Ledger->ReconciledPredictedShots : INDEX_NONE;
}

bool UShooterGameplayAbility_Fire::IsActivationLedgerResolvedForTest(int32 PredictionKey) const
{
	const FPredictedFireActivationLedger* Ledger = PendingActivationLedgers.Find(PredictionKey);
	return Ledger != nullptr && Ledger->bResolved;
}

bool UShooterGameplayAbility_Fire::IsActivationLedgerServerConfirmedForTest(int32 PredictionKey) const
{
	const FPredictedFireActivationLedger* Ledger = PendingActivationLedgers.Find(PredictionKey);
	return Ledger != nullptr && Ledger->bServerConfirmed;
}

void UShooterGameplayAbility_Fire::RegisterActivationLedgerForTest(int32 PredictionKey, AShooterWeapon* Weapon)
{
	RegisterActivationLedger(PredictionKey, Weapon);
}

void UShooterGameplayAbility_Fire::NoteOwnerPredictedShotsForTest(int32 PredictionKey, int32 Count)
{
	for (int32 Index = 0; Index < Count; ++Index)
	{
		NoteOwnerPredictedShot(PredictionKey);
	}
}

void UShooterGameplayAbility_Fire::HandleAuthorityFireActivationResultForTest(int32 PredictionKey,
	int32 ActivationSerial, int32 ProcessedShots, bool bSettled)
{
	HandleAuthorityFireActivationResult(PredictionKey, ActivationSerial, ProcessedShots, bSettled);
}

void UShooterGameplayAbility_Fire::MarkActivationServerConfirmedForTest(int32 PredictionKey)
{
	if (FPredictedFireActivationLedger* Ledger = PendingActivationLedgers.Find(PredictionKey))
	{
		Ledger->bServerConfirmed = true;
	}
}

void UShooterGameplayAbility_Fire::ResolveRejectedActivationForTest(int32 PredictionKey)
{
	ResolveRejectedActivation(PredictionKey, nullptr, 0);
}

#endif
