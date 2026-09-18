// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterEquipmentComponent.h"

#include "AbilitySystem/ShooterAbilitySystemComponent.h"
#include "AbilitySystem/ShooterGameplayTags.h"
#include "Characters/Aim/ShooterAimPresentationComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "ShootGame.h"
#include "Weapons/ShooterWeapon.h"

UShooterEquipmentComponent::UShooterEquipmentComponent()
{
	SetIsReplicatedByDefault(true);
}

void UShooterEquipmentComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UShooterInventoryComponent* Inventory = GetOwnerInventory())
	{
		Inventory->OnWeaponRemovedFromInventory.AddUObject(this, &UShooterEquipmentComponent::NotifyWeaponRemoved);
		Inventory->OnInventoryCleared.AddUObject(this, &UShooterEquipmentComponent::NotifyInventoryCleared);
	}

	// 复制属性可能先于 BeginPlay 到达；补做一次幂等表现回放，不发布逻辑事件。
	if (AShooterCharacter* Character = GetOwnerCharacter())
	{
		Character->EnsureWeaponPresentation(CurrentWeaponActor);
	}
}

void UShooterEquipmentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UShooterInventoryComponent* Inventory = GetOwnerInventory())
	{
		Inventory->OnWeaponRemovedFromInventory.RemoveAll(this);
		Inventory->OnInventoryCleared.RemoveAll(this);
	}

	Super::EndPlay(EndPlayReason);
}

bool UShooterEquipmentComponent::EquipWeapon(AShooterWeapon* TargetWeapon)
{
	AShooterCharacter* Character = GetOwnerCharacter();
	UShooterInventoryComponent* Inventory = GetOwnerInventory();
	if (!Character || !Character->HasAuthority() || !Inventory || !IsValid(TargetWeapon))
	{
		return false;
	}

	// 事务目标必须真实存在于 Inventory、绑定到本角色且不在池内。
	if (!Inventory->ContainsWeapon(TargetWeapon) || TargetWeapon->GetOwner() != Character ||
		TargetWeapon->IsActorBeingDestroyed() ||
		TargetWeapon->GetLifecycleState() == EShooterWeaponLifecycleState::InPool)
	{
		return false;
	}

	AShooterWeapon* PreviousWeapon = CurrentWeaponActor;
	const bool bChangedCurrentWeapon = PreviousWeapon != TargetWeapon;
	if (bChangedCurrentWeapon && IsValid(PreviousWeapon))
	{
		// Equipment 只负责在逻辑提交前停止旧武器；隐藏与表现回调由 Character 表现层统一执行一次。
		PreviousWeapon->StopFiring();
	}

	// 生命周期状态机：装备事务 Holstered -> Equipping；表现完成（ActivateWeapon）后进入 Equipped。
	// 同武器重复提交保持幂等，不重复进入事务。
	if (bChangedCurrentWeapon)
	{
		TargetWeapon->BeginEquipTransaction();
	}

	CurrentWeaponActor = TargetWeapon;

	// E2：逻辑变化只在真实转移时发布；相同武器重复提交不再重复广播。
	BroadcastEquippedWeaponChanged(PreviousWeapon, TargetWeapon);

	// 逻辑事件发布后进入同一幂等表现收敛入口；重复提交不重复 Attach / Activate / HUD。
	Character->EnsureWeaponPresentation(TargetWeapon);
	ResetAimPresentationForEquipChange();
	Character->ForceNetUpdate();

	UE_LOG(LogShootGame, Display, TEXT("Equipment EquipWeapon committed: Actor=%s WeaponId=%s Weapon=%s"),
		*GetNameSafe(Character), *TargetWeapon->GetWeaponId().ToString(), *GetNameSafe(TargetWeapon));
	return true;
}

void UShooterEquipmentComponent::ClearEquippedWeapon()
{
	AShooterWeapon* PreviousWeapon = CurrentWeaponActor;
	if (IsValid(PreviousWeapon))
	{
		// 保证逻辑清空时旧武器立即停火；本机隐藏与表现回调由 EnsureWeaponPresentation 收敛。
		PreviousWeapon->StopFiring();
	}

	CurrentWeaponActor = nullptr;

	// E2：Unequip 也是真实逻辑转移；重复 Clear 不重复发布。
	BroadcastEquippedWeaponChanged(PreviousWeapon, nullptr);

	if (AShooterCharacter* Character = GetOwnerCharacter())
	{
		// 清空本地表现缓存并发布 (Previous, nullptr)，不把 AnimClass 设为 nullptr。
		Character->EnsureWeaponPresentation(nullptr);

		// 清空是死亡 / Inventory Clear 生命周期，Aim 表现必须显式失效，不复用旧值。
		if (UShooterAimPresentationComponent* AimPresentation = Character->GetAimPresentationComponent())
		{
			AimPresentation->ClearPresentationAimSmoothing();
		}

		Character->ForceNetUpdate();
	}

	UE_LOG(LogShootGame, Display, TEXT("Equipment ClearEquippedWeapon: Actor=%s"), *GetNameSafe(GetOwner()));
}

