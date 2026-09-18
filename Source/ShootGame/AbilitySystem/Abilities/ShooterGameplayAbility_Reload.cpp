// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterGameplayAbility_Reload.h"

#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "AbilitySystem/ShooterGameplayTags.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "Weapons/ShooterWeapon.h"
#include "ShootGame.h"

bool UShooterGameplayAbility_Reload::HasInputReloadTag() const
{
	return GetAssetTags().HasTag(ShooterGameplayTags::Input_Reload);
}

bool UShooterGameplayAbility_Reload::IsBlockedByStateDead() const
{
	return ActivationBlockedTags.HasTag(ShooterGameplayTags::State_Dead);
}

bool UShooterGameplayAbility_Reload::IsBlockedByStateReloading() const
{
	return ActivationBlockedTags.HasTag(ShooterGameplayTags::State_Reloading);
}

bool UShooterGameplayAbility_Reload::IsBlockedByStateEquipping() const
{
	return ActivationBlockedTags.HasTag(ShooterGameplayTags::State_Equipping);
}

bool UShooterGameplayAbility_Reload::OwnsStateReloadingWhileActive() const
{
	return ActivationOwnedTags.HasTag(ShooterGameplayTags::State_Reloading);
}

bool UShooterGameplayAbility_Reload::CanRetriggerInstancedAbility() const
{
	return bRetriggerInstancedAbility;
}

bool UShooterGameplayAbility_Reload::ServerRespectsRemoteAbilityCancellation() const
{
	return bServerRespectsRemoteAbilityCancellation;
}

UShooterGameplayAbility_Reload::UShooterGameplayAbility_Reload()
{
	// 同一 Avatar 同生命周期内只保留一个实例；拥有者与服务器各自持有一份实例。
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	// 拥有者本地预测换弹窗口，服务器独立执行权威事务；两端只允许相位差。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	// UE 5.6 的 UGameplayAbility 构造函数把该标志默认置为 true。
	// 本地换弹窗口到期只允许结束预测实例，绝不能让客户端提前结束服务器权威事务。
	bServerRespectsRemoteAbilityCancellation = false;

	// Input.Reload 是 ASC 输入查找与 Ability Spec 之间的稳定映射。
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(ShooterGameplayTags::Input_Reload);
	SetAssetTags(AssetTags);
	// 死亡、换弹中与装备中状态阻塞激活；State.Reloading 在激活期间由 GAS 自动挂到拥有者 ASC。
	ActivationBlockedTags.AddTag(ShooterGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(ShooterGameplayTags::State_Reloading);
	ActivationBlockedTags.AddTag(ShooterGameplayTags::State_Equipping);
	ActivationOwnedTags.AddTag(ShooterGameplayTags::State_Reloading);

	// 与 Fire 的关系只通过 GAS Tag 表达：激活换弹时取消活动中的开火事务。
	// 引擎在 PreActivate 里按 AssetTags 匹配并执行取消（Ignore 为发起者自身），
	// 因此这里不需要 ASC 的命令式取消调用，也不需要包含项目 ASC 头文件。
	CancelAbilitiesWithTag.AddTag(ShooterGameplayTags::Input_Fire);
}

bool UShooterGameplayAbility_Reload::CanActivateAbility(
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

	// 预测客户端的资格判定顺序（本地确定性条件先于 GAS Tag 门控）：
	// 1. ASC 与 Avatar 一致，且必须是本机拥有者视图；
	// 2. 本地换弹窗口必须有表现目标：当前武器有效且未隐藏；
	// 3. Super 的 ActivationBlockedTags 门控（State.Dead / State.Reloading / State.Equipping）。
	//
	// 客户端不得读取复制的 Magazine / Reserve 真值：它们可能过期，
	// 用「本地弹匣已满」否决会让服务器本会接受的换弹请求永远发不出去。
	// 权威校验（Inventory / Equipment / ownership / lifecycle / 弹药）全部留在服务器。
	if (!AvatarActor->HasAuthority())
	{
		const UAbilitySystemComponent* AbilitySystemComponent = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
		if (!AbilitySystemComponent || AbilitySystemComponent->GetAvatarActor() != AvatarActor)
		{
			return false;
		}

		if (!ActorInfo->IsLocallyControlledPlayer())
		{
			return false;
		}

		const AShooterWeapon* LocalWeapon = ResolveLocalReloadWeapon(const_cast<AActor*>(AvatarActor));
		if (!IsValid(LocalWeapon) || LocalWeapon->IsHidden())
		{
			return false;
		}

		return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
	}

	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	AShooterWeapon* Weapon = nullptr;
	return ResolveReloadTarget(ActorInfo, Weapon);
}

AShooterWeapon* UShooterGameplayAbility_Reload::ResolveLocalReloadWeapon(AActor* AvatarActor) const
{
	// 预测端只用它取换弹时长与表现目标；不做任何弹药或 Inventory 真值判定。
	const AShooterCharacter* Character = Cast<AShooterCharacter>(AvatarActor);
	if (!Character)
	{
		return nullptr;
	}

	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	AShooterWeapon* Weapon = Equipment ? Equipment->GetCurrentWeaponActor() : Character->GetCurrentWeapon();
	return IsValid(Weapon) && Weapon->GetOwner() == Character ? Weapon : nullptr;
}

bool UShooterGameplayAbility_Reload::ResolveReloadTarget(const FGameplayAbilityActorInfo* ActorInfo, AShooterWeapon*& OutWeapon) const
{
	OutWeapon = nullptr;

	const AShooterCharacter* Character = Cast<AShooterCharacter>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
	const UAbilitySystemComponent* AbilitySystemComponent = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!Character || !AbilitySystemComponent || AbilitySystemComponent->GetAvatarActor() != Character ||
		AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Dead))
	{
		return false;
	}

	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	AShooterWeapon* Weapon = Equipment
		? Equipment->GetCurrentWeaponActor()
		: Character->GetCurrentWeapon();
	// 提交前校验（重构方案 4.7）：Actor 仍在背包、归属未变、是当前装备且不在池内。
	if (!Inventory || !Equipment || !IsValid(Weapon) || !Inventory->ContainsWeapon(Weapon) ||
		Weapon->GetOwner() != Character || Weapon->IsHidden() ||
		Weapon->GetLifecycleState() == EShooterWeaponLifecycleState::InPool)
	{
		return false;
	}

	// 弹药权威在 WeaponActor：满弹匣或无备弹直接拒绝。
	if (Weapon->GetBulletCount() >= Weapon->GetMagazineSize() || Weapon->GetReserveAmmo() <= 0)
	{
		return false;
	}

	OutWeapon = Weapon;
	return true;
}

