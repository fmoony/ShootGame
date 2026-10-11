// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterRifleTransitionFixture.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "UObject/UnrealType.h"
#include "Weapons/Data/ShooterWeaponConfigRow.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/Subsystems/ShooterWeaponRuntimeSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogRifleInheritedCamera, Log, All);

namespace RifleTransitionFixture
{
	/** 保留原生图求值；可选取证回调发生在引擎已有的 Cached Pose 作用域内。 */
	struct FObservationProxy : FAnimInstanceProxy
	{
		explicit FObservationProxy(UShooterRifleTransitionTestAnimInstance* InInstance)
			: FAnimInstanceProxy(InInstance), Instance(InInstance) {}

		virtual bool Evaluate(FPoseContext& Output) override
		{
			if (!Instance->CapturePoseCallback)
			{
				return false;
			}
			EvaluateAnimationNode(Output);
			Instance->CapturePoseCallback(*this);
			return true;
		}

		UShooterRifleTransitionTestAnimInstance* Instance;
	};

	const FShooterWeaponConfigRow* GetRifleRow()
	{
		UDataTable* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/Shooter/Data/DT_WeaponData.DT_WeaponData"));
		return Table ? Table->FindRow<FShooterWeaponConfigRow>(TEXT("Rifle"), TEXT("RifleTransitionFixture")) : nullptr;
	}
}

FAnimInstanceProxy* UShooterRifleTransitionTestAnimInstance::CreateAnimInstanceProxy()
{
	return new RifleTransitionFixture::FObservationProxy(this);
}

bool UShooterRifleTransitionTestAnimInstance::ApplyFixtureSnapshot(
	UShooterThirdPersonAnimInstance* Instance, float Pitch, bool bIK)
{
	const FShooterWeaponConfigRow* Row = RifleTransitionFixture::GetRifleRow();
	if (!Instance || !Row || !Row->ThirdPersonHoldSequence || !Row->ThirdPersonAimOffset)
	{
		return false;
	}
	Instance->ThirdPersonHoldSequence = Row->ThirdPersonHoldSequence;
	Instance->ThirdPersonAimOffset = Row->ThirdPersonAimOffset;
	Instance->ThirdPersonReloadSequence = Row->ThirdPersonReloadSequence;
	Instance->bHasEquippedWeapon = true;
	Instance->WeaponUpperBodyWeight = 1.0f;
	Instance->bIsReloading = false;
	Instance->bIsFiring = false;
	Instance->bShouldMove = false;
	Instance->LocomotionGroundSpeed = 0.0f;
	Instance->Velocity = FVector::ZeroVector;
	Instance->bIsInAir = false;
	Instance->AimPitchN = FMath::Sin(FMath::DegreesToRadians(Pitch));
	Instance->AimPitchDegrees = Pitch;
	Instance->AimDirectionWorld = FRotator(Pitch, 0.0f, 0.0f).Vector();
	Instance->bAimTargetWorldValid = false;

	USkeletalMeshComponent* Mesh = Instance->GetSkelMeshComponent();
	USkeletalMeshComponent* Rifle = nullptr;
	for (USceneComponent* Child : Mesh->GetAttachChildren())
	{
		if (Child->GetFName() == TEXT("RifleMesh"))
		{
			Rifle = Cast<USkeletalMeshComponent>(Child);
			break;
		}
	}
	const bool bHasSockets = Rifle && Mesh->DoesSocketExist(TEXT("HandGrip_R")) &&
		Mesh->DoesSocketExist(TEXT("HandGrip_L")) && Rifle->DoesSocketExist(Row->MuzzleSocketName) &&
		Rifle->DoesSocketExist(Row->LeftHandGripSocketName);
	Instance->bAimIKEnabled = bIK && bHasSockets;
	Instance->bLeftHandIKEnabled = bIK && bHasSockets;
	Instance->AimIKPresentationAlpha = Instance->bAimIKEnabled ? 1.0f : 0.0f;
	Instance->LeftHandIKPresentationAlpha = Instance->bLeftHandIKEnabled ? 1.0f : 0.0f;
	if (bHasSockets)
	{
		Rifle->UpdateComponentToWorld();
		const FTransform Hand = Mesh->GetSocketTransform(TEXT("hand_r"));
		Instance->HandToMuzzle = ComputeHandToMuzzleTransform(Hand, Rifle->GetSocketTransform(Row->MuzzleSocketName));
		Instance->LeftHandGripInRightHandSpace = ComputeLeftHandGripInRightHandSpace(
			Hand, Rifle->GetSocketTransform(Row->LeftHandGripSocketName));
		Instance->HandGripInLeftHandSpace = ComputeHandGripInLeftHandSpace(
			Mesh->GetSocketTransform(TEXT("hand_l")), Mesh->GetSocketTransform(TEXT("HandGrip_L")));
	}
	return true;
}

void UShooterRifleTransitionTestAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	ApplyFixtureSnapshot(this, RifleTestPitch, bRifleTestIK);
	const bool bLower = RifleTestGoal == ERifleTransitionTestGoal::LowReady;
	RifleTestPlayRate = bLower ? 1.0f : -1.0f;
	const FAnimNode_SequencePlayer* Player = GetTransitionPlayer();
	RifleTestStartPosition = bLower || !Player || !Player->GetSequence()
		? 0.0f : Player->GetSequence()->GetPlayLength();
}

const FAnimNode_SequencePlayer* UShooterRifleTransitionTestAnimInstance::GetTransitionPlayer() const
{
	const IAnimClassInterface* Interface = IAnimClassInterface::GetFromClass(GetClass());
	if (Interface)
	{
		for (const FStructProperty* Property : Interface->GetAnimNodeProperties())
		{
			if (Property->Struct == FAnimNode_SequencePlayer::StaticStruct())
			{
				const auto* Player = Property->ContainerPtrToValuePtr<FAnimNode_SequencePlayer>(this);
				if (Player->GetSequence() && Player->GetSequence()->GetName().StartsWith(TEXT("MM_Rifle_AimToLowReady")))
				{
					return Player;
				}
			}
		}
	}
	return nullptr;
}

FAnimInstanceProxy& UShooterRifleTransitionTestAnimInstance::GetFixtureProxy(UAnimInstance* Instance)
{
	return *GetProxyOnGameThreadStatic<FAnimInstanceProxy>(Instance);
}

bool UShooterRifleTransitionTestAnimInstance::GetRifleTestLowerRequested() const
{
	return RifleTestGoal == ERifleTransitionTestGoal::LowReady;
}

bool UShooterRifleTransitionTestAnimInstance::GetRifleTestRaiseRequested() const
{
	return !GetRifleTestLowerRequested();
}

bool UShooterRifleTransitionTestAnimInstance::GetRifleTestAtLowEndpoint() const
{
	const FAnimNode_SequencePlayer* Player = GetTransitionPlayer();
	return GetRifleTestLowerRequested() && Player && Player->GetSequence() &&
		Player->GetAccumulatedTime() >= Player->GetSequence()->GetPlayLength() - UE_SMALL_NUMBER;
}

bool UShooterRifleTransitionTestAnimInstance::GetRifleTestAtAimEndpoint() const
{
	const FAnimNode_SequencePlayer* Player = GetTransitionPlayer();
	return GetRifleTestRaiseRequested() && Player && Player->GetAccumulatedTime() <= UE_SMALL_NUMBER;
}

