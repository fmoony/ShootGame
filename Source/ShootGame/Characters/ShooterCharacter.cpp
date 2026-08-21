// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "ShooterInventoryComponent.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffectTypes.h"
#include "ShooterAbilitySystemComponent.h"
#include "ShooterAttributeSet.h"
#include "ShooterGameplayTags.h"
#include "ShooterGameplayEffectStatics.h"
#include "EnhancedInputComponent.h"
#include "Components/InputComponent.h"
#include "Components/PawnNoiseEmitterComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "InputActionValue.h"
#include "TimerManager.h"
#include "ShooterGameMode.h"
#include "ShooterPlayerState.h"
#include "ShootGame.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UnrealType.h"

AShooterCharacter::AShooterCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);

	// Create the first person mesh that will be viewed only by this character's owner
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
	FirstPersonMesh->SetupAttachment(GetMesh());
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));

	// Create the Camera Component
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	FirstPersonCameraComponent->SetupAttachment(FirstPersonMesh, FName("head"));
	FirstPersonCameraComponent->SetRelativeLocationAndRotation(FVector(-2.8f, 5.89f, 0.0f), FRotator(0.0f, 90.0f, -90.0f));
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
	FirstPersonCameraComponent->bEnableFirstPersonFieldOfView = true;
	FirstPersonCameraComponent->bEnableFirstPersonScale = true;
	FirstPersonCameraComponent->FirstPersonFieldOfView = 70.0f;
	FirstPersonCameraComponent->FirstPersonScale = 0.6f;

	// configure the character comps
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;

	GetCapsuleComponent()->SetCapsuleSize(34.0f, 96.0f);

	// Configure character movement
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;
	GetCharacterMovement()->AirControl = 0.5f;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 600.0f, 0.0f);

	// create the noise emitter component
	PawnNoiseEmitter = CreateDefaultSubobject<UPawnNoiseEmitterComponent>(TEXT("Pawn Noise Emitter"));

	// Inventory 组件作为武器持有关系的逻辑权威源；2A 只创建宿主，不接 Pickup。
	InventoryComponent = CreateDefaultSubobject<UShooterInventoryComponent>(TEXT("InventoryComponent"));

	bReplicates = true;
}

void AShooterCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 生命值只由服务器初始化，客户端通过初始复制获得。
	if (HasAuthority())
	{
		CurrentHP = MaxHP;
		bIsDead = false;
	}

	// update the HUD
	OnDamaged.Broadcast(MaxHP > 0.0f ? CurrentHP / MaxHP : 0.0f);
}

void AShooterCharacter::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	// 角色销毁时若仍是 ASC 的 Avatar，先解除 ActorInfo，避免残留旧 Pawn 引用。
	// 服务器复活或客户端收到新 Pawn 时会重新建立 ActorInfo。
	if (AShooterPlayerState* ShooterPlayerState = GetPlayerState<AShooterPlayerState>())
	{
		if (IsValid(ShooterPlayerState))
		{
			if (UAbilitySystemComponent* AbilitySystemComponent =
				ShooterPlayerState->GetAbilitySystemComponent();
				AbilitySystemComponent && AbilitySystemComponent->GetAvatarActor() == this)
			{
				AbilitySystemComponent->ClearActorInfo();
			}
		}
	}

	// 清理角色自身的延迟回调，避免销毁后继续触发。
	GetWorld()->GetTimerManager().ClearTimer(RespawnTimer);

	// 角色销毁 / 断线时先结束 GA_Fire / GA_Reload / GA_Equip，避免 Ability 生命周期残留旧 Avatar 或旧 Weapon。
	if (HasAuthority())
	{
		CancelFireAbility();
		CancelReloadAbility();
		CancelEquipAbility();
	}

	// 武器是服务器按角色生命周期生成的独立 Actor；Owner 关系不会自动级联销毁。
	// Inventory 是唯一回收入口：它销毁 BoundWeaponActors、清空 FastArray 与 Active。
	if (HasAuthority())
	{
		if (InventoryComponent)
		{
			InventoryComponent->ClearInventory();
		}

		OwnedWeapons.Empty();
		CurrentWeapon = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void AShooterCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// 服务器（含监听主机）在占有发生时建立 ASC ActorInfo：Owner=PlayerState，Avatar=本角色。
	InitializeAbilityActorInfo();
}

void AShooterCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// 客户端在 PlayerState 复制到达后建立 ASC ActorInfo；重生后的新 Pawn 会再次进入这里。
	InitializeAbilityActorInfo();
}