void UShooterGameplayAbility_Reload::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 分流的唯一依据：权威侧与本机拥有者本地视图。两者都不成立时不执行任何事务。
	const bool bAuthoritySide = HasAuthority(&ActivationInfo);
	const bool bOwnerLocalView = ActorInfo != nullptr && ActorInfo->IsLocallyControlledPlayer();
	if (!bAuthoritySide && !bOwnerLocalView)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility*/ false, /*bWasCancelled*/ true);
		return;
	}

	AShooterWeapon* Weapon = nullptr;
	if (bAuthoritySide)
	{
		// 服务器权威校验：目标必须仍在背包、仍是当前装备、归属未变且确实需要转移弹药。
		if (!ResolveReloadTarget(ActorInfo, Weapon))
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility*/ true, /*bWasCancelled*/ true);
			return;
		}
	}
	else
	{
		// 预测端只缓存表现目标与本地时钟所需的当前武器，不做任何真值判定。
		Weapon = ResolveLocalReloadWeapon(ActorInfo->AvatarActor.Get());
		if (!IsValid(Weapon))
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility*/ false, /*bWasCancelled*/ true);
			return;
		}
	}

	CachedWeapon = Weapon;

	// 换弹时钟只来自 WeaponActor 配置；Montage 丢失不影响任何一端的时序。
	// 预测端使用本地时钟，服务器使用权威时钟，两端只允许相位差。
	const float ReloadDuration = FMath::Max(0.0f, Weapon->GetReloadDuration());
	UAbilityTask_WaitDelay* WaitTask = UAbilityTask_WaitDelay::WaitDelay(this, ReloadDuration);
	WaitTask->OnFinish.AddDynamic(this, &UShooterGameplayAbility_Reload::HandleReloadWaitFinished);
	ReloadWaitTask = WaitTask;
	WaitTask->ReadyForActivation();

	const AShooterCharacter* ReloadCharacter = Cast<AShooterCharacter>(GetShooterAvatarActor());
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("GA_Reload activated: Side=%s Avatar=%s Weapon=%s WeaponId=%s Duration=%.3f Mag=%d Reserve=%d"),
		bAuthoritySide ? TEXT("Authority") : TEXT("Predicted"),
		*GetNameSafe(ReloadCharacter),
		*GetNameSafe(Weapon),
		*Weapon->GetWeaponId().ToString(),
		ReloadDuration,
		Weapon->GetBulletCount(),
		Weapon->GetReserveAmmo());
}

