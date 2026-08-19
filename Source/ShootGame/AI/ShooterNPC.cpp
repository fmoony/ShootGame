// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterNPC.h"
#include "ShooterWeapon.h"
#include "ShootGame.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffectTypes.h"
#include "ShooterAbilitySystemComponent.h"
#include "ShooterAttributeSet.h"
#include "ShooterGameplayAbility_Fire.h"
#include "ShooterGameplayEffectStatics.h"
#include "ShooterGameplayTags.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/World.h"
#include "ShooterGameMode.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

AShooterNPC::AShooterNPC()
{
	AbilitySystemComponent = CreateDefaultSubobject<UShooterAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	FireAbilityClass = UShooterGameplayAbility_Fire::StaticClass();
	AbilitySystemComponent->SetIsReplicated(true);
	// Minimal：NPC 只需服务器维护能力状态，客户端只同步最小数据。
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	AttributeSet = CreateDefaultSubobject<UShooterAttributeSet>(TEXT("AttributeSet"));
}

UAbilitySystemComponent* AShooterNPC::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AShooterNPC::BeginPlay()
{
	Super::BeginPlay();

	// NPC 的 ASC Owner 与 Avatar 都是自身，在专用服务器上无需 Controller 也能独立初始化。
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
		AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Dead);
		AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Firing);
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("GAS NPC ActorInfo init: Actor=%s Role=%d NetMode=%d ASC=%s OwnerActor=%s AvatarActor=%s"),
			*GetName(),
			static_cast<int32>(GetLocalRole()),
			static_cast<int32>(GetNetMode()),
			*GetNameSafe(AbilitySystemComponent),
			*GetNameSafe(AbilitySystemComponent->GetOwnerActor()),
			*GetNameSafe(AbilitySystemComponent->GetAvatarActor()));
	}

	// 注册属性集并绑定 Health 变化桥接；服务器写入出生生命。
	if (AbilitySystemComponent)
	{
		if (AttributeSet)
		{
			AbilitySystemComponent->AddAttributeSetSubobject(AttributeSet.Get());
		}
		BindHealthAttributeDelegate();
		if (HasAuthority())
		{
			// 出生生命沿用 NPC 的 CurrentHP 配置值（模板默认 100）。
			UShooterGameplayEffectStatics::ApplyInitHealthEffect(AbilitySystemComponent, CurrentHP);
		}
		GrantFireAbility();
	}

	// spawn the weapon
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	Weapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponClass, GetActorTransform(), SpawnParams);
}

void AShooterNPC::GrantFireAbility()
{
	if (!HasAuthority() || !AbilitySystemComponent || !FireAbilityClass)
	{
		return;
	}

	// 幂等授予：每个 NPC ASC 只保留一个 Fire Ability Spec。
	if (AbilitySystemComponent->FindAbilitySpecFromClass(FireAbilityClass))
	{
		return;
	}

	const FGameplayAbilitySpec FireAbilitySpec(
		FireAbilityClass,
		/*AbilityLevel*/1,
		INDEX_NONE,
		this);
	AbilitySystemComponent->GiveAbility(FireAbilitySpec);
}

int32 AShooterNPC::GetFireAbilitySpecCount() const
{
	if (!AbilitySystemComponent)
	{
		return 0;
	}

	return AbilitySystemComponent->GetAbilitySpecCountForClass(FireAbilityClass);
}

void AShooterNPC::BindHealthAttributeDelegate()
{
	if (bHealthAttributeDelegateBound || !AbilitySystemComponent)
	{
		return;
	}

	AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
		UShooterAttributeSet::GetHealthAttribute())
		.AddUObject(this, &AShooterNPC::HandleHealthAttributeChanged);
	bHealthAttributeDelegateBound = true;
}

void AShooterNPC::HandleHealthAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	// NPC 属性不向客户端复制（Minimal），桥接只对服务器有意义。
	if (!HasAuthority())
	{
		return;
	}

	// 镜像保留给蓝图 / StateTree 可能的读取；死亡进入现有 Die() 流程。
	CurrentHP = ChangeData.NewValue;
	if (ChangeData.NewValue <= 0.0f && !bIsDead)
	{
		Die();
	}
}

void AShooterNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// 销毁时先结束 GA_Fire，再解除 ActorInfo，避免 Ability 残留旧 Avatar。
	if (HasAuthority())
	{
		CancelFireAbility();
	}

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->ClearActorInfo();
	}

	// clear the death timer
	GetWorld()->GetTimerManager().ClearTimer(DeathTimer);
}

float AShooterNPC::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// 服务器权威：NPC 伤害只由服务器（弹丸命中）施加；死亡后不重复处理。
	if (!HasAuthority() || bIsDead || Damage <= 0.0f)
	{
		return 0.0f;
	}

	UAbilitySystemComponent* NpcAbilitySystemComponent = GetAbilitySystemComponent();
	if (!NpcAbilitySystemComponent)
	{
		return 0.0f;
	}

	// 实际作用量不超过当前属性生命；属性归零由 Health 变化桥接进入现有 Die() 流程。
	const float CurrentHealth = NpcAbilitySystemComponent->GetNumericAttribute(
		UShooterAttributeSet::GetHealthAttribute());
	const float AppliedDamage = FMath::Clamp(Damage, 0.0f, CurrentHealth);
	if (AppliedDamage <= 0.0f)
	{
		return 0.0f;
	}

	// 保持引擎伤害广播链（OnTakeAnyDamage 等）。
	Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	UShooterGameplayEffectStatics::ApplyDamageEffect(
		NpcAbilitySystemComponent,
		AppliedDamage,
		EventInstigator,
		DamageCauser);

	return AppliedDamage;
}

