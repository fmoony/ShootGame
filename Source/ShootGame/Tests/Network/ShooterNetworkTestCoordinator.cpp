// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/Network/ShooterNetworkTestCoordinator.h"

#include "ShootGame.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "ShooterAbilitySystemComponent.h"
#include "ShooterAttributeSet.h"
#include "ShooterGameplayAbility_Fire.h"
#include "ShooterGameplayAbility_Equip.h"
#include "ShooterGameplayAbility_Reload.h"
#include "ShooterGameplayTags.h"
#include "ShooterCharacter.h"
#include "ShooterInventoryComponent.h"
#include "ShooterGameState.h"
#include "ShooterPlayerState.h"
#include "ShooterWeapon.h"
#include "ShooterProjectile.h"
#include "UObject/ConstructorHelpers.h"

namespace ShooterNetworkTest
{
	constexpr float PollIntervalSeconds = 0.1f;
	constexpr float TimeoutSeconds = 60.0f;
	const TCHAR* RifleClassPath =
		TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C");
	const TCHAR* PistolClassPath =
		TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Pistol.BP_ShooterWeapon_Pistol_C");
}

AShooterNetworkTestWeapon::AShooterNetworkTestWeapon()
{
	// 测试 NPC 使用不依赖蓝图的武器/弹丸配置；单发避免 AI 停火验证出现计时器噪音。
	static ConstructorHelpers::FClassFinder<AShooterProjectile> ProjectileClassFinder(
		TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterProjectile_Bullet.BP_ShooterProjectile_Bullet_C"));
	ProjectileClass = ProjectileClassFinder.Class;
	bFullAuto = false;
}

AShooterNetworkTestReloadWeapon::AShooterNetworkTestReloadWeapon()
{
	MagazineSize = 30;
	ReloadDuration = 1.5f;
}

AShooterNetworkTestNPC::AShooterNetworkTestNPC()
{
	WeaponClass = AShooterNetworkTestWeapon::StaticClass();
}

AShooterNetworkTestCoordinator::AShooterNetworkTestCoordinator()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bOnlyRelevantToOwner = true;
	SetReplicateMovement(false);
	bRequireRemoteMontage = !FParse::Param(
		FCommandLine::Get(),
		TEXT("ShootGameSkipRemoteMontage"));
	bRequireRemoteCurrentWeapon = !FParse::Param(
		FCommandLine::Get(),
		TEXT("ShootGameSkipRemoteCurrentWeapon"));
	bDisconnectCleanupMode = FParse::Param(
		FCommandLine::Get(),
		TEXT("ShootGameDisconnectTest"));
	bDisconnectEquipMode = FParse::Param(
		FCommandLine::Get(),
		TEXT("ShootGameDisconnectEquip"));
}

bool AShooterNetworkTestCoordinator::ReplaceInventoryWeaponForReloadTest(
	AShooterCharacter* Character,
	const FGuid& InstanceId,
	int32 SlotIndex,
	int32 MagazineAmmo,
	int32 ReserveAmmo)
{
	UShooterInventoryComponent* Inventory = Character
		? Character->GetInventoryComponent()
		: nullptr;
	if (!Inventory || !InstanceId.IsValid())
	{
		return false;
	}

	// 先走正式移除路径销毁旧 Actor / 清空 Active，再以测试数据重建同一逻辑身份。
	Inventory->RemoveWeaponInstance(InstanceId);

	FShooterWeaponInstanceData InstanceData;
	InstanceData.InstanceId = InstanceId;
	InstanceData.DefinitionId = FPrimaryAssetId(
		FPrimaryAssetType(TEXT("ShooterTestReload")),
		FName(TEXT("ReloadWeapon")));
	InstanceData.MagazineAmmo = MagazineAmmo;
	InstanceData.ReserveAmmo = ReserveAmmo;
	InstanceData.SlotIndex = SlotIndex;
	if (!Inventory->AddWeaponInstance(InstanceData))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = Character;
	SpawnParameters.Instigator = Character;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterWeapon* Weapon = GetWorld()->SpawnActor<AShooterWeapon>(
		AShooterNetworkTestReloadWeapon::StaticClass(),
		Character->GetActorTransform(),
		SpawnParameters);
	if (!Weapon)
	{
		Inventory->RemoveWeaponInstance(InstanceId);
		return false;
	}

	Weapon->SetBoundInstanceId(InstanceId);
	Weapon->SetActorHiddenInGame(true);
	Inventory->RegisterWeaponActor(Weapon);
	Character->HandleWeaponAddedToInventory(InstanceId);
	return true;
}

bool AShooterNetworkTestCoordinator::HasActiveReloadAbility(
	AShooterCharacter* Character) const
{
	AShooterPlayerState* ShooterPlayerState = Character
		? Character->GetPlayerState<AShooterPlayerState>()
		: nullptr;
	UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
		ShooterPlayerState
			? Cast<UShooterAbilitySystemComponent>(
				ShooterPlayerState->GetAbilitySystemComponent())
			: nullptr;
	return ShooterAbilitySystemComponent &&
		ShooterAbilitySystemComponent->GetActiveAbilityCountForClass(
			ShooterPlayerState->GetReloadAbilityClass()) == 1;
}

bool AShooterNetworkTestCoordinator::HasActiveFireAbility(
	AShooterCharacter* Character) const
{
	AShooterPlayerState* ShooterPlayerState = Character
		? Character->GetPlayerState<AShooterPlayerState>()
		: nullptr;
	UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
		ShooterPlayerState
			? Cast<UShooterAbilitySystemComponent>(
				ShooterPlayerState->GetAbilitySystemComponent())
			: nullptr;
	return ShooterAbilitySystemComponent &&
		ShooterAbilitySystemComponent->GetActiveAbilityCountForClass(
			ShooterPlayerState->GetFireAbilityClass()) == 1;
}

bool AShooterNetworkTestCoordinator::HasActiveEquipAbility(
	AShooterCharacter* Character) const
{
	AShooterPlayerState* ShooterPlayerState = Character
		? Character->GetPlayerState<AShooterPlayerState>()
		: nullptr;
	UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
		ShooterPlayerState
			? Cast<UShooterAbilitySystemComponent>(
				ShooterPlayerState->GetAbilitySystemComponent())
			: nullptr;
	return ShooterAbilitySystemComponent &&
		ShooterAbilitySystemComponent->GetActiveAbilityCountForClass(
			ShooterPlayerState->GetEquipAbilityClass()) == 1;
}

void AShooterNetworkTestCoordinator::TriggerDisconnectReload()
{
	AShooterCharacter* Character = GetShooterCharacter();
	UShooterInventoryComponent* Inventory = Character
		? Character->GetInventoryComponent()
		: nullptr;
	if (!Character || !Inventory || !ServerReloadAbilityHandle.IsValid())
	{
		return;
	}

	const FGuid ActiveInstanceId = Inventory->GetActiveWeaponInstanceId();
	if (ActiveInstanceId.IsValid() && Inventory->GetMagazineAmmo(ActiveInstanceId) > 0)
	{
		Inventory->ConsumeMagazineAmmo(ActiveInstanceId, 1);
	}

	if (UAbilitySystemComponent* AbilitySystemComponent =
		Character->GetAbilitySystemComponent())
	{
		const bool bActivated = AbilitySystemComponent->TryActivateAbility(
			ServerReloadAbilityHandle);
		const bool bActive = HasActiveReloadAbility(Character);
		const bool bReloadingTag = AbilitySystemComponent->HasMatchingGameplayTag(
			ShooterGameplayTags::State_Reloading);
		if (!bActivated || !bActive || !bReloadingTag)
		{
			FailTest(FString::Printf(
				TEXT("Disconnect reload precondition failed; Activated=%s Active=%s Tag=%s Avatar=%s"),
				bActivated ? TEXT("true") : TEXT("false"),
				bActive ? TEXT("true") : TEXT("false"),
				bReloadingTag ? TEXT("true") : TEXT("false"),
				*GetNameSafe(Character)));
			return;
		}

		UE_LOG(
			LogShootGame,
			Display,
			TEXT("AUTOMATION_TEST_DISCONNECT_RELOAD_READY Avatar=%s ActiveInstance=%s ActiveReload=true ReloadingTag=true"),
			*GetNameSafe(Character),
			*ActiveInstanceId.ToString());
	}
}

bool AShooterNetworkTestCoordinator::TriggerLongEquip(
	AShooterCharacter* Character,
	const TCHAR* Context)
{
	UShooterInventoryComponent* Inventory = Character
		? Character->GetInventoryComponent()
		: nullptr;
	UAbilitySystemComponent* AbilitySystemComponent = Character
		? Character->GetAbilitySystemComponent()
		: nullptr;
	if (!Inventory || !AbilitySystemComponent || !ServerEquipAbilityHandle.IsValid())
	{
		FailTest(FString::Printf(
			TEXT("%s equip precondition missing Inventory, ASC or Ability handle"),
			Context));
		return false;
	}

	FGuid TargetInstanceId;
	if (!Inventory->FindNextWeaponInstanceId(
		Inventory->GetActiveWeaponInstanceId(),
		TargetInstanceId))
	{
		FailTest(FString::Printf(
			TEXT("%s equip precondition could not resolve next instance"),
			Context));
		return false;
	}

	AShooterWeapon* TargetWeapon = Inventory->FindWeaponActor(TargetInstanceId);
	FFloatProperty* EquipDurationProperty = TargetWeapon
		? FindFProperty<FFloatProperty>(TargetWeapon->GetClass(), TEXT("EquipDuration"))
		: nullptr;
	if (!TargetWeapon || !EquipDurationProperty)
	{
		FailTest(FString::Printf(
			TEXT("%s equip precondition missing target Weapon or EquipDuration"),
			Context));
		return false;
	}

	// 给脚本和死亡检查留出充足窗口；仅修改本次无头测试生成的 Actor 实例。
	EquipDurationProperty->SetPropertyValue_InContainer(TargetWeapon, 5.0f);
	const bool bActivated = AbilitySystemComponent->TryActivateAbility(
		ServerEquipAbilityHandle);
	const bool bActive = HasActiveEquipAbility(Character);
	const bool bEquippingTag = AbilitySystemComponent->HasMatchingGameplayTag(
		ShooterGameplayTags::State_Equipping);
	if (!bActivated || !bActive || !bEquippingTag)
	{
		FailTest(FString::Printf(
			TEXT("%s equip precondition failed; Activated=%s Active=%s Tag=%s Avatar=%s"),
			Context,
			bActivated ? TEXT("true") : TEXT("false"),
			bActive ? TEXT("true") : TEXT("false"),
			bEquippingTag ? TEXT("true") : TEXT("false"),
			*GetNameSafe(Character)));
		return false;
	}

	return true;
}

void AShooterNetworkTestCoordinator::TriggerDisconnectEquip()
{
	AShooterCharacter* Character = GetShooterCharacter();
	if (!TriggerLongEquip(Character, TEXT("Disconnect")))
	{
		return;
	}

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("AUTOMATION_TEST_DISCONNECT_EQUIP_READY Avatar=%s ActiveEquip=true EquippingTag=true"),
		*GetNameSafe(Character));
}

void AShooterNetworkTestCoordinator::TriggerEquipDeath()
{
	AShooterCharacter* Character = GetShooterCharacter();
	if (!TriggerLongEquip(Character, TEXT("Death")))
	{
		return;
	}

	CharacterBeforeDeath = Character;
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("AUTOMATION_TEST_EQUIP_DEATH_READY Avatar=%s ActiveEquip=true EquippingTag=true"),
		*GetNameSafe(Character));

	UGameplayStatics::ApplyDamage(
		Character,
		Character->GetMaxHP() * 2.0f,
		nullptr,
		this,
		nullptr);
	GetWorldTimerManager().SetTimer(
		EquipDeathVerifyTimer,
		this,
		&AShooterNetworkTestCoordinator::VerifyEquipDeathCleanup,
		0.2f,
		false);
}

void AShooterNetworkTestCoordinator::VerifyEquipDeathCleanup()
{
	AShooterCharacter* Character = CharacterBeforeDeath.Get();
	UAbilitySystemComponent* AbilitySystemComponent = Character
		? Character->GetAbilitySystemComponent()
		: nullptr;
	UShooterInventoryComponent* Inventory = Character
		? Character->GetInventoryComponent()
		: nullptr;
	const bool bClean = Character && Character->IsDead() && Inventory &&
		Inventory->GetWeaponCount() == 0 &&
		!Inventory->GetActiveWeaponInstanceId().IsValid() &&
		Character->GetCurrentWeapon() == nullptr &&
		AbilitySystemComponent &&
		!HasActiveEquipAbility(Character) &&
		!AbilitySystemComponent->HasMatchingGameplayTag(
			ShooterGameplayTags::State_Equipping);
	if (!bClean)
	{
		FailTest(FString::Printf(
			TEXT("Equip death cleanup invalid; Dead=%s Count=%d ActiveId=%s CurrentWeapon=%s ActiveEquip=%s EquippingTag=%s"),
			Character && Character->IsDead() ? TEXT("true") : TEXT("false"),
			Inventory ? Inventory->GetWeaponCount() : INDEX_NONE,
			Inventory ? *Inventory->GetActiveWeaponInstanceId().ToString() : TEXT("null"),
			*GetNameSafe(Character ? Character->GetCurrentWeapon() : nullptr),
			Character && HasActiveEquipAbility(Character) ? TEXT("true") : TEXT("false"),
			AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(
				ShooterGameplayTags::State_Equipping)
				? TEXT("true")
				: TEXT("false")));
		return;
	}

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("AUTOMATION_TEST_EQUIP_CLEANUP_SUCCESS Kind=Death ActiveEquip=0 EquippingTags=0 Inventory=0 CurrentWeapon=null"));
}

void AShooterNetworkTestCoordinator::BeginPlay()
{
	Super::BeginPlay();

	TestStartTime = GetWorld()->GetTimeSeconds();
	if (HasAuthority())
	{
		ActorSpawnedHandle = GetWorld()->AddOnActorSpawnedHandler(
			FOnActorSpawned::FDelegate::CreateUObject(
				this,
				&AShooterNetworkTestCoordinator::HandleActorSpawned));
	}

	const FTimerDelegate PollDelegate = HasAuthority()
		? FTimerDelegate::CreateUObject(this, &AShooterNetworkTestCoordinator::PollServerState)
		: FTimerDelegate::CreateUObject(this, &AShooterNetworkTestCoordinator::PollClientState);

	GetWorldTimerManager().SetTimer(
		PollTimer,
		PollDelegate,
		ShooterNetworkTest::PollIntervalSeconds,
		true,
		ShooterNetworkTest::PollIntervalSeconds);
}

void AShooterNetworkTestCoordinator::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	if (ActorSpawnedHandle.IsValid())
	{
		GetWorld()->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
	}
	GetWorldTimerManager().ClearTimer(PollTimer);

	GetWorldTimerManager().ClearTimer(CleanupAbilityTimer);
	GetWorldTimerManager().ClearTimer(EquipDeathVerifyTimer);
	Super::EndPlay(EndPlayReason);
}

void AShooterNetworkTestCoordinator::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShooterNetworkTestCoordinator, bServerReadyToSwitch);
	DOREPLIFETIME(AShooterNetworkTestCoordinator, bServerReadyToFire);
	DOREPLIFETIME(AShooterNetworkTestCoordinator, bServerReadyForFullAuto);
	DOREPLIFETIME(AShooterNetworkTestCoordinator, bServerReadyForSwitchCancel);
	DOREPLIFETIME(AShooterNetworkTestCoordinator, WeaponBeforeSwitch);
	DOREPLIFETIME(AShooterNetworkTestCoordinator, bRequireRemoteMontage);
	DOREPLIFETIME(AShooterNetworkTestCoordinator, bRequireRemoteCurrentWeapon);
	DOREPLIFETIME(AShooterNetworkTestCoordinator, ReloadInputRequestId);
	DOREPLIFETIME(AShooterNetworkTestCoordinator, bServerReadyForReloadSwitch);
	DOREPLIFETIME(AShooterNetworkTestCoordinator, bServerReadyForReloadSwitchBack);
	DOREPLIFETIME(AShooterNetworkTestCoordinator, bServerReadyForFireAfterReload);
	DOREPLIFETIME(AShooterNetworkTestCoordinator, bServerReadyForStopFireAfterReload);
	DOREPLIFETIME(AShooterNetworkTestCoordinator, bServerReadyForEquipSingleReject);
}