AShooterRifleTransitionPreview::AShooterRifleTransitionPreview()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("FixtureRoot")));
	ThirdPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ThirdPersonMesh"));
	ThirdPersonMesh->SetupAttachment(GetRootComponent());
	ThirdPersonMesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	ThirdPersonMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	ThirdPersonMesh->bEnableUpdateRateOptimizations = false;
	ThirdPersonMesh->SetUpdateAnimationInEditor(true);
	ThirdPersonMesh->AddTickPrerequisiteActor(this);
	RifleMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("RifleMesh"));
	RifleMesh->SetupAttachment(ThirdPersonMesh, TEXT("HandGrip_R"));
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonMesh"));
	FirstPersonMesh->SetupAttachment(ThirdPersonMesh);
	FirstPersonMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	FirstPersonMesh->bEnableUpdateRateOptimizations = false;
	FirstPersonMesh->SetUpdateAnimationInEditor(true);
	FirstPersonMesh->AddTickPrerequisiteComponent(ThirdPersonMesh);
	FirstPersonMesh->SetVisibility(false);
	FirstPersonRifleMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonRifleMesh"));
	FirstPersonRifleMesh->SetupAttachment(FirstPersonMesh, TEXT("HandGrip_R"));
	FirstPersonRifleMesh->SetVisibility(false);
	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(FirstPersonMesh, TEXT("head"));
	FirstPersonCamera->SetAutoActivate(false);
}

void AShooterRifleTransitionPreview::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	const FShooterWeaponConfigRow* Row = RifleTransitionFixture::GetRifleRow();
	USkeletalMesh* Body = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (!Row || !Body)
	{
		return;
	}
	ThirdPersonMesh->SetSkeletalMesh(Body);
	FirstPersonMesh->SetSkeletalMesh(Body);
	RifleMesh->SetSkeletalMesh(Row->ThirdPersonMesh.LoadSynchronous());
	FirstPersonRifleMesh->SetSkeletalMesh(Row->FirstPersonMesh.LoadSynchronous());
	UAnimBlueprint* Preview = LoadObject<UAnimBlueprint>(nullptr,
		TEXT("/Game/Experiments/RifleTransition/Blender_20261011/ABP_RifleBlender_GripCorrected_Test"));
	ThirdPersonMesh->SetAnimInstanceClass(Preview ? Preview->GeneratedClass.Get() : nullptr);
	FirstPersonMesh->SetAnimInstanceClass(LoadClass<UAnimInstance>(nullptr,
		TEXT("/Game/Shooter/Animation/FirstPerson/ABP_FP_Player.ABP_FP_Player_C")));
	Tick(0.0f);
}

void AShooterRifleTransitionPreview::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (auto* Instance = Cast<UShooterRifleTransitionTestAnimInstance>(ThirdPersonMesh->GetAnimInstance()))
	{
		Instance->RifleTestGoal = RifleTestGoal;
		Instance->RifleTestPitch = RifleTestPitch;
		Instance->RifleTestNaturalPitchStrength = FMath::Clamp(RifleTestNaturalPitchStrength, 0.0f, 1.0f);
		Instance->bRifleTestIK = bRifleTestIK;
	}
}

void AShooterRifleTransitionPreview::PlayFireAnimationOnly()
{
	const FShooterWeaponConfigRow* Row = RifleTransitionFixture::GetRifleRow();
	if (Row && Row->FiringMontage && ThirdPersonMesh->GetAnimInstance())
	{
		ThirdPersonMesh->GetAnimInstance()->Montage_Play(Row->FiringMontage);
	}
}

void AShooterRifleTransitionPreview::ToggleRifleTestGoal()
{
	RifleTestGoal = RifleTestGoal == ERifleTransitionTestGoal::ArmedAim
		? ERifleTransitionTestGoal::LowReady : ERifleTransitionTestGoal::ArmedAim;
	Tick(0.0f);
}

void AShooterRifleTransitionPreview::CycleRifleTestPitch()
{
	RifleTestPitch = RifleTestPitch < -45.0f ? 0.0f
		: RifleTestPitch < -15.0f ? -60.0f : RifleTestPitch < 15.0f ? 30.0f : RifleTestPitch < 45.0f ? 60.0f : -30.0f;
	Tick(0.0f);
}

void AShooterRifleTransitionPreview::CycleRifleTestNaturalPitchStrength()
{
	RifleTestNaturalPitchStrength = RifleTestNaturalPitchStrength < 0.25f ? 0.5f
		: RifleTestNaturalPitchStrength < 0.75f ? 1.0f : 0.0f;
	Tick(0.0f);
}