bool UShooterGameplayAbility_Reload::IsReloadTargetStillCurrent() const
{
	const AShooterCharacter* Character = Cast<AShooterCharacter>(GetShooterAvatarActor());
	UShooterInventoryComponent* Inventory = Character ? Character->GetInventoryComponent() : nullptr;
	UShooterEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	const UAbilitySystemComponent* AbilitySystemComponent = Character ? Character->GetAbilitySystemComponent() : nullptr;
	if (!Character || !Inventory || !Equipment || !AbilitySystemComponent ||
		AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Dead) || !CachedWeapon.IsValid() ||
		Equipment->GetCurrentWeaponActor() != CachedWeapon.Get() || !Inventory->ContainsWeapon(CachedWeapon.Get()) ||
		CachedWeapon->GetOwner() != Character || CachedWeapon->IsHidden() ||
		CachedWeapon->GetLifecycleState() == EShooterWeaponLifecycleState::InPool)
	{
		return false;
	}

	return true;
}

void UShooterGameplayAbility_Reload::HandleReloadWaitFinished()
{
	if (bReloadCommitted || !ReloadWaitTask.IsValid())
	{
		return;
	}

	ReloadWaitTask.Reset();

	// 预测端本地窗口到期：只结束本地预测实例。
	// 不提交任何事务、不改 Ammo，也不允许反向结束服务器权威事务
	// （bServerRespectsRemoteAbilityCancellation = false + bReplicateEndAbility = false）。
	// 服务器可能仍在换弹，这是允许的相位差，最终结果仍由服务器决定。
	if (!IsAvatarAuthoritative())
	{
		UE_LOG(LogShootGame, Verbose, TEXT("GA_Reload local window ended: Avatar=%s Weapon=%s"),
			*GetNameSafe(GetShooterAvatarActor()), *GetNameSafe(CachedWeapon.Get()));
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(),
			/*bReplicateEndAbility*/ false, /*bWasCancelled*/ false);
		return;
	}

	if (!IsReloadTargetStillCurrent())
	{
		UE_LOG(LogShootGame, Display, TEXT("GA_Reload commit aborted: target no longer current Avatar=%s WeaponId=%s"),
			*GetNameSafe(GetShooterAvatarActor()), *CachedWeapon->GetWeaponId().ToString());
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}

	int32 TransferredAmmo = 0;
	if (!CachedWeapon->ReloadFromReserve(TransferredAmmo))
	{
		UE_LOG(LogShootGame, Warning, TEXT("GA_Reload commit failed: Avatar=%s WeaponId=%s"),
			*GetNameSafe(GetShooterAvatarActor()), *CachedWeapon->GetWeaponId().ToString());
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}

	// 单事务护栏：即使 WaitDelay 异常重入，也只允许这次弹药转移。
	bReloadCommitted = true;
	UE_LOG(LogShootGame, Display, TEXT("GA_Reload committed: Avatar=%s WeaponId=%s Transferred=%d"),
		*GetNameSafe(GetShooterAvatarActor()), *CachedWeapon->GetWeaponId().ToString(), TransferredAmmo);

	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

void UShooterGameplayAbility_Reload::CleanupReloadTransaction()
{
	if (ReloadWaitTask.IsValid())
	{
		ReloadWaitTask->EndTask();
	}
	ReloadWaitTask.Reset();

	CachedWeapon.Reset();
	bReloadCommitted = false;
}

void UShooterGameplayAbility_Reload::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 完成、取消、失败共用清理：不会依赖 Ability 对象销毁来解除引用。
	CleanupReloadTransaction();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

	UE_LOG(LogShootGame, Display, TEXT("GA_Reload ended: Cancelled=%s Avatar=%s"),
		bWasCancelled ? TEXT("true") : TEXT("false"), *GetNameSafe(GetShooterAvatarActor()));
}
