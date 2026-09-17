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

	// 预测客户端只做"本地开火资格"预检，必须先于 Super，且不得读取 ActivationBlockedTags 等复制状态：
	// 弱网下上一轮 State.Reloading / State.Firing 的移除复制可能迟到，
	// 若先执行 Super 会把真实玩家唯一一次 Fire 输入吞在本地（修复 310faf9 的语义必须保留）。
	// 本机已知的阻塞态只用于禁止本地预测表现，见 ActivateAbility 第 2 步；请求仍然照常发给服务器。
	if (!AvatarActor->HasAuthority())
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

		// 本地 Shot Attempt 的资格：必须有可用的表现目标（当前武器有效且未隐藏），
		// 否则会出现"请求已发出、本地却无表现目标"的分裂。
		// 判据只读本地已知的武器对象，不读 Ammo / Reloading / Equipping / Dead 等可能过期的复制状态。
		const AShooterWeapon* LocalWeapon = GetCurrentWeaponForAvatar(const_cast<AActor*>(AvatarActor));
		if (!IsValid(LocalWeapon) || LocalWeapon->IsHidden())
		{
			return false;
		}

		// 半自动：距上一次本地有效开火不足 RefireRate 时直接拒绝这次输入，
		// 既不播放开火表现，也不向服务器发送 Fire 请求。
		// 全自动不参与该判据：它一次按下只发一个开始请求，之后由本地表现 Timer 推进节拍。
		if (!LocalWeapon->IsFullAuto() && !LocalWeapon->IsLocalFireCooldownReady())
		{
			return false;
		}

		return true;
	}

	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return RecordAuthorityRejectAndReturnFalse();
	}

	// 服务器完整校验：Avatar 必须是 ASC 当前 Avatar、State.Dead 未挂载、
	// 当前 WeaponActor 有效且属于该 Avatar，并且存在可消耗弹药。
	// 死亡判断统一使用 ASC Tag，不再 Cast Character/NPC 读各自 IsDead()。
	const UAbilitySystemComponent* AbilitySystemComponent = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const AShooterWeapon* Weapon = GetCurrentWeaponForAvatar(const_cast<AActor*>(AvatarActor));
	const bool bDead = AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Dead);

	if (!AbilitySystemComponent || AbilitySystemComponent->GetAvatarActor() != AvatarActor || bDead ||
		!IsValid(Weapon) || Weapon->GetOwner() != AvatarActor || Weapon->IsHidden() || !Weapon->CanConsumeAmmo())
	{
		return RecordAuthorityRejectAndReturnFalse();
	}

	// 半自动必须显式拒绝冷却期内的重复激活，不再沿用“激活成功但 StartFiring 静默不发射”的语义。
	// 全自动允许冷却期激活：服务器沿用剩余冷却后继续权威射击，真实补射时机由权威 RefireTimer 决定。
	if (!Weapon->IsFullAuto() && !Weapon->CanStartSemiAutoShotNow())
	{
		return RecordAuthorityRejectAndReturnFalse();
	}

	return true;
}

void UShooterGameplayAbility_Fire::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 分流的唯一依据：权威侧与拥有者本地玩家视图。
	// 监听主机与 Standalone 同时为真；Dedicated Server 上的 NPC 与远端玩家都不成立本地视图。
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

	// 第 1 步：权威端的 Activate 防御复核，失效时按 Cancelled 结束。
	// 正式 Reject 只由服务器 CanActivateAbility 返回 false 后经 GAS 下发，
	// 不在这里手工伪造 ActivationMode::Rejected。
	if (bAuthoritySide)
	{
		const bool bDead = AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Dead);
		const bool bAuthorityValid = AvatarActor && !bDead && IsValid(Weapon) && Weapon->GetOwner() == AvatarActor &&
			!Weapon->IsHidden() && Weapon->CanConsumeAmmo() &&
			(Weapon->IsFullAuto() || Weapon->CanStartSemiAutoShotNow());

		if (!bAuthorityValid)
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
	}

	// 双端缓存：权威端控武器，拥有端控表现。
	CachedWeapon = Weapon;

	// 本地反馈序号必须按激活复位：Ability 实例跨激活复用，
	// 不复位会让日志的 ShotOrdinal 跨 Burst 累积（PredictionKey 才标识一次激活或 Burst）。
	PredictedShotOrdinal = 0;
	bOwnerFeedbackPlayedThisActivation = false;

	// 第 2 步：拥有者本地视图立即播放一次纯表现；全自动同时启动本地表现节拍。
	if (bOwnerLocalView && CachedWeapon.IsValid())
	{
		StartOwnerPredictedFeedback(*CachedWeapon.Get());
	}

	// 第 3 步：权威端接管武器事务。
	if (bAuthoritySide)
	{
		CachedWeapon->OnOutOfAmmo.AddUObject(this, &UShooterGameplayAbility_Fire::HandleWeaponOutOfAmmo);
		CachedWeapon->StartFiring();

		UE_LOG(LogShootGame, Display, TEXT("GA_Fire activated: Avatar=%s Weapon=%s Ammo=%d"),
			*GetNameSafe(AvatarActor), *GetNameSafe(Weapon), Weapon->GetBulletCount());
	}
}