UAbilitySystemComponent* AShooterCharacter::GetAbilitySystemComponent() const
{
	const AShooterPlayerState* ShooterPlayerState = GetPlayerState<AShooterPlayerState>();
	return ShooterPlayerState ? ShooterPlayerState->GetAbilitySystemComponent() : nullptr;
}

void AShooterCharacter::InitializeAbilityActorInfo()
{
	AShooterPlayerState* ShooterPlayerState = GetPlayerState<AShooterPlayerState>();
	UAbilitySystemComponent* AbilitySystemComponent = ShooterPlayerState
		? ShooterPlayerState->GetAbilitySystemComponent()
		: nullptr;
	if (!ShooterPlayerState || !AbilitySystemComponent ||
		AbilitySystemComponent->GetAvatarActor() == this)
	{
		return;
	}

	AbilitySystemComponent->InitAbilityActorInfo(ShooterPlayerState, this);

	// 新 Avatar 不继承上一生命周期的死亡/开火/换弹状态；服务器防御性清理残留 Tag。
	if (HasAuthority())
	{
		AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Dead);
		AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Firing);
		AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
		AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Equipping);
	}

	// 注册属性集（所有机器都需要；属性数值由属性集复制收敛）。
	if (UShooterAttributeSet* AttributeSet = ShooterPlayerState->GetAttributeSet())
	{
		AbilitySystemComponent->AddAttributeSetSubobject(AttributeSet);
	}

	// Health 属性变化桥接由 PlayerState 持久绑定（跨重生有效），无需每角色重复绑定。

	// 服务器在每次出生时通过初始化效果写入 Health = MaxHealth = 角色配置值。
	if (HasAuthority())
	{
		UShooterGameplayEffectStatics::ApplyInitHealthEffect(AbilitySystemComponent, MaxHP);
	}

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("GAS ActorInfo init: Actor=%s Role=%d NetMode=%d ASC=%s OwnerActor=%s AvatarActor=%s PlayerState=%s Character=%s"),
		*GetName(),
		static_cast<int32>(GetLocalRole()),
		static_cast<int32>(GetNetMode()),
		*GetNameSafe(AbilitySystemComponent),
		*GetNameSafe(AbilitySystemComponent->GetOwnerActor()),
		*GetNameSafe(AbilitySystemComponent->GetAvatarActor()),
		*GetNameSafe(ShooterPlayerState),
		*GetName());
}

void AShooterCharacter::HandleHealthAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	UAbilitySystemComponent* AbilitySystemComponent = GetAbilitySystemComponent();
	const float MaxHealthValue = AbilitySystemComponent
		? AbilitySystemComponent->GetNumericAttribute(UShooterAttributeSet::GetMaxHealthAttribute())
		: MaxHP;

	if (HasAuthority())
	{
		// 服务器：同步旧 CurrentHP 复制镜像（观察者/回归仍依赖），并驱动死亡桥接。
		CurrentHP = ChangeData.NewValue;

		// 立即推送角色镜像与 PlayerState 上的属性集复制，
		// 避免属性数值在两次网络更新之间被合并，保证拥有者客户端能观察到伤害过程。
		ForceNetUpdate();
		if (AShooterPlayerState* ShooterPlayerState = GetPlayerState<AShooterPlayerState>())
		{
			ShooterPlayerState->ForceNetUpdate();
		}

		// 本地权威（含监听主机）也需要刷新本地 HUD。
		OnDamaged.Broadcast(MaxHealthValue > 0.0f ? ChangeData.NewValue / MaxHealthValue : 0.0f);

		if (ChangeData.NewValue <= 0.0f && !bIsDead)
		{
			AController* KillerController = PendingDeathInstigator;
			PendingDeathInstigator = nullptr;
			Die(KillerController);
		}
	}
	else
	{
		// 拥有者客户端：HUD 事件链由属性复制驱动（与 CurrentHP 镜像并存，重复广播幂等）。
		OnDamaged.Broadcast(MaxHealthValue > 0.0f ? ChangeData.NewValue / MaxHealthValue : 0.0f);
	}
}

void AShooterCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AShooterCharacter::DoJumpStart);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &AShooterCharacter::DoJumpEnd);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AShooterCharacter::MoveInput);

		// Looking/Aiming
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AShooterCharacter::LookInput);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AShooterCharacter::LookInput);

		// Firing
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Started, this, &AShooterCharacter::DoStartFiring);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Completed, this, &AShooterCharacter::DoStopFiring);

		// Reload：IA_Reload 只提交 Input.Reload，不直接改弹药。
		EnhancedInputComponent->BindAction(ReloadAction, ETriggerEvent::Started, this, &AShooterCharacter::DoReload);

		// Switch weapon
		EnhancedInputComponent->BindAction(SwitchWeaponAction, ETriggerEvent::Triggered, this, &AShooterCharacter::DoSwitchWeapon);
	}
	else
	{
		UE_LOG(LogShootGame, Error, TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}

}

void AShooterCharacter::MoveInput(const FInputActionValue& Value)
{
	// get the Vector2D move axis
	FVector2D MovementVector = Value.Get<FVector2D>();

	// pass the axis values to the move input
	DoMove(MovementVector.X, MovementVector.Y);
}

void AShooterCharacter::LookInput(const FInputActionValue& Value)
{
	// get the Vector2D look axis
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// pass the axis values to the aim input
	DoAim(LookAxisVector.X, LookAxisVector.Y);
}