void AShooterNPC::AttachWeaponMeshes(AShooterWeapon* WeaponToAttach)
{
	const FAttachmentTransformRules AttachmentRule(EAttachmentRule::SnapToTarget, false);

	// attach the weapon actor
	WeaponToAttach->AttachToActor(this, AttachmentRule);

	// NPC 只挂接第三人称武器 Mesh，不再借用玩家第一人称手臂。
	WeaponToAttach->GetThirdPersonMesh()->AttachToComponent(GetMesh(), AttachmentRule, ThirdPersonWeaponSocket);
}

void AShooterNPC::PlayFiringMontage(UAnimMontage* Montage)
{
	// unused
}

void AShooterNPC::AddWeaponRecoil(float Recoil)
{
	// unused
}

void AShooterNPC::UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize)
{
	// unused
}

FVector AShooterNPC::GetWeaponTargetLocation()
{
	// NPC 不再借用玩家第一人称摄像机，瞄准起点改用 Pawn 视点。
	const FVector AimSource = GetPawnViewLocation();

	FVector AimDir, AimTarget = FVector::ZeroVector;

	// do we have an aim target?
	if (CurrentAimTarget)
	{
		// target the actor location
		AimTarget = CurrentAimTarget->GetActorLocation();

		// apply a vertical offset to target head/feet
		AimTarget.Z += FMath::RandRange(MinAimOffsetZ, MaxAimOffsetZ);

		// get the aim direction and apply randomness in a cone
		AimDir = (AimTarget - AimSource).GetSafeNormal();
		AimDir = UKismetMathLibrary::RandomUnitVectorInConeInDegrees(AimDir, AimVarianceHalfAngle);

		
	} else {

		// no aim target, so use the NPC's own facing (driven by the AI Controller)
		AimDir = UKismetMathLibrary::RandomUnitVectorInConeInDegrees(GetActorForwardVector(), AimVarianceHalfAngle);

	}

	// calculate the unobstructed aim target location
	AimTarget = AimSource + (AimDir * AimRange);

	// run a visibility trace to see if there's obstructions
	FHitResult OutHit;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	GetWorld()->LineTraceSingleByChannel(OutHit, AimSource, AimTarget, ECC_Visibility, QueryParams);

	// return either the impact point or the trace end
	return OutHit.bBlockingHit ? OutHit.ImpactPoint : OutHit.TraceEnd;
}

void AShooterNPC::AddWeaponClass(const TSubclassOf<AShooterWeapon>& InWeaponClass)
{
	// unused
}

void AShooterNPC::OnWeaponActivated(AShooterWeapon* InWeapon)
{
	// unused
}

void AShooterNPC::OnWeaponDeactivated(AShooterWeapon* InWeapon)
{
	// unused
}

void AShooterNPC::OnSemiWeaponRefire()
{
	// are we still shooting?
	if (bIsShooting)
	{
		// fire the weapon
		Weapon->StartFiring();
	}
}

void AShooterNPC::Die()
{
	// ignore if already dead
	if (bIsDead)
	{
		return;
	}

	// 死亡事务：设置 State.Dead 并取消 GA_Fire，再进入现有死亡流程。
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Dead);
	}
	CancelFireAbility();
	if (Weapon)
	{
		Weapon->StopFiring();
	}

	// raise the dead flag
	bIsDead = true;

	// increment the team score
	if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->IncrementTeamScore(TeamByte);
	}

	// disable capsule collision
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// stop movement
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->StopActiveMovement();

	// enable ragdoll physics on the third person mesh
	GetMesh()->SetCollisionProfileName(RagdollCollisionProfile);
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetPhysicsBlendWeight(1.0f);

	// schedule actor destruction
	GetWorld()->GetTimerManager().SetTimer(DeathTimer, this, &AShooterNPC::DeferredDestruction, DeferredDestructionTime, false);
}

void AShooterNPC::DeferredDestruction()
{
	Destroy();
}

void AShooterNPC::StartShooting(AActor* ActorToShoot)
{
	// AI 只提交开火意图：保存目标并让 GA_Fire 在服务器执行武器事务。
	CurrentAimTarget = ActorToShoot;
	bIsShooting = true;

	if (UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
		Cast<UShooterAbilitySystemComponent>(GetAbilitySystemComponent()))
	{
		ShooterAbilitySystemComponent->AbilityInputTagPressed(
			ShooterGameplayTags::Input_Fire);
	}
}

void AShooterNPC::StopShooting()
{
	// AI 只提交停火意图；服务器活动 GA_Fire 收到释放后结束武器事务。
	bIsShooting = false;

	if (UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
		Cast<UShooterAbilitySystemComponent>(GetAbilitySystemComponent()))
	{
		ShooterAbilitySystemComponent->AbilityInputTagReleased(
			ShooterGameplayTags::Input_Fire);
	}
}

void AShooterNPC::CancelFireAbility()
{
	if (UShooterAbilitySystemComponent* ShooterAbilitySystemComponent =
		Cast<UShooterAbilitySystemComponent>(GetAbilitySystemComponent()))
	{
		ShooterAbilitySystemComponent->CancelAbilitiesByTag(
			ShooterGameplayTags::Input_Fire);
	}
}