void UShooterGameplayAbility_Fire::InputReleased(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	// 拥有端与权威端都会进入本函数：先停本地表现节拍，再由权威端停武器。
	StopOwnerPredictedFeedback();

	const bool bAuthoritySide = HasAuthority(&ActivationInfo);
	if (bAuthoritySide)
	{
		StopAuthorityWeapon();
	}

	// 拥有者第一人称表现只来自本地预测，服务器 Reject 不回滚也不补播，
	// 因此本地预测实例在释放时即可收口，不再需要等待 Confirm / Reject 决定补播。
	// 预测客户端的释放意图已由可靠 ServerSetInputReleased 承载；
	// GA_Fire 显式不接受客户端直接结束服务器实例，因此不复制第二条结束命令。
	EndAbility(Handle, ActorInfo, ActivationInfo, bAuthoritySide, false);
}

void UShooterGameplayAbility_Fire::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 幂等清理：Reject、释放、切枪、换弹、死亡、断线与弹药耗尽可能同时到达。
	// 权威字段写入门控在 HasAuthority：拥有端只清本地表现。
	StopOwnerPredictedFeedback();

	if (CachedWeapon.IsValid())
	{
		CachedWeapon->OnOutOfAmmo.RemoveAll(this);
		if (HasAuthority(&ActivationInfo))
		{
			CachedWeapon->StopFiring();
		}
		CachedWeapon.Reset();
	}

	bOwnerFeedbackPlayedThisActivation = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

	UE_LOG(LogShootGame, Display, TEXT("GA_Fire ended: Cancelled=%s Avatar=%s"),
		bWasCancelled ? TEXT("true") : TEXT("false"), *GetNameSafe(GetShooterAvatarActor()));
}

void UShooterGameplayAbility_Fire::StopAuthorityWeapon()
{
	if (CachedWeapon.IsValid())
	{
		CachedWeapon->StopFiring();
	}
}

void UShooterGameplayAbility_Fire::StartOwnerPredictedFeedback(AShooterWeapon& Weapon)
{
	// 本地表现只提交纯表现：不扣弹、不生成弹丸、不写权威字段。
	// 表现目标必须成立（武器有效、未隐藏、仍是当前装备），否则不表现。
	// 这里不再读取 Ammo / Reloading / Equipping / Dead：它们可能过期，
	// 一旦用来二次否决，就会把服务器已经接受的真实射击压成"有弹无特效"。
	if (!IsOwnerPredictedFeedbackAllowed())
	{
		return;
	}

	if (!Weapon.IsFullAuto() || Weapon.IsLocalFireCooldownReady())
	{
		// 本地 Shot Attempt 被接受：这是唯一推进本地节拍的地方，
		// 与 Montage / Niagara / Sound 是否真的播放成功无关。
		Weapon.AdvanceLocalFireCooldown();
		TryPlayOwnerFeedback(Weapon, TEXT("FIRE_PREDICTED_OWNER"));
	}

	if (!Weapon.IsFullAuto())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// RefireRate 允许为 0；下限保护避免同帧死循环。
	const float Interval = FMath::Max(Weapon.GetRefireRate(), 0.01f);
	// 全自动首拍：刚播过就等一个完整 Interval；上一 Burst 的节拍还没结束就只等剩余 cooldown，
	// 不允许把"快速 Release → Re-Press"惩罚成一个完整 RefireRate。
	const float FirstDelay = FMath::Max(Weapon.GetLocalFireCooldownRemaining(), 0.01f);
	World->GetTimerManager().SetTimer(PredictedFeedbackTimer, this,
		&UShooterGameplayAbility_Fire::HandlePredictedFeedbackTick, Interval, /*bLoop*/ true, FirstDelay);
	bPredictedFeedbackActive = true;
}