void AShooterCharacter::DoAim(float Yaw, float Pitch)
{
	if (GetController())
	{
		// pass the rotation inputs
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AShooterCharacter::DoMove(float Right, float Forward)
{
	if (GetController())
	{
		// pass the move inputs
		AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void AShooterCharacter::DoJumpStart()
{
	// pass Jump to the character
	Jump();
}

void AShooterCharacter::DoJumpEnd()
{
	// pass StopJumping to the character
	StopJumping();
}

float AShooterCharacter::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// 客户端不能自行扣血，且死亡后不重复处理伤害。
	if (!HasAuthority() || bIsDead || Damage <= 0.0f)
	{
		return 0.0f;
	}

	UAbilitySystemComponent* AbilitySystemComponent = GetAbilitySystemComponent();
	if (!AbilitySystemComponent)
	{
		return 0.0f;
	}

	// 实际作用量不超过当前属性生命；属性归零由 Health 变化桥接进入现有死亡闭环。
	const float CurrentHealth = AbilitySystemComponent->GetNumericAttribute(
		UShooterAttributeSet::GetHealthAttribute());
	const float AppliedDamage = FMath::Clamp(Damage, 0.0f, CurrentHealth);
	if (AppliedDamage <= 0.0f)
	{
		return 0.0f;
	}

	// 保持引擎伤害广播链（OnTakeAnyDamage 等）。
	Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	// 击杀者交给 Health 归零桥接，用于现有 Death/Kill/计分闭环。
	PendingDeathInstigator = EventInstigator;
	UShooterGameplayEffectStatics::ApplyDamageEffect(
		AbilitySystemComponent,
		AppliedDamage,
		EventInstigator,
		DamageCauser);
	PendingDeathInstigator = nullptr;

	return AppliedDamage;
}

void AShooterCharacter::OnRep_CurrentHP()
{
	OnDamaged.Broadcast(MaxHP > 0.0f ? CurrentHP / MaxHP : 0.0f);
}

void AShooterCharacter::PostNetReceive()
{
	Super::PostNetReceive();
	ApplyRemoteAimPitch();
}

void AShooterCharacter::OnRep_IsDead()
{
	if (bIsDead)
	{
		ApplyDeathState();
	}
}

void AShooterCharacter::CancelFireAbility()
{
	if (UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
		Cast<UShooterAbilitySystemComponent>(GetAbilitySystemComponent()))
	{
		ShooterAbilitySystemComponent->CancelAbilitiesByTag(
			ShooterGameplayTags::Input_Fire);
	}
}

void AShooterCharacter::CancelReloadAbility()
{
	if (UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
		Cast<UShooterAbilitySystemComponent>(GetAbilitySystemComponent()))
	{
		ShooterAbilitySystemComponent->CancelAbilitiesByTag(
			ShooterGameplayTags::Input_Reload);
	}
}

void AShooterCharacter::CancelEquipAbility()
{
	if (UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
		Cast<UShooterAbilitySystemComponent>(GetAbilitySystemComponent()))
	{
		ShooterAbilitySystemComponent->CancelAbilitiesByTag(
			ShooterGameplayTags::Input_Equip_Next);
	}
}

void AShooterCharacter::DoStartFiring()
{
	// 输入只提交给 ASC：GA_Fire 为 ServerOnly，客户端按 Input.Fire 发起激活，
	// GAS 自动把激活请求可靠转发到服务器。
	if (UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
		Cast<UShooterAbilitySystemComponent>(GetAbilitySystemComponent()))
	{
		ShooterAbilitySystemComponent->AbilityInputTagPressed(
			ShooterGameplayTags::Input_Fire);
		return;
	}

	// PlayerState / ASC 尚未就绪时静默忽略，不保留旧 RPC 双路径。
	UE_LOG(
		LogShootGame,
		Warning,
		TEXT("DoStartFiring ignored: ShooterASC unavailable for %s"),
		*GetName());
}

void AShooterCharacter::DoStopFiring()
{
	// 松开输入同样进入 ASC；服务器活动 GA_Fire 收到释放后停止 Weapon 并结束 Ability。
	if (UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
		Cast<UShooterAbilitySystemComponent>(GetAbilitySystemComponent()))
	{
		ShooterAbilitySystemComponent->AbilityInputTagReleased(
			ShooterGameplayTags::Input_Fire);
		return;
	}

	UE_LOG(
		LogShootGame,
		Warning,
		TEXT("DoStopFiring ignored: ShooterASC unavailable for %s"),
		*GetName());
}

void AShooterCharacter::DoReload()
{
	// 输入只提交给 ASC：GA_Reload 为 ServerOnly，客户端按 Input.Reload 发起激活，
	// GAS 自动把激活请求可靠转发到服务器。
	if (UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
		Cast<UShooterAbilitySystemComponent>(GetAbilitySystemComponent()))
	{
		ShooterAbilitySystemComponent->AbilityInputTagPressed(
			ShooterGameplayTags::Input_Reload);
		return;
	}

	UE_LOG(
		LogShootGame,
		Warning,
		TEXT("DoReload ignored: ShooterASC unavailable for %s"),
		*GetName());
}

void AShooterCharacter::MulticastPlayFiringMontage_Implementation(UAnimMontage* Montage)
{
	if (!Montage)
	{
		return;
	}

	// 第三人称 mesh 在所有客户端上播放开火动画
	if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
	{
		AnimInstance->Montage_Play(Montage);
	}

	// 第一人称手臂只对拥有者播放
	if (IsLocallyControlled())
	{
		if (UAnimInstance* FPAnimInstance = GetFirstPersonMesh()->GetAnimInstance())
		{
			FPAnimInstance->Montage_Play(Montage);
		}
	}
}

void AShooterCharacter::DoSwitchWeapon()
{
	// 输入只提交给 ASC：GA_Equip 为 ServerOnly，客户端按 Input.Equip.Next 发起激活，
	// GAS 自动把激活请求可靠转发到服务器。
	if (UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
		Cast<UShooterAbilitySystemComponent>(GetAbilitySystemComponent()))
	{
		ShooterAbilitySystemComponent->AbilityInputTagPressed(
			ShooterGameplayTags::Input_Equip_Next);
		return;
	}

	UE_LOG(
		LogShootGame,
		Warning,
		TEXT("DoSwitchWeapon ignored: ShooterASC unavailable for %s"),
		*GetName());
}

void AShooterCharacter::AttachWeaponMeshes(AShooterWeapon* Weapon)
{
	const FAttachmentTransformRules AttachmentRule(EAttachmentRule::SnapToTarget, false);

	// attach the weapon actor
	Weapon->AttachToActor(this, AttachmentRule);

	// attach the weapon meshes
	Weapon->GetFirstPersonMesh()->AttachToComponent(GetFirstPersonMesh(), AttachmentRule, FirstPersonWeaponSocket);
	Weapon->GetThirdPersonMesh()->AttachToComponent(GetMesh(), AttachmentRule, FirstPersonWeaponSocket);
	
}

void AShooterCharacter::PlayFiringMontage(UAnimMontage* Montage)
{
	// 服务器开火时广播动画到所有客户端
	MulticastPlayFiringMontage(Montage);
}

void AShooterCharacter::AddWeaponRecoil(float Recoil)
{
	// apply the recoil as pitch input
	AddControllerPitchInput(Recoil);
}

void AShooterCharacter::UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize)
{
	OnBulletCountUpdated.Broadcast(MagazineSize, CurrentAmmo);
}

FVector AShooterCharacter::GetWeaponTargetLocation()
{
	// Server-authoritative aim: remote camera component rotation is not reliable on
	// the server, while ControlRotation is updated by the owning connection.
	FHitResult OutHit;

	const FVector Start = GetPawnViewLocation();
	const FVector End = Start + (GetControlRotation().Vector() * MaxAimDistance);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, QueryParams);

	// return either the impact point or the trace end
	return OutHit.bBlockingHit ? OutHit.ImpactPoint : OutHit.TraceEnd;
}

void AShooterCharacter::HandleWeaponAddedToInventory(const FGuid& InstanceId)
{
	if (!HasAuthority() || !InventoryComponent)
	{
		return;
	}

	AShooterWeapon* AddedWeapon = InventoryComponent->FindWeaponActor(InstanceId);
	if (!AddedWeapon)
	{
		return;
	}

	// OwnedWeapons 是兼容镜像，逻辑权威仍在 Inventory。
	OwnedWeapons.AddUnique(AddedWeapon);

	// 与旧 Pickup 表现一致：拾取新武器后立即装备。
	if (CurrentWeapon)
	{
		CurrentWeapon->DeactivateWeapon();
	}

	CurrentWeapon = AddedWeapon;
	InventoryComponent->SetActiveWeaponInstanceId(InstanceId);
	AddedWeapon->ActivateWeapon();
	ApplyCurrentWeapon();
}

bool AShooterCharacter::CommitActiveWeapon(const FGuid& InstanceId)
{
	if (!HasAuthority() || !InventoryComponent || !InstanceId.IsValid())
	{
		return false;
	}

	AShooterWeapon* TargetWeapon = InventoryComponent->FindWeaponActor(InstanceId);
	if (!IsValid(TargetWeapon) || TargetWeapon->GetOwner() != this)
	{
		return false;
	}

	// 旧武器先停火并隐藏；同一事务内更新 Inventory Active、公开 CurrentWeapon 与新武器可见性。
	if (IsValid(CurrentWeapon) && CurrentWeapon != TargetWeapon)
	{
		CurrentWeapon->DeactivateWeapon();
	}

	InventoryComponent->SetActiveWeaponInstanceId(InstanceId);
	CurrentWeapon = TargetWeapon;
	TargetWeapon->ActivateWeapon();
	ApplyCurrentWeapon();
	ForceNetUpdate();

	UE_LOG(
		LogShootGame,
		Display,
		TEXT("Character CommitActiveWeapon: Actor=%s InstanceId=%s Weapon=%s"),
		*GetName(),
		*InstanceId.ToString(),
		*GetNameSafe(TargetWeapon));
	return true;
}

void AShooterCharacter::AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass)
{
	// 只有服务器可以生成并装备武器
	if (!HasAuthority())
	{
		return;
	}

	// 是否已经拥有该类型的武器？
	AShooterWeapon* OwnedWeapon = FindWeaponOfType(WeaponClass);

	if (!OwnedWeapon)
	{
		// 生成新武器
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot;

		AShooterWeapon* AddedWeapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponClass, GetActorTransform(), SpawnParams);

		if (AddedWeapon)
		{
			// 加入拥有列表（第一版仅服务器维护）
			OwnedWeapons.Add(AddedWeapon);

			// 已有武器时先停用旧武器
			if (CurrentWeapon)
			{
				CurrentWeapon->DeactivateWeapon();
			}

			// 切换到新武器并刷新表现
			CurrentWeapon = AddedWeapon;
			ApplyCurrentWeapon();
		}
	}
}

