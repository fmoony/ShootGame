// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterGameplayAbility_Equip.h"

#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "AbilitySystem/ShooterGameplayTags.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "Weapons/ShooterWeapon.h"
#include "ShootGame.h"

bool UShooterGameplayAbility_Equip::HasInputEquipTag() const
{
	return GetAssetTags().HasTagExact(ShooterGameplayTags::Input_Equip);
}

bool UShooterGameplayAbility_Equip::IsBlockedByStateDead() const
{
	return ActivationBlockedTags.HasTag(ShooterGameplayTags::State_Dead);
}

bool UShooterGameplayAbility_Equip::IsBlockedByStateEquipping() const
{
	return ActivationBlockedTags.HasTag(ShooterGameplayTags::State_Equipping);
}

bool UShooterGameplayAbility_Equip::OwnsStateEquippingWhileActive() const
{
	return ActivationOwnedTags.HasTag(ShooterGameplayTags::State_Equipping);
}

bool UShooterGameplayAbility_Equip::CanRetriggerInstancedAbility() const
{
	return bRetriggerInstancedAbility;
}

UShooterGameplayAbility_Equip::UShooterGameplayAbility_Equip()
{
	// 同一 Avatar 同生命周期内只保留一个实例；网络执行只发生在服务器。
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	// Input.Equip 只负责统一分类和取消；Next / Previous 由两个 Ability Spec 的动态标签区分。
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(ShooterGameplayTags::Input_Equip);
	SetAssetTags(AssetTags);
	// 死亡与装备中状态阻塞激活；State.Equipping 在激活期间由 GAS 自动挂到拥有者 ASC。
	// 切枪期间旧武器必须停火、换弹事务必须让位：关系只通过 GAS Tag 表达，
	// 由引擎在 PreActivate 里按 AssetTags 执行取消。
	ActivationBlockedTags.AddTag(ShooterGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(ShooterGameplayTags::State_Equipping);
	ActivationOwnedTags.AddTag(ShooterGameplayTags::State_Equipping);
	CancelAbilitiesWithTag.AddTag(ShooterGameplayTags::Input_Fire);
	CancelAbilitiesWithTag.AddTag(ShooterGameplayTags::Input_Reload);
}

bool UShooterGameplayAbility_Equip::CanActivateAbility(
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

	// ServerOnly 客户端预检必须先于 Super，只确认请求来自当前 Avatar 的本地 ASC；
	// 不读取可能过期的 ActivationBlockedTags。权威端执行完整校验。
	if (!AvatarActor->HasAuthority())
	{
		const UAbilitySystemComponent* AbilitySystemComponent = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
		return AbilitySystemComponent && AbilitySystemComponent->GetAvatarActor() == AvatarActor;
	}

	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	AShooterWeapon* Weapon = nullptr;
	return ResolveEquipTarget(Handle, ActorInfo, Weapon);
}

int32 UShooterGameplayAbility_Equip::ResolveEquipDirection(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo) const
{
	const UAbilitySystemComponent* AbilitySystemComponent = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const FGameplayAbilitySpec* AbilitySpec = AbilitySystemComponent ? AbilitySystemComponent->FindAbilitySpecFromHandle(Handle) : nullptr;
	if (!AbilitySpec)
	{
		return 0;
	}

	const FGameplayTagContainer& InputTags = AbilitySpec->GetDynamicSpecSourceTags();
	if (InputTags.HasTagExact(ShooterGameplayTags::Input_Equip_Next))
	{
		return 1;
	}
	if (InputTags.HasTagExact(ShooterGameplayTags::Input_Equip_Previous))
	{
		return -1;
	}

	return 0;
}

bool UShooterGameplayAbility_Equip::ResolveEquipTarget(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, AShooterWeapon*& OutWeapon) const
{
	OutWeapon = nullptr;

	const AShooterCharacter* Character = Cast<AShooterCharacter>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
	const UAbilitySystemComponent* AbilitySystemComponent =	ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!Character || !AbilitySystemComponent || AbilitySystemComponent->GetAvatarActor() != Character ||
		AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Dead))
	{
		return false;
	}

	UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
	UShooterEquipmentComponent* Equipment = Character->GetEquipmentComponent();
	if (!Inventory || !Equipment || Inventory->GetWeaponCount() < 2)
	{
		return false;
	}

	const int32 EquipDirection = ResolveEquipDirection(Handle, ActorInfo);
	// 按输入方向计算相邻合法 Actor；缺少方向、单武器或当前装备无效时明确拒绝。
	AShooterWeapon* TargetWeapon = Inventory->FindAdjacentWeapon(Equipment->GetCurrentWeaponActor(), EquipDirection);
	if (!IsValid(TargetWeapon) || TargetWeapon->GetOwner() != Character || TargetWeapon->IsActorBeingDestroyed() ||
		TargetWeapon == Equipment->GetCurrentWeaponActor())
	{
		return false;
	}

	OutWeapon = TargetWeapon;
	return true;
}

