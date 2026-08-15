// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterCharacter.h"
#include "ShooterWeapon.h"
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
	// 清理角色自身的延迟回调，避免销毁后继续触发。
	GetWorld()->GetTimerManager().ClearTimer(RespawnTimer);

	// 武器是服务器按角色生命周期生成的独立 Actor；Owner 关系不会自动级联销毁。
	// 角色死亡销毁或玩家断线时由服务器显式回收，避免遗留不可见的复制 Actor。
	if (HasAuthority())
	{
		for (AShooterWeapon* Weapon : OwnedWeapons)
		{
			if (IsValid(Weapon))
			{
				Weapon->Destroy();
			}
		}

		OwnedWeapons.Empty();
		CurrentWeapon = nullptr;
	}

	Super::EndPlay(EndPlayReason);
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

	const float AppliedDamage = FMath::Clamp(
		Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser),
		0.0f,
		CurrentHP);
	if (AppliedDamage <= 0.0f)
	{
		return 0.0f;
	}

	CurrentHP = FMath::Clamp(CurrentHP - AppliedDamage, 0.0f, MaxHP);
	OnRep_CurrentHP();

	// Have we depleted HP?
	if (CurrentHP <= 0.0f)
	{
			Die(EventInstigator);
	}

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

void AShooterCharacter::DoStartFiring()
{
	// 客户端只提交输入意图，由服务器权威执行开火
	ServerStartFire();
}

void AShooterCharacter::DoStopFiring()
{
	// 客户端只提交输入意图，由服务器权威执行停火
	ServerStopFire();
}

void AShooterCharacter::ServerStartFire_Implementation()
{
	// 服务器校验：只有控制该角色的客户端能请求开火
	if (!GetController() || GetController()->GetPawn() != this)
	{
		return;
	}

	// 服务器校验：必须有装备的武器才能开火
	if (!bIsDead && CurrentWeapon)
	{
		CurrentWeapon->StartFiring();
	}
}

void AShooterCharacter::ServerStopFire_Implementation()
{
	// 服务器校验：只有控制该角色的客户端能请求停火
	if (!GetController() || GetController()->GetPawn() != this)
	{
		return;
	}

	if (CurrentWeapon)
	{
		CurrentWeapon->StopFiring();
	}
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
	ServerSwitchWeapon();
}

void AShooterCharacter::ServerSwitchWeapon_Implementation()
{
	// 只有控制该角色的客户端可以请求切枪，死亡角色不能切枪。
	if (!GetController() || GetController()->GetPawn() != this || bIsDead)
	{
		return;
	}

	// ensure we have at least two weapons two switch between
	if (OwnedWeapons.Num() > 1 && CurrentWeapon && OwnedWeapons.Contains(CurrentWeapon))
	{
		// deactivate the old weapon
		CurrentWeapon->DeactivateWeapon();

		// find the index of the current weapon in the owned list
		int32 WeaponIndex = OwnedWeapons.Find(CurrentWeapon);

		// is this the last weapon?
		if (WeaponIndex == OwnedWeapons.Num() - 1)
		{
			// loop back to the beginning of the array
			WeaponIndex = 0;
		}
		else {
			// select the next weapon index
			++WeaponIndex;
		}

		// set the new weapon as current
		CurrentWeapon = OwnedWeapons[WeaponIndex];

		// activate the new weapon
		CurrentWeapon->ActivateWeapon();
		ForceNetUpdate();
	}
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
