// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterGameplayAbility_Reload.h"

#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "ShooterAbilitySystemComponent.h"
#include "ShooterCharacter.h"
#include "ShooterGameplayTags.h"
#include "ShooterInventoryComponent.h"
#include "ShooterWeapon.h"
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

UShooterGameplayAbility_Reload::UShooterGameplayAbility_Reload()
{
	// 同一 Avatar 同生命周期内只保留一个实例；网络执行只发生在服务器。
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	// Input.Reload 是 ASC 输入查找与 Ability Spec 之间的稳定映射。
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(ShooterGameplayTags::Input_Reload);
	SetAssetTags(AssetTags);
	// 死亡、换弹中与装备中状态阻塞激活；State.Reloading 在激活期间由 GAS 自动挂到拥有者 ASC。
	ActivationBlockedTags.AddTag(ShooterGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(ShooterGameplayTags::State_Reloading);
	ActivationBlockedTags.AddTag(ShooterGameplayTags::State_Equipping);
	ActivationOwnedTags.AddTag(ShooterGameplayTags::State_Reloading);
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

	// ServerOnly 的客户端本地预检必须只检查“请求确实来自当前 Avatar 的本地 ASC”，
	// 不能先执行 Super::CanActivateAbility：Super 会读取 ActivationBlockedTags 等依赖复制的状态，
	// 弱网下客户端可能还残留上一轮 State.Reloading，从而在本地吞掉唯一一次 R 输入。
	// 权威端仍会执行 Super + ResolveReloadTarget 的完整校验，非法请求由服务器拒绝。
	if (!AvatarActor->HasAuthority())
	{
		const UAbilitySystemComponent* AbilitySystemComponent =
			ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
		return AbilitySystemComponent &&
			AbilitySystemComponent->GetAvatarActor() == AvatarActor;
	}

	if (!Super::CanActivateAbility(
		Handle,
		ActorInfo,
		SourceTags,
		TargetTags,
		OptionalRelevantTags))
	{
		return false;
	}

	AShooterWeapon* Weapon = nullptr;
	FGuid InstanceId;
	return ResolveReloadTarget(ActorInfo, Weapon, InstanceId);
}

bool UShooterGameplayAbility_Reload::ResolveReloadTarget(
	const FGameplayAbilityActorInfo* ActorInfo,
	AShooterWeapon*& OutWeapon,
	FGuid& OutInstanceId) const
{
	OutWeapon = nullptr;
	OutInstanceId = FGuid();

	const AShooterCharacter* Character = Cast<AShooterCharacter>(
		ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
	const UAbilitySystemComponent* AbilitySystemComponent =
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!Character || !AbilitySystemComponent ||
		AbilitySystemComponent->GetAvatarActor() != Character ||
		AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Dead))
	{
		return false;
	}

	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	AShooterWeapon* Weapon = Equipment
		? Equipment->GetCurrentWeaponActor()
		: Character->GetCurrentWeapon();
	if (!Inventory || !Equipment || !IsValid(Weapon) ||
		Weapon->GetOwner() != Character ||
		Weapon->IsHidden())
	{
		return false;
	}

	const FGuid InstanceId = Weapon->GetBoundInstanceId();
	const FShooterWeaponInstanceData* Instance =
		Inventory->FindWeaponInstance(InstanceId);
	if (!Instance ||
		Equipment->GetActiveWeaponInstanceId() != InstanceId ||
		Instance->MagazineAmmo >= Weapon->GetMagazineSize() ||
		Instance->ReserveAmmo <= 0)
	{
		return false;
	}

	OutWeapon = Weapon;
	OutInstanceId = InstanceId;
	return true;
}

