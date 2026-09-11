// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterInventoryComponent.h"

#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Pool/ShooterActorPoolSubsystem.h"
#include "ShootGame.h"
#include "ShooterWeapon.h"
#include "ShooterWeaponConfigRow.h"
#include "ShooterWeaponTable.h"

UShooterInventoryComponent::UShooterInventoryComponent()
{
	// 组件随 Character 一起复制；数组属性本身使用 COND_OwnerOnly。
	SetIsReplicatedByDefault(true);

	// UE5.6 不会因子类重写 InitializeComponent 而自动置位该标志；
	// 不显式开启时 InitializeComponent 永不执行，Owner Client 的
	// FastArray Add/Change/Remove 通知（备弹 HUD 刷新桥）不会被绑定。
	bWantsInitializeComponent = true;
}

void UShooterInventoryComponent::InitializeComponent()
{
	Super::InitializeComponent();

	// Owner Client 的 FastArray Remove 回调驱动本地 WeaponActor 解绑镜像。
	ReplicatedInventory.OnInstanceRemoved.AddUObject(this, &UShooterInventoryComponent::HandleInstanceRemoved);
}

EShooterInventoryAddResult UShooterInventoryComponent::TryAddWeaponRow(
	FName WeaponRowName,
	FGuid& OutInstanceId)
{
	OutInstanceId = FGuid();
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return EShooterInventoryAddResult::NotAuthoritative;
	}

	// 表缺失、行名为空、行缺失或行非法都不消费 Pickup；错误原因在入口统一记录一次。
	const FShooterWeaponConfigRow* Row = ResolveWeaponRow(WeaponRowName);
	if (!ShooterWeaponTable::IsRowValidForGrant(Row))
	{
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("Inventory TryAddWeaponRow rejected invalid row: Actor=%s Row=%s Table=%s"),
			*GetNameSafe(GetOwner()),
			*WeaponRowName.ToString(),
			*GetNameSafe(GetWeaponTable()));
		return EShooterInventoryAddResult::InvalidWeaponRow;
	}

	return TryAddWeaponInternal(WeaponRowName, *Row, OutInstanceId);
}

EShooterInventoryAddResult UShooterInventoryComponent::TryAddWeaponInternal(
	FName WeaponRowName,
	const FShooterWeaponConfigRow& Row,
	FGuid& OutInstanceId)
{
	OutInstanceId = FGuid();

	if (FindWeaponInstanceByRowName(WeaponRowName))
	{
		return EShooterInventoryAddResult::DuplicateWeaponRow;
	}

	const int32 FreeSlot = FindFreeSlotIndex();
	if (FreeSlot == INDEX_NONE)
	{
		return EShooterInventoryAddResult::SlotFull;
	}

	FShooterWeaponInstanceData InstanceData;
	InstanceData.InstanceId = FGuid::NewGuid();
	InstanceData.WeaponRowName = WeaponRowName;
	InstanceData.MagazineAmmo = Row.MagazineSize;
	InstanceData.ReserveAmmo = Row.ResolveInitialReserveAmmo();
	InstanceData.SlotIndex = FreeSlot;

	if (!AddWeaponInstance(InstanceData))
	{
		return EShooterInventoryAddResult::SlotOccupied;
	}

	// WeaponActor 统一由 World 级对象池提供：池命中即复用，未命中才生成。
	UShooterActorPoolSubsystem* Pool = GetWorld()
		? GetWorld()->GetSubsystem<UShooterActorPoolSubsystem>()
		: nullptr;
	if (!Pool)
	{
		// 没有池就不存在正式的 WeaponActor 生命周期边界：fail closed 并回滚实例。
		ReplicatedInventory.RemoveItem(InstanceData.InstanceId);
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("Inventory TryAddWeaponRow rejected: actor pool unavailable. Actor=%s Row=%s"),
			*GetNameSafe(GetOwner()),
			*WeaponRowName.ToString());
		return EShooterInventoryAddResult::AcquireFailed;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = GetOwner();
	SpawnParameters.Instigator = Cast<APawn>(GetOwner());
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot;

	// Acquire 已完成通用复位（Owner / Instigator / 变换 / 可见性 / 碰撞 / Tick）；
	// 行配置与 Instance 身份在下面的绑定入口一起写入。
	AShooterWeapon* Weapon = Cast<AShooterWeapon>(Pool->Acquire(
		Row.WeaponActorClass,
		GetOwner()->GetActorTransform(),
		SpawnParameters));
	if (!Weapon)
	{
		// Acquire 失败必须回滚刚写入的 WeaponInstance，不能留下只有逻辑数据的幽灵武器。
		ReplicatedInventory.RemoveItem(InstanceData.InstanceId);
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("Inventory TryAddWeaponRow rolled back after pool acquire failure: Actor=%s Row=%s Class=%s"),
			*GetNameSafe(GetOwner()),
			*WeaponRowName.ToString(),
			*GetNameSafe(Row.WeaponActorClass));
		return EShooterInventoryAddResult::AcquireFailed;
	}

	// 绑定同时把该行的只读配置应用到 WeaponActor；此后 WeaponActor 不再读取默认配置。
	Weapon->SetInstanceBinding(InstanceData.InstanceId, WeaponRowName);
	Weapon->SetActorHiddenInGame(true);
	RegisterWeaponActor(Weapon);

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Inventory TryAddWeaponRow acquired WeaponActor: Actor=%s InstanceId=%s Row=%s Weapon=%s Slot=%d"),
		*GetNameSafe(GetOwner()),
		*InstanceData.InstanceId.ToString(),
		*WeaponRowName.ToString(),
		*GetNameSafe(Weapon),
		InstanceData.SlotIndex);

	OutInstanceId = InstanceData.InstanceId;
	return EShooterInventoryAddResult::Added;
}