AShooterRifleTransitionDebugPawn::AShooterRifleTransitionDebugPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessPlayer = EAutoReceiveInput::Player0;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("DebugRoot")));
	PreviewComponent = CreateDefaultSubobject<UChildActorComponent>(TEXT("PreviewComponent"));
	PreviewComponent->SetupAttachment(GetRootComponent());
	PreviewComponent->SetChildActorClass(AShooterRifleTransitionPreview::StaticClass());
	PreviewComponent->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);
}

AShooterRifleTransitionPreview* AShooterRifleTransitionDebugPawn::GetPreview() const
{
	return Cast<AShooterRifleTransitionPreview>(PreviewComponent->GetChildActor());
}

void AShooterRifleTransitionDebugPawn::ConfigurePreview()
{
	if (AShooterRifleTransitionPreview* Preview = GetPreview())
	{
		Preview->SetOwner(this);
		Preview->bRifleTestIK = false;
		Preview->bAlwaysUpdateSourcePose = false;
		Preview->AddTickPrerequisiteActor(this);
		Preview->FirstPersonCamera->SetRelativeLocationAndRotation(FVector(-2.8f, 5.89f, 0.0f),
			FRotator(0.0f, 90.0f, -90.0f));
		Preview->FirstPersonCamera->bEnableFirstPersonFieldOfView = true;
		Preview->FirstPersonCamera->bEnableFirstPersonScale = true;
		Preview->FirstPersonCamera->FirstPersonFieldOfView = 90.0f;
		Preview->FirstPersonCamera->FirstPersonScale = 0.6f;
		Preview->FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
		Preview->FirstPersonRifleMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
		Preview->FirstPersonMesh->SetOnlyOwnerSee(true);
		Preview->FirstPersonRifleMesh->SetOnlyOwnerSee(true);
	}
}

void AShooterRifleTransitionDebugPawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ConfigurePreview();
}

void AShooterRifleTransitionDebugPawn::BeginPlay()
{
	Super::BeginPlay();
	ConfigurePreview();
	if (auto* Player = Cast<APlayerController>(GetController()); Player && Player->IsLocalController())
	{
		Player->SetControlRotation(GetActorRotation());
		Player->SetInputMode(FInputModeGameOnly());
		Player->bShowMouseCursor = false;
	}
}

void AShooterRifleTransitionDebugPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AShooterRifleTransitionPreview* Preview = GetPreview();
	if (!IsLocallyControlled() || !Preview)
	{
		return;
	}
	Preview->RifleTestPitch = FMath::Clamp(FRotator::NormalizeAxis(GetControlRotation().Pitch), -60.0f, 60.0f);
	SetActorRotation(FRotator(0.0f, GetControlRotation().Yaw, 0.0f));
	Preview->ThirdPersonMesh->SetOwnerNoSee(!bThirdPersonView);
	Preview->RifleMesh->SetOwnerNoSee(!bThirdPersonView);
	Preview->FirstPersonMesh->SetVisibility(!bThirdPersonView);
	Preview->FirstPersonRifleMesh->SetVisibility(!bThirdPersonView);
	Preview->FirstPersonCamera->SetActive(!bThirdPersonView);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(uint64(GetUniqueID()), 0.2f, FColor::Cyan, GetDebugText(), false);
	}
}

void AShooterRifleTransitionDebugPawn::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	AShooterRifleTransitionPreview* Preview = GetPreview();
	if (!IsLocallyControlled() || !Preview)
	{
		Super::CalcCamera(DeltaTime, OutResult);
		return;
	}
	if (bThirdPersonView)
	{
		const FVector Target = GetActorLocation() + FVector(0.0f, 0.0f, 100.0f);
		OutResult.Location = Target - GetActorForwardVector() * 300.0f + GetActorRightVector() * 90.0f;
		OutResult.Rotation = (Target - OutResult.Location).Rotation();
		OutResult.FOV = 90.0f;
		OutResult.bUseFirstPersonParameters = false;
		return;
	}
	// 相机所属 Actor 不是 Pawn，需显式采用控制器朝向，避免把 head 俯仰重复加入视角。
	Preview->FirstPersonCamera->SetWorldRotation(GetControlRotation());
	Preview->FirstPersonCamera->GetCameraView(DeltaTime, OutResult);
	OutResult.PerspectiveNearClipPlane = 1.0f;
}

void AShooterRifleTransitionDebugPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	PlayerInputComponent->BindKey(EKeys::One, IE_Pressed, this, &AShooterRifleTransitionDebugPawn::ToggleGoal);
	PlayerInputComponent->BindKey(EKeys::Two, IE_Pressed, this, &AShooterRifleTransitionDebugPawn::CyclePitch);
	PlayerInputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AShooterRifleTransitionDebugPawn::CycleStrength);
	PlayerInputComponent->BindKey(EKeys::V, IE_Pressed, this, &AShooterRifleTransitionDebugPawn::ToggleView);
	PlayerInputComponent->BindAxisKey(EKeys::MouseX, this, &AShooterRifleTransitionDebugPawn::LookYaw);
	PlayerInputComponent->BindAxisKey(EKeys::MouseY, this, &AShooterRifleTransitionDebugPawn::LookPitch);
}

void AShooterRifleTransitionDebugPawn::ToggleGoal()
{
	if (AShooterRifleTransitionPreview* Preview = GetPreview())
	{
		Preview->ToggleRifleTestGoal();
		LastDebugAction = TEXT("[1] Goal toggled");
	}
}

void AShooterRifleTransitionDebugPawn::CyclePitch()
{
	if (AShooterRifleTransitionPreview* Preview = GetPreview())
	{
		Preview->CycleRifleTestPitch();
		if (Controller)
		{
			Controller->SetControlRotation(FRotator(Preview->RifleTestPitch, GetControlRotation().Yaw, 0.0f));
		}
		LastDebugAction = TEXT("[2] Pitch cycled");
	}
}

void AShooterRifleTransitionDebugPawn::CycleStrength()
{
	if (AShooterRifleTransitionPreview* Preview = GetPreview())
	{
		Preview->CycleRifleTestNaturalPitchStrength();
		LastDebugAction = TEXT("[3] Mask strength cycled");
	}
}

void AShooterRifleTransitionDebugPawn::ToggleView()
{
	bThirdPersonView = !bThirdPersonView;
	LastDebugAction = TEXT("[V] View toggled");
}

void AShooterRifleTransitionDebugPawn::LookYaw(float Value)
{
	if (Controller && !FMath::IsNearlyZero(Value))
	{
		Controller->SetControlRotation(FRotator(GetControlRotation().Pitch, GetControlRotation().Yaw + Value, 0.0f));
	}
}

void AShooterRifleTransitionDebugPawn::LookPitch(float Value)
{
	if (Controller && !FMath::IsNearlyZero(Value))
	{
		const float Pitch = FMath::Clamp(FRotator::NormalizeAxis(GetControlRotation().Pitch) + Value, -60.0f, 60.0f);
		Controller->SetControlRotation(FRotator(Pitch, GetControlRotation().Yaw, 0.0f));
	}
}

FString AShooterRifleTransitionDebugPawn::GetDebugText() const
{
	const AShooterRifleTransitionPreview* Preview = GetPreview();
	if (!Preview)
	{
		return TEXT("Rifle Debug: preview unavailable");
	}
	auto* Instance = Cast<UShooterRifleTransitionTestAnimInstance>(Preview->ThirdPersonMesh->GetAnimInstance());
	const FAnimNode_SequencePlayer* Player = Instance ? Instance->GetTransitionPlayer() : nullptr;
	FName State = NAME_None;
	if (Instance)
	{
		const FAnimInstanceProxy& Proxy = UShooterRifleTransitionTestAnimInstance::GetFixtureProxy(Instance);
		const FAnimNode_StateMachine* Machine = Proxy.GetStateMachineInstanceFromName(TEXT("WeaponAction"));
		State = Machine ? Machine->GetCurrentStateName() : NAME_None;
	}
	return FString::Printf(
		TEXT("RIFLE DEBUG | %s | IK OFF\nState: %s  Goal: %s\nPitch: %.0f  Mask: %.2f\n")
		TEXT("Transition time: %.3f  Rate: %.0f\nLast: %s\n[1] Goal  [2] Pitch  [3] Mask  [V] FP/TP  Mouse: Look"),
		bThirdPersonView ? TEXT("TP") : TEXT("FP"), *State.ToString(),
		Preview->RifleTestGoal == ERifleTransitionTestGoal::LowReady ? TEXT("LowReady") : TEXT("ArmedAim"),
		Preview->RifleTestPitch, Preview->RifleTestNaturalPitchStrength,
		Player ? Player->GetAccumulatedTime() : 0.0f, Instance ? Instance->RifleTestPlayRate : 0.0f, *LastDebugAction);
}