void UShooterEquipmentComponent::HandleWeaponActorReady(AShooterWeapon* Weapon)
{
	if (IsValid(Weapon) && Weapon == CurrentWeaponActor)
	{
		// WeaponActor 的 Owner / WeaponId / 行配置晚到时补做幂等收敛，不发布逻辑事件。
		// 连发语义来自行配置，因此必须在这里重新同步输入行为标签：
		// 客户端先收到 CurrentWeaponActor、随后才收到 WeaponId 并应用配置是常见路径。
		SyncFireHeldRepeatInputBehavior(Weapon);

		if (AShooterCharacter* Character = GetOwnerCharacter())
		{
			Character->EnsureWeaponPresentation(Weapon);
		}
	}
}

void UShooterEquipmentComponent::NotifyWeaponRemoved(AShooterWeapon* Weapon)
{
	if (Weapon == CurrentWeaponActor)
	{
		ClearEquippedWeapon();
	}
}

void UShooterEquipmentComponent::NotifyInventoryCleared()
{
	ClearEquippedWeapon();
}

void UShooterEquipmentComponent::ResetAimPresentationForEquipChange()
{
	if (AShooterCharacter* Character = GetOwnerCharacter())
	{
		if (UShooterAimPresentationComponent* AimPresentation = Character->GetAimPresentationComponent())
		{
			AimPresentation->ResetPresentationAimSmoothing();
		}
	}
}

AShooterCharacter* UShooterEquipmentComponent::GetOwnerCharacter() const
{
	return Cast<AShooterCharacter>(GetOwner());
}

UShooterInventoryComponent* UShooterEquipmentComponent::GetOwnerInventory() const
{
	const AShooterCharacter* Character = GetOwnerCharacter();
	return Character ? Character->GetInventoryComponent() : nullptr;
}

void UShooterEquipmentComponent::BroadcastEquippedWeaponChanged(AShooterWeapon* PreviousWeapon, AShooterWeapon* CurrentWeapon)
{
	// E2 语义：只有 CurrentWeaponActor 真实转移才发布逻辑装备变化；
	// Ready 补偿与 BeginPlay 回放不得进入这里。
	if (PreviousWeapon == CurrentWeapon)
	{
		return;
	}

	// 当前武器是输入语义的唯一来源：逻辑转移时同步 Fire Spec 的通用输入行为标签。
	// 服务器与拥有者客户端走同一条路径，各自按自己看到的 CurrentWeaponActor 收敛。
	SyncFireHeldRepeatInputBehavior(CurrentWeapon);

	OnEquippedWeaponChanged.Broadcast(PreviousWeapon, CurrentWeapon);
}

void UShooterEquipmentComponent::SyncFireHeldRepeatInputBehavior(AShooterWeapon* Weapon)
{
	// 「当前武器语义 → 输入行为标签」的唯一同步点：装备逻辑转移与武器配置晚到都收敛到这里。
	// ASC 只认标签与 Spec.InputPressed，不判断武器类型；Ability 侧不再需要任何输入语义虚函数。
	// 幂等：语义没有变化时 ASC 不重复标脏，因此这里可以被安全地重复调用。
	const AShooterCharacter* Character = GetOwnerCharacter();
	UShooterAbilitySystemComponent* AbilitySystemComponent = Character
		? Cast<UShooterAbilitySystemComponent>(Character->GetAbilitySystemComponent())
		: nullptr;
	if (!AbilitySystemComponent)
	{
		// 能力尚未授予（例如拥有者客户端还没收到 PlayerState 的 Spec 复制）：
		// Spec 复制到达时会带着服务器已同步好的标签，这里不需要补偿。
		return;
	}

	AbilitySystemComponent->SetHeldRepeatInputBehavior(ShooterGameplayTags::Input_Fire,
		IsValid(Weapon) && Weapon->IsFullAuto());
}

void UShooterEquipmentComponent::OnRep_CurrentWeaponActor(AShooterWeapon* PreviousWeapon)
{
	// E2：逻辑事件只由 CurrentWeaponActor 真实转移产生；表现随后进入同一幂等入口。
	BroadcastEquippedWeaponChanged(PreviousWeapon, CurrentWeaponActor);
	if (AShooterCharacter* Character = GetOwnerCharacter())
	{
		Character->EnsureWeaponPresentation(CurrentWeaponActor);
	}

	// 装备到新武器时重置平滑；Unequip 时显式清空，不复用旧目标。
	if (CurrentWeaponActor != nullptr)
	{
		ResetAimPresentationForEquipChange();
	}
	else if (AShooterCharacter* Character = GetOwnerCharacter())
	{
		if (UShooterAimPresentationComponent* AimPresentation = Character->GetAimPresentationComponent())
		{
			AimPresentation->ClearPresentationAimSmoothing();
		}
	}

	UE_LOG(LogShootGame, Display, TEXT("Equipment CurrentWeaponActor replicated: Actor=%s Previous=%s Current=%s"),
		*GetNameSafe(GetOwner()), *GetNameSafe(PreviousWeapon), *GetNameSafe(CurrentWeaponActor));
}

void UShooterEquipmentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 观察者只需要 CurrentWeaponActor；不再存在只发给 Owner 的实例身份。
	DOREPLIFETIME(UShooterEquipmentComponent, CurrentWeaponActor);
}