bool UShooterGameplayAbility_Fire::TryPlayOwnerFeedback(AShooterWeapon& Weapon, const TCHAR* Marker)
{
	++PredictedShotOrdinal;
	if (!Weapon.PlayOwnerPredictedShotFeedback())
	{
		return false;
	}

	bOwnerFeedbackPlayedThisActivation = true;
	LogFirePredictionMarker(Marker, &Weapon, PredictedShotOrdinal);
	return true;
}

void UShooterGameplayAbility_Fire::StopOwnerPredictedFeedback()
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

void UShooterGameplayAbility_Fire::HandlePredictedFeedbackTick()
{
	AActor* AvatarActor = GetShooterAvatarActor();
	if (!IsCurrentWeaponStillValidForFeedback(AvatarActor))
	{
		// 武器被切换、隐藏或生命周期失效：立即结束 Ability，避免本地节拍串到新 Owner。
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(),
			/*bReplicateEndAbility*/ false, /*bWasCancelled*/ true);
		return;
	}

	AShooterWeapon* Weapon = CachedWeapon.Get();
	if (!IsOwnerPredictedFeedbackAllowed())
	{
		// 表现目标暂不成立：本拍不构成 Shot Attempt，也不推进节拍。
		return;
	}

	// 全自动每一拍同样是一次本地 Shot Attempt：先推进节拍，再只负责四路 cosmetic。
	Weapon->AdvanceLocalFireCooldown();
	TryPlayOwnerFeedback(*Weapon, TEXT("FIRE_PREDICTED_OWNER"));
}

bool UShooterGameplayAbility_Fire::IsCurrentWeaponStillValidForFeedback(AActor* AvatarActor) const
{
	const AShooterWeapon* Weapon = CachedWeapon.Get();
	if (!AvatarActor || !IsValid(Weapon) || Weapon->IsHidden())
	{
		return false;
	}

	return GetCurrentWeaponForAvatar(AvatarActor) == Weapon;
}

bool UShooterGameplayAbility_Fire::IsOwnerPredictedFeedbackAllowed()
{
	return IsOwnerPredictedFeedbackAllowedForContext(GetShooterAvatarActor(), CachedWeapon.Get(), GetAbilitySystemComponentFromActorInfo());
}

bool UShooterGameplayAbility_Fire::IsOwnerPredictedFeedbackAllowedForContext(AActor* AvatarActor,
	const AShooterWeapon* Weapon, const UAbilitySystemComponent* AbilitySystemComponent)
{
	// 唯一判据是"表现目标是否成立"：武器有效、未隐藏、仍是当前装备。
	//
	// 刻意不读取 Ammo / Reloading / Equipping / Dead：这些复制状态可能过期，
	// 用它们二次否决首次 Owner 表现，就会让服务器已经接受并生成弹丸的那一发永久没有反馈。
	// 空枪连点时的 cosmetic phantom 是本模型明确接受的代价（服务器仍会 Reject）。
	(void)AbilitySystemComponent;
	return AvatarActor && IsValid(Weapon) && !Weapon->IsHidden() && GetCurrentWeaponForAvatar(AvatarActor) == Weapon;
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

void UShooterGameplayAbility_Fire::HandleWeaponOutOfAmmo(AShooterWeapon* Weapon)
{
	if (Weapon != CachedWeapon.Get())
	{
		return;
	}

	// Weapon 已自行 StopFiring；这里只结束 Ability 并移除 State.Firing。
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

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