bool UShooterInventoryComponent::AddWeaponInstance(const FShooterWeaponInstanceData& InstanceData)
{
	if (!InstanceData.IsValid())
	{
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("Inventory AddWeaponInstance rejected invalid data: InstanceId=%s Row=%s Slot=%d Mag=%d Reserve=%d"),
			*InstanceData.InstanceId.ToString(),
			*InstanceData.WeaponRowName.ToString(),
			InstanceData.SlotIndex,
			InstanceData.MagazineAmmo,
			InstanceData.ReserveAmmo);
		return false;
	}

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	const bool bAdded = ReplicatedInventory.AddItem(InstanceData);
	if (bAdded)
	{
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("Inventory AddWeaponInstance succeeded: Actor=%s InstanceId=%s Slot=%d Count=%d"),
			*GetNameSafe(GetOwner()),
			*InstanceData.InstanceId.ToString(),
			InstanceData.SlotIndex,
			GetWeaponCount());
	}
	return bAdded;
}

bool UShooterInventoryComponent::RemoveWeaponInstance(const FGuid& InstanceId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !InstanceId.IsValid())
	{
		return false;
	}

	const bool bRemoved = ReplicatedInventory.RemoveItem(InstanceId);
	if (bRemoved)
	{
		// E1 顺序：先广播移除，让 Equipment 在 WeaponActor 归还池之前清理当前装备。
		OnWeaponInstanceRemovedFromInventory.Broadcast(InstanceId);

		if (AShooterWeapon* Weapon = FindWeaponActor(InstanceId))
		{
			ReleaseWeaponActor(Weapon);
		}

		UE_LOG(
			LogShootGame,
			Display,
			TEXT("Inventory RemoveWeaponInstance succeeded: Actor=%s InstanceId=%s Count=%d"),
			*GetNameSafe(GetOwner()),
			*InstanceId.ToString(),
			GetWeaponCount());
	}
	return bRemoved;
}

void UShooterInventoryComponent::ClearInventory()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// E1 顺序：冻结待归还 WeaponActor，先清逻辑 Entries 并广播，
	// 让 Equipment 在归还池之前清理当前装备，最后统一归还。
	TArray<AShooterWeapon*> WeaponsToRelease;
	for (AShooterWeapon* Weapon : BoundWeaponActors)
	{
		if (IsValid(Weapon))
		{
			WeaponsToRelease.Add(Weapon);
		}
	}

	if (ReplicatedInventory.Items.Num() == 0 && WeaponsToRelease.Num() == 0)
	{
		// 重复 Clear / 空 Inventory 幂等返回，不重复广播错误状态。
		return;
	}

	ReplicatedInventory.ClearItems();
	OnInventoryCleared.Broadcast();

	for (AShooterWeapon* Weapon : WeaponsToRelease)
	{
		if (IsValid(Weapon))
		{
			ReleaseWeaponActor(Weapon);
		}
	}

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Inventory ClearInventory: Actor=%s Count=%d"),
		*GetNameSafe(GetOwner()),
		GetWeaponCount());
}