void AShooterCharacter::OnRep_CurrentWeapon(AShooterWeapon* PreviousWeapon)
{
	if (IsValid(PreviousWeapon) && PreviousWeapon != CurrentWeapon)
	{
		PreviousWeapon->DeactivateWeapon();
	}

	// 客户端根据复制的武器引用刷新表现
	ApplyCurrentWeapon();
	ApplyRemoteAimPitch();
}

void AShooterCharacter::ApplyCurrentWeapon()
{
	if (!CurrentWeapon)
	{
		return;
	}

	// 将武器网格附着到角色（幂等，可重复调用）
	AttachWeaponMeshes(CurrentWeapon);

	// 切换 AnimBP 并更新 HUD
	OnWeaponActivated(CurrentWeapon);
}

void AShooterCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 所有客户端都需要当前武器，以显示第三人称视角
	DOREPLIFETIME(AShooterCharacter, CurrentWeapon);
	DOREPLIFETIME(AShooterCharacter, CurrentHP);
	DOREPLIFETIME(AShooterCharacter, bIsDead);
}

void AShooterCharacter::OnWeaponActivated(AShooterWeapon* Weapon)
{
	// update the bullet counter
	OnBulletCountUpdated.Broadcast(Weapon->GetMagazineSize(), Weapon->GetBulletCount());

	// set the character mesh AnimInstances
	GetFirstPersonMesh()->SetAnimInstanceClass(Weapon->GetFirstPersonAnimInstanceClass());
	GetMesh()->SetAnimInstanceClass(Weapon->GetThirdPersonAnimInstanceClass());
}

