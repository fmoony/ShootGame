// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterGameplayAbility_Equip.h"

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

bool UShooterGameplayAbility_Equip::HasInputEquipNextTag() const
{
	return GetAssetTags().HasTag(ShooterGameplayTags::Input_Equip_Next);
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

	// Input.Equip.Next 是 ASC 输入查找与 Ability Spec 之间的稳定映射。
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(ShooterGameplayTags::Input_Equip_Next);
	SetAssetTags(AssetTags);
	// 死亡与装备中状态阻塞激活；State.Equipping 在激活期间由 GAS 自动挂到拥有者 ASC。
	// 激活时显式取消 Fire / Reload，因此这里不把 State.Firing / State.Reloading 设为阻塞。
	ActivationBlockedTags.AddTag(ShooterGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(ShooterGameplayTags::State_Equipping);
	ActivationOwnedTags.AddTag(ShooterGameplayTags::State_Equipping);
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
	return ResolveEquipTarget(ActorInfo, Weapon, InstanceId);
}

bool UShooterGameplayAbility_Equip::ResolveEquipTarget(
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
	if (!Inventory || !Equipment || Inventory->GetWeaponCount() < 2)
	{
		return false;
	}

	// 按 Slot 顺序计算下一个合法实例；单武器或当前 Active 无效时明确拒绝。
	FGuid NextInstanceId;
	if (!Inventory->FindNextWeaponInstanceId(
		Equipment->GetActiveWeaponInstanceId(),
		NextInstanceId))
	{
		return false;
	}

	AShooterWeapon* TargetWeapon = Inventory->FindWeaponActor(NextInstanceId);
	if (!IsValid(TargetWeapon) ||
		TargetWeapon->GetOwner() != Character ||
		TargetWeapon->IsActorBeingDestroyed() ||
		TargetWeapon == Equipment->GetCurrentWeaponActor())
	{
		return false;
	}

	OutWeapon = TargetWeapon;
	OutInstanceId = NextInstanceId;
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
	if (!ResolveEquipTarget(ActorInfo, TargetWeapon, TargetInstanceId))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AShooterCharacter* Character = Cast<AShooterCharacter>(
		GetShooterAvatarActor());
	UShooterEquipmentComponent* Equipment = Character
		? Character->GetEquipmentComponent()
		: nullptr;
	PreviousInstanceId = Equipment
		? Equipment->GetActiveWeaponInstanceId()
		: FGuid();
	CachedPreviousWeapon = Equipment
		? Equipment->GetCurrentWeaponActor()
		: nullptr;
	CachedTargetWeapon = TargetWeapon;

	// 激活成功时 GAS 已挂上 State.Equipping；显式取消 Fire / Reload，保证切换期间旧武器停火。
	if (UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
		Cast<UShooterAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get()))
	{
		ShooterAbilitySystemComponent->CancelAbilitiesByTag(
			ShooterGameplayTags::Input_Fire);
		ShooterAbilitySystemComponent->CancelAbilitiesByTag(
			ShooterGameplayTags::Input_Reload);
	}

	// 服务器事务时钟只来自目标 WeaponActor 配置；表现 Montage 不影响提交。
	const float EquipDuration = FMath::Max(0.0f, TargetWeapon->GetEquipDuration());
	UAbilityTask_WaitDelay* WaitTask = UAbilityTask_WaitDelay::WaitDelay(
		this,
		EquipDuration);
	WaitTask->OnFinish.AddDynamic(
		this,
		&UShooterGameplayAbility_Equip::HandleEquipWaitFinished);
	EquipWaitTask = WaitTask;
	WaitTask->ReadyForActivation();

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("GA_Equip activated: Avatar=%s Target=%s TargetInstanceId=%s PreviousInstanceId=%s Duration=%.3f"),
		*GetNameSafe(Character),
		*GetNameSafe(TargetWeapon),
		*TargetInstanceId.ToString(),
		*PreviousInstanceId.ToString(),
		EquipDuration);
}

bool UShooterGameplayAbility_Equip::IsEquipTargetStillValid() const
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
		!CachedTargetWeapon.IsValid() ||
		CachedTargetWeapon->IsActorBeingDestroyed() ||
		CachedTargetWeapon->GetOwner() != Character ||
		!Inventory->FindWeaponInstance(TargetInstanceId) ||
		Equipment->GetActiveWeaponInstanceId() != PreviousInstanceId)
	{
		return false;
	}

	// 等待期间 Equipment.CurrentWeaponActor 被其他路径改写则放弃提交，避免逻辑 ID 与 Actor 分离。
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
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("GA_Equip commit aborted: target no longer valid Avatar=%s InstanceId=%s"),
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
	UShooterEquipmentComponent* Equipment = Character
		? Character->GetEquipmentComponent()
		: nullptr;
	if (!Equipment || !Equipment->EquipWeapon(TargetInstanceId))
	{
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("GA_Equip commit failed: Avatar=%s InstanceId=%s"),
			*GetNameSafe(Character),
			*TargetInstanceId.ToString());
		EndAbility(
			GetCurrentAbilitySpecHandle(),
			GetCurrentActorInfo(),
			GetCurrentActivationInfo(),
			true,
			true);
		return;
	}

	bEquipCommitted = true;
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("GA_Equip committed: Avatar=%s InstanceId=%s Weapon=%s"),
		*GetNameSafe(Character),
		*TargetInstanceId.ToString(),
		*GetNameSafe(Character->GetCurrentWeapon()));

	EndAbility(
		GetCurrentAbilitySpecHandle(),
		GetCurrentActorInfo(),
		GetCurrentActivationInfo(),
		true,
		false);
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
	TargetInstanceId = FGuid();
	PreviousInstanceId = FGuid();
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

	Super::EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		bReplicateEndAbility,
		bWasCancelled);

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("GA_Equip ended: Cancelled=%s Avatar=%s"),
		bWasCancelled ? TEXT("true") : TEXT("false"),
		*GetNameSafe(GetShooterAvatarActor()));
}