const FShooterWeaponInstanceData* UShooterInventoryComponent::FindWeaponInstance(
	const FGuid& InstanceId) const
{
	if (!InstanceId.IsValid())
	{
		return nullptr;
	}

	const FShooterWeaponInstanceEntry* Entry = ReplicatedInventory.FindItem(InstanceId);
	return Entry ? &Entry->InstanceData : nullptr;
}

const FShooterWeaponInstanceData* UShooterInventoryComponent::FindWeaponInstanceByRowName(
	FName WeaponRowName) const
{
	if (WeaponRowName.IsNone())
	{
		return nullptr;
	}

	const FShooterWeaponInstanceEntry* Entry = ReplicatedInventory.FindItemByRowName(WeaponRowName);
	return Entry ? &Entry->InstanceData : nullptr;
}

const FShooterWeaponConfigRow* UShooterInventoryComponent::ResolveWeaponRow(
	FName WeaponRowName) const
{
	return ShooterWeaponTable::FindWeaponRow(GetWeaponTable(), WeaponRowName);
}

UDataTable* UShooterInventoryComponent::GetWeaponTable() const
{
	// 未注入时使用固定的 DT_WeaponData，保证武器模板表只有一个权威来源。
	return WeaponTable ? WeaponTable.Get() : ShooterWeaponTable::ResolveWeaponTable();
}

void UShooterInventoryComponent::SetWeaponTable(UDataTable* InWeaponTable)
{
	WeaponTable = InWeaponTable;
}

const FShooterWeaponInstanceData* UShooterInventoryComponent::FindWeaponInstanceBySlot(
	int32 SlotIndex) const
{
	const FShooterWeaponInstanceEntry* Entry = ReplicatedInventory.FindItemBySlot(SlotIndex);
	return Entry ? &Entry->InstanceData : nullptr;
}

int32 UShooterInventoryComponent::FindFreeSlotIndex() const
{
	for (int32 SlotIndex = 0; SlotIndex < MaxWeaponSlots; ++SlotIndex)
	{
		if (!ReplicatedInventory.FindItemBySlot(SlotIndex))
		{
			return SlotIndex;
		}
	}

	return INDEX_NONE;
}

bool UShooterInventoryComponent::FindNextWeaponInstanceId(
	const FGuid& CurrentId,
	FGuid& OutNextId) const
{
	return ReplicatedInventory.FindNextItemId(CurrentId, OutNextId);
}

AShooterWeapon* UShooterInventoryComponent::FindWeaponActor(const FGuid& InstanceId) const
{
	if (!InstanceId.IsValid())
	{
		return nullptr;
	}

	for (AShooterWeapon* Weapon : BoundWeaponActors)
	{
		if (IsValid(Weapon) && Weapon->GetBoundInstanceId() == InstanceId)
		{
			return Weapon;
		}
	}

	return nullptr;
}

void UShooterInventoryComponent::RegisterWeaponActor(AShooterWeapon* Weapon)
{
	if (!IsValid(Weapon))
	{
		return;
	}

	BoundWeaponActors.AddUnique(Weapon);
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Inventory registered WeaponActor: Actor=%s InstanceId=%s Weapon=%s"),
		*GetNameSafe(GetOwner()),
		*Weapon->GetBoundInstanceId().ToString(),
		*GetNameSafe(Weapon));
}

int32 UShooterInventoryComponent::GetMagazineAmmo(const FGuid& InstanceId) const
{
	// S2 起弹药权威在 WeaponActor；Inventory 只做 InstanceId -> Actor 的转发查询。
	const AShooterWeapon* Weapon = FindWeaponActor(InstanceId);
	return Weapon ? Weapon->GetBulletCount() : 0;
}

int32 UShooterInventoryComponent::GetReserveAmmo(const FGuid& InstanceId) const
{
	const AShooterWeapon* Weapon = FindWeaponActor(InstanceId);
	return Weapon ? Weapon->GetReserveAmmo() : 0;
}

bool UShooterInventoryComponent::CanConsumeMagazineAmmo(const FGuid& InstanceId) const
{
	const AShooterWeapon* Weapon = FindWeaponActor(InstanceId);
	return Weapon && Weapon->CanConsumeAmmo();
}