void UShooterGameplayAbility_Equip::ActivateAbility(
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

	AShooterWeapon* TargetWeapon = nullptr;
	if (!ResolveEquipTarget(Handle, ActorInfo, TargetWeapon))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AShooterCharacter* Character = Cast<AShooterCharacter>(GetShooterAvatarActor());
	UShooterEquipmentComponent* Equipment = Character
		? Character->GetEquipmentComponent()
		: nullptr;
	CachedPreviousWeapon = Equipment
		? Equipment->GetCurrentWeaponActor()
		: nullptr;
	CachedTargetWeapon = TargetWeapon;

	// 服务器事务时钟只来自目标 WeaponActor 配置；表现 Montage 不影响提交。
	const float EquipDuration = FMath::Max(0.0f, TargetWeapon->GetEquipDuration());
	UAbilityTask_WaitDelay* WaitTask = UAbilityTask_WaitDelay::WaitDelay(this, EquipDuration);
	WaitTask->OnFinish.AddDynamic(this, &UShooterGameplayAbility_Equip::HandleEquipWaitFinished);
	EquipWaitTask = WaitTask;
	WaitTask->ReadyForActivation();

	UE_LOG(LogShootGame, Display, TEXT("GA_Equip activated: Avatar=%s Target=%s TargetWeaponId=%s Duration=%.3f"),
		*GetNameSafe(Character), *GetNameSafe(TargetWeapon), *TargetWeapon->GetWeaponId().ToString(), EquipDuration);
}

bool UShooterGameplayAbility_Equip::IsEquipTargetStillValid() const
{
	const AShooterCharacter* Character = Cast<AShooterCharacter>(GetShooterAvatarActor());
	UShooterInventoryComponent* Inventory = Character ? Character->GetInventoryComponent() : nullptr;
	UShooterEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	const UAbilitySystemComponent* AbilitySystemComponent = Character ? Character->GetAbilitySystemComponent() : nullptr;
	if (!Character || !Inventory || !Equipment || !AbilitySystemComponent ||
		AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Dead) || !CachedTargetWeapon.IsValid() ||
		CachedTargetWeapon->IsActorBeingDestroyed() || CachedTargetWeapon->GetOwner() != Character ||
		!Inventory->ContainsWeapon(CachedTargetWeapon.Get()))
	{
		return false;
	}

	// 等待期间 Equipment.CurrentWeaponActor 被其他路径改写则放弃提交，避免抢占第三方事务结果。
	if (CachedPreviousWeapon.IsValid())
	{
		if (Equipment->GetCurrentWeaponActor() != CachedPreviousWeapon.Get())
		{
			return false;
		}
	}
	else if (Equipment->GetCurrentWeaponActor() != nullptr)
	{
		return false;
	}

	return true;
}

void UShooterGameplayAbility_Equip::HandleEquipWaitFinished()
{
	if (bEquipCommitted || !EquipWaitTask.IsValid())
	{
		return;
	}

	EquipWaitTask.Reset();

	if (!IsEquipTargetStillValid())
	{
		UE_LOG(LogShootGame, Display, TEXT("GA_Equip commit aborted: target no longer valid Avatar=%s WeaponId=%s"),
			*GetNameSafe(GetShooterAvatarActor()), *CachedTargetWeapon->GetWeaponId().ToString());
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}

	AShooterCharacter* Character = Cast<AShooterCharacter>(GetShooterAvatarActor());
	UShooterEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	if (!Equipment || !Equipment->EquipWeapon(CachedTargetWeapon.Get()))
	{
		UE_LOG(LogShootGame, Warning, TEXT("GA_Equip commit failed: Avatar=%s WeaponId=%s"), *GetNameSafe(Character),
			*CachedTargetWeapon->GetWeaponId().ToString());
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}

	bEquipCommitted = true;
	UE_LOG(LogShootGame, Display, TEXT("GA_Equip committed: Avatar=%s WeaponId=%s Weapon=%s"), *GetNameSafe(Character),
		*CachedTargetWeapon->GetWeaponId().ToString(), *GetNameSafe(Character->GetCurrentWeapon()));

	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

void UShooterGameplayAbility_Equip::CleanupEquipTransaction()
{
	if (EquipWaitTask.IsValid())
	{
		EquipWaitTask->EndTask();
	}
	EquipWaitTask.Reset();

	CachedPreviousWeapon.Reset();
	CachedTargetWeapon.Reset();
	bEquipCommitted = false;
}

void UShooterGameplayAbility_Equip::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	CleanupEquipTransaction();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

	UE_LOG(LogShootGame, Display, TEXT("GA_Equip ended: Cancelled=%s Avatar=%s"),
		bWasCancelled ? TEXT("true") : TEXT("false"), *GetNameSafe(GetShooterAvatarActor()));
}