UShooterRifleCharacterDebugComponent::UShooterRifleCharacterDebugComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UShooterRifleCharacterDebugComponent::SetDebugView(AShooterCharacter* Character)
{
	// 外部观察复用父蓝图的备用相机，不改变正式第一人称相机参数或 CalcCamera。
	TInlineComponentArray<UCameraComponent*> Cameras(Character);
	for (UCameraComponent* Camera : Cameras)
	{
		const bool bFirstPerson = Camera == Character->GetFirstPersonCameraComponent();
		Camera->SetActive(bThirdPersonView ? !bFirstPerson : bFirstPerson);
	}
	Character->GetMesh()->SetOwnerNoSee(!bThirdPersonView);
	Character->GetMesh()->SetFirstPersonPrimitiveType(bThirdPersonView
		? EFirstPersonPrimitiveType::None : EFirstPersonPrimitiveType::WorldSpaceRepresentation);
	Character->GetFirstPersonMesh()->SetVisibility(!bThirdPersonView);
	if (AShooterWeapon* Weapon = Character->GetCurrentWeapon())
	{
		Weapon->GetThirdPersonMesh()->SetOwnerNoSee(!bThirdPersonView);
		Weapon->GetThirdPersonMesh()->SetFirstPersonPrimitiveType(bThirdPersonView
			? EFirstPersonPrimitiveType::None : EFirstPersonPrimitiveType::WorldSpaceRepresentation);
		Weapon->GetFirstPersonMesh()->SetVisibility(!bThirdPersonView);
	}
}

void UShooterRifleCharacterDebugComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwner());
	APlayerController* Controller = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	// 本组件只服务单机隔离关卡，不产生联机装备或输入请求。
	if (!Character || !Character->IsLocallyControlled() || !Controller || GetWorld()->GetNetMode() != NM_Standalone)
	{
		return;
	}
	UShooterWeaponRuntimeSubsystem* Runtime = GetWorld()->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
	if (!bEquipAttempted && Runtime && Runtime->IsRuntimeInitialized())
	{
		bEquipAttempted = true;
		UShooterInventoryComponent* Inventory = Character->GetInventoryComponent();
		AShooterWeapon* Weapon = Inventory->FindWeaponByWeaponId(TEXT("Rifle"));
		if (!Weapon)
		{
			Weapon = Runtime->AcquireWeapon(TEXT("Rifle"), Character, Character);
			if (Weapon && Inventory->AddWeapon(Weapon) != EShooterInventoryAddResult::Added)
			{
				Runtime->ReleaseWeapon(Weapon);
				Weapon = nullptr;
			}
		}
		const bool bEquipped = Weapon && Character->GetEquipmentComponent()->EquipWeapon(Weapon);
		LastAction = bEquipped ? TEXT("Ready: real Rifle equipped") : TEXT("Rifle equip failed");
		SetDebugView(Character);
		AddTickPrerequisiteActor(Controller);
		Character->GetMesh()->AddTickPrerequisiteComponent(this);
	}
	bool bAction = false;
	if (Controller->WasInputKeyJustPressed(EKeys::One))
	{
		Goal = Goal == ERifleTransitionTestGoal::ArmedAim
			? ERifleTransitionTestGoal::LowReady : ERifleTransitionTestGoal::ArmedAim;
		LastAction = TEXT("1: Toggle goal");
		bAction = true;
	}
	if (Controller->WasInputKeyJustPressed(EKeys::Two))
	{
		const float Pitch = FRotator::NormalizeAxis(Controller->GetControlRotation().Pitch);
		const float Samples[] = { 0.0f, 30.0f, 60.0f, -30.0f, -60.0f };
		int32 SampleIndex = 0;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Samples); ++Index)
		{
			if (FMath::IsNearlyEqual(Pitch, Samples[Index], 0.1f))
			{
				SampleIndex = (Index + 1) % UE_ARRAY_COUNT(Samples);
				break;
			}
		}
		Controller->SetControlRotation(FRotator(Samples[SampleIndex], Controller->GetControlRotation().Yaw, 0.0f));
		LastAction = TEXT("2: Cycle pitch");
		bAction = true;
	}
	if (Controller->WasInputKeyJustPressed(EKeys::Three))
	{
		NaturalPitchStrength = NaturalPitchStrength < 0.25f ? 0.5f : NaturalPitchStrength < 0.75f ? 1.0f : 0.0f;
		LastAction = TEXT("3: Cycle mask");
		bAction = true;
	}
	if (Controller->WasInputKeyJustPressed(EKeys::V))
	{
		bThirdPersonView = !bThirdPersonView;
		SetDebugView(Character);
		LastAction = TEXT("V: Toggle view");
		bAction = true;
	}
	auto* Instance = Cast<UShooterRifleTransitionTestAnimInstance>(Character->GetMesh()->GetAnimInstance());
	if (!Instance)
	{
		return;
	}
	Instance->RifleTestGoal = Goal;
	Instance->RifleTestPitch = FRotator::NormalizeAxis(Controller->GetControlRotation().Pitch);
	Instance->RifleTestNaturalPitchStrength = FMath::Clamp(NaturalPitchStrength, 0.0f, 1.0f);
	Instance->bRifleTestIK = false;
	const FAnimInstanceProxy& Proxy = UShooterRifleTransitionTestAnimInstance::GetFixtureProxy(Instance);
	const FAnimNode_StateMachine* Machine = Proxy.GetStateMachineInstanceFromName(TEXT("WeaponAction"));
	const FAnimNode_SequencePlayer* Player = Instance->GetTransitionPlayer();
	const FName State = Machine ? Machine->GetCurrentStateName() : NAME_None;
	const UCameraComponent* Camera = Character->GetFirstPersonCameraComponent();
	const float BodyDelta = FVector::Distance(
		Character->GetMesh()->GetSocketTransform(TEXT("pelvis"), RTS_Component).GetLocation(),
		Character->GetFirstPersonMesh()->GetSocketTransform(TEXT("pelvis"), RTS_Component).GetLocation());
	const FString Debug = FString::Printf(
		TEXT("RIFLE SHOOTER CAMERA | %s | TP IK OFF\nState: %s  Goal: %s\nPitch: %.1f  Mask: %.2f\n")
		TEXT("Transition: %.3f  Rate: %.0f\nFP Scale: %.2f  FP body delta: %.2f cm\nWeapon: %s\n")
		TEXT("Last: %s\n[1] Goal  [2] Pitch  [3] Mask  [V] FP/TP  Mouse: Shooter Look"),
		bThirdPersonView ? TEXT("TP") : TEXT("FP"), *State.ToString(),
		Goal == ERifleTransitionTestGoal::LowReady ? TEXT("LowReady") : TEXT("ArmedAim"),
		Instance->RifleTestPitch, Instance->RifleTestNaturalPitchStrength,
		Player ? Player->GetAccumulatedTime() : 0.0f, Instance->RifleTestPlayRate,
		Camera->FirstPersonScale, BodyDelta, *GetNameSafe(Character->GetCurrentWeapon()), *LastAction);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 0.2f, FColor::Cyan, Debug, false);
	}
	LogElapsed += DeltaTime;
	if (bAction || LogElapsed >= 1.0f)
	{
		LogElapsed = 0.0f;
		UE_LOG(LogRifleInheritedCamera, Display, TEXT("RIFLE_INHERITED_CAMERA %s"), *Debug.Replace(TEXT("\n"), TEXT(" | ")));
	}
}