void AShooterNetworkTestCoordinator::PollServerState()
{
	// 网络测试期间停用地图 NPC 的 AI 与射击：首次轮询时世界已开始运行，
	// 能覆盖监听服务器上晚于 PostLogin 才完成拥有的 NPC。只停止行为，不销毁 NPC。
	if (!bNpcAiSuppressed)
	{
		bNpcAiSuppressed = true;
		for (TActorIterator<AShooterNPC> It(GetWorld()); It; ++It)
		{
			AShooterNPC* Npc = *It;
			Npc->StopShooting();
			if (AController* NpcController = Npc->GetController())
			{
				NpcController->Destroy();
			}
		}
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		if (GetWorld()->GetTimeSeconds() - TestStartTime >= ShooterNetworkTest::TimeoutSeconds)
		{
			FailTest(TEXT("Server did not receive a shooter character"));
		}
		return;
	}

	// ---- Inventory 2B：服务器通过正式授予路径创建 WeaponInstance + WeaponActor ----
	if (!bServerInventoryPrepared)
	{
		UShooterInventoryComponent* InventoryComponent = Character->GetInventoryComponent();
		if (!InventoryComponent)
		{
			FailTest(TEXT("Server character does not have an InventoryComponent"));
			return;
		}

		const TSubclassOf<AShooterWeapon> RifleClass = LoadClass<AShooterWeapon>(
			nullptr,
			ShooterNetworkTest::RifleClassPath);
		const TSubclassOf<AShooterWeapon> PistolClass = LoadClass<AShooterWeapon>(
			nullptr,
			ShooterNetworkTest::PistolClassPath);
		if (!RifleClass || !PistolClass)
		{
			FailTest(TEXT("Rifle or Pistol class could not be loaded"));
			return;
		}

		const EShooterInventoryAddResult RifleResult =
			InventoryComponent->TryAddWeapon(RifleClass, ServerInventoryFirstId);
		if (RifleResult == EShooterInventoryAddResult::Added)
		{
			Character->HandleWeaponAddedToInventory(ServerInventoryFirstId);
		}

		const EShooterInventoryAddResult PistolResult =
			InventoryComponent->TryAddWeapon(PistolClass, ServerInventorySecondId);
		if (PistolResult == EShooterInventoryAddResult::Added)
		{
			Character->HandleWeaponAddedToInventory(ServerInventorySecondId);
		}

		// SingleGrant：同一 WeaponClass 不能第二次授予。
		FGuid DuplicateInstanceId;
		const EShooterInventoryAddResult DuplicateResult =
			InventoryComponent->TryAddWeapon(RifleClass, DuplicateInstanceId);

		// SlotFull：临时把 Slot 上限设为 1，使用额外测试类验证唯一空位耗尽后明确 Reject。
		const int32 PreviousMaxSlots = InventoryComponent->GetMaxWeaponSlots();
		InventoryComponent->SetMaxWeaponSlots(1);
		FGuid SlotFullInstanceId;
		const EShooterInventoryAddResult SlotFullResult =
			InventoryComponent->TryAddWeapon(AShooterNetworkTestWeapon::StaticClass(), SlotFullInstanceId);
		InventoryComponent->SetMaxWeaponSlots(PreviousMaxSlots);

		// WeaponActorBinding：每个 InstanceId 必须与对应 Actor 的 BoundInstanceId 完全一致。
		AShooterWeapon* RifleActor = InventoryComponent->FindWeaponActor(ServerInventoryFirstId);
		AShooterWeapon* PistolActor = InventoryComponent->FindWeaponActor(ServerInventorySecondId);
		const bool bBindingOk = IsValid(RifleActor) && IsValid(PistolActor) &&
			RifleActor->GetBoundInstanceId() == ServerInventoryFirstId &&
			PistolActor->GetBoundInstanceId() == ServerInventorySecondId;

		// Switch.InvalidInstance：不存在的 InstanceId 不能改写 Active 身份。
		const FGuid ActiveBeforeInvalid = InventoryComponent->GetActiveWeaponInstanceId();
		InventoryComponent->SetActiveWeaponInstanceId(FGuid::NewGuid());
		const bool bInvalidInstanceRejected =
			InventoryComponent->GetActiveWeaponInstanceId() == ActiveBeforeInvalid;

		InitialRifleMagazineAmmo = InventoryComponent->GetMagazineAmmo(ServerInventoryFirstId);
		InitialPistolMagazineAmmo = InventoryComponent->GetMagazineAmmo(ServerInventorySecondId);

		ServerInventoryActiveId = InventoryComponent->GetActiveWeaponInstanceId();
		if (RifleResult != EShooterInventoryAddResult::Added ||
			PistolResult != EShooterInventoryAddResult::Added ||
			DuplicateResult != EShooterInventoryAddResult::DuplicateDefinition ||
			SlotFullResult != EShooterInventoryAddResult::SlotFull ||
			InventoryComponent->GetWeaponCount() != 2 ||
			ServerInventoryActiveId != ServerInventorySecondId ||
			!bBindingOk ||
			!bInvalidInstanceRejected)
		{
			FailTest(FString::Printf(
				TEXT("Server Inventory 2B preparation failed; Rifle=%d Pistol=%d Duplicate=%d SlotFull=%d Count=%d Active=%s ExpectedActive=%s Binding=%s InvalidInstanceRejected=%s"),
				static_cast<int32>(RifleResult),
				static_cast<int32>(PistolResult),
				static_cast<int32>(DuplicateResult),
				static_cast<int32>(SlotFullResult),
				InventoryComponent->GetWeaponCount(),
				*ServerInventoryActiveId.ToString(),
				*ServerInventorySecondId.ToString(),
				bBindingOk ? TEXT("true") : TEXT("false"),
				bInvalidInstanceRejected ? TEXT("true") : TEXT("false")));
			return;
		}

		bServerInventoryPrepared = true;
		bSecondaryWeaponGranted = true;
		WeaponBeforeSwitch = Character->GetCurrentWeapon();
		bServerReadyToSwitch = true;
		ForceNetUpdate();
		return;
	}

	const AGameStateBase* GameState = GetWorld()->GetGameState();
	if (!GameState || GameState->PlayerArray.Num() < 2)
	{
		return;
	}

	// ---- GAS ASC 生命周期（服务器视角）：Owner=PlayerState，Avatar=当前角色 ----
	if (!bServerGasLifecycleChecked)
	{
		bServerGasLifecycleChecked = true;

		const APlayerController* PlayerController = Cast<APlayerController>(GetOwner());
		AShooterPlayerState* ShooterPlayerState = PlayerController
			? PlayerController->GetPlayerState<AShooterPlayerState>()
			: nullptr;
		UAbilitySystemComponent* AbilitySystemComponent = ShooterPlayerState
			? ShooterPlayerState->GetAbilitySystemComponent()
			: nullptr;

		bServerGasOwnerOk = AbilitySystemComponent &&
			AbilitySystemComponent->GetOwnerActor() == ShooterPlayerState;
		bServerGasAvatarOk = AbilitySystemComponent &&
			AbilitySystemComponent->GetAvatarActor() == Character;
		// Mixed 复制模式依赖 OwnerActor（PlayerState → PlayerController）的网络连接。
		// 运行时事实：监听服务器的主机玩家是本地权威玩家，PlayerState 没有网络连接，
		// 属于预期情况；连接要求只对远程客户端成立。
		bServerGasConnectionOk = (ShooterPlayerState &&
			ShooterPlayerState->GetNetConnection() != nullptr) ||
			(GetNetMode() == NM_ListenServer && PlayerController && PlayerController->IsLocalController());
		ObservedAbilitySystemComponent = AbilitySystemComponent;

		if (!bServerGasOwnerOk || !bServerGasAvatarOk || !bServerGasConnectionOk)
		{
			FailTest(FString::Printf(
				TEXT("Server-side GAS ASC lifecycle invalid; ASC=%s OwnerOk=%s AvatarOk=%s ConnectionOk=%s Owner=%s Avatar=%s"),
				*GetNameSafe(AbilitySystemComponent),
				bServerGasOwnerOk ? TEXT("true") : TEXT("false"),
				bServerGasAvatarOk ? TEXT("true") : TEXT("false"),
				bServerGasConnectionOk ? TEXT("true") : TEXT("false"),
				*GetNameSafe(AbilitySystemComponent ? AbilitySystemComponent->GetOwnerActor() : nullptr),
				*GetNameSafe(AbilitySystemComponent ? AbilitySystemComponent->GetAvatarActor() : nullptr)));
			return;
		}

		// GAS Health 初始化：出生后 Health == MaxHealth 且大于零（初始化效果已同步应用）。
		bServerGasHealthInitChecked = true;
		const float MaxHealthAttributeValue = AbilitySystemComponent
			? AbilitySystemComponent->GetNumericAttribute(UShooterAttributeSet::GetMaxHealthAttribute())
			: 0.0f;
		const float HealthAttributeValue = AbilitySystemComponent
			? AbilitySystemComponent->GetNumericAttribute(UShooterAttributeSet::GetHealthAttribute())
			: 0.0f;
		bServerGasHealthInitOk = MaxHealthAttributeValue > 0.0f &&
			FMath::IsNearlyEqual(HealthAttributeValue, MaxHealthAttributeValue, 0.01f);
		if (!bServerGasHealthInitOk)
		{
			FailTest(FString::Printf(
				TEXT("Server-side GAS health init invalid; MaxHealth=%.1f Health=%.1f"),
				MaxHealthAttributeValue,
				HealthAttributeValue));
			return;
		}

		// ---- 4A Fire Ability 授予生命周期（服务器视角）----
		if (!bServerFireGrantChecked)
		{
			bServerFireGrantChecked = true;

			UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
				Cast<UShooterAbilitySystemComponent>(AbilitySystemComponent);
			const TSubclassOf<UShooterGameplayAbility_Fire> FireAbilityClass =
				ShooterPlayerState->GetFireAbilityClass();
			const FGameplayAbilitySpec* FireAbilitySpec =
				AbilitySystemComponent
					? AbilitySystemComponent->FindAbilitySpecFromClass(FireAbilityClass)
					: nullptr;
			ServerFireAbilityCount = ShooterPlayerState->GetFireAbilitySpecCount();
			ServerFireAbilityHandle = FireAbilitySpec
				? FireAbilitySpec->Handle
				: FGameplayAbilitySpecHandle();

			// 幂等授予：重复调用不得新增第二个 Spec。
			ShooterPlayerState->GrantFireAbility();
			ShooterPlayerState->GrantFireAbility();
			const int32 FireAbilityCountAfterGrant =
				ShooterPlayerState->GetFireAbilitySpecCount();

			bServerFireGrantOk = ShooterAbilitySystemComponent &&
				FireAbilityClass == UShooterGameplayAbility_Fire::StaticClass() &&
				ServerFireAbilityCount == 1 &&
				FireAbilityCountAfterGrant == 1 &&
				FireAbilitySpec &&
				FireAbilitySpec->Ability &&
				FireAbilitySpec->Ability->GetClass() == FireAbilityClass &&
				ServerFireAbilityHandle.IsValid();
			if (!bServerFireGrantOk)
			{
				FailTest(FString::Printf(
					TEXT("Server Fire Ability grant invalid; ASC=%s Class=%s Count=%d CountAfterGrant=%d Handle=%s SpecAbility=%s"),
					*GetNameSafe(ShooterAbilitySystemComponent),
					*GetNameSafe(FireAbilityClass),
					ServerFireAbilityCount,
					FireAbilityCountAfterGrant,
					*ServerFireAbilityHandle.ToString(),
					FireAbilitySpec && FireAbilitySpec->Ability
						? *GetNameSafe(FireAbilitySpec->Ability->GetClass())
						: TEXT("null")));
				return;
			}
		}

		// ---- 5A Reload / Equip Ability 授予生命周期（服务器视角）----
		if (!bServerReloadEquipGrantChecked)
		{
			bServerReloadEquipGrantChecked = true;

			const TSubclassOf<UShooterGameplayAbility_Reload> ReloadAbilityClass =
				ShooterPlayerState->GetReloadAbilityClass();
			const TSubclassOf<UShooterGameplayAbility_Equip> EquipAbilityClass =
				ShooterPlayerState->GetEquipAbilityClass();
			const FGameplayAbilitySpec* ReloadAbilitySpec =
				AbilitySystemComponent
					? AbilitySystemComponent->FindAbilitySpecFromClass(ReloadAbilityClass)
					: nullptr;
			const FGameplayAbilitySpec* EquipAbilitySpec =
				AbilitySystemComponent
					? AbilitySystemComponent->FindAbilitySpecFromClass(EquipAbilityClass)
					: nullptr;
			ServerReloadAbilityCount = ShooterPlayerState->GetReloadAbilitySpecCount();
			ServerEquipAbilityCount = ShooterPlayerState->GetEquipAbilitySpecCount();
			ServerReloadAbilityHandle = ReloadAbilitySpec
				? ReloadAbilitySpec->Handle
				: FGameplayAbilitySpecHandle();
			ServerEquipAbilityHandle = EquipAbilitySpec
				? EquipAbilitySpec->Handle
				: FGameplayAbilitySpecHandle();

			// 幂等授予：重复调用不得新增 Spec。
			ShooterPlayerState->GrantReloadAbility();
			ShooterPlayerState->GrantReloadAbility();
			ShooterPlayerState->GrantEquipAbility();
			ShooterPlayerState->GrantEquipAbility();

			bServerReloadEquipGrantOk =
				ReloadAbilityClass == UShooterGameplayAbility_Reload::StaticClass() &&
				EquipAbilityClass == UShooterGameplayAbility_Equip::StaticClass() &&
				ServerReloadAbilityCount == 1 &&
				ServerEquipAbilityCount == 1 &&
				ShooterPlayerState->GetReloadAbilitySpecCount() == 1 &&
				ShooterPlayerState->GetEquipAbilitySpecCount() == 1 &&
				ReloadAbilitySpec &&
				ReloadAbilitySpec->Ability &&
				ReloadAbilitySpec->Ability->GetClass() == ReloadAbilityClass &&
				EquipAbilitySpec &&
				EquipAbilitySpec->Ability &&
				EquipAbilitySpec->Ability->GetClass() == EquipAbilityClass &&
				ServerReloadAbilityHandle.IsValid() &&
				ServerEquipAbilityHandle.IsValid();
			if (!bServerReloadEquipGrantOk)
			{
				FailTest(FString::Printf(
					TEXT("Server Reload/Equip Ability grant invalid; ReloadClass=%s EquipClass=%s ReloadCount=%d EquipCount=%d ReloadAfter=%d EquipAfter=%d ReloadHandle=%s EquipHandle=%s ReloadSpecAbility=%s EquipSpecAbility=%s"),
					*GetNameSafe(ReloadAbilityClass),
					*GetNameSafe(EquipAbilityClass),
					ServerReloadAbilityCount,
					ServerEquipAbilityCount,
					ShooterPlayerState->GetReloadAbilitySpecCount(),
					ShooterPlayerState->GetEquipAbilitySpecCount(),
					*ServerReloadAbilityHandle.ToString(),
					*ServerEquipAbilityHandle.ToString(),
					ReloadAbilitySpec && ReloadAbilitySpec->Ability
						? *GetNameSafe(ReloadAbilitySpec->Ability->GetClass())
						: TEXT("null"),
					EquipAbilitySpec && EquipAbilitySpec->Ability
						? *GetNameSafe(EquipAbilitySpec->Ability->GetClass())
						: TEXT("null")));
				return;
			}
		}
	}


	// ---- DisconnectCleanup 专用：先制造确定活动的 Ability，再由脚本收到 Ready 标记后断线 ----
	if (bDisconnectCleanupMode && !bCleanupAbilityScheduled &&
		bServerInventoryPrepared && bServerReloadEquipGrantOk)
	{
		APlayerController* OwnerController = Cast<APlayerController>(GetOwner());
		AShooterPlayerState* OwnerState = OwnerController
			? OwnerController->GetPlayerState<AShooterPlayerState>()
			: nullptr;
		if (OwnerState)
		{
			bCleanupAbilityScheduled = true;
			void (AShooterNetworkTestCoordinator::*TriggerFunction)() =
				&AShooterNetworkTestCoordinator::TriggerDisconnectReload;
			const TCHAR* Scenario = TEXT("ReloadDisconnect");
			if (bDisconnectEquipMode)
			{
				// RunNetworkSession 按连接顺序断开第一个客户端；PostLogin 为其分配 Team 0。
				// Team 1 在同一会话执行活动 Equip → Death Clear，覆盖另一条取消路径。
				TriggerFunction = OwnerState->GetTeamId() == 0
					? &AShooterNetworkTestCoordinator::TriggerDisconnectEquip
					: &AShooterNetworkTestCoordinator::TriggerEquipDeath;
				Scenario = OwnerState->GetTeamId() == 0
					? TEXT("EquipDisconnect")
					: TEXT("EquipDeath");
			}

			GetWorldTimerManager().SetTimer(
				CleanupAbilityTimer,
				this,
				TriggerFunction,
				0.5f,
				false);
			UE_LOG(
				LogShootGame,
				Display,
				TEXT("Ability cleanup scheduled: Scenario=%s PlayerId=%d Team=%u"),
				Scenario,
				OwnerState->GetPlayerId(),
				OwnerState->GetTeamId());
		}
	}
	// ---- GAS NPC ASC 生命周期（服务器视角）：Owner=Avatar=NPC ----
	if (!bNpcGasLifecycleChecked)
	{
		bNpcGasLifecycleChecked = true;

		// 生成无控制器的测试 NPC，验证其 ASC 后立即销毁。
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AShooterNetworkTestNPC* TestNpc = GetWorld()->SpawnActor<AShooterNetworkTestNPC>(
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			SpawnParameters);
		if (!TestNpc)
		{
			FailTest(TEXT("Server could not spawn the GAS NPC lifecycle test actor"));
			return;
		}

		UAbilitySystemComponent* NpcAbilitySystemComponent = TestNpc->GetAbilitySystemComponent();
		bNpcGasLifecycleOk = NpcAbilitySystemComponent &&
			NpcAbilitySystemComponent->GetOwnerActor() == TestNpc &&
			NpcAbilitySystemComponent->GetAvatarActor() == TestNpc;

		// ---- 4A NPC Fire Ability 授予：NPC ASC 独立持有一个 GA_Fire Spec ----
		const FGameplayAbilitySpec* NpcFireAbilitySpec =
			NpcAbilitySystemComponent
				? NpcAbilitySystemComponent->FindAbilitySpecFromClass(
					TestNpc->GetFireAbilityClass())
				: nullptr;
		bNpcFireGrantOk = NpcAbilitySystemComponent &&
			NpcAbilitySystemComponent->IsA<UShooterAbilitySystemComponent>() &&
			NpcFireAbilitySpec &&
			NpcFireAbilitySpec->Ability &&
			NpcFireAbilitySpec->Ability->GetClass() == TestNpc->GetFireAbilityClass() &&
			TestNpc->GetFireAbilitySpecCount() == 1;
		if (!bNpcGasLifecycleOk)
		{
			FailTest(FString::Printf(
				TEXT("NPC GAS ASC lifecycle invalid; ASC=%s Owner=%s Avatar=%s"),
				*GetNameSafe(NpcAbilitySystemComponent),
				*GetNameSafe(NpcAbilitySystemComponent ? NpcAbilitySystemComponent->GetOwnerActor() : nullptr),
				*GetNameSafe(NpcAbilitySystemComponent ? NpcAbilitySystemComponent->GetAvatarActor() : nullptr)));
		}

		// NPC Health 初始化。
		if (NpcAbilitySystemComponent)
		{
			const float NpcMaxHealth = NpcAbilitySystemComponent->GetNumericAttribute(
				UShooterAttributeSet::GetMaxHealthAttribute());
			const float NpcHealthBefore = NpcAbilitySystemComponent->GetNumericAttribute(
				UShooterAttributeSet::GetHealthAttribute());
			bNpcGasHealthInitOk = NpcMaxHealth > 0.0f &&
				FMath::IsNearlyEqual(NpcHealthBefore, NpcMaxHealth, 0.01f);
		}

		// ---- 4C NPC GA_Fire：AI 只提交意图，ASC 激活 Ability，服务器生成弹丸 ----
		if (bNpcGasLifecycleOk && bNpcFireGrantOk && bNpcGasHealthInitOk)
		{
			NpcFireTestNpc = TestNpc;
			const int32 NpcProjectilesBefore = CountProjectilesForInstigator(TestNpc);
			TestNpc->StartShooting(Character);

			UShooterAbilitySystemComponent* NpcShooterAbilitySystemComponent =
				Cast<UShooterAbilitySystemComponent>(NpcAbilitySystemComponent);
			bNpcFireActivated = NpcShooterAbilitySystemComponent &&
				NpcShooterAbilitySystemComponent->GetActiveAbilityCountForClass(
					TestNpc->GetFireAbilityClass()) == 1 &&
				CountProjectilesForInstigator(TestNpc) > NpcProjectilesBefore;

			TestNpc->StopShooting();
			bNpcFireStopOk = NpcShooterAbilitySystemComponent &&
				NpcShooterAbilitySystemComponent->GetActiveAbilityCountForClass(
					TestNpc->GetFireAbilityClass()) == 0;
			NpcProjectileCountAtStop = CountProjectilesForInstigator(TestNpc);
			NpcFireStopCheckTime = GetWorld()->GetTimeSeconds();
		}

		// 致死伤害桥接：属性归零进入现有 Die() 死亡流程。
		if (NpcAbilitySystemComponent && bNpcGasHealthInitOk)
		{
			const float NpcMaxHealth = NpcAbilitySystemComponent->GetNumericAttribute(
				UShooterAttributeSet::GetMaxHealthAttribute());
			UGameplayStatics::ApplyDamage(TestNpc, NpcMaxHealth + 100.0f, nullptr, this, nullptr);
			const float NpcHealthAfter = NpcAbilitySystemComponent->GetNumericAttribute(
				UShooterAttributeSet::GetHealthAttribute());
			// bIsDead 是未反射的私有成员；用可观察证据判定死亡：
			// 属性归零 + CurrentHP 镜像归零 + 现有 Die() 流程禁用胶囊碰撞。
			// 不用布娃娃物理作为证据：测试 NPC 使用 C++ 裸默认网格，没有物理资产。
			bNpcGasDeathOk = NpcHealthAfter <= 0.0f &&
				TestNpc->CurrentHP <= 0.0f &&
				TestNpc->GetCapsuleComponent() &&
				TestNpc->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision;
		}
		if (!bNpcGasHealthInitOk || !bNpcGasDeathOk || !bNpcFireGrantOk ||
			!bNpcFireActivated || !bNpcFireStopOk)
		{
			TestNpc->Destroy();
			FailTest(FString::Printf(
				TEXT("NPC GAS health bridge invalid; InitOk=%s DeathOk=%s FireGrantOk=%s FireActivated=%s FireStopOk=%s Health=%.1f CurrentHP=%.1f CapsuleCollision=%s"),
				bNpcGasHealthInitOk ? TEXT("true") : TEXT("false"),
				bNpcGasDeathOk ? TEXT("true") : TEXT("false"),
				bNpcFireGrantOk ? TEXT("true") : TEXT("false"),
				bNpcFireActivated ? TEXT("true") : TEXT("false"),
				bNpcFireStopOk ? TEXT("true") : TEXT("false"),
				NpcAbilitySystemComponent
					? NpcAbilitySystemComponent->GetNumericAttribute(UShooterAttributeSet::GetHealthAttribute())
					: -1.0f,
				TestNpc->CurrentHP,
				TestNpc->GetCapsuleComponent() &&
					TestNpc->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision
					? TEXT("NoCollision")
					: TEXT("not-disabled")));
		}
	}

	// NPC 停火后的静默期：0.5 秒内不得再生成 NPC 弹丸，随后销毁测试 NPC。
	if (NpcFireTestNpc.IsValid() && bNpcFireActivated && !bNpcFireQuiescenceConfirmed)
	{
		if (GetWorld()->GetTimeSeconds() - NpcFireStopCheckTime >= 0.5f)
		{
			const int32 NpcProjectilesNow =
				CountProjectilesForInstigator(NpcFireTestNpc.Get());
			bNpcFireQuiescenceConfirmed =
				NpcProjectilesNow == NpcProjectileCountAtStop;
			if (!bNpcFireQuiescenceConfirmed)
			{
				FailTest(FString::Printf(
					TEXT("NPC fire stop left projectiles; Projectiles=%d->%d"),
					NpcProjectileCountAtStop,
					NpcProjectilesNow));
			}
			NpcFireTestNpc->Destroy();
			NpcFireTestNpc.Reset();
		}
	}

	if (APlayerController* OwnerController = Cast<APlayerController>(GetOwner());
		OwnerController && OwnerController->IsLocalController())
	{
		FRotator ControlRotation = OwnerController->GetControlRotation();
		ControlRotation.Pitch = 30.0f;
		OwnerController->SetControlRotation(ControlRotation);
	}

	// 死亡后的清空阶段不允许旧 AddWeaponClass 路径重新补枪；重生后 Inventory 也必须保持为空。
	AShooterWeapon* Weapon = GetCurrentWeapon(Character);
	const int32 CurrentBulletCount = Weapon ? Weapon->GetBulletCount() : INDEX_NONE;

	if (bDisconnectCleanupMode)
	{
		return;
	}

	if (Weapon)
	{
	// 5C：初始切换提交后 ActiveInstanceId 与 CurrentWeapon Actor 必须一致。
	if (bClientObservedSwitch && !bEquipInitialCommitConsistent)
	{
		UShooterInventoryComponent* EquipInventory = Character->GetInventoryComponent();
		AShooterWeapon* EquippedWeapon = Character->GetCurrentWeapon();
		bEquipInitialCommitConsistent = EquipInventory && EquippedWeapon &&
			EquippedWeapon->GetBoundInstanceId() == ServerInventoryFirstId &&
			EquipInventory->GetActiveWeaponInstanceId() == ServerInventoryFirstId;
	}

	// 初始切换阶段要求 CurrentWeapon 离开旧手枪；4C 切枪取消阶段会合法回到旧手枪，
	// 此时不能再被该早期门挡住。
	if (!bClientObservedSwitch ||
		(Weapon == WeaponBeforeSwitch && !bSwitchCancelPhaseTriggered))
	{
		return;
	}

	if (InitialBulletCount == INDEX_NONE)
	{
		InitialBulletCount = Weapon->GetBulletCount();
		// 网络测试验证权威瞄准链路，关闭单次随机散布以保证方向断言可重复。
		if (FFloatProperty* AimVarianceProperty =
			FindFProperty<FFloatProperty>(Weapon->GetClass(), TEXT("AimVariance")))
		{
			AimVarianceProperty->SetPropertyValue_InContainer(Weapon, 0.0f);
		}
		bServerReadyToFire = true;
		ForceNetUpdate();
		return;
	}

	const bool bSingleShotVerified = CurrentBulletCount == InitialBulletCount - 1 &&
		ProjectileSpawnCount == 1;
	const bool bFireReplicationVerified = bClientObservedWeapon &&
		bClientObservedProjectile &&
		bClientObservedOwnerAmmo &&
		bClientObservedNonOwnerAmmoHidden &&
		bServerObservedProjectile &&
		bAimDirectionValid &&
		bSingleShotVerified;

	if (bFireReplicationVerified && !bPartialDamageApplied)
	{
		BulletCountAfterFire = CurrentBulletCount;

		UShooterInventoryComponent* AmmoInventory = Character->GetInventoryComponent();
		const int32 RifleAmmoAfterFire = AmmoInventory
			? AmmoInventory->GetMagazineAmmo(ServerInventoryFirstId)
			: INDEX_NONE;
		const int32 PistolAmmoAfterFire = AmmoInventory
			? AmmoInventory->GetMagazineAmmo(ServerInventorySecondId)
			: INDEX_NONE;
		bAmmoIsolationVerified =
			InitialRifleMagazineAmmo != INDEX_NONE &&
			InitialPistolMagazineAmmo != INDEX_NONE &&
			RifleAmmoAfterFire == InitialRifleMagazineAmmo - 1 &&
			PistolAmmoAfterFire == InitialPistolMagazineAmmo;
		if (!bAmmoIsolationVerified)
		{
			FailTest(FString::Printf(
				TEXT("Ammo isolation invalid; Rifle=%d->%d Pistol=%d->%d"),
				InitialRifleMagazineAmmo,
				RifleAmmoAfterFire,
				InitialPistolMagazineAmmo,
				PistolAmmoAfterFire));
			return;
		}

		bSingleProjectileVerified = bSingleShotVerified;

		if (!bFullAutoPhaseTriggered)
		{
			// ---- 4B FullAutoRelease：单发验证通过后，再要求客户端保持开火至少两发 ----
			bFullAutoPhaseTriggered = true;
			BulletCountBeforeFullAuto = CurrentBulletCount;
			ProjectileCountBeforeFullAuto = ProjectileSpawnCount;
			bServerReadyForFullAuto = true;
			ForceNetUpdate();
			return;
		}
	}

	// 全自动保持期间：服务器必须观察到有且只有一个活动 GA_Fire。
	if (bFullAutoPhaseTriggered && !bClientReportedFullAutoRelease &&
		!bFullAutoActiveObserved)
	{
		if (AShooterPlayerState* ShooterPlayerState =
			Character->GetPlayerState<AShooterPlayerState>())
		{
			UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
				Cast<UShooterAbilitySystemComponent>(Character->GetAbilitySystemComponent());
			bFullAutoActiveObserved =
				ShooterAbilitySystemComponent &&
				ShooterAbilitySystemComponent->GetActiveAbilityCountForClass(
					ShooterPlayerState->GetFireAbilityClass()) == 1;
		}
	}

	// 释放后静默期：0.5 秒内不得再生成弹丸，Inventory Ammo 不得继续下降。
	if (bClientReportedFullAutoRelease && !bFullAutoQuiescentConfirmed)
	{
		if (GetWorld()->GetTimeSeconds() - FullAutoReleaseCheckTime >= 0.5f)
		{
			UShooterInventoryComponent* QuiescenceInventory =
				Character->GetInventoryComponent();
			const int32 RifleAmmoNow = QuiescenceInventory
				? QuiescenceInventory->GetMagazineAmmo(ServerInventoryFirstId)
				: INDEX_NONE;
			bFullAutoQuiescentConfirmed =
				ProjectileSpawnCount == ProjectileCountAfterRelease &&
				RifleAmmoNow == AmmoAfterRelease;
			if (!bFullAutoQuiescentConfirmed)
			{
				FailTest(FString::Printf(
					TEXT("Full-auto release left firing residue; Projectiles=%d->%d Ammo=%d->%d"),
					ProjectileCountAfterRelease,
					ProjectileSpawnCount,
					AmmoAfterRelease,
					RifleAmmoNow));
				return;
			}
		}
	}

	// ---- 4C Cancel.SwitchWeapon：保持步枪开火时切枪，旧 GA_Fire 必须被取消 ----
	if (bFullAutoQuiescentConfirmed && bFullAutoReleaseVerified &&
		!bSwitchCancelPhaseTriggered)
	{
		bSwitchCancelPhaseTriggered = true;
		ProjectileCountBeforeSwitchCancel = ProjectileSpawnCount;
		UShooterInventoryComponent* SwitchCancelInventory =
			Character->GetInventoryComponent();
		RifleAmmoBeforeSwitchCancel = SwitchCancelInventory
			? SwitchCancelInventory->GetMagazineAmmo(ServerInventoryFirstId)
			: INDEX_NONE;
		bServerReadyForSwitchCancel = true;
		ForceNetUpdate();
		return;
	}

	if (bSwitchCancelPhaseTriggered && !bClientReportedSwitchCancel &&
		!bSwitchCancelActiveObserved)
	{
		AShooterPlayerState* ShooterPlayerState =
			Character->GetPlayerState<AShooterPlayerState>();
		UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
			Cast<UShooterAbilitySystemComponent>(Character->GetAbilitySystemComponent());
		bSwitchCancelActiveObserved = ShooterAbilitySystemComponent &&
			ShooterAbilitySystemComponent->GetActiveAbilityCountForClass(
				ShooterPlayerState->GetFireAbilityClass()) == 1;
	}

	if (bClientReportedSwitchCancel && !bSwitchCancelQuiescentConfirmed)
	{
		if (GetWorld()->GetTimeSeconds() - SwitchCancelCheckTime >= 0.5f)
		{
			UShooterInventoryComponent* SwitchCancelInventory =
				Character->GetInventoryComponent();
			const int32 RifleAmmoNow = SwitchCancelInventory
				? SwitchCancelInventory->GetMagazineAmmo(ServerInventoryFirstId)
				: INDEX_NONE;
			AShooterWeapon* CurrentWeaponAfterSwitch =
				Character->GetCurrentWeapon();
			bSwitchCancelQuiescentConfirmed =
				ProjectileSpawnCount == ProjectileCountAfterSwitchCancel &&
				RifleAmmoNow == RifleAmmoAfterSwitchCancel &&
				CurrentWeaponAfterSwitch &&
				CurrentWeaponAfterSwitch->GetBoundInstanceId() ==
					ServerInventorySecondId;
			bSwitchCancelVerified = bSwitchCancelActiveObserved &&
				bClientObservedSwitchCancel &&
				bSwitchCancelQuiescentConfirmed;
			if (!bSwitchCancelVerified)
			{
				FailTest(FString::Printf(
					TEXT("Switch-weapon cancel invalid; ActiveObserved=%s ClientObserved=%s Quiescent=%s Projectiles=%d->%d RifleAmmo=%d->%d Weapon=%s"),
					bSwitchCancelActiveObserved ? TEXT("true") : TEXT("false"),
					bClientObservedSwitchCancel ? TEXT("true") : TEXT("false"),
					bSwitchCancelQuiescentConfirmed ? TEXT("true") : TEXT("false"),
					ProjectileCountAfterSwitchCancel,
					ProjectileSpawnCount,
					RifleAmmoAfterSwitchCancel,
					RifleAmmoNow,
					CurrentWeaponAfterSwitch && CurrentWeaponAfterSwitch->GetBoundInstanceId() == ServerInventorySecondId
						? TEXT("pistol")
						: TEXT("invalid")));
				return;
			}
		}
	}

	// ---- 4C Reject.NoAmmo：手枪弹药耗尽后服务器 TryActivate 必须拒绝 ----
	// ---- 5B Reject.FullMagazine：满弹匣输入不得产生活动事务或 Ammo 变化 ----
	if (bSwitchCancelVerified && !bReloadFullRejectPhaseTriggered)
	{
		bReloadFullRejectPhaseTriggered = true;
		if (!ReplaceInventoryWeaponForReloadTest(
			Character,
			ServerInventorySecondId,
			/*SlotIndex*/1,
			/*MagazineAmmo*/30,
			/*ReserveAmmo*/20))
		{
			FailTest(TEXT("Reload full-magazine preparation failed to replace pistol instance"));
			return;
		}

		UShooterInventoryComponent* ReloadInventory = Character->GetInventoryComponent();
		ReloadMagazineBeforeFullReject = ReloadInventory
			? ReloadInventory->GetMagazineAmmo(ServerInventorySecondId)
			: INDEX_NONE;
		ReloadReserveBeforeFullReject = ReloadInventory
			? ReloadInventory->GetReserveAmmo(ServerInventorySecondId)
			: INDEX_NONE;
		bClientTriggeredReload = false;
		++ReloadInputRequestId;
		ForceNetUpdate();
		return;
	}

	if (bReloadFullRejectPhaseTriggered && bClientTriggeredReload &&
		!bReloadFullRejectVerified)
	{
		if (ReloadFullRejectCheckTime <= 0.0f)
		{
			ReloadFullRejectCheckTime = GetWorld()->GetTimeSeconds();
		}

		if (GetWorld()->GetTimeSeconds() - ReloadFullRejectCheckTime >= 0.5f)
		{
			UShooterInventoryComponent* ReloadInventory =
				Character->GetInventoryComponent();
			UAbilitySystemComponent* AbilitySystemComponent =
				Character->GetAbilitySystemComponent();
			bReloadFullRejectVerified = ReloadInventory &&
				ReloadInventory->GetMagazineAmmo(ServerInventorySecondId) ==
					ReloadMagazineBeforeFullReject &&
				ReloadInventory->GetReserveAmmo(ServerInventorySecondId) ==
					ReloadReserveBeforeFullReject &&
				!HasActiveReloadAbility(Character) &&
				AbilitySystemComponent &&
				!AbilitySystemComponent->HasMatchingGameplayTag(
					ShooterGameplayTags::State_Reloading);
			if (!bReloadFullRejectVerified)
			{
				FailTest(FString::Printf(
					TEXT("Full-magazine reload was not rejected; Mag=%d->%d Reserve=%d->%d ActiveReload=%s Tag=%s"),
					ReloadMagazineBeforeFullReject,
					ReloadInventory ? ReloadInventory->GetMagazineAmmo(ServerInventorySecondId) : INDEX_NONE,
					ReloadReserveBeforeFullReject,
					ReloadInventory ? ReloadInventory->GetReserveAmmo(ServerInventorySecondId) : INDEX_NONE,
					HasActiveReloadAbility(Character) ? TEXT("true") : TEXT("false"),
					AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Reloading)
						? TEXT("true")
						: TEXT("false")));
				return;
			}
		}
	}

	// ---- 5B TransferOnce：一次输入只提交一次 Inventory 原子转移 ----
	if (bReloadFullRejectVerified && !bReloadTransferPhaseTriggered)
	{
		bReloadTransferPhaseTriggered = true;
		UShooterInventoryComponent* ReloadInventory =
			Character->GetInventoryComponent();
		if (!ReloadInventory ||
			!ReloadInventory->ConsumeMagazineAmmo(ServerInventorySecondId, 5))
		{
			FailTest(TEXT("Reload transfer preparation could not consume test ammo"));
			return;
		}

		ReloadMagazineBeforeTransfer =
			ReloadInventory->GetMagazineAmmo(ServerInventorySecondId);
		ReloadReserveBeforeTransfer =
			ReloadInventory->GetReserveAmmo(ServerInventorySecondId);
		AShooterWeapon* ReloadWeapon = ReloadInventory->FindWeaponActor(
			ServerInventorySecondId);
		ExpectedReloadTransfer = ReloadWeapon
			? FMath::Min(
				ReloadWeapon->GetMagazineSize() - ReloadMagazineBeforeTransfer,
				ReloadReserveBeforeTransfer)
			: INDEX_NONE;
		bClientTriggeredReload = false;
		++ReloadInputRequestId;
		ForceNetUpdate();
		return;
	}

	if (bReloadTransferPhaseTriggered && bClientTriggeredReload &&
		!bReloadTransferActiveObserved)
	{
		bReloadTransferActiveObserved = HasActiveReloadAbility(Character);
	}

	if (bReloadTransferPhaseTriggered && bReloadTransferActiveObserved &&
		!bReloadTransferVerified)
	{
		if (ReloadTransferCheckTime <= 0.0f)
		{
			ReloadTransferCheckTime = GetWorld()->GetTimeSeconds();
		}

		if (GetWorld()->GetTimeSeconds() - ReloadTransferCheckTime >= 1.8f)
		{
			UShooterInventoryComponent* ReloadInventory =
				Character->GetInventoryComponent();
			UAbilitySystemComponent* AbilitySystemComponent =
				Character->GetAbilitySystemComponent();
			ReloadMagazineAfterTransfer = ReloadInventory
				? ReloadInventory->GetMagazineAmmo(ServerInventorySecondId)
				: INDEX_NONE;
			ReloadReserveAfterTransfer = ReloadInventory
				? ReloadInventory->GetReserveAmmo(ServerInventorySecondId)
				: INDEX_NONE;
			bReloadTransferVerified = ReloadInventory &&
				ReloadMagazineAfterTransfer ==
					ReloadMagazineBeforeTransfer + ExpectedReloadTransfer &&
				ReloadReserveAfterTransfer ==
					ReloadReserveBeforeTransfer - ExpectedReloadTransfer &&
				ExpectedReloadTransfer == 5 &&
				!HasActiveReloadAbility(Character) &&
				AbilitySystemComponent &&
				!AbilitySystemComponent->HasMatchingGameplayTag(
					ShooterGameplayTags::State_Reloading);
			if (!bReloadTransferVerified)
			{
				FailTest(FString::Printf(
					TEXT("Reload transfer invalid; Mag=%d->%d Reserve=%d->%d Expected=%d ActiveReload=%s Tag=%s"),
					ReloadMagazineBeforeTransfer,
					ReloadMagazineAfterTransfer,
					ReloadReserveBeforeTransfer,
					ReloadReserveAfterTransfer,
					ExpectedReloadTransfer,
					HasActiveReloadAbility(Character) ? TEXT("true") : TEXT("false"),
					AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Reloading)
						? TEXT("true")
						: TEXT("false")));
				return;
			}
		}
	}


	// ---- 5B 弱网 Fire-after-Reload：Reload 完成后客户端只按一次 Fire ----
	// 服务器必须只观察到一次 GA_Fire 激活、一颗弹丸、一发弹药；
	// 该阶段紧跟 Transfer 验证，目标窗口是服务器已结束 Reload、客户端 Tag 可能尚未收敛。
	if (bReloadTransferVerified && !bFireAfterReloadPhaseTriggered)
	{
		bFireAfterReloadPhaseTriggered = true;
		UShooterInventoryComponent* FireReloadInventory =
			Character->GetInventoryComponent();
		FireAfterReloadMagazineBefore = FireReloadInventory
			? FireReloadInventory->GetMagazineAmmo(ServerInventorySecondId)
			: INDEX_NONE;
		FireAfterReloadProjectileBefore = ProjectileSpawnCount;
		bServerReadyForFireAfterReload = true;
		ForceNetUpdate();
		return;
	}

	if (bClientTriggeredFireAfterReload && !bFireAfterReloadActiveObserved)
	{
		if (AShooterPlayerState* ShooterPlayerState =
			Character->GetPlayerState<AShooterPlayerState>())
		{
			UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
				Cast<UShooterAbilitySystemComponent>(Character->GetAbilitySystemComponent());
			bFireAfterReloadActiveObserved = ShooterAbilitySystemComponent &&
				ShooterAbilitySystemComponent->GetActiveAbilityCountForClass(
					ShooterPlayerState->GetFireAbilityClass()) == 1;
		}
	}

	if (bClientTriggeredFireAfterReload && !bFireAfterReloadSingleShotVerified)
	{
		UShooterInventoryComponent* FireReloadInventory =
			Character->GetInventoryComponent();
		const int32 MagazineAfterFire = FireReloadInventory
			? FireReloadInventory->GetMagazineAmmo(ServerInventorySecondId)
			: INDEX_NONE;
		bFireAfterReloadSingleShotVerified = bFireAfterReloadActiveObserved &&
			FireAfterReloadMagazineBefore != INDEX_NONE &&
			MagazineAfterFire == FireAfterReloadMagazineBefore - 1 &&
			ProjectileSpawnCount == FireAfterReloadProjectileBefore + 1;
		if (bFireAfterReloadSingleShotVerified)
		{
			bServerReadyForStopFireAfterReload = true;
			ForceNetUpdate();
			return;
		}
	}

	if (bClientTriggeredStopFireAfterReload && !bFireAfterReloadQuiescentVerified)
	{
		if (FireAfterReloadQuiescenceCheckTime <= 0.0f)
		{
			FireAfterReloadQuiescenceCheckTime = GetWorld()->GetTimeSeconds();
		}

		if (GetWorld()->GetTimeSeconds() - FireAfterReloadQuiescenceCheckTime >= 0.5f)
		{
			UShooterInventoryComponent* FireReloadInventory =
				Character->GetInventoryComponent();
			UAbilitySystemComponent* AbilitySystemComponent =
				Character->GetAbilitySystemComponent();
			bFireAfterReloadQuiescentVerified = FireReloadInventory &&
				FireReloadInventory->GetMagazineAmmo(ServerInventorySecondId) ==
					FireAfterReloadMagazineBefore - 1 &&
				ProjectileSpawnCount == FireAfterReloadProjectileBefore + 1 &&
				AbilitySystemComponent &&
				!AbilitySystemComponent->HasMatchingGameplayTag(
					ShooterGameplayTags::State_Firing) &&
				!HasActiveFireAbility(Character);
			if (!bFireAfterReloadQuiescentVerified)
			{
				FailTest(FString::Printf(
					TEXT("Fire-after-reload quiescence invalid; Mag=%d->%d Projectiles=%d->%d ActiveFire=%s FiringTag=%s"),
					FireAfterReloadMagazineBefore,
					FireReloadInventory ? FireReloadInventory->GetMagazineAmmo(ServerInventorySecondId) : INDEX_NONE,
					FireAfterReloadProjectileBefore,
					ProjectileSpawnCount,
					HasActiveFireAbility(Character) ? TEXT("true") : TEXT("false"),
					AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Firing)
						? TEXT("true")
						: TEXT("false")));
				return;
			}
		}
	}

	// ---- 5B Cancel.Equip：换弹等待中走旧切枪路径，提交前取消且 Ammo 不变 ----
	if (bReloadTransferVerified && bFireAfterReloadQuiescentVerified &&
		!bReloadCancelEquipPhaseTriggered)
	{
		bReloadCancelEquipPhaseTriggered = true;
		UShooterInventoryComponent* ReloadInventory =
			Character->GetInventoryComponent();
		if (!ReloadInventory ||
			!ReloadInventory->ConsumeMagazineAmmo(ServerInventorySecondId, 5))
		{
			FailTest(TEXT("Reload equip-cancel preparation could not consume test ammo"));
			return;
		}

		ReloadMagazineBeforeCancelEquip =
			ReloadInventory->GetMagazineAmmo(ServerInventorySecondId);
		ReloadReserveBeforeCancelEquip =
			ReloadInventory->GetReserveAmmo(ServerInventorySecondId);
		bClientTriggeredReload = false;
		++ReloadInputRequestId;
		ForceNetUpdate();
		return;
	}

	if (bReloadCancelEquipPhaseTriggered && bClientTriggeredReload &&
		!bReloadCancelEquipActiveObserved)
	{
		bReloadCancelEquipActiveObserved = HasActiveReloadAbility(Character);
	}

	if (bReloadCancelEquipActiveObserved && !bClientTriggeredReloadSwitch)
	{
		bServerReadyForReloadSwitch = true;
		ForceNetUpdate();
		return;
	}

	if (bClientTriggeredReloadSwitch && !bEquipCancelReloadActiveObserved)
	{
		bEquipCancelReloadActiveObserved = HasActiveEquipAbility(Character);
	}

	if (bClientTriggeredReloadSwitch && !bReloadCancelEquipVerified)
	{
		// GA_Equip 提交有 0.5s 权威延迟：先等 CurrentWeapon 收敛到步枪再判定。
		UShooterInventoryComponent* ReloadInventory =
			Character->GetInventoryComponent();
		AShooterWeapon* CurrentWeapon = Character->GetCurrentWeapon();
		UAbilitySystemComponent* AbilitySystemComponent =
			Character->GetAbilitySystemComponent();
		bReloadCancelEquipVerified = ReloadInventory &&
			CurrentWeapon &&
			CurrentWeapon->GetBoundInstanceId() == ServerInventoryFirstId &&
			ReloadInventory->GetMagazineAmmo(ServerInventorySecondId) ==
				ReloadMagazineBeforeCancelEquip &&
			ReloadInventory->GetReserveAmmo(ServerInventorySecondId) ==
				ReloadReserveBeforeCancelEquip &&
			!HasActiveReloadAbility(Character) &&
			AbilitySystemComponent &&
			!AbilitySystemComponent->HasMatchingGameplayTag(
				ShooterGameplayTags::State_Reloading);
		if (!bReloadCancelEquipVerified)
		{
			if (ReloadCancelEquipCheckTime <= 0.0f)
			{
				ReloadCancelEquipCheckTime = GetWorld()->GetTimeSeconds();
			}
			if (GetWorld()->GetTimeSeconds() - ReloadCancelEquipCheckTime >= 2.0f)
			{
				FailTest(FString::Printf(
					TEXT("Reload equip cancel invalid; Weapon=%s Mag=%d->%d Reserve=%d->%d ActiveReload=%s Tag=%s"),
					*GetNameSafe(CurrentWeapon),
					ReloadMagazineBeforeCancelEquip,
					ReloadInventory ? ReloadInventory->GetMagazineAmmo(ServerInventorySecondId) : INDEX_NONE,
					ReloadReserveBeforeCancelEquip,
					ReloadInventory ? ReloadInventory->GetReserveAmmo(ServerInventorySecondId) : INDEX_NONE,
					HasActiveReloadAbility(Character) ? TEXT("true") : TEXT("false"),
					AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Reloading)
						? TEXT("true")
						: TEXT("false")));
				return;
			}
		}
	}

	// 切回手枪，保持后续 NoAmmo / Death 验证沿用 ServerInventorySecondId。
	if (bReloadCancelEquipVerified && !bReloadSwitchBackPhaseTriggered)
	{
		bReloadSwitchBackPhaseTriggered = true;
		bServerReadyForReloadSwitchBack = true;
		ForceNetUpdate();
		return;
	}

	if (bClientTriggeredReloadSwitchBack && !bEquipSwitchBackActiveObserved)
	{
		bEquipSwitchBackActiveObserved = HasActiveEquipAbility(Character);
	}

	if (bClientTriggeredReloadSwitchBack && !bReloadSwitchBackVerified)
	{
		// 同样等待 GA_Equip 提交完成后再验证 CurrentWeapon 回到手枪。
		AShooterWeapon* CurrentWeapon = Character->GetCurrentWeapon();
		bReloadSwitchBackVerified = CurrentWeapon &&
			CurrentWeapon->GetBoundInstanceId() == ServerInventorySecondId &&
			!HasActiveReloadAbility(Character);
		if (!bReloadSwitchBackVerified)
		{
			if (ReloadSwitchBackCheckTime <= 0.0f)
			{
				ReloadSwitchBackCheckTime = GetWorld()->GetTimeSeconds();
			}
			if (GetWorld()->GetTimeSeconds() - ReloadSwitchBackCheckTime >= 2.0f)
			{
				FailTest(FString::Printf(
					TEXT("Reload equip-cancel switch back invalid; Weapon=%s ActiveReload=%s"),
					*GetNameSafe(CurrentWeapon),
					HasActiveReloadAbility(Character) ? TEXT("true") : TEXT("false")));
				return;
			}
		}
	}


	// ---- 5C Reject.SingleWeapon：只保留手枪后客户端切枪，GA_Equip 必须在服务器拒绝 ----
	if (bReloadSwitchBackVerified && !bEquipSingleRejectPhaseTriggered)
	{
		bEquipSingleRejectPhaseTriggered = true;
		UShooterInventoryComponent* EquipRejectInventory =
			Character->GetInventoryComponent();
		if (!EquipRejectInventory ||
			!EquipRejectInventory->RemoveWeaponInstance(ServerInventoryFirstId) ||
			EquipRejectInventory->GetWeaponCount() != 1 ||
			EquipRejectInventory->GetActiveWeaponInstanceId() != ServerInventorySecondId)
		{
			FailTest(TEXT("Equip single-weapon preparation failed to keep only pistol"));
			return;
		}

		bClientTriggeredEquipSingleReject = false;
		bServerReadyForEquipSingleReject = true;
		ForceNetUpdate();
		return;
	}

	if (bClientTriggeredEquipSingleReject && !bEquipSingleRejectVerified)
	{
		if (EquipSingleRejectCheckTime <= 0.0f)
		{
			EquipSingleRejectCheckTime = GetWorld()->GetTimeSeconds();
		}

		if (GetWorld()->GetTimeSeconds() - EquipSingleRejectCheckTime >= 0.5f)
		{
			UShooterInventoryComponent* EquipRejectInventory =
				Character->GetInventoryComponent();
			AShooterWeapon* CurrentWeapon = Character->GetCurrentWeapon();
			UAbilitySystemComponent* AbilitySystemComponent =
				Character->GetAbilitySystemComponent();
			bEquipSingleRejectVerified = EquipRejectInventory &&
				EquipRejectInventory->GetWeaponCount() == 1 &&
				EquipRejectInventory->GetActiveWeaponInstanceId() == ServerInventorySecondId &&
				CurrentWeapon &&
				CurrentWeapon->GetBoundInstanceId() == ServerInventorySecondId &&
				!HasActiveEquipAbility(Character) &&
				AbilitySystemComponent &&
				!AbilitySystemComponent->HasMatchingGameplayTag(
					ShooterGameplayTags::State_Equipping);
			if (!bEquipSingleRejectVerified)
			{
				FailTest(FString::Printf(
					TEXT("Single-weapon equip reject invalid; Count=%d Active=%s Weapon=%s ActiveEquip=%s Tag=%s"),
					EquipRejectInventory ? EquipRejectInventory->GetWeaponCount() : INDEX_NONE,
					EquipRejectInventory ? *EquipRejectInventory->GetActiveWeaponInstanceId().ToString() : TEXT("null"),
					*GetNameSafe(CurrentWeapon),
					HasActiveEquipAbility(Character) ? TEXT("true") : TEXT("false"),
					AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Equipping)
						? TEXT("true")
						: TEXT("false")));
				return;
			}
		}
	}

	// ---- 5B Reject.NoReserve：备用弹药为零时输入不得产生活动事务 ----
	if (bReloadSwitchBackVerified && bEquipSingleRejectVerified && !bReloadNoReservePhaseTriggered)
	{
		bReloadNoReservePhaseTriggered = true;
		if (!ReplaceInventoryWeaponForReloadTest(
			Character,
			ServerInventorySecondId,
			/*SlotIndex*/1,
			/*MagazineAmmo*/5,
			/*ReserveAmmo*/0))
		{
			FailTest(TEXT("Reload no-reserve preparation failed to replace pistol instance"));
			return;
		}

		UShooterInventoryComponent* ReloadInventory =
			Character->GetInventoryComponent();
		ReloadMagazineBeforeNoReserve =
			ReloadInventory->GetMagazineAmmo(ServerInventorySecondId);
		ReloadReserveBeforeNoReserve =
			ReloadInventory->GetReserveAmmo(ServerInventorySecondId);
		bClientTriggeredReload = false;
		++ReloadInputRequestId;
		ForceNetUpdate();
		return;
	}

	if (bReloadNoReservePhaseTriggered && bClientTriggeredReload &&
		!bReloadNoReserveVerified)
	{
		if (ReloadNoReserveCheckTime <= 0.0f)
		{
			ReloadNoReserveCheckTime = GetWorld()->GetTimeSeconds();
		}

		if (GetWorld()->GetTimeSeconds() - ReloadNoReserveCheckTime >= 0.5f)
		{
			UShooterInventoryComponent* ReloadInventory =
				Character->GetInventoryComponent();
			UAbilitySystemComponent* AbilitySystemComponent =
				Character->GetAbilitySystemComponent();
			bReloadNoReserveVerified = ReloadInventory &&
				ReloadInventory->GetMagazineAmmo(ServerInventorySecondId) ==
					ReloadMagazineBeforeNoReserve &&
				ReloadInventory->GetReserveAmmo(ServerInventorySecondId) ==
					ReloadReserveBeforeNoReserve &&
				!HasActiveReloadAbility(Character) &&
				AbilitySystemComponent &&
				!AbilitySystemComponent->HasMatchingGameplayTag(
					ShooterGameplayTags::State_Reloading);
			if (!bReloadNoReserveVerified)
			{
				FailTest(FString::Printf(
					TEXT("No-reserve reload was not rejected; Mag=%d->%d Reserve=%d->%d ActiveReload=%s Tag=%s"),
					ReloadMagazineBeforeNoReserve,
					ReloadInventory ? ReloadInventory->GetMagazineAmmo(ServerInventorySecondId) : INDEX_NONE,
					ReloadReserveBeforeNoReserve,
					ReloadInventory ? ReloadInventory->GetReserveAmmo(ServerInventorySecondId) : INDEX_NONE,
					HasActiveReloadAbility(Character) ? TEXT("true") : TEXT("false"),
					AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Reloading)
						? TEXT("true")
						: TEXT("false")));
				return;
			}
		}
	}

	// ---- 5B Cancel.Death 前置：在部分伤害前制造一个仍在等待的 Reload 事务 ----
	if (bNoAmmoRejectVerified && !bReloadCancelDeathPhaseTriggered)
	{
		bReloadCancelDeathPhaseTriggered = true;
		if (!ReplaceInventoryWeaponForReloadTest(
			Character,
			ServerInventorySecondId,
			/*SlotIndex*/1,
			/*MagazineAmmo*/5,
			/*ReserveAmmo*/20))
		{
			FailTest(TEXT("Reload death-cancel preparation failed to replace pistol instance"));
			return;
		}

		UShooterInventoryComponent* ReloadInventory =
			Character->GetInventoryComponent();
		ReloadMagazineBeforeCancelDeath =
			ReloadInventory->GetMagazineAmmo(ServerInventorySecondId);
		ReloadReserveBeforeCancelDeath =
			ReloadInventory->GetReserveAmmo(ServerInventorySecondId);
		bClientTriggeredReload = false;
		++ReloadInputRequestId;
		ForceNetUpdate();
		return;
	}

	if (bReloadCancelDeathPhaseTriggered && bClientTriggeredReload &&
		!bReloadCancelDeathActiveObserved)
	{
		bReloadCancelDeathActiveObserved = HasActiveReloadAbility(Character);
	}
	if (bSwitchCancelVerified && bReloadNoReserveVerified && !bNoAmmoRejectVerified)
	{
		UShooterInventoryComponent* NoAmmoInventory =
			Character->GetInventoryComponent();
		const int32 PistolAmmoBefore = NoAmmoInventory
			? NoAmmoInventory->GetMagazineAmmo(ServerInventorySecondId)
			: INDEX_NONE;
		if (NoAmmoInventory && PistolAmmoBefore > 0)
		{
			NoAmmoInventory->ConsumeMagazineAmmo(
				ServerInventorySecondId,
				PistolAmmoBefore);
		}

		UAbilitySystemComponent* AbilitySystemComponent =
			Character->GetAbilitySystemComponent();
		UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
			Cast<UShooterAbilitySystemComponent>(AbilitySystemComponent);
		const int32 ProjectilesBeforeReject = ProjectileSpawnCount;
		const bool bActivated = AbilitySystemComponent
			? AbilitySystemComponent->TryActivateAbility(ServerFireAbilityHandle)
			: false;
		bNoAmmoRejectVerified = !bActivated &&
			ProjectileSpawnCount == ProjectilesBeforeReject &&
			ShooterAbilitySystemComponent &&
			ShooterAbilitySystemComponent->GetActiveAbilityCountForClass(
				Character->GetPlayerState<AShooterPlayerState>()->GetFireAbilityClass()) == 0 &&
			NoAmmoInventory &&
			NoAmmoInventory->GetMagazineAmmo(ServerInventorySecondId) == 0;
		if (!bNoAmmoRejectVerified)
		{
			FailTest(TEXT("No-ammo activation was not rejected without projectile"));
			return;
		}
	}
	} // if (Weapon)

	if (bFullAutoQuiescentConfirmed && bFullAutoReleaseVerified &&
		bSwitchCancelVerified && bNoAmmoRejectVerified &&
		bReloadCancelDeathActiveObserved && !bPartialDamageApplied)
	{
		InitialHP = Character->GetCurrentHP();
		// GAS：记录伤害前属性生命，随后验证部分伤害恰好只应用一次。
		if (UAbilitySystemComponent* AbilitySystemComponent = Character->GetAbilitySystemComponent())
		{
			InitialAttributeHealth = AbilitySystemComponent->GetNumericAttribute(
				UShooterAttributeSet::GetHealthAttribute());
			ExpectedPartialHealth = InitialAttributeHealth -
				FMath::Max(1.0f, Character->GetMaxHP() * 0.25f);
		}
		UGameplayStatics::ApplyDamage(
			Character,
			FMath::Max(1.0f, Character->GetMaxHP() * 0.25f),
			nullptr,
			this,
			nullptr);
		bPartialDamageApplied = true;
		return;
	}

	if (bPartialDamageApplied && !bServerGasDamageChecked)
	{
		bServerGasDamageChecked = true;
		if (UAbilitySystemComponent* AbilitySystemComponent = Character->GetAbilitySystemComponent())
		{
			const float HealthAfterPartial = AbilitySystemComponent->GetNumericAttribute(
				UShooterAttributeSet::GetHealthAttribute());
			bServerGasDamageOk = HealthAfterPartial < InitialAttributeHealth &&
				FMath::IsNearlyEqual(HealthAfterPartial, ExpectedPartialHealth, 0.01f);
			if (!bServerGasDamageOk)
			{
				FailTest(FString::Printf(
					TEXT("Server-side GAS partial damage invalid; Before=%.1f Expected=%.1f After=%.1f"),
					InitialAttributeHealth,
					ExpectedPartialHealth,
					HealthAfterPartial));
				return;
			}
		}
	}

	if (bPartialDamageApplied && bClientObservedDamage && !bLethalDamageApplied)
	{
		// 致死伤害前确认 Reload 仍在提交窗口且 Ammo 未变化；随后 Die() 必须取消事务。
		UShooterInventoryComponent* DeathReloadInventory =
			Character->GetInventoryComponent();
		bReloadCancelDeathAmmoUnchanged = DeathReloadInventory &&
			DeathReloadInventory->GetMagazineAmmo(ServerInventorySecondId) ==
				ReloadMagazineBeforeCancelDeath &&
			DeathReloadInventory->GetReserveAmmo(ServerInventorySecondId) ==
				ReloadReserveBeforeCancelDeath &&
			HasActiveReloadAbility(Character);
		if (!bReloadCancelDeathAmmoUnchanged)
		{
			FailTest(FString::Printf(
				TEXT("Reload death-cancel precondition invalid; Mag=%d->%d Reserve=%d->%d ActiveReload=%s"),
				ReloadMagazineBeforeCancelDeath,
				DeathReloadInventory ? DeathReloadInventory->GetMagazineAmmo(ServerInventorySecondId) : INDEX_NONE,
				ReloadReserveBeforeCancelDeath,
				DeathReloadInventory ? DeathReloadInventory->GetReserveAmmo(ServerInventorySecondId) : INDEX_NONE,
				HasActiveReloadAbility(Character) ? TEXT("true") : TEXT("false")));
			return;
		}

		AController* OpponentController = GetOpponentController();
		if (!OpponentController)
		{
			return;
		}

		CharacterBeforeDeath = Character;
		UGameplayStatics::ApplyDamage(
			Character,
			Character->GetMaxHP() * 2.0f,
			OpponentController,
			this,
			nullptr);
		bLethalDamageApplied = true;
		return;
	}

	if (bLethalDamageApplied && !bServerGasDeathChecked && Character->IsDead())
	{
		bServerGasDeathChecked = true;
		if (UAbilitySystemComponent* AbilitySystemComponent = Character->GetAbilitySystemComponent())
		{
			const float HealthAfterLethal = AbilitySystemComponent->GetNumericAttribute(
				UShooterAttributeSet::GetHealthAttribute());
			bServerGasDeathOk = HealthAfterLethal <= 0.0f;
		}
		if (!bServerGasDeathOk)
		{
			FailTest(TEXT("Server-side GAS lethal damage did not reduce Health to zero"));
			return;
		}

		UShooterInventoryComponent* DeathInventory = Character->GetInventoryComponent();
		bServerDeathInventoryCleared = DeathInventory &&
			DeathInventory->GetWeaponCount() == 0 &&
			!DeathInventory->GetActiveWeaponInstanceId().IsValid() &&
			Character->GetCurrentWeapon() == nullptr;
		if (!bServerDeathInventoryCleared)
		{
			FailTest(FString::Printf(
				TEXT("Server Inventory death clear invalid; Inventory=%s Count=%d Active=%s CurrentWeapon=%s"),
				*GetNameSafe(DeathInventory),
				DeathInventory ? DeathInventory->GetWeaponCount() : INDEX_NONE,
				DeathInventory ? *DeathInventory->GetActiveWeaponInstanceId().ToString() : TEXT("null"),
				*GetNameSafe(Character->GetCurrentWeapon())));
			return;
		}

		// ---- 5B Cancel.Death：Die() 必须取消活动 GA_Reload 且不留下 State.Reloading ----
		if (!bReloadCancelDeathVerified)
		{
			UAbilitySystemComponent* AbilitySystemComponent =
				Character->GetAbilitySystemComponent();
			bReloadCancelDeathVerified = !HasActiveReloadAbility(Character) &&
				AbilitySystemComponent &&
				!AbilitySystemComponent->HasMatchingGameplayTag(
					ShooterGameplayTags::State_Reloading);
			if (!bReloadCancelDeathVerified)
			{
				FailTest(FString::Printf(
					TEXT("Death did not cancel active reload; ActiveReload=%s Tag=%s"),
					HasActiveReloadAbility(Character) ? TEXT("true") : TEXT("false"),
					AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Reloading)
						? TEXT("true")
						: TEXT("false")));
				return;
			}
		}

		// ---- 4C Reject.Dead：State.Dead 已设置，死亡角色不能再次激活 GA_Fire ----
		if (!bFireRejectDeadVerified)
		{
			UAbilitySystemComponent* AbilitySystemComponent =
				Character->GetAbilitySystemComponent();
			UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
				Cast<UShooterAbilitySystemComponent>(AbilitySystemComponent);
			const bool bActivated = AbilitySystemComponent
				? AbilitySystemComponent->TryActivateAbility(ServerFireAbilityHandle)
				: false;
			bFireRejectDeadVerified = !bActivated &&
				AbilitySystemComponent &&
				AbilitySystemComponent->HasMatchingGameplayTag(
					ShooterGameplayTags::State_Dead) &&
				ShooterAbilitySystemComponent &&
				ShooterAbilitySystemComponent->GetActiveAbilityCountForClass(
					Character->GetPlayerState<AShooterPlayerState>()->GetFireAbilityClass()) == 0;
			if (!bFireRejectDeadVerified)
			{
				FailTest(FString::Printf(
					TEXT("Dead activation reject invalid; Activated=%s DeadTag=%s"),
					bActivated ? TEXT("true") : TEXT("false"),
					AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Dead)
						? TEXT("true")
						: TEXT("false")));
				return;
			}
		}

		// ---- 5C Reject.Dead：死亡角色不能激活 GA_Equip ----
		if (!bEquipRejectDeadVerified)
		{
			UAbilitySystemComponent* AbilitySystemComponent =
				Character->GetAbilitySystemComponent();
			UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
				Cast<UShooterAbilitySystemComponent>(AbilitySystemComponent);
			const bool bActivated = AbilitySystemComponent
				? AbilitySystemComponent->TryActivateAbility(ServerEquipAbilityHandle)
				: false;
			bEquipRejectDeadVerified = !bActivated &&
				ShooterAbilitySystemComponent &&
				ShooterAbilitySystemComponent->GetActiveAbilityCountForClass(
					Character->GetPlayerState<AShooterPlayerState>()->GetEquipAbilityClass()) == 0 &&
				AbilitySystemComponent &&
				!AbilitySystemComponent->HasMatchingGameplayTag(
					ShooterGameplayTags::State_Equipping);
			if (!bEquipRejectDeadVerified)
			{
				FailTest(FString::Printf(
					TEXT("Dead equip activation reject invalid; Activated=%s ActiveEquip=%s EquippingTag=%s"),
					bActivated ? TEXT("true") : TEXT("false"),
					HasActiveEquipAbility(Character) ? TEXT("true") : TEXT("false"),
					AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Equipping)
						? TEXT("true")
						: TEXT("false")));
				return;
			}
		}
	}

	if (GetNetMode() == NM_ListenServer && bLethalDamageApplied &&
		Character->IsDead() && !bOpponentKilledForStats)
	{
		APlayerController* OwnerController = Cast<APlayerController>(GetOwner());
		const AShooterPlayerState* OwnerState = OwnerController
			? OwnerController->GetPlayerState<AShooterPlayerState>()
			: nullptr;
		if (OwnerState && OwnerState->GetKills() >= 1)
		{
			bOpponentKilledForStats = true;
		}
		else if (OwnerState)
		{
			AController* OpponentController = GetOpponentController();
			AShooterCharacter* OpponentCharacter = OpponentController
				? Cast<AShooterCharacter>(OpponentController->GetPawn())
				: nullptr;
			if (OpponentCharacter && !OpponentCharacter->IsDead())
			{
				UGameplayStatics::ApplyDamage(
					OpponentCharacter,
					OpponentCharacter->GetMaxHP() * 2.0f,
					OwnerController,
					this,
					nullptr);
				bOpponentKilledForStats = OwnerState->GetKills() >= 1;
			}
		}
	}

	const APlayerController* PlayerController = Cast<APlayerController>(GetOwner());
	const bool bServerObservedRespawn = bLethalDamageApplied &&
		Character != CharacterBeforeDeath.Get() &&
		!Character->IsDead() &&
		Character->GetCurrentHP() > 0.0f &&
		Character->GetCharacterMovement()->MovementMode != MOVE_None &&
		PlayerController && PlayerController->GetPawn() == Character &&
		Character->GetController() == PlayerController;

	// ---- GAS ASC 重生生命周期（服务器视角）：ASC 本体与 Owner 不变，Avatar 切换到新角色 ----
	if (bServerObservedRespawn && bLethalDamageApplied && !bServerGasRespawnChecked)
	{
		bServerGasRespawnChecked = true;

		AShooterPlayerState* ShooterPlayerState = PlayerController
			? PlayerController->GetPlayerState<AShooterPlayerState>()
			: nullptr;
		UAbilitySystemComponent* AbilitySystemComponent = ShooterPlayerState
			? ShooterPlayerState->GetAbilitySystemComponent()
			: nullptr;
		// 新 Pawn 满血：初始化效果在 PossessedBy 时同步应用。
		float RespawnMaxHealth = 0.0f;
		float RespawnHealth = 0.0f;
		if (AbilitySystemComponent)
		{
			RespawnMaxHealth = AbilitySystemComponent->GetNumericAttribute(
				UShooterAttributeSet::GetMaxHealthAttribute());
			RespawnHealth = AbilitySystemComponent->GetNumericAttribute(
				UShooterAttributeSet::GetHealthAttribute());
		}
		bServerGasRespawnOk = AbilitySystemComponent &&
			AbilitySystemComponent == ObservedAbilitySystemComponent.Get() &&
			AbilitySystemComponent->GetOwnerActor() == ShooterPlayerState &&
			AbilitySystemComponent->GetAvatarActor() == Character &&
			Character != CharacterBeforeDeath.Get() &&
			RespawnMaxHealth > 0.0f &&
			FMath::IsNearlyEqual(RespawnHealth, RespawnMaxHealth, 0.01f);

		// ---- 4A 重生不重复授予：同一个 PlayerState ASC / Spec Handle 在新 Avatar 上继续存在 ----
		const FGameplayAbilitySpec* RespawnFireAbilitySpec =
			AbilitySystemComponent
				? AbilitySystemComponent->FindAbilitySpecFromClass(
					ShooterPlayerState->GetFireAbilityClass())
				: nullptr;
		bServerFireRespawnGrantOk = RespawnFireAbilitySpec &&
			RespawnFireAbilitySpec->Handle == ServerFireAbilityHandle &&
			RespawnFireAbilitySpec->Ability &&
			RespawnFireAbilitySpec->Ability->GetClass() ==
				ShooterPlayerState->GetFireAbilityClass() &&
			ShooterPlayerState->GetFireAbilitySpecCount() == 1;

		// ---- 5A Reload / Equip 重生不重复授予：原 Spec Handle 在新 Avatar 上继续存在 ----
		const FGameplayAbilitySpec* RespawnReloadAbilitySpec =
			AbilitySystemComponent
				? AbilitySystemComponent->FindAbilitySpecFromClass(
					ShooterPlayerState->GetReloadAbilityClass())
				: nullptr;
		const FGameplayAbilitySpec* RespawnEquipAbilitySpec =
			AbilitySystemComponent
				? AbilitySystemComponent->FindAbilitySpecFromClass(
					ShooterPlayerState->GetEquipAbilityClass())
				: nullptr;
		bServerReloadEquipRespawnGrantOk = RespawnReloadAbilitySpec &&
			RespawnEquipAbilitySpec &&
			RespawnReloadAbilitySpec->Handle == ServerReloadAbilityHandle &&
			RespawnEquipAbilitySpec->Handle == ServerEquipAbilityHandle &&
			RespawnReloadAbilitySpec->Ability &&
			RespawnReloadAbilitySpec->Ability->GetClass() ==
				ShooterPlayerState->GetReloadAbilityClass() &&
			RespawnEquipAbilitySpec->Ability &&
			RespawnEquipAbilitySpec->Ability->GetClass() ==
				ShooterPlayerState->GetEquipAbilityClass() &&
			ShooterPlayerState->GetReloadAbilitySpecCount() == 1 &&
			ShooterPlayerState->GetEquipAbilitySpecCount() == 1;

		UShooterInventoryComponent* RespawnInventory = Character->GetInventoryComponent();
		bServerRespawnInventoryEmpty = RespawnInventory &&
			RespawnInventory->GetWeaponCount() == 0 &&
			!RespawnInventory->GetActiveWeaponInstanceId().IsValid() &&
			Character->GetCurrentWeapon() == nullptr;

		// ---- 4C / 5B / 5C 重生 Tag 清理：Dead / Firing / Reloading / Equipping 与活动 Ability 不得跨生命保留 ----
		UShooterAbilitySystemComponent* RespawnShooterAbilitySystemComponent =
			Cast<UShooterAbilitySystemComponent>(AbilitySystemComponent);
		bRespawnTagCleanupVerified = AbilitySystemComponent &&
			!AbilitySystemComponent->HasMatchingGameplayTag(
				ShooterGameplayTags::State_Dead) &&
			!AbilitySystemComponent->HasMatchingGameplayTag(
				ShooterGameplayTags::State_Firing) &&
			!AbilitySystemComponent->HasMatchingGameplayTag(
				ShooterGameplayTags::State_Reloading) &&
			!AbilitySystemComponent->HasMatchingGameplayTag(
				ShooterGameplayTags::State_Equipping) &&
			RespawnShooterAbilitySystemComponent &&
			RespawnShooterAbilitySystemComponent->GetActiveAbilityCountForClass(
				ShooterPlayerState->GetFireAbilityClass()) == 0 &&
			RespawnShooterAbilitySystemComponent->GetActiveAbilityCountForClass(
				ShooterPlayerState->GetReloadAbilityClass()) == 0 &&
			RespawnShooterAbilitySystemComponent->GetActiveAbilityCountForClass(
				ShooterPlayerState->GetEquipAbilityClass()) == 0;

		// ---- 4C Reject.NoWeapon：重生 Inventory 为空，激活必须被拒绝 ----
		if (bServerGasRespawnOk && bServerRespawnInventoryEmpty &&
			!bFireRejectNoWeaponVerified)
		{
			const bool bActivated = AbilitySystemComponent
				? AbilitySystemComponent->TryActivateAbility(ServerFireAbilityHandle)
				: false;
			bFireRejectNoWeaponVerified = !bActivated &&
				RespawnShooterAbilitySystemComponent &&
				RespawnShooterAbilitySystemComponent->GetActiveAbilityCountForClass(
					ShooterPlayerState->GetFireAbilityClass()) == 0;
			if (!bFireRejectNoWeaponVerified)
			{
				FailTest(TEXT("No-weapon activation after respawn was not rejected"));
				return;
			}
		}

		if (!bServerGasRespawnOk || !bServerRespawnInventoryEmpty ||
			!bServerFireRespawnGrantOk || !bServerReloadEquipRespawnGrantOk ||
			!bRespawnTagCleanupVerified)
		{
			if (!bRespawnTagCleanupVerified)
			{
				FailTest(FString::Printf(
					TEXT("Respawn ability tag cleanup invalid; DeadTag=%s FiringTag=%s ReloadingTag=%s EquippingTag=%s ActiveFire=%d ActiveReload=%d ActiveEquip=%d"),
					AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Dead)
						? TEXT("true")
						: TEXT("false"),
					AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Firing)
						? TEXT("true")
						: TEXT("false"),
					AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Reloading)
						? TEXT("true")
						: TEXT("false"),
					AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Equipping)
						? TEXT("true")
						: TEXT("false"),
					RespawnShooterAbilitySystemComponent
						? RespawnShooterAbilitySystemComponent->GetActiveAbilityCountForClass(ShooterPlayerState->GetFireAbilityClass())
						: INDEX_NONE,
					RespawnShooterAbilitySystemComponent
						? RespawnShooterAbilitySystemComponent->GetActiveAbilityCountForClass(ShooterPlayerState->GetReloadAbilityClass())
						: INDEX_NONE,
					RespawnShooterAbilitySystemComponent
						? RespawnShooterAbilitySystemComponent->GetActiveAbilityCountForClass(ShooterPlayerState->GetEquipAbilityClass())
						: INDEX_NONE));
				return;
			}

			if (!bServerFireRespawnGrantOk)
			{
				FailTest(FString::Printf(
					TEXT("Server respawn Fire Ability grant invalid; Count=%d Handle=%s Expected=%s Ability=%s"),
					ShooterPlayerState->GetFireAbilitySpecCount(),
					RespawnFireAbilitySpec ? *RespawnFireAbilitySpec->Handle.ToString() : TEXT("null"),
					*ServerFireAbilityHandle.ToString(),
					RespawnFireAbilitySpec && RespawnFireAbilitySpec->Ability
						? *GetNameSafe(RespawnFireAbilitySpec->Ability->GetClass())
						: TEXT("null")));
				return;
			}

			if (!bServerReloadEquipRespawnGrantOk)
			{
				FailTest(FString::Printf(
					TEXT("Server respawn Reload/Equip Ability grant invalid; ReloadCount=%d EquipCount=%d ReloadHandle=%s/%s EquipHandle=%s/%s ReloadAbility=%s EquipAbility=%s"),
					ShooterPlayerState->GetReloadAbilitySpecCount(),
					ShooterPlayerState->GetEquipAbilitySpecCount(),
					RespawnReloadAbilitySpec ? *RespawnReloadAbilitySpec->Handle.ToString() : TEXT("null"),
					*ServerReloadAbilityHandle.ToString(),
					RespawnEquipAbilitySpec ? *RespawnEquipAbilitySpec->Handle.ToString() : TEXT("null"),
					*ServerEquipAbilityHandle.ToString(),
					RespawnReloadAbilitySpec && RespawnReloadAbilitySpec->Ability
						? *GetNameSafe(RespawnReloadAbilitySpec->Ability->GetClass())
						: TEXT("null"),
					RespawnEquipAbilitySpec && RespawnEquipAbilitySpec->Ability
						? *GetNameSafe(RespawnEquipAbilitySpec->Ability->GetClass())
						: TEXT("null")));
				return;
			}

			if (!bServerGasRespawnOk)
			{
			FailTest(FString::Printf(
				TEXT("Server-side GAS ASC respawn avatar invalid; ASC=%s SameASC=%s OwnerOk=%s Avatar=%s OldAvatar=%s MaxHealth=%.1f Health=%.1f"),
				*GetNameSafe(AbilitySystemComponent),
				AbilitySystemComponent == ObservedAbilitySystemComponent.Get() ? TEXT("true") : TEXT("false"),
				AbilitySystemComponent && AbilitySystemComponent->GetOwnerActor() == ShooterPlayerState ? TEXT("true") : TEXT("false"),
				*GetNameSafe(AbilitySystemComponent ? AbilitySystemComponent->GetAvatarActor() : nullptr),
				*GetNameSafe(CharacterBeforeDeath.Get()),
				RespawnMaxHealth,
				RespawnHealth));
				return;
			}

			FailTest(FString::Printf(
				TEXT("Server respawn Inventory is not empty; Count=%d Active=%s CurrentWeapon=%s"),
				RespawnInventory ? RespawnInventory->GetWeaponCount() : INDEX_NONE,
				RespawnInventory ? *RespawnInventory->GetActiveWeaponInstanceId().ToString() : TEXT("null"),
				*GetNameSafe(Character->GetCurrentWeapon())));
			return;
		}
	}

	if (bLethalDamageApplied && bClientObservedDeath && bClientObservedRespawn &&
		bClientObservedMatchState && bClientObservedRemoteAim &&
		(!bRequireRemoteMontage || bClientObservedRemoteMontage) && bServerObservedRespawn &&
		bClientObservedOwnerInventory && bClientObservedRemoteInventoryHidden &&
		bClientObservedPickupAuthority && bAmmoIsolationVerified &&
		bServerDeathInventoryCleared && bClientObservedDeathInventoryClear &&
		bServerRespawnInventoryEmpty && bClientObservedRespawnInventoryEmpty &&
		bClientObservedGasLifecycle && bClientObservedGasRespawn && bServerGasRespawnOk &&
		bClientObservedGasHealthInit && bClientObservedGasHealthDamage &&
		bClientObservedGasHealthRespawn && bServerGasDeathOk &&
		bServerFireGrantOk && bNpcFireGrantOk && bServerFireRespawnGrantOk &&
		bClientObservedFireGrant && bServerReloadEquipGrantOk &&
		bServerReloadEquipRespawnGrantOk && bClientObservedReloadEquipGrant &&
		bReloadFullRejectVerified && bReloadTransferVerified &&
		bReloadCancelEquipVerified && bReloadSwitchBackVerified &&
		bReloadNoReserveVerified && bReloadCancelDeathVerified &&
		bEquipInitialCommitConsistent &&
		bEquipCancelReloadActiveObserved && bEquipSwitchBackActiveObserved &&
		bEquipSingleRejectVerified && bEquipRejectDeadVerified &&
		bFireAfterReloadActiveObserved && bFireAfterReloadSingleShotVerified &&
		bFireAfterReloadQuiescentVerified &&
		bSingleProjectileVerified &&
		bFullAutoReleaseVerified && bFullAutoQuiescentConfirmed &&
		bSwitchCancelVerified && bNoAmmoRejectVerified && bFireRejectDeadVerified &&
		bFireRejectNoWeaponVerified && bRespawnTagCleanupVerified &&
		bNpcFireActivated && bNpcFireStopOk && bNpcFireQuiescenceConfirmed)
	{
		const int32 PlayerId = PlayerController && PlayerController->PlayerState
			? PlayerController->PlayerState->GetPlayerId()
			: INDEX_NONE;

		UE_LOG(
			LogShootGame,
			Display,
			TEXT("AUTOMATION_TEST_CLIENT_SUCCESS PlayerId=%d Switch=true OwnerAmmo=true NonOwnerAmmoHidden=true Bullets=%d->%d HP=%.0f->0 Dead=true Respawn=true RespawnHP=%.0f AimDot=%.3f Team=%u Kills=%d Deaths=%d TeamScore=%d RemotePitch=%.3f/%.3f RemoteMontage=%s GasServer=%s/%s/%s GasNPC=%s GasRespawn=%s GasClient=%s GasClientRespawn=%s GasHealthInit=%s GasDamage=%s GasDeath=%s GasNpcHealth=%s/%s GasClientHealth=%s/%s/%s GasHud=%s FireGrant=%s/%s/%s/%s ReloadEquipGrant=%s/%s/%s Reload=%s/%s/%s/%s/%s/%s FireAfterReload=%s/%s/%s Equip=%s/%s/%s/%s/%s FireGA=%s/%s/%s Cancellation=%s/%s/%s/%s/%s/%s/%s PickupAuthority=%s InventoryOwner=%s InventoryRemoteHidden=%s AmmoIsolation=%s DeathClear=%s/%s RespawnEmpty=%s/%s"),
			PlayerId,
			InitialBulletCount,
			BulletCountAfterFire,
			InitialHP,
			Character->GetCurrentHP(),
			ObservedAimDot,
			ObservedTeamId,
			ObservedKills,
			ObservedDeaths,
			ObservedTeamScore,
			ObservedRemotePitchN,
			ExpectedRemotePitchN,
			bClientObservedRemoteMontage ? TEXT("true") : TEXT("skipped"),
			bServerGasOwnerOk ? TEXT("true") : TEXT("false"),
			bServerGasAvatarOk ? TEXT("true") : TEXT("false"),
			bServerGasConnectionOk ? TEXT("true") : TEXT("false"),
			bNpcGasLifecycleOk ? TEXT("true") : TEXT("false"),
			bServerGasRespawnOk ? TEXT("true") : TEXT("false"),
			bClientObservedGasLifecycle ? TEXT("true") : TEXT("false"),
			bClientObservedGasRespawn ? TEXT("true") : TEXT("false"),
			bServerGasHealthInitOk ? TEXT("true") : TEXT("false"),
			bServerGasDamageOk ? TEXT("true") : TEXT("false"),
			bServerGasDeathOk ? TEXT("true") : TEXT("false"),
			bNpcGasHealthInitOk ? TEXT("true") : TEXT("false"),
			bNpcGasDeathOk ? TEXT("true") : TEXT("false"),
			bClientObservedGasHealthInit ? TEXT("true") : TEXT("false"),
			bClientObservedGasHealthDamage ? TEXT("true") : TEXT("false"),
			bClientObservedGasHealthRespawn ? TEXT("true") : TEXT("false"),
			bClientObservedFullHealthHudEvent ? TEXT("true") : TEXT("false"),
			bServerFireGrantOk ? TEXT("true") : TEXT("false"),
			bNpcFireGrantOk ? TEXT("true") : TEXT("false"),
			bServerFireRespawnGrantOk ? TEXT("true") : TEXT("false"),
			bClientObservedFireGrant ? TEXT("true") : TEXT("false"),
			bServerReloadEquipGrantOk ? TEXT("true") : TEXT("false"),
			bServerReloadEquipRespawnGrantOk ? TEXT("true") : TEXT("false"),
			bClientObservedReloadEquipGrant ? TEXT("true") : TEXT("false"),
			bReloadFullRejectVerified ? TEXT("true") : TEXT("false"),
			bReloadTransferVerified ? TEXT("true") : TEXT("false"),
			bReloadCancelEquipVerified ? TEXT("true") : TEXT("false"),
			bReloadSwitchBackVerified ? TEXT("true") : TEXT("false"),
			bReloadNoReserveVerified ? TEXT("true") : TEXT("false"),
			bReloadCancelDeathVerified ? TEXT("true") : TEXT("false"),
			bFireAfterReloadActiveObserved ? TEXT("true") : TEXT("false"),
			bFireAfterReloadSingleShotVerified ? TEXT("true") : TEXT("false"),
			bFireAfterReloadQuiescentVerified ? TEXT("true") : TEXT("false"),
			bEquipInitialCommitConsistent ? TEXT("true") : TEXT("false"),
			bEquipCancelReloadActiveObserved ? TEXT("true") : TEXT("false"),
			bEquipSwitchBackActiveObserved ? TEXT("true") : TEXT("false"),
			bEquipSingleRejectVerified ? TEXT("true") : TEXT("false"),
			bEquipRejectDeadVerified ? TEXT("true") : TEXT("false"),
			bSingleProjectileVerified ? TEXT("true") : TEXT("false"),
			bFullAutoReleaseVerified ? TEXT("true") : TEXT("false"),
			bFullAutoQuiescentConfirmed ? TEXT("true") : TEXT("false"),
			bSwitchCancelVerified ? TEXT("true") : TEXT("false"),
			bNoAmmoRejectVerified ? TEXT("true") : TEXT("false"),
			bFireRejectDeadVerified ? TEXT("true") : TEXT("false"),
			bFireRejectNoWeaponVerified ? TEXT("true") : TEXT("false"),
			bRespawnTagCleanupVerified ? TEXT("true") : TEXT("false"),
			bNpcFireActivated && bNpcFireStopOk ? TEXT("true") : TEXT("false"),
			bNpcFireQuiescenceConfirmed ? TEXT("true") : TEXT("false"),
			bClientObservedPickupAuthority ? TEXT("true") : TEXT("false"),
			bClientObservedOwnerInventory ? TEXT("true") : TEXT("false"),
			bClientObservedRemoteInventoryHidden ? TEXT("true") : TEXT("false"),
			bAmmoIsolationVerified ? TEXT("true") : TEXT("false"),
			bServerDeathInventoryCleared ? TEXT("true") : TEXT("false"),
			bClientObservedDeathInventoryClear ? TEXT("true") : TEXT("false"),
			bServerRespawnInventoryEmpty ? TEXT("true") : TEXT("false"),
			bClientObservedRespawnInventoryEmpty ? TEXT("true") : TEXT("false"));
		GetWorldTimerManager().ClearTimer(PollTimer);
		return;
	}

	if (GetWorld()->GetTimeSeconds() - TestStartTime >= ShooterNetworkTest::TimeoutSeconds)
	{
		const AShooterPlayerState* TimeoutPlayerState = PlayerController
			? PlayerController->GetPlayerState<AShooterPlayerState>()
			: nullptr;
		const AShooterGameState* TimeoutGameState =
			GetWorld()->GetGameState<AShooterGameState>();
		const uint8 TimeoutTeamId = TimeoutPlayerState
			? TimeoutPlayerState->GetTeamId()
			: MAX_uint8;
		const int32 TimeoutTeamScore = TimeoutGameState
			? TimeoutGameState->GetTeamScore(TimeoutTeamId)
			: INDEX_NONE;

		FailTest(FString::Printf(
			TEXT("Timed out waiting for network state; switch=%s weapon=%s clientProjectile=%s ownerAmmo=%s nonOwnerAmmo=%s serverProjectile=%s aim=%s damage=%s death=%s respawn=%s matchState=%s remoteAim=%s remoteMontage=%s gasOwner=%s gasAvatar=%s gasConnection=%s gasNpc=%s gasRespawn=%s gasClient=%s gasClientRespawn=%s gasHealthInit=%s gasDamage=%s gasDeath=%s gasNpcHealth=%s/%s gasClientHealth=%s/%s/%s gasHud=%s fireGrant=%s/%s/%s/%s reloadEquipGrant=%s/%s/%s reload=%s/%s/%s/%s/%s/%s fireAfterReload=%s/%s/%s equip=%s/%s/%s/%s/%s fireGA=%s/%s/%s cancellation=%s/%s/%s/%s/%s/%s/%s bullets=%d->%d hp=%.0f team=%u kills=%d deaths=%d score=%.0f teamScore=%d PickupAuthority=%s InventoryOwner=%s InventoryRemoteHidden=%s AmmoIsolation=%s DeathClear=%s/%s RespawnEmpty=%s/%s"),
			bClientObservedSwitch ? TEXT("true") : TEXT("false"),
			bClientObservedWeapon ? TEXT("true") : TEXT("false"),
			bClientObservedProjectile ? TEXT("true") : TEXT("false"),
			bClientObservedOwnerAmmo ? TEXT("true") : TEXT("false"),
			bClientObservedNonOwnerAmmoHidden ? TEXT("true") : TEXT("false"),
			bServerObservedProjectile ? TEXT("true") : TEXT("false"),
			bAimDirectionValid ? TEXT("true") : TEXT("false"),
			bClientObservedDamage ? TEXT("true") : TEXT("false"),
			bClientObservedDeath ? TEXT("true") : TEXT("false"),
			bClientObservedRespawn ? TEXT("true") : TEXT("false"),
			bClientObservedMatchState ? TEXT("true") : TEXT("false"),
			bClientObservedRemoteAim ? TEXT("true") : TEXT("false"),
			bClientObservedRemoteMontage ? TEXT("true") : TEXT("false"),
			bServerGasOwnerOk ? TEXT("true") : TEXT("false"),
			bServerGasAvatarOk ? TEXT("true") : TEXT("false"),
			bServerGasConnectionOk ? TEXT("true") : TEXT("false"),
			bNpcGasLifecycleOk ? TEXT("true") : TEXT("false"),
			bServerGasRespawnOk ? TEXT("true") : TEXT("false"),
			bClientObservedGasLifecycle ? TEXT("true") : TEXT("false"),
			bClientObservedGasRespawn ? TEXT("true") : TEXT("false"),
			bServerGasHealthInitOk ? TEXT("true") : TEXT("false"),
			bServerGasDamageOk ? TEXT("true") : TEXT("false"),
			bServerGasDeathOk ? TEXT("true") : TEXT("false"),
			bNpcGasHealthInitOk ? TEXT("true") : TEXT("false"),
			bNpcGasDeathOk ? TEXT("true") : TEXT("false"),
			bClientObservedGasHealthInit ? TEXT("true") : TEXT("false"),
			bClientObservedGasHealthDamage ? TEXT("true") : TEXT("false"),
			bClientObservedGasHealthRespawn ? TEXT("true") : TEXT("false"),
			bClientObservedFullHealthHudEvent ? TEXT("true") : TEXT("false"),
			bServerFireGrantOk ? TEXT("true") : TEXT("false"),
			bNpcFireGrantOk ? TEXT("true") : TEXT("false"),
			bServerFireRespawnGrantOk ? TEXT("true") : TEXT("false"),
			bClientObservedFireGrant ? TEXT("true") : TEXT("false"),
			bServerReloadEquipGrantOk ? TEXT("true") : TEXT("false"),
			bServerReloadEquipRespawnGrantOk ? TEXT("true") : TEXT("false"),
			bClientObservedReloadEquipGrant ? TEXT("true") : TEXT("false"),
			bReloadFullRejectVerified ? TEXT("true") : TEXT("false"),
			bReloadTransferVerified ? TEXT("true") : TEXT("false"),
			bReloadCancelEquipVerified ? TEXT("true") : TEXT("false"),
			bReloadSwitchBackVerified ? TEXT("true") : TEXT("false"),
			bReloadNoReserveVerified ? TEXT("true") : TEXT("false"),
			bReloadCancelDeathVerified ? TEXT("true") : TEXT("false"),
			bFireAfterReloadActiveObserved ? TEXT("true") : TEXT("false"),
			bFireAfterReloadSingleShotVerified ? TEXT("true") : TEXT("false"),
			bFireAfterReloadQuiescentVerified ? TEXT("true") : TEXT("false"),
			bEquipInitialCommitConsistent ? TEXT("true") : TEXT("false"),
			bEquipCancelReloadActiveObserved ? TEXT("true") : TEXT("false"),
			bEquipSwitchBackActiveObserved ? TEXT("true") : TEXT("false"),
			bEquipSingleRejectVerified ? TEXT("true") : TEXT("false"),
			bEquipRejectDeadVerified ? TEXT("true") : TEXT("false"),
			bSingleProjectileVerified ? TEXT("true") : TEXT("false"),
			bFullAutoReleaseVerified ? TEXT("true") : TEXT("false"),
			bFullAutoQuiescentConfirmed ? TEXT("true") : TEXT("false"),
			bSwitchCancelVerified ? TEXT("true") : TEXT("false"),
			bNoAmmoRejectVerified ? TEXT("true") : TEXT("false"),
			bFireRejectDeadVerified ? TEXT("true") : TEXT("false"),
			bFireRejectNoWeaponVerified ? TEXT("true") : TEXT("false"),
			bRespawnTagCleanupVerified ? TEXT("true") : TEXT("false"),
			bNpcFireActivated && bNpcFireStopOk ? TEXT("true") : TEXT("false"),
			bNpcFireQuiescenceConfirmed ? TEXT("true") : TEXT("false"),
			InitialBulletCount,
			CurrentBulletCount,
			Character->GetCurrentHP(),
			TimeoutTeamId,
			TimeoutPlayerState ? TimeoutPlayerState->GetKills() : INDEX_NONE,
			TimeoutPlayerState ? TimeoutPlayerState->GetDeaths() : INDEX_NONE,
			TimeoutPlayerState ? TimeoutPlayerState->GetScore() : -1.0f,
			TimeoutTeamScore,
			bClientObservedPickupAuthority ? TEXT("true") : TEXT("false"),
			bClientObservedOwnerInventory ? TEXT("true") : TEXT("false"),
			bClientObservedRemoteInventoryHidden ? TEXT("true") : TEXT("false"),
			bAmmoIsolationVerified ? TEXT("true") : TEXT("false"),
			bServerDeathInventoryCleared ? TEXT("true") : TEXT("false"),
			bClientObservedDeathInventoryClear ? TEXT("true") : TEXT("false"),
			bServerRespawnInventoryEmpty ? TEXT("true") : TEXT("false"),
			bClientObservedRespawnInventoryEmpty ? TEXT("true") : TEXT("false")));
	}
}