void AShooterCharacter::OnWeaponDeactivated(AShooterWeapon* Weapon)
{
	// unused
}

void AShooterCharacter::OnSemiWeaponRefire()
{
	// unused
}

AShooterWeapon* AShooterCharacter::FindWeaponOfType(TSubclassOf<AShooterWeapon> WeaponClass) const
{
	// check each owned weapon
	for (AShooterWeapon* Weapon : OwnedWeapons)
	{
		if (Weapon->IsA(WeaponClass))
		{
			return Weapon;
		}
	}

	// weapon not found
	return nullptr;

}

void AShooterCharacter::Die(AController* KillerController)
{
	if (!HasAuthority() || bIsDead)
	{
		return;
	}

	// 死亡事务：先设置 State.Dead 并取消 GA_Fire / GA_Reload / GA_Equip，再执行 Death Clear。
	if (UAbilitySystemComponent* AbilitySystemComponent = GetAbilitySystemComponent())
	{
		AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Dead);
	}
	CancelFireAbility();
	CancelReloadAbility();
	CancelEquipAbility();

	// Death Clear：停火 -> 销毁全部 WeaponActor -> Inventory Clear -> Active Invalid -> CurrentWeapon null。
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->StopFiring();
	}
	if (InventoryComponent)
	{
		InventoryComponent->ClearInventory();
	}
	OwnedWeapons.Empty();
	CurrentWeapon = nullptr;

	bIsDead = true;
	ApplyDeathState();
	ForceNetUpdate();

	if (AShooterPlayerState* VictimState = GetPlayerState<AShooterPlayerState>())
	{
		VictimState->AddDeath();
	}

	if (KillerController && KillerController != GetController())
	{
		if (AShooterPlayerState* KillerState =
			KillerController->GetPlayerState<AShooterPlayerState>())
		{
			KillerState->AddKill();

			if (AShooterGameMode* GameMode =
				Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
			{
				GameMode->IncrementTeamScore(KillerState->GetTeamId());
			}
		}
	}

	// 只有服务器安排角色销毁和重生。
	GetWorld()->GetTimerManager().SetTimer(RespawnTimer, this, &AShooterCharacter::OnRespawn, RespawnTime, false);
}

void AShooterCharacter::ApplyRemoteAimPitch()
{
	// 本地玩家和服务器都有 Controller，原 AnimBP 路径可以直接读取 ControlRotation。
	// 这里只有观察其他玩家的客户端需要使用 APawn 已复制的 RemoteViewPitch。
	if (GetLocalRole() != ROLE_SimulatedProxy || !GetMesh())
	{
		return;
	}

	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	FNumericProperty* PitchProperty = AnimInstance
		? FindFProperty<FNumericProperty>(AnimInstance->GetClass(), TEXT("PitchN"))
		: nullptr;
	if (!PitchProperty || !PitchProperty->IsFloatingPoint())
	{
		return;
	}

	// 模板 AnimBP 的 PitchN 定义为瞄准前向与世界 Up 的点积，即 sin(Pitch)。
	void* PitchValue = PitchProperty->ContainerPtrToValuePtr<void>(AnimInstance);
	PitchProperty->SetFloatingPointPropertyValue(
		PitchValue,
		GetBaseAimRotation().Vector().Z);
}

void AShooterCharacter::ApplyDeathState()
{
	// deactivate the weapon
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->DeactivateWeapon();
	}

	// stop character movement
	GetCharacterMovement()->StopMovementImmediately();

	// 只禁用本机拥有者的输入；模拟代理本来就没有本地输入。
	if (IsLocallyControlled())
	{
		DisableInput(nullptr);
	}

	// reset the bullet counter UI
	OnBulletCountUpdated.Broadcast(0, 0);

	// 专用服务器不执行纯表现蓝图。
	if (GetNetMode() != NM_DedicatedServer)
	{
		BP_OnDeath();
	}
}

void AShooterCharacter::OnRespawn()
{
	if (!HasAuthority())
	{
		return;
	}

	AController* PlayerController = GetController();
	if (AShooterGameMode* GameMode = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode());
		GameMode && IsValid(PlayerController))
	{
		GameMode->RestartPlayerAfterDeath(PlayerController);
		return;
	}

	// 非 Shooter GameMode 下仍清理死亡角色，但不会尝试客户端自行复活。
	Destroy();
}