bool UShooterInventoryComponent::ConsumeMagazineAmmo(const FGuid& InstanceId, int32 Amount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Amount <= 0)
	{
		return false;
	}

	AShooterWeapon* Weapon = FindWeaponActor(InstanceId);
	return Weapon && Weapon->ConsumeAmmo(Amount);
}

bool UShooterInventoryComponent::ReloadMagazine(
	const FGuid& InstanceId,
	int32& OutTransferredAmmo)
{
	OutTransferredAmmo = 0;
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	// 换弹事务的容量与弹药全部来自 WeaponActor 自身的静态配置与权威弹药。
	AShooterWeapon* Weapon = FindWeaponActor(InstanceId);
	if (!IsValid(Weapon))
	{
		return false;
	}

	if (!Weapon->ReloadFromReserve(OutTransferredAmmo))
	{
		return false;
	}

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Inventory ReloadMagazine committed: Actor=%s InstanceId=%s Transfer=%d Mag=%d Reserve=%d"),
		*GetNameSafe(GetOwner()),
		*InstanceId.ToString(),
		OutTransferredAmmo,
		GetMagazineAmmo(InstanceId),
		GetReserveAmmo(InstanceId));
	return true;
}

void UShooterInventoryComponent::HandleInstanceRemoved(const FGuid& InstanceId)
{
	if (AShooterWeapon* Weapon = FindWeaponActor(InstanceId))
	{
		UnregisterWeaponActor(Weapon);
		Weapon->SetInstanceBinding(FGuid());
	}
}

void UShooterInventoryComponent::ReleaseWeaponActor(AShooterWeapon* Weapon)
{
	if (!IsValid(Weapon))
	{
		return;
	}

	// 身份必须在归还前取出：归还清理会清空 BoundInstanceId。
	const FGuid ReleasedInstanceId = Weapon->GetBoundInstanceId();

	UShooterActorPoolSubsystem* Pool = GetWorld()
		? GetWorld()->GetSubsystem<UShooterActorPoolSubsystem>()
		: nullptr;
	// 先判是否由池管理：非池出生是受支持的兼容路径，不应触发池自身的 fail closed 警告。
	if (Pool && Pool->IsManaged(Weapon) && Pool->Release(Weapon))
	{
		// 池归还已包含：停 Timer / 解委托 / 清绑定与弹药镜像 / 隐藏 / 关碰撞 / 关 Tick / 清 Owner。
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("Inventory released WeaponActor to pool: Actor=%s InstanceId=%s Weapon=%s"),
			*GetNameSafe(GetOwner()),
			*ReleasedInstanceId.ToString(),
			*GetNameSafe(Weapon));
		return;
	}

	// 兼容路径：非池出生（NPC / 旧测试直接 Spawn）无法归还，只能销毁并显式解除映射。
	UnregisterWeaponActor(Weapon);
	Weapon->Destroy();
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Inventory destroyed non-pooled WeaponActor: Actor=%s InstanceId=%s Weapon=%s"),
		*GetNameSafe(GetOwner()),
		*ReleasedInstanceId.ToString(),
		*GetNameSafe(Weapon));
}

void UShooterInventoryComponent::UnregisterWeaponActor(AShooterWeapon* Weapon)
{
	if (!IsValid(Weapon))
	{
		return;
	}

	BoundWeaponActors.Remove(Weapon);
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Inventory unregistered WeaponActor: Actor=%s InstanceId=%s Weapon=%s"),
		*GetNameSafe(GetOwner()),
		*Weapon->GetBoundInstanceId().ToString(),
		*GetNameSafe(Weapon));
}

FGuid UShooterInventoryComponent::GetActiveWeaponInstanceId() const
{
	const AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwner());
	const UShooterEquipmentComponent* Equipment = Character
		? Character->GetEquipmentComponent()
		: nullptr;
	return Equipment ? Equipment->GetActiveWeaponInstanceId() : FGuid();
}

AShooterWeapon* UShooterInventoryComponent::GetActiveWeaponActor() const
{
	const AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwner());
	const UShooterEquipmentComponent* Equipment = Character
		? Character->GetEquipmentComponent()
		: nullptr;
	return Equipment ? Equipment->GetCurrentWeaponActor() : nullptr;
}

void UShooterInventoryComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 完整 Inventory 只复制给 Owner；远端只通过 Equipment.CurrentWeaponActor 表现当前持枪。
	DOREPLIFETIME_CONDITION(UShooterInventoryComponent, ReplicatedInventory, COND_OwnerOnly);
}