void AShooterNetworkTestCoordinator::HandleActorSpawned(AActor* SpawnedActor)
{
	const AShooterProjectile* Projectile = Cast<AShooterProjectile>(SpawnedActor);
	AShooterCharacter* Character = GetShooterCharacter();
	if (!Projectile || !Character || Projectile->GetInstigator() != Character)
	{
		return;
	}

	bServerObservedProjectile = true;
	++ProjectileSpawnCount;
	const FVector ExpectedDirection = Character->GetControlRotation().Vector();
	const FVector ProjectileDirection = Projectile->GetActorForwardVector();
	ObservedAimDot = FVector::DotProduct(ExpectedDirection, ProjectileDirection);
	// 枪口会朝摄像机射线的实际命中点发射；近处遮挡会让它明显偏离控制器前向，
	// 但正常弹道不应落入控制器朝向的后半球。该条件仍能捕获远程摄像机失效时的反向弹道。
	bAimDirectionValid = ObservedAimDot >= 0.0f;

	if (!bAimDirectionValid)
	{
		FailTest(FString::Printf(
			TEXT("Projectile aim differs from server control rotation; dot=%.3f"),
			ObservedAimDot));
	}
}

void AShooterNetworkTestCoordinator::PollClientState()
{
	APlayerController* PlayerController = Cast<APlayerController>(GetOwner());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

	AShooterWeapon* Weapon = GetCurrentWeapon(Character);

	// ---- 5B Reload 客户端输入驱动：每个 RequestId 只允许一次 DoReload ----
	// 生产修复后，客户端 CanActivateAbility 不再读取可能过期的 State.Reloading；
	// 因此测试端也不得通过重试掩盖问题，单次输入必须直达服务器。
	if (ReloadInputRequestId > LastObservedReloadInputRequestId)
	{
		LastObservedReloadInputRequestId = ReloadInputRequestId;
		Character->DoReload();
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("Reload client input submitted once: PlayerId=%d RequestId=%d"),
			PlayerController->PlayerState ? PlayerController->PlayerState->GetPlayerId() : INDEX_NONE,
			ReloadInputRequestId);
		ServerReportClientTriggeredReload(ReloadInputRequestId);
	}

	// ---- 5B 弱网 Fire-after-Reload：Reload 完成后客户端只提交一次 Fire 与一次 Stop ----
	if (bServerReadyForFireAfterReload && !bClientTriggeredFireAfterReload)
	{
		bClientTriggeredFireAfterReload = true;
		Character->DoStartFiring();
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("Fire-after-reload client input submitted once: PlayerId=%d"),
			PlayerController->PlayerState ? PlayerController->PlayerState->GetPlayerId() : INDEX_NONE);
		ServerReportClientTriggeredFireAfterReload();
	}

	if (bServerReadyForStopFireAfterReload && !bClientStoppedFireAfterReload)
	{
		bClientStoppedFireAfterReload = true;
		Character->DoStopFiring();
		ServerReportClientStoppedFireAfterReload();
	}

	// ---- 5C Reject.SingleWeapon：客户端仍走生产切枪入口，服务器必须拒绝 GA_Equip ----
	if (bServerReadyForEquipSingleReject && !bClientTriggeredEquipSingleReject)
	{
		bClientTriggeredEquipSingleReject = true;
		Character->DoSwitchWeapon();
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("Equip-single-reject client input submitted once: PlayerId=%d"),
			PlayerController->PlayerState ? PlayerController->PlayerState->GetPlayerId() : INDEX_NONE);
		ServerReportClientTriggeredEquipSingleReject();
	}

	// ---- 5B Cancel.Equip：等待服务器观察到活动 Reload 后切枪，随后按需切回 ----
	if (bServerReadyForReloadSwitch && !bClientTriggeredReloadSwitch)
	{
		bClientTriggeredReloadSwitch = true;
		Character->DoSwitchWeapon();
		ServerReportClientTriggeredReloadSwitch();
	}

	if (bServerReadyForReloadSwitchBack && !bClientTriggeredReloadSwitchBack)
	{
		bClientTriggeredReloadSwitchBack = true;
		Character->DoSwitchWeapon();
		ServerReportClientTriggeredReloadSwitchBack();
	}

	// ---- Inventory 2A（拥有者客户端视角）：Owner 完整收到，远端角色不收到完整列表 ----
	if (!bClientReportedInventory)
	{
		UShooterInventoryComponent* InventoryComponent = Character->GetInventoryComponent();
		const FGuid ActiveId = InventoryComponent ? InventoryComponent->GetActiveWeaponInstanceId() : FGuid();
		const bool bOwnerInventoryOk = InventoryComponent &&
			InventoryComponent->GetWeaponCount() == 2 &&
			ActiveId.IsValid() &&
			InventoryComponent->FindWeaponInstance(ActiveId) != nullptr;

		bool bRemoteInventoryHidden = false;
		if (bOwnerInventoryOk)
		{
			for (TActorIterator<AShooterCharacter> It(GetWorld()); It; ++It)
			{
				AShooterCharacter* RemoteCharacter = *It;
				if (RemoteCharacter == Character ||
					RemoteCharacter->GetLocalRole() != ROLE_SimulatedProxy)
				{
					continue;
				}

				UShooterInventoryComponent* RemoteInventory =
					RemoteCharacter->GetInventoryComponent();
				bRemoteInventoryHidden = RemoteInventory &&
					RemoteInventory->GetWeaponCount() == 0;
				break;
			}
		}

		if (bOwnerInventoryOk && bRemoteInventoryHidden)
		{
			bClientReportedInventory = true;
			ServerReportClientObservedInventory(
				InventoryComponent->GetWeaponCount(),
				ActiveId.ToString(),
				true);
		}
	}

	// ---- Inventory 2B Pickup ServerAuthority：客户端直接调用授予入口必须被拒绝 ----
	if (!bClientReportedPickupAuthority)
	{
		UShooterInventoryComponent* InventoryComponent = Character->GetInventoryComponent();
		const TSubclassOf<AShooterWeapon> RifleClass = LoadClass<AShooterWeapon>(
			nullptr,
			ShooterNetworkTest::RifleClassPath);
		if (InventoryComponent && RifleClass)
		{
			FGuid ClientAttemptInstanceId;
			const EShooterInventoryAddResult ClientAttemptResult =
				InventoryComponent->TryAddWeapon(RifleClass, ClientAttemptInstanceId);
			if (ClientAttemptResult == EShooterInventoryAddResult::NotAuthoritative &&
				InventoryComponent->GetWeaponCount() == 2)
			{
				bClientReportedPickupAuthority = true;
				ServerReportClientObservedPickupAuthority();
			}
		}
	}

	if (!bClientSetAimPitch)
	{
		FRotator ControlRotation = PlayerController->GetControlRotation();
		ControlRotation.Pitch = 30.0f;
		PlayerController->SetControlRotation(ControlRotation);
		bClientSetAimPitch = true;
	}

	// ---- GAS ASC 生命周期（拥有者客户端视角）：Owner=PlayerState，Avatar=本地角色 ----
	if (!bClientReportedGasLifecycle)
	{
		AShooterPlayerState* ShooterPlayerState =
			PlayerController->GetPlayerState<AShooterPlayerState>();
		UAbilitySystemComponent* AbilitySystemComponent = ShooterPlayerState
			? ShooterPlayerState->GetAbilitySystemComponent()
			: nullptr;
		if (AbilitySystemComponent &&
			AbilitySystemComponent->GetOwnerActor() == ShooterPlayerState &&
			AbilitySystemComponent->GetAvatarActor() == Character &&
			Character->IsLocallyControlled())
		{
			bClientReportedGasLifecycle = true;
			ServerReportClientObservedGasLifecycle();
		}
	}

	// ---- 4A Fire Ability 授予复制（拥有者客户端视角）：Owner 恰好一个，远端不收到完整 Spec ----
	if (!bClientReportedFireGrant)
	{
		AShooterPlayerState* ShooterPlayerState =
			PlayerController->GetPlayerState<AShooterPlayerState>();
		if (ShooterPlayerState && ShooterPlayerState->GetFireAbilitySpecCount() == 1)
		{
			bool bRemoteFireSpecsHidden = false;
			const AGameStateBase* ClientGameState = GetWorld()->GetGameState();
			if (ClientGameState)
			{
				for (APlayerState* OtherPlayerState : ClientGameState->PlayerArray)
				{
					if (OtherPlayerState == ShooterPlayerState)
					{
						continue;
					}

					const AShooterPlayerState* RemoteShooterPlayerState =
						Cast<AShooterPlayerState>(OtherPlayerState);
					if (!RemoteShooterPlayerState)
					{
						continue;
					}

					// Mixed 模式：完整 Ability Spec 只复制给拥有者连接；
					// 远端 PlayerState 的 ASC 不应持有 Fire Spec。
					bRemoteFireSpecsHidden =
						RemoteShooterPlayerState->GetFireAbilitySpecCount() == 0;
					break;
				}
			}

			if (bRemoteFireSpecsHidden)
			{
				bClientReportedFireGrant = true;
				ServerReportClientObservedFireAbilityGrant(1, true);
			}
		}
	}

	// ---- 5A Reload / Equip Ability 授予复制（拥有者客户端视角）：Owner 各一个，远端不收到完整 Spec ----
	if (!bClientReportedReloadEquipGrant)
	{
		AShooterPlayerState* ShooterPlayerState =
			PlayerController->GetPlayerState<AShooterPlayerState>();
		if (ShooterPlayerState &&
			ShooterPlayerState->GetReloadAbilitySpecCount() == 1 &&
			ShooterPlayerState->GetEquipAbilitySpecCount() == 1)
		{
			bool bRemoteReloadSpecsHidden = false;
			bool bRemoteEquipSpecsHidden = false;
			const AGameStateBase* ClientGameState = GetWorld()->GetGameState();
			if (ClientGameState)
			{
				for (APlayerState* OtherPlayerState : ClientGameState->PlayerArray)
				{
					if (OtherPlayerState == ShooterPlayerState)
					{
						continue;
					}

					const AShooterPlayerState* RemoteShooterPlayerState =
						Cast<AShooterPlayerState>(OtherPlayerState);
					if (!RemoteShooterPlayerState)
					{
						continue;
					}

					// Mixed 模式：完整 Reload / Equip Spec 同样只复制给拥有者连接。
					bRemoteReloadSpecsHidden =
						RemoteShooterPlayerState->GetReloadAbilitySpecCount() == 0;
					bRemoteEquipSpecsHidden =
						RemoteShooterPlayerState->GetEquipAbilitySpecCount() == 0;
					break;
				}
			}

			if (bRemoteReloadSpecsHidden && bRemoteEquipSpecsHidden)
			{
				bClientReportedReloadEquipGrant = true;
				ServerReportClientObservedReloadEquipAbilityGrant(1, true, 1, true);
			}
		}
	}

	// 绑定 OnDamaged 事件：验证 HUD 事件链由属性变化驱动且数值一致。
	// 复活会更换角色，需要在角色变化时重新绑定。
	if (HudBoundCharacter.Get() != Character)
	{
		if (HudBoundCharacter.IsValid())
		{
			HudBoundCharacter->OnDamaged.RemoveDynamic(
				this,
				&AShooterNetworkTestCoordinator::HandleDamagedEvent);
		}
		HudBoundCharacter = Character;
		Character->OnDamaged.AddDynamic(this, &AShooterNetworkTestCoordinator::HandleDamagedEvent);
	}

	// ---- GAS Health 初始化（拥有者客户端视角）：属性复制到达且为满血 ----
	if (!bClientReportedGasHealthInit)
	{
		AShooterPlayerState* ShooterPlayerState =
			PlayerController->GetPlayerState<AShooterPlayerState>();
		UAbilitySystemComponent* AbilitySystemComponent = ShooterPlayerState
			? ShooterPlayerState->GetAbilitySystemComponent()
			: nullptr;
		if (AbilitySystemComponent)
		{
			const float HealthAttributeValue = AbilitySystemComponent->GetNumericAttribute(
				UShooterAttributeSet::GetHealthAttribute());
			const float MaxHealthAttributeValue = AbilitySystemComponent->GetNumericAttribute(
				UShooterAttributeSet::GetMaxHealthAttribute());
			if (MaxHealthAttributeValue > 0.0f && HealthAttributeValue > 0.0f &&
				FMath::IsNearlyEqual(HealthAttributeValue, MaxHealthAttributeValue, 0.01f))
			{
				ClientMaxHealthAttributeValue = MaxHealthAttributeValue;
				bClientReportedGasHealthInit = true;
				ServerReportClientObservedGasHealthInit();
			}
		}
	}

	// 订阅 ASC 的 Health 属性变化事件：HUD 事件链的源头，跨角色重生无竞态。
	if (!bClientHealthAttributeDelegateBound)
	{
		AShooterPlayerState* ShooterPlayerState =
			PlayerController->GetPlayerState<AShooterPlayerState>();
		UAbilitySystemComponent* AbilitySystemComponent = ShooterPlayerState
			? ShooterPlayerState->GetAbilitySystemComponent()
			: nullptr;
		if (AbilitySystemComponent)
		{
			AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
				UShooterAttributeSet::GetHealthAttribute())
				.AddUObject(
					this,
					&AShooterNetworkTestCoordinator::HandleClientHealthAttributeChanged);
			bClientHealthAttributeDelegateBound = true;
		}
	}

	for (TActorIterator<AShooterCharacter> It(GetWorld()); It; ++It)
	{
		AShooterCharacter* RemoteCharacter = *It;
		if (RemoteCharacter == Character ||
			RemoteCharacter->GetLocalRole() != ROLE_SimulatedProxy)
		{
			continue;
		}

		UAnimInstance* RemoteAnimInstance = RemoteCharacter->GetMesh()
			? RemoteCharacter->GetMesh()->GetAnimInstance()
			: nullptr;
		if (!RemoteAnimInstance)
		{
			continue;
		}

		if (!bClientReportedRemoteAim)
		{
			const FNumericProperty* PitchProperty = FindFProperty<FNumericProperty>(
				RemoteAnimInstance->GetClass(),
				TEXT("PitchN"));
			if (PitchProperty && PitchProperty->IsFloatingPoint())
			{
				const void* PitchValue = PitchProperty->ContainerPtrToValuePtr<void>(RemoteAnimInstance);
				const float PitchN = static_cast<float>(
					PitchProperty->GetFloatingPointPropertyValue(PitchValue));
				const float ExpectedPitchN = RemoteCharacter->GetBaseAimRotation().Vector().Z;
				if (FMath::Abs(ExpectedPitchN) >= 0.2f &&
					FMath::IsNearlyEqual(PitchN, ExpectedPitchN, 0.05f))
				{
					bClientReportedRemoteAim = true;
					ServerReportClientObservedRemoteAim(PitchN, ExpectedPitchN);
				}
			}
		}

		if (!bClientReportedRemoteMontage && RemoteAnimInstance->IsAnyMontagePlaying())
		{
			bClientReportedRemoteMontage = true;
			ServerReportClientObservedRemoteMontage();
		}
	}

	if (Weapon)
	{
	if (bServerReadyToSwitch && WeaponBeforeSwitch &&
		Weapon == WeaponBeforeSwitch && !bClientTriggeredSwitch)
	{
		bClientTriggeredSwitch = true;
		InitialClientWeapon = Weapon;
		Character->DoSwitchWeapon();
		return;
	}

	if (bClientTriggeredSwitch && !bClientReportedSwitch &&
		Weapon != InitialClientWeapon.Get() && Weapon->GetBulletCount() > 0)
	{
		UShooterInventoryComponent* InventoryComponent = Character->GetInventoryComponent();
		const FGuid ActiveId = InventoryComponent
			? InventoryComponent->GetActiveWeaponInstanceId()
			: FGuid();
		const FGuid CurrentBoundId = Weapon->GetBoundInstanceId();

		bool bRemoteCurrentWeaponVisible = !bRequireRemoteCurrentWeapon;
		if (bRequireRemoteCurrentWeapon)
		{
			for (TActorIterator<AShooterCharacter> It(GetWorld()); It; ++It)
			{
				AShooterCharacter* RemoteCharacter = *It;
				if (RemoteCharacter == Character ||
					RemoteCharacter->GetLocalRole() != ROLE_SimulatedProxy)
				{
					continue;
				}

				AShooterWeapon* RemoteCurrentWeapon =
					RemoteCharacter->GetCurrentWeapon();
				// 远端复制的是另一个 Actor 实例，按武器类验证公共表现一致性。
				bRemoteCurrentWeaponVisible = RemoteCurrentWeapon &&
					RemoteCurrentWeapon->GetClass() == Weapon->GetClass();
				break;
			}
		}

		if (ActiveId.IsValid() && CurrentBoundId.IsValid() && bRemoteCurrentWeaponVisible)
		{
			bClientReportedSwitch = true;
			InitialClientBulletCount = Weapon->GetBulletCount();
			ServerReportClientObservedSwitch(
				ActiveId.ToString(),
				CurrentBoundId.ToString(),
				true);
		}
	}

	if (!bClientReportedNonOwnerAmmoHidden)
	{
		for (TActorIterator<AShooterWeapon> It(GetWorld()); It; ++It)
		{
			if (It->GetOwner() != Character && It->GetBulletCount() == 0)
			{
				bClientReportedNonOwnerAmmoHidden = true;
				ServerReportNonOwnerAmmoHidden();
				break;
			}
		}
	}

	if (!bServerReadyToFire || !bClientReportedSwitch)
	{
		return;
	}

	if (!bClientTriggeredFire)
	{
		InitialClientBulletCount = Weapon->GetBulletCount();
		bClientTriggeredFire = true;
		ServerReportClientObservedWeapon();
		Character->DoStartFiring();
		Character->DoStopFiring();
	}

	if (!bClientReportedOwnerAmmo && InitialClientBulletCount > 0 &&
		Weapon->GetBulletCount() < InitialClientBulletCount)
	{
		bClientReportedOwnerAmmo = true;
		ServerReportOwnerAmmoReplicated();
	}

	if (!bClientReportedProjectile)
	{
		for (TActorIterator<AShooterProjectile> It(GetWorld()); It; ++It)
		{
			if (It->GetOwner() == Character || It->GetInstigator() == Character)
			{
				bClientReportedProjectile = true;
				ServerReportClientObservedProjectile();
				break;
			}
		}
	}

	// ---- 4B FullAutoRelease：保持按下直到全自动计时器再打出至少两发，然后松开 ----
	if (bServerReadyForFullAuto && !bClientTriggeredFullAuto &&
		bClientReportedProjectile)
	{
		BulletCountBeforeFullAuto = Weapon->GetBulletCount();
		bClientTriggeredFullAuto = true;
		Character->DoStartFiring();
	}

	if (bClientTriggeredFullAuto && !bClientStoppedFullAuto &&
		BulletCountBeforeFullAuto != INDEX_NONE &&
		Weapon->GetBulletCount() < BulletCountBeforeFullAuto - 1)
	{
		Character->DoStopFiring();
		bClientStoppedFullAuto = true;
		FullAutoReleaseWaitStartTime = GetWorld()->GetTimeSeconds();
	}

	// 释放后给 OwnerOnly FastArray 复制留出收敛时间，避免弱网下
	// 客户端镜像仍落后一发给服务器上报错误 Ammo。
	if (bClientStoppedFullAuto && !bClientReportedFullAuto &&
		GetWorld()->GetTimeSeconds() - FullAutoReleaseWaitStartTime >= 0.4f)
	{
		ClientBulletCountAfterRelease = Weapon->GetBulletCount();
		bClientReportedFullAuto = true;
		ServerReportFullAutoReleased(ClientBulletCountAfterRelease);
	}

	// ---- 4C Cancel.SwitchWeapon：保持步枪开火，等到再打出一发后直接切枪 ----
	if (bServerReadyForSwitchCancel && !bClientTriggeredSwitchCancel &&
		bClientReportedFullAuto)
	{
		BulletCountBeforeClientSwitchCancel = Weapon->GetBulletCount();
		ClientWeaponBeforeSwitchCancel = Weapon;
		bClientTriggeredSwitchCancel = true;
		Character->DoStartFiring();
	}

	if (bClientTriggeredSwitchCancel && !bClientSwitchCancelRequested &&
		BulletCountBeforeClientSwitchCancel != INDEX_NONE &&
		Weapon->GetBulletCount() < BulletCountBeforeClientSwitchCancel)
	{
		bClientSwitchCancelRequested = true;
		Character->DoSwitchWeapon();
	}

	if (bClientSwitchCancelRequested && !bClientReportedSwitchCancel &&
		Weapon != ClientWeaponBeforeSwitchCancel.Get())
	{
		bClientReportedSwitchCancel = true;
		ServerReportClientObservedCancelSwitch(
			Weapon->GetBoundInstanceId().ToString());
	}
	} // if (Weapon)

	if (!bClientReportedDamage && Character->GetCurrentHP() > 0.0f &&
		Character->GetCurrentHP() < Character->GetMaxHP())
	{
		bClientReportedDamage = true;
		ServerReportClientObservedDamage();
	}

	if (!bClientReportedDeath && Character->IsDead())
	{
		bClientReportedDeath = true;
		ServerReportClientObservedDeath();
	}

	if (bClientReportedDeath && !bClientReportedDeathInventoryClear && Character->IsDead())
	{
		UShooterInventoryComponent* DeathInventory = Character->GetInventoryComponent();
		if (DeathInventory &&
			DeathInventory->GetWeaponCount() == 0 &&
			!DeathInventory->GetActiveWeaponInstanceId().IsValid() &&
			Character->GetCurrentWeapon() == nullptr)
		{
			bClientReportedDeathInventoryClear = true;
			ServerReportClientObservedInventoryDeathClear();
		}
	}

	if (bClientReportedDeath && !bClientReportedRespawn && !Character->IsDead() &&
		Character->GetCurrentHP() > 0.0f && Character->IsLocallyControlled() &&
		Character->GetCharacterMovement()->MovementMode != MOVE_None &&
		PlayerController->GetPawn() == Character && Character->GetController() == PlayerController)
	{
		bClientReportedRespawn = true;
		ServerReportClientObservedRespawn();
	}

	if (bClientReportedRespawn && !bClientReportedRespawnInventoryEmpty)
	{
		UShooterInventoryComponent* RespawnInventory = Character->GetInventoryComponent();
		if (RespawnInventory &&
			RespawnInventory->GetWeaponCount() == 0 &&
			!RespawnInventory->GetActiveWeaponInstanceId().IsValid() &&
			Character->GetCurrentWeapon() == nullptr)
		{
			bClientReportedRespawnInventoryEmpty = true;
			ServerReportClientObservedInventoryRespawnEmpty();
		}
	}

	// ---- GAS ASC 重生生命周期（拥有者客户端视角）：Avatar 切换到复活后的新角色 ----
	if (bClientReportedRespawn && !bClientReportedGasRespawn)
	{
		AShooterPlayerState* ShooterPlayerState =
			PlayerController->GetPlayerState<AShooterPlayerState>();
		UAbilitySystemComponent* AbilitySystemComponent = ShooterPlayerState
			? ShooterPlayerState->GetAbilitySystemComponent()
			: nullptr;
		if (AbilitySystemComponent &&
			AbilitySystemComponent->GetOwnerActor() == ShooterPlayerState &&
			AbilitySystemComponent->GetAvatarActor() == Character)
		{
			bClientReportedGasRespawn = true;
			ServerReportClientObservedGasRespawn();
		}
	}

	// ---- GAS Health 伤害收敛（拥有者客户端视角）：属性与复制镜像在受伤状态最终一致 ----
	if (bClientReportedDamage && !bClientReportedGasHealthDamage)
	{
		AShooterPlayerState* ShooterPlayerState =
			PlayerController->GetPlayerState<AShooterPlayerState>();
		UAbilitySystemComponent* AbilitySystemComponent = ShooterPlayerState
			? ShooterPlayerState->GetAbilitySystemComponent()
			: nullptr;
		if (AbilitySystemComponent)
		{
			const float HealthAttributeValue = AbilitySystemComponent->GetNumericAttribute(
				UShooterAttributeSet::GetHealthAttribute());
			const float MaxHealthAttributeValue = AbilitySystemComponent->GetNumericAttribute(
				UShooterAttributeSet::GetMaxHealthAttribute());
			// 属性值可能在两次网络更新间被合并（部分伤害后紧跟致死），
			// 因此不要求特定的中间值，只要求属性与镜像收敛到同一受伤状态。
			if (HealthAttributeValue < MaxHealthAttributeValue &&
				FMath::IsNearlyEqual(HealthAttributeValue, Character->GetCurrentHP(), 0.01f))
			{
				bClientReportedGasHealthDamage = true;
				ServerReportClientObservedGasHealthDamage();
			}
		}
	}

	// ---- GAS Health 重生收敛：新 Pawn 满血，且 HUD 事件链收到满血事件 ----
	if (bClientReportedRespawn && !bClientReportedGasHealthRespawn)
	{
		AShooterPlayerState* ShooterPlayerState =
			PlayerController->GetPlayerState<AShooterPlayerState>();
		UAbilitySystemComponent* AbilitySystemComponent = ShooterPlayerState
			? ShooterPlayerState->GetAbilitySystemComponent()
			: nullptr;
		if (AbilitySystemComponent)
		{
			const float HealthAttributeValue = AbilitySystemComponent->GetNumericAttribute(
				UShooterAttributeSet::GetHealthAttribute());
			const float MaxHealthAttributeValue = AbilitySystemComponent->GetNumericAttribute(
				UShooterAttributeSet::GetMaxHealthAttribute());
			if (HealthAttributeValue > 0.0f &&
				FMath::IsNearlyEqual(HealthAttributeValue, MaxHealthAttributeValue, 0.01f) &&
				FMath::IsNearlyEqual(HealthAttributeValue, Character->GetCurrentHP(), 0.01f))
			{
				bClientReportedGasHealthRespawn = true;
				ServerReportClientObservedGasHealthRespawn(bClientObservedFullHealthHudEvent);
			}
		}
	}

	if (!bClientReportedMatchState)
	{
		const AShooterPlayerState* ShooterPlayerState =
			PlayerController->GetPlayerState<AShooterPlayerState>();
		const AShooterGameState* ShooterGameState =
			GetWorld()->GetGameState<AShooterGameState>();
		if (ShooterPlayerState && ShooterGameState)
		{
			const uint8 TeamId = ShooterPlayerState->GetTeamId();
			const int32 TeamScore = ShooterGameState->GetTeamScore(TeamId);
			if (TeamId < 2 && ShooterPlayerState->GetKills() >= 1 &&
				ShooterPlayerState->GetDeaths() >= 1 &&
				ShooterPlayerState->GetScore() >= 1.0f && TeamScore >= 1)
			{
				bClientReportedMatchState = true;
				ServerReportClientObservedMatchState(
					TeamId,
					ShooterPlayerState->GetKills(),
					ShooterPlayerState->GetDeaths(),
					TeamScore);
			}
		}
	}
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedWeapon_Implementation()
{
	bClientObservedWeapon = true;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedProjectile_Implementation()
{
	bClientObservedProjectile = true;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedSwitch_Implementation(
	const FString& ActiveWeaponInstanceId,
	const FString& CurrentWeaponBoundInstanceId,
	bool bRemoteCurrentWeaponVisible)
{
	FGuid ObservedActiveId;
	FGuid ObservedBoundId;
	FGuid::Parse(ActiveWeaponInstanceId, ObservedActiveId);
	FGuid::Parse(CurrentWeaponBoundInstanceId, ObservedBoundId);

	// 从手枪切回步枪：Active、当前 WeaponActor 绑定、远端公共表现必须三方一致。
	bClientObservedSwitch = bServerInventoryPrepared &&
		ObservedActiveId == ServerInventoryFirstId &&
		ObservedBoundId == ServerInventoryFirstId &&
		bRemoteCurrentWeaponVisible;

	UE_LOG(LogShootGame, Display, TEXT("Switch client report: Active=%s Bound=%s RemoteVisible=%s Valid=%s"),
		*ActiveWeaponInstanceId,
		*CurrentWeaponBoundInstanceId,
		bRemoteCurrentWeaponVisible ? TEXT("true") : TEXT("false"),
		bClientObservedSwitch ? TEXT("true") : TEXT("false"));

	if (!bClientObservedSwitch)
	{
		FailTest(FString::Printf(
			TEXT("Switch state invalid; Active=%s Expected=%s Bound=%s Expected=%s RemoteVisible=%s"),
			*ActiveWeaponInstanceId,
			*ServerInventoryFirstId.ToString(),
			*CurrentWeaponBoundInstanceId,
			*ServerInventoryFirstId.ToString(),
			bRemoteCurrentWeaponVisible ? TEXT("true") : TEXT("false")));
	}
}

void AShooterNetworkTestCoordinator::ServerReportOwnerAmmoReplicated_Implementation()
{
	bClientObservedOwnerAmmo = true;
}

void AShooterNetworkTestCoordinator::ServerReportNonOwnerAmmoHidden_Implementation()
{
	bClientObservedNonOwnerAmmoHidden = true;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedDamage_Implementation()
{
	bClientObservedDamage = true;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedDeath_Implementation()
{
	bClientObservedDeath = true;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedRespawn_Implementation()
{
	bClientObservedRespawn = true;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedMatchState_Implementation(
	uint8 TeamId,
	int32 Kills,
	int32 Deaths,
	int32 TeamScore)
{
	bClientObservedMatchState = TeamId < 2 && Kills >= 1 && Deaths >= 1 && TeamScore >= 1;
	ObservedTeamId = TeamId;
	ObservedKills = Kills;
	ObservedDeaths = Deaths;
	ObservedTeamScore = TeamScore;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedRemoteAim_Implementation(
	float PitchN,
	float ExpectedPitchN)
{
	bClientObservedRemoteAim = FMath::Abs(ExpectedPitchN) >= 0.2f &&
		FMath::IsNearlyEqual(PitchN, ExpectedPitchN, 0.05f);
	ObservedRemotePitchN = PitchN;
	ExpectedRemotePitchN = ExpectedPitchN;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedRemoteMontage_Implementation()
{
	bClientObservedRemoteMontage = true;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedGasLifecycle_Implementation()
{
	UE_LOG(LogShootGame, Display, TEXT("GAS client report: GasLifecycle"));
	bClientObservedGasLifecycle = true;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedGasRespawn_Implementation()
{
	UE_LOG(LogShootGame, Display, TEXT("GAS client report: GasRespawn"));
	bClientObservedGasRespawn = true;
}

void AShooterNetworkTestCoordinator::ServerReportFullAutoReleased_Implementation(
	int32 BulletCountAfterRelease)
{
	bClientReportedFullAutoRelease = true;
	ClientBulletCountAfterRelease = BulletCountAfterRelease;

	AShooterCharacter* Character = GetShooterCharacter();
	UShooterInventoryComponent* InventoryComponent = Character
		? Character->GetInventoryComponent()
		: nullptr;
	AmmoAfterRelease = InventoryComponent
		? InventoryComponent->GetMagazineAmmo(ServerInventoryFirstId)
		: INDEX_NONE;
	ProjectileCountAfterRelease = ProjectileSpawnCount;

	// 全自动阶段必须在单发基线（1 发）之后再产生至少 2 发，
	// 且服务器保持期间只观察到一个活动 GA_Fire，释放时客户端镜像与权威 Ammo 一致。
	bFullAutoReleaseVerified = bFullAutoActiveObserved &&
		ProjectileCountBeforeFullAuto != INDEX_NONE &&
		AmmoAfterRelease != INDEX_NONE &&
		ProjectileCountAfterRelease >= ProjectileCountBeforeFullAuto + 2 &&
		AmmoAfterRelease == BulletCountAfterRelease;
	FullAutoReleaseCheckTime = GetWorld()->GetTimeSeconds();

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Full-auto client report: ClientAmmo=%d ServerAmmo=%d Projectiles=%d->%d ActiveObserved=%s Valid=%s"),
		BulletCountAfterRelease,
		AmmoAfterRelease,
		ProjectileCountBeforeFullAuto,
		ProjectileCountAfterRelease,
		bFullAutoActiveObserved ? TEXT("true") : TEXT("false"),
		bFullAutoReleaseVerified ? TEXT("true") : TEXT("false"));

	if (!bFullAutoReleaseVerified)
	{
		FailTest(FString::Printf(
			TEXT("Full-auto release verification invalid; ClientAmmo=%d ServerAmmo=%d Projectiles=%d->%d ActiveObserved=%s"),
			BulletCountAfterRelease,
			AmmoAfterRelease,
			ProjectileCountBeforeFullAuto,
			ProjectileCountAfterRelease,
			bFullAutoActiveObserved ? TEXT("true") : TEXT("false")));
	}
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedCancelSwitch_Implementation(
	const FString& CurrentWeaponBoundInstanceId)
{
	FGuid ObservedBoundId;
	FGuid::Parse(CurrentWeaponBoundInstanceId, ObservedBoundId);

	AShooterCharacter* Character = GetShooterCharacter();
	AShooterWeapon* CurrentWeaponAfterSwitch = Character
		? Character->GetCurrentWeapon()
		: nullptr;
	bClientReportedSwitchCancel = true;
	bClientObservedSwitchCancel = CurrentWeaponAfterSwitch &&
		CurrentWeaponAfterSwitch->GetBoundInstanceId() == ServerInventorySecondId &&
		ObservedBoundId == ServerInventorySecondId;

	ProjectileCountAfterSwitchCancel = ProjectileSpawnCount;
	UShooterInventoryComponent* SwitchCancelInventory = Character
		? Character->GetInventoryComponent()
		: nullptr;
	RifleAmmoAfterSwitchCancel = SwitchCancelInventory
		? SwitchCancelInventory->GetMagazineAmmo(ServerInventoryFirstId)
		: INDEX_NONE;
	SwitchCancelCheckTime = GetWorld()->GetTimeSeconds();

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Switch-cancel client report: Bound=%s Expected=%s Projectiles=%d->%d RifleAmmo=%d->%d Valid=%s"),
		*CurrentWeaponBoundInstanceId,
		*ServerInventorySecondId.ToString(),
		ProjectileCountBeforeSwitchCancel,
		ProjectileCountAfterSwitchCancel,
		RifleAmmoBeforeSwitchCancel,
		RifleAmmoAfterSwitchCancel,
		bClientObservedSwitchCancel ? TEXT("true") : TEXT("false"));

	if (!bClientObservedSwitchCancel)
	{
		FailTest(FString::Printf(
			TEXT("Switch-cancel client observation invalid; Bound=%s Expected=%s Weapon=%s"),
			*CurrentWeaponBoundInstanceId,
			*ServerInventorySecondId.ToString(),
			*GetNameSafe(CurrentWeaponAfterSwitch)));
	}
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedFireAbilityGrant_Implementation(
	int32 OwnerFireSpecCount,
	bool bRemoteFireSpecsHidden)
{
	bClientObservedFireGrant = OwnerFireSpecCount == 1 && bRemoteFireSpecsHidden;
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("GAS client report: FireAbilityGrant OwnerSpecs=%d RemoteHidden=%s Valid=%s"),
		OwnerFireSpecCount,
		bRemoteFireSpecsHidden ? TEXT("true") : TEXT("false"),
		bClientObservedFireGrant ? TEXT("true") : TEXT("false"));

	if (!bClientObservedFireGrant)
	{
		FailTest(FString::Printf(
			TEXT("Client Fire Ability grant observation invalid; OwnerSpecs=%d RemoteHidden=%s"),
			OwnerFireSpecCount,
			bRemoteFireSpecsHidden ? TEXT("true") : TEXT("false")));
	}
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedReloadEquipAbilityGrant_Implementation(
	int32 OwnerReloadSpecCount,
	bool bRemoteReloadSpecsHidden,
	int32 OwnerEquipSpecCount,
	bool bRemoteEquipSpecsHidden)
{
	bClientObservedReloadEquipGrant =
		OwnerReloadSpecCount == 1 &&
		OwnerEquipSpecCount == 1 &&
		bRemoteReloadSpecsHidden &&
		bRemoteEquipSpecsHidden;
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("GAS client report: ReloadEquipAbilityGrant Reload=%d/%s Equip=%d/%s Valid=%s"),
		OwnerReloadSpecCount,
		bRemoteReloadSpecsHidden ? TEXT("true") : TEXT("false"),
		OwnerEquipSpecCount,
		bRemoteEquipSpecsHidden ? TEXT("true") : TEXT("false"),
		bClientObservedReloadEquipGrant ? TEXT("true") : TEXT("false"));

	if (!bClientObservedReloadEquipGrant)
	{
		FailTest(FString::Printf(
			TEXT("Client Reload/Equip Ability grant observation invalid; Reload=%d/%s Equip=%d/%s"),
			OwnerReloadSpecCount,
			bRemoteReloadSpecsHidden ? TEXT("true") : TEXT("false"),
			OwnerEquipSpecCount,
			bRemoteEquipSpecsHidden ? TEXT("true") : TEXT("false")));
	}
}

void AShooterNetworkTestCoordinator::ServerReportClientTriggeredReload_Implementation(
	int32 RequestId)
{
	// 只接受当前请求编号，避免前一阶段的迟到 RPC 污染下一阶段判定。
	bClientTriggeredReload = RequestId == ReloadInputRequestId;
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Reload client report: Triggered request=%d current=%d"),
		RequestId,
		ReloadInputRequestId);
}

void AShooterNetworkTestCoordinator::ServerReportClientTriggeredReloadSwitch_Implementation()
{
	bClientTriggeredReloadSwitch = true;
	UE_LOG(LogShootGame, Display, TEXT("Reload client report: Switch triggered"));
}

void AShooterNetworkTestCoordinator::ServerReportClientTriggeredReloadSwitchBack_Implementation()
{
	bClientTriggeredReloadSwitchBack = true;
	UE_LOG(LogShootGame, Display, TEXT("Reload client report: Switch back triggered"));
}

void AShooterNetworkTestCoordinator::ServerReportClientTriggeredFireAfterReload_Implementation()
{
	bClientTriggeredFireAfterReload = true;
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Fire-after-reload server report: Fire triggered once"));
}

void AShooterNetworkTestCoordinator::ServerReportClientStoppedFireAfterReload_Implementation()
{
	bClientTriggeredStopFireAfterReload = true;
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Fire-after-reload server report: Stop triggered once"));
}

void AShooterNetworkTestCoordinator::ServerReportClientTriggeredEquipSingleReject_Implementation()
{
	bClientTriggeredEquipSingleReject = true;
	UE_LOG(LogShootGame, Display, TEXT("Equip-single-reject server report: switch triggered once"));
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedInventory_Implementation(
	int32 WeaponCount,
	const FString& ActiveWeaponInstanceId,
	bool bRemoteInventoryHidden)
{
	FGuid ObservedActiveId;
	FGuid::Parse(ActiveWeaponInstanceId, ObservedActiveId);

	// 初始 Owner Inventory 报告允许 Active 仍是第一或第二把：快速切换可能在报告前发生；
	// 切换后的精确 Active 由 Switch client report 另行验证。
	bClientObservedOwnerInventory = bServerInventoryPrepared &&
		WeaponCount == 2 &&
		(ObservedActiveId == ServerInventoryFirstId ||
			ObservedActiveId == ServerInventorySecondId);
	bClientObservedRemoteInventoryHidden = bRemoteInventoryHidden;

	UE_LOG(LogShootGame, Display, TEXT("Inventory client report: Count=%d Active=%s RemoteHidden=%s OwnerOk=%s"),
		WeaponCount,
		*ActiveWeaponInstanceId,
		bRemoteInventoryHidden ? TEXT("true") : TEXT("false"),
		bClientObservedOwnerInventory ? TEXT("true") : TEXT("false"));

	if (!bClientObservedOwnerInventory || !bClientObservedRemoteInventoryHidden)
	{
		FailTest(FString::Printf(
			TEXT("Client Inventory observation invalid; Count=%d Active=%s ExpectedActive=%s RemoteHidden=%s"),
			WeaponCount,
			*ActiveWeaponInstanceId,
			*ServerInventoryActiveId.ToString(),
			bRemoteInventoryHidden ? TEXT("true") : TEXT("false")));
	}
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedPickupAuthority_Implementation()
{
	UE_LOG(LogShootGame, Display, TEXT("Inventory client report: PickupAuthority rejected"));
	bClientObservedPickupAuthority = true;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedInventoryDeathClear_Implementation()
{
	UE_LOG(LogShootGame, Display, TEXT("Inventory client report: DeathClear"));
	bClientObservedDeathInventoryClear = true;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedInventoryRespawnEmpty_Implementation()
{
	UE_LOG(LogShootGame, Display, TEXT("Inventory client report: RespawnEmpty"));
	bClientObservedRespawnInventoryEmpty = true;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedGasHealthInit_Implementation()
{
	UE_LOG(LogShootGame, Display, TEXT("GAS client report: GasHealthInit"));
	bClientObservedGasHealthInit = true;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedGasHealthDamage_Implementation()
{
	UE_LOG(LogShootGame, Display, TEXT("GAS client report: GasHealthDamage"));
	bClientObservedGasHealthDamage = true;
}

void AShooterNetworkTestCoordinator::ServerReportClientObservedGasHealthRespawn_Implementation(
	bool bFullHealthHudEvent)
{
	UE_LOG(LogShootGame, Display, TEXT("GAS client report: GasHealthRespawn Hud=%s"), bFullHealthHudEvent ? TEXT("true") : TEXT("false"));
	bClientObservedGasHealthRespawn = true;
	bClientObservedFullHealthHudEvent |= bFullHealthHudEvent;
}

void AShooterNetworkTestCoordinator::HandleDamagedEvent(float LifePercent)
{
	LastDamagedLifePercent = LifePercent;
	// 观察过死亡之后收到满血事件，证明属性驱动的 HUD 事件链最终一致（复活满血）。
	// 用死亡观测而不是复活观测做门，避免与复活上报的竞态。
	if (bClientReportedDeath && FMath::IsNearlyEqual(LifePercent, 1.0f, 0.001f))
	{
		bClientObservedFullHealthHudEvent = true;
	}
}

void AShooterNetworkTestCoordinator::HandleClientHealthAttributeChanged(
	const FOnAttributeChangeData& ChangeData)
{
	LastClientAttributeHealth = ChangeData.NewValue;
	// HUD 事件链源头：属性变化事件在死亡后送达满血值（复活满血收敛）。
	if (bClientReportedDeath && ClientMaxHealthAttributeValue > 0.0f &&
		FMath::IsNearlyEqual(ChangeData.NewValue, ClientMaxHealthAttributeValue, 0.01f))
	{
		bClientObservedFullHealthHudEvent = true;
	}
}

AShooterCharacter* AShooterNetworkTestCoordinator::GetShooterCharacter() const
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetOwner());
	return PlayerController ? Cast<AShooterCharacter>(PlayerController->GetPawn()) : nullptr;
}

AShooterWeapon* AShooterNetworkTestCoordinator::GetCurrentWeapon(AShooterCharacter* Character) const
{
	if (!Character)
	{
		return nullptr;
	}

	const FObjectPropertyBase* CurrentWeaponProperty =
		FindFProperty<FObjectPropertyBase>(Character->GetClass(), TEXT("CurrentWeapon"));
	return CurrentWeaponProperty
		? Cast<AShooterWeapon>(CurrentWeaponProperty->GetObjectPropertyValue_InContainer(Character))
		: nullptr;
}

int32 AShooterNetworkTestCoordinator::CountProjectilesForInstigator(
	APawn* ProjectileInstigator) const
{
	int32 Count = 0;
	for (TActorIterator<AShooterProjectile> It(GetWorld()); It; ++It)
	{
		if (It->GetInstigator() == ProjectileInstigator)
		{
			++Count;
		}
	}

	return Count;
}

AController* AShooterNetworkTestCoordinator::GetOpponentController() const
{
	const AController* OwnerController = Cast<AController>(GetOwner());
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		if (PlayerController && PlayerController != OwnerController)
		{
			return PlayerController;
		}
	}

	return nullptr;
}

void AShooterNetworkTestCoordinator::FailTest(const FString& Reason)
{
	UE_LOG(LogShootGame, Error, TEXT("AUTOMATION_TEST_FAILURE: %s"), *Reason);
	GetWorldTimerManager().ClearTimer(PollTimer);
}
