// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/Equipment/ShooterEquipmentComponent.h"

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
		Inventory->OnWeaponInstanceRemovedFromInventory.AddUObject(this, &UShooterEquipmentComponent::NotifyWeaponInstanceRemoved);
		Inventory->OnInventoryCleared.AddUObject(this, &UShooterEquipmentComponent::NotifyInventoryCleared);
	}

	// 复制属性可能先于 BeginPlay 到达；补做一次幂等应用，避免漏掉初始装备。
	ApplyCurrentWeapon(nullptr);
}

void UShooterEquipmentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UShooterInventoryComponent* Inventory = GetOwnerInventory())
	{
		Inventory->OnWeaponInstanceRemovedFromInventory.RemoveAll(this);
		Inventory->OnInventoryCleared.RemoveAll(this);
	}

	Super::EndPlay(EndPlayReason);
}

bool UShooterEquipmentComponent::EquipWeapon(const FGuid& InstanceId)
{
	AShooterCharacter* Character = GetOwnerCharacter();
	UShooterInventoryComponent* Inventory = GetOwnerInventory();
	if (!Character || !Character->HasAuthority() || !Inventory || !InstanceId.IsValid())
	{
		return false;
	}

	// 事务目标必须真实存在于 Inventory，并且是绑定到本角色的 WeaponActor。
	AShooterWeapon* TargetWeapon = Inventory->FindWeaponActor(InstanceId);
	if (!IsValid(TargetWeapon) || TargetWeapon->GetOwner() != Character || TargetWeapon->IsActorBeingDestroyed())
	{
		return false;
	}

	AShooterWeapon* PreviousWeapon = CurrentWeaponActor;
	const bool bChangedCurrentWeapon = PreviousWeapon != TargetWeapon;
	if (bChangedCurrentWeapon && IsValid(PreviousWeapon))
	{
		PreviousWeapon->DeactivateWeapon();
	}

	// 身份与实体必须作为同一事务原子提交。
	ActiveWeaponInstanceId = InstanceId;
	CurrentWeaponActor = TargetWeapon;

	// E2：逻辑变化只在真实转移时发布；相同武器重复提交不再重复广播。
	BroadcastEquippedWeaponChanged(PreviousWeapon, TargetWeapon);

	// E3 前仍走迁移期表现应用；本函数不再附带任何逻辑事件。
	ApplyCurrentWeapon(nullptr);
	ResetAimPresentationForEquipChange();
	Character->ForceNetUpdate();

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Equipment EquipWeapon committed: Actor=%s InstanceId=%s Weapon=%s"),
		*GetNameSafe(Character),
		*InstanceId.ToString(),
		*GetNameSafe(TargetWeapon));
	return true;
}

void UShooterEquipmentComponent::ClearEquippedWeapon()
{
	AShooterWeapon* PreviousWeapon = CurrentWeaponActor;
	if (IsValid(PreviousWeapon))
	{
		PreviousWeapon->DeactivateWeapon();
	}

	// 身份与实体必须作为同一事务原子清空。
	CurrentWeaponActor = nullptr;
	ActiveWeaponInstanceId = FGuid();

	// E2：Unequip 也是真实逻辑转移；重复 Clear 不重复发布。
	BroadcastEquippedWeaponChanged(PreviousWeapon, nullptr);

	if (AShooterCharacter* Character = GetOwnerCharacter())
	{
		// 清空是死亡 / Inventory Clear 生命周期，Aim 表现必须显式失效，不复用旧值。
		if (UShooterAimPresentationComponent* AimPresentation = Character->GetAimPresentationComponent())
		{
			AimPresentation->ClearPresentationAimSmoothing();
		}

		Character->ForceNetUpdate();
	}

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Equipment ClearEquippedWeapon: Actor=%s"),
		*GetNameSafe(GetOwner()));
}

void UShooterEquipmentComponent::HandleWeaponActorReady(AShooterWeapon* Weapon)
{
	if (IsValid(Weapon) && Weapon == CurrentWeaponActor)
	{
		// WeaponActor 的 Owner / BoundInstanceId 晚到时补做幂等表现应用。
		ApplyCurrentWeapon(nullptr);
	}
}

void UShooterEquipmentComponent::NotifyWeaponInstanceRemoved(const FGuid& InstanceId)
{
	if (ActiveWeaponInstanceId == InstanceId)
	{
		ClearEquippedWeapon();
	}
}

void UShooterEquipmentComponent::NotifyInventoryCleared()
{
	ClearEquippedWeapon();
}

void UShooterEquipmentComponent::ApplyCurrentWeapon(AShooterWeapon* PreviousWeapon)
{
	if (IsValid(PreviousWeapon) && PreviousWeapon != CurrentWeaponActor)
	{
		PreviousWeapon->DeactivateWeapon();
	}

	AShooterCharacter* Character = GetOwnerCharacter();
	if (!Character || !IsValid(CurrentWeaponActor))
	{
		return;
	}

	// 幂等：WeaponActor 的所有权信息尚未到达时暂不附着，待 HandleWeaponActorReady 补做。
	if (CurrentWeaponActor->GetOwner() != Character)
	{
		return;
	}

	Character->AttachWeaponMeshes(CurrentWeaponActor);
	CurrentWeaponActor->ActivateWeapon();
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

void UShooterEquipmentComponent::BroadcastEquippedWeaponChanged(
	AShooterWeapon* PreviousWeapon,
	AShooterWeapon* CurrentWeapon)
{
	// E2 语义：只有 CurrentWeaponActor 真实转移才发布逻辑装备变化；
	// Ready 补偿、BeginPlay 回放与 ActiveInstanceId OnRep 都不得进入这里。
	if (PreviousWeapon == CurrentWeapon)
	{
		return;
	}

	OnEquippedWeaponChanged.Broadcast(PreviousWeapon, CurrentWeapon);
}

void UShooterEquipmentComponent::OnRep_CurrentWeaponActor(AShooterWeapon* PreviousWeapon)
{
	// E2：逻辑事件只由 CurrentWeaponActor 真实转移产生；表现应用随后幂等补做。
	BroadcastEquippedWeaponChanged(PreviousWeapon, CurrentWeaponActor);
	ApplyCurrentWeapon(PreviousWeapon);

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

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Equipment CurrentWeaponActor replicated: Actor=%s Previous=%s Current=%s"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(PreviousWeapon),
		*GetNameSafe(CurrentWeaponActor));
}

void UShooterEquipmentComponent::OnRep_ActiveWeaponInstanceId()
{
	// E2：Owner 身份可以单独消费该值，但它不再触发表现应用或逻辑事件。
	// 表现只由 CurrentWeaponActor OnRep / BeginPlay / HandleWeaponActorReady 幂等补做。
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Equipment ActiveWeaponInstanceId replicated: Actor=%s InstanceId=%s"),
		*GetNameSafe(GetOwner()),
		*ActiveWeaponInstanceId.ToString());
}

void UShooterEquipmentComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 观察者只需要 CurrentWeaponActor；完整装备身份只发给拥有者。
	DOREPLIFETIME(UShooterEquipmentComponent, CurrentWeaponActor);
	DOREPLIFETIME_CONDITION(UShooterEquipmentComponent, ActiveWeaponInstanceId, COND_OwnerOnly);
}