void UShooterGameplayAbility_Reload::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 纵深防御：ServerOnly 能力仍显式确认服务器权威。
	if (!HasAuthority(&ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AShooterWeapon* Weapon = nullptr;
	if (!ResolveReloadTarget(ActorInfo, Weapon, TargetInstanceId))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	CachedWeapon = Weapon;

	// 激活成功时 GAS 已按 ActivationOwnedTags 挂上 State.Reloading；
	// 这里显式取消 GA_Fire，保证换弹期间不再产生弹丸。
	if (UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
		Cast<UShooterAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get()))
	{
		ShooterAbilitySystemComponent->CancelAbilitiesByTag(
			ShooterGameplayTags::Input_Fire);
	}

	// 服务器事务时钟只来自 WeaponActor 配置；Montage 丢失不影响提交。
	const float ReloadDuration = FMath::Max(0.0f, Weapon->GetReloadDuration());
	UAbilityTask_WaitDelay* WaitTask = UAbilityTask_WaitDelay::WaitDelay(
		this,
		ReloadDuration);
	WaitTask->OnFinish.AddDynamic(
		this,
		&UShooterGameplayAbility_Reload::HandleReloadWaitFinished);
	ReloadWaitTask = WaitTask;
	WaitTask->ReadyForActivation();

	const AShooterCharacter* ReloadCharacter = Cast<AShooterCharacter>(
		GetShooterAvatarActor());
	const int32 ReserveAmmo = ReloadCharacter && ReloadCharacter->GetInventoryComponent()
		? ReloadCharacter->GetInventoryComponent()->GetReserveAmmo(TargetInstanceId)
		: INDEX_NONE;
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("GA_Reload activated: Avatar=%s Weapon=%s InstanceId=%s Duration=%.3f Mag=%d Reserve=%d"),
		*GetNameSafe(ReloadCharacter),
		*GetNameSafe(Weapon),
		*TargetInstanceId.ToString(),
		ReloadDuration,
		Weapon->GetBulletCount(),
		ReserveAmmo);
}

bool UShooterGameplayAbility_Reload::IsReloadTargetStillCurrent() const
{
	const AShooterCharacter* Character = Cast<AShooterCharacter>(
		GetShooterAvatarActor());
	UShooterInventoryComponent* Inventory = Character
		? Character->GetInventoryComponent()
		: nullptr;
	UShooterEquipmentComponent* Equipment = Character
		? Character->GetEquipmentComponent()
		: nullptr;
	const UAbilitySystemComponent* AbilitySystemComponent =
		Character ? Character->GetAbilitySystemComponent() : nullptr;
	if (!Character || !Inventory || !Equipment || !AbilitySystemComponent ||
		AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Dead) ||
		!CachedWeapon.IsValid() ||
		Equipment->GetCurrentWeaponActor() != CachedWeapon.Get() ||
		CachedWeapon->GetOwner() != Character ||
		CachedWeapon->IsHidden() ||
		CachedWeapon->GetBoundInstanceId() != TargetInstanceId ||
		Equipment->GetActiveWeaponInstanceId() != TargetInstanceId ||
		!Inventory->FindWeaponInstance(TargetInstanceId))
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

	if (!IsReloadTargetStillCurrent())
	{
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("GA_Reload commit aborted: target no longer current Avatar=%s InstanceId=%s"),
			*GetNameSafe(GetShooterAvatarActor()),
			*TargetInstanceId.ToString());
		EndAbility(
			GetCurrentAbilitySpecHandle(),
			GetCurrentActorInfo(),
			GetCurrentActivationInfo(),
			true,
			true);
		return;
	}

	AShooterCharacter* Character = Cast<AShooterCharacter>(
		GetShooterAvatarActor());
	UShooterInventoryComponent* Inventory = Character
		? Character->GetInventoryComponent()
		: nullptr;
	int32 TransferredAmmo = 0;
	if (!Inventory ||
		!Inventory->ReloadMagazine(TargetInstanceId, TransferredAmmo))
	{
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("GA_Reload commit failed: Avatar=%s InstanceId=%s"),
			*GetNameSafe(GetShooterAvatarActor()),
			*TargetInstanceId.ToString());
		EndAbility(
			GetCurrentAbilitySpecHandle(),
			GetCurrentActorInfo(),
			GetCurrentActivationInfo(),
			true,
			true);
		return;
	}

	// 单事务护栏：即使 WaitDelay 异常重入，也只允许这次 Inventory 写入。
	bReloadCommitted = true;
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("GA_Reload committed: Avatar=%s InstanceId=%s Transferred=%d"),
		*GetNameSafe(GetShooterAvatarActor()),
		*TargetInstanceId.ToString(),
		TransferredAmmo);

	EndAbility(
		GetCurrentAbilitySpecHandle(),
		GetCurrentActorInfo(),
		GetCurrentActivationInfo(),
		true,
		false);
}

void UShooterGameplayAbility_Reload::CleanupReloadTransaction()
{
	if (ReloadWaitTask.IsValid())
	{
		ReloadWaitTask->EndTask();
	}
	ReloadWaitTask.Reset();

	CachedWeapon.Reset();
	TargetInstanceId = FGuid();
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

	Super::EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		bReplicateEndAbility,
		bWasCancelled);

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("GA_Reload ended: Cancelled=%s Avatar=%s"),
		bWasCancelled ? TEXT("true") : TEXT("false"),
		*GetNameSafe(GetShooterAvatarActor()));
}
