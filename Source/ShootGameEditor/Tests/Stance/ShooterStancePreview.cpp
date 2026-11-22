#include "ShooterStancePreview.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/AimOffsetBlendSpace.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/Skeleton.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "AnimNodes/AnimNode_RotationOffsetBlendSpace.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_BlendListByInt.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_CopyPoseFromMesh.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_RotationOffsetBlendSpace.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_TwoWayBlend.h"
#include "Camera/CameraComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Factories/AnimBlueprintFactory.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "Weapons/Data/ShooterWeaponConfigRow.h"

DEFINE_LOG_CATEGORY_STATIC(LogStancePreview, Log, All);

namespace StanceNative
{
	const FString Folder = TEXT("/Game/Experiments/Stance_20261022/NativeGraph");
	const FString SourceFolder = TEXT("/Game/Experiments/Stance_20261022/SourceLayer");
	const FString AwareFolder = TEXT("/Game/Experiments/Stance_20261022/StanceAware_20261116/Refined");
	const FString FourFolder = TEXT("/Game/Experiments/Stance_20261022/FourDirection_20261117");
	constexpr float MoveSpeed = 150.0f;
	constexpr float AwareMoveSpeed = 45.0f;

	/** 只读原生播放器状态，供运行证据标注；不调用 Update、Evaluate 或时间设置接口。 */
	void ReadPlayerTimes(const UAnimInstance* Instance, float& IdleTime, float& MovePhase, float& MoveLength, float& AimY)
	{
		IdleTime = MovePhase = MoveLength = AimY = 0;
		for (TFieldIterator<FStructProperty> It(Instance->GetClass()); It; ++It)
		{
			if (It->Struct == FAnimNode_BlendSpacePlayer::StaticStruct())
			{
				const FAnimNode_BlendSpacePlayer* Player = It->ContainerPtrToValuePtr<FAnimNode_BlendSpacePlayer>(Instance);
				if (Player->GetBlendSpace() && (Player->GetBlendSpace()->GetPathName().Contains(TEXT("/SourceLayer/")) ||
					Player->GetBlendSpace()->GetName() == TEXT("BS_StanceAware_Lower")))
				{
					MovePhase = Player->GetAccumulatedTime();
					MoveLength = Player->GetCurrentAssetLength();
				}
			}
			if (It->Struct == FAnimNode_RotationOffsetBlendSpace::StaticStruct())
			{
				const auto* Aim = It->ContainerPtrToValuePtr<FAnimNode_RotationOffsetBlendSpace>(Instance);
				AimY = Aim->GetPosition().Y;
			}
			if (It->Struct == FAnimNode_SequencePlayer::StaticStruct())
			{
				const FAnimNode_SequencePlayer* Player = It->ContainerPtrToValuePtr<FAnimNode_SequencePlayer>(Instance);
				if (Player->GetSequence() && Player->GetSequence()->GetName().Contains(TEXT("CrouchIdle")))
				{
					IdleTime = Player->GetAccumulatedTime();
				}
			}
		}
	}
}

AShooterStancePreviewPawn::AShooterStancePreviewPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("PreviewRoot")));
	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(GetRootComponent());
	Body->SetRelativeRotation(FRotator(0, -90, 0));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastHiddenShadow(true);
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Body->bEnableUpdateRateOptimizations = false;
	Body->SetDisablePostProcessBlueprint(true);
	Rifle = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Rifle"));
	Rifle->SetupAttachment(Body, TEXT("HandGrip_R"));
	Rifle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Rifle->SetCastHiddenShadow(true);
	FirstPersonArms = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonArms"));
	FirstPersonArms->SetupAttachment(Body);
	FirstPersonArms->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonArms->SetOnlyOwnerSee(true);
	FirstPersonArms->SetCastShadow(false);
	FirstPersonArms->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	FirstPersonArms->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	FirstPersonArms->bEnableUpdateRateOptimizations = false;
	FirstPersonArms->SetDisablePostProcessBlueprint(true);
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	Camera->SetupAttachment(FirstPersonArms, TEXT("head"));
	FirstPersonRifle = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonRifle"));
	FirstPersonRifle->SetupAttachment(FirstPersonArms, TEXT("HandGrip_R"));
	FirstPersonRifle->SetOnlyOwnerSee(true);
	FirstPersonRifle->SetCastShadow(false);
	FirstPersonRifle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonRifle->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
}

void AShooterStancePreviewPawn::BeginPlay()
{
	Super::BeginPlay();
	Body->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")));
	const AShooterFourDirectionPreviewGameMode* PreviewGameMode = GetWorld()->GetAuthGameMode<AShooterFourDirectionPreviewGameMode>();
	bFourDirection = PreviewGameMode != nullptr;
	if (bFourDirection)
	{
		UClass* Graph = PreviewGameMode->PreviewAnimClass.LoadSynchronous();
		check(Graph);
		Body->SetAnimInstanceClass(Graph);
		UE_LOG(LogStancePreview, Display, TEXT("STANCE_PREVIEW_INITIAL AnimClass=%s GameMode=%s"),
			*Graph->GetPathName(), *PreviewGameMode->GetClass()->GetPathName());
		// 只定位原生输入过滤器；零输入暂停不能从默认中心生成前后混合。
		for (TFieldIterator<FStructProperty> It(Body->GetAnimInstance()->GetClass()); It; ++It)
		{
			if (It->Struct == FAnimNode_BlendSpacePlayer::StaticStruct())
			{
				It->ContainerPtrToValuePtr<FAnimNode_BlendSpacePlayer>(Body->GetAnimInstance())->
					SnapToPosition(FVector(1, 0, 0));
			}
		}
		Body->AddTickPrerequisiteActor(this);
		View = 1;
		PreviewForward = 1;
		PreviewRight = 0;
		PreviewMode = 2;
		Rifle->SetVisibility(false);
		FirstPersonArms->SetVisibility(false);
		FirstPersonRifle->SetVisibility(false);
		bCapture = FParse::Param(FCommandLine::Get(), TEXT("StanceFourCapture"));
		if (bCapture)
		{
			FString Relative;
			FParse::Value(FCommandLine::Get(), TEXT("StanceNativeOutput="), Relative);
			check(!Relative.IsEmpty());
			CaptureOutput = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / Relative);
			check(FPaths::IsUnderDirectory(CaptureOutput, FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir())));
			check(!FPaths::FileExists(CaptureOutput));
			CaptureReport = TEXT("time,case,forward,right,filtered_forward,filtered_right,phase,length,")
				TEXT("bone,x,y,z,qx,qy,qz,qw,world_x,world_y,world_z\n");
		}
		return;
	}
	UDataTable* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/Shooter/Data/DT_WeaponData"));
	const FShooterWeaponConfigRow* Row =
		Table ? Table->FindRow<FShooterWeaponConfigRow>(TEXT("Rifle"), TEXT("StanceNative")) : nullptr;
	check(Row);
	MuzzleSocketName = Row->MuzzleSocketName;
	Rifle->SetSkeletalMesh(Row->ThirdPersonMesh.LoadSynchronous());
	FirstPersonRifle->SetSkeletalMesh(Row->FirstPersonMesh.LoadSynchronous());
	UClass* CharacterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Shooter/Blueprints/Characters/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	check(CharacterClass);
	const AShooterCharacter* Defaults = CharacterClass->GetDefaultObject<AShooterCharacter>();
	const UCameraComponent* SourceCamera = Defaults->GetFirstPersonCameraComponent();
	check(SourceCamera && Defaults->GetFirstPersonMesh());
	Camera->SetRelativeTransform(SourceCamera->GetRelativeTransform());
	Camera->bUsePawnControlRotation = SourceCamera->bUsePawnControlRotation;
	Camera->FieldOfView = SourceCamera->FieldOfView;
	Camera->AspectRatio = SourceCamera->AspectRatio;
	Camera->bConstrainAspectRatio = SourceCamera->bConstrainAspectRatio;
	Camera->bEnableFirstPersonFieldOfView = SourceCamera->bEnableFirstPersonFieldOfView;
	Camera->bEnableFirstPersonScale = SourceCamera->bEnableFirstPersonScale;
	Camera->FirstPersonFieldOfView = SourceCamera->FirstPersonFieldOfView;
	Camera->FirstPersonScale = SourceCamera->FirstPersonScale;
	FirstPersonArms->SetSkeletalMesh(Defaults->GetFirstPersonMesh()->GetSkeletalMeshAsset());
	FirstPersonArms->SetRelativeTransform(Defaults->GetFirstPersonMesh()->GetRelativeTransform());
	bBaseline = FParse::Param(FCommandLine::Get(), TEXT("StanceBaseline"));
	const FString GraphFolder = bBaseline ? StanceNative::Folder : StanceNative::AwareFolder;
	const FString TPName = bBaseline ? TEXT("ABP_StanceNative_TP") : TEXT("ABP_StanceAware_TP");
	const FString FPName = bBaseline ? TEXT("ABP_StanceNative_FP") : TEXT("ABP_StanceAware_FP");
	UClass* TPClass = LoadClass<UAnimInstance>(nullptr, *(GraphFolder / (TPName + TEXT(".") + TPName + TEXT("_C"))));
	UClass* FPClass = LoadClass<UAnimInstance>(nullptr, *(GraphFolder / (FPName + TEXT(".") + FPName + TEXT("_C"))));
	check(TPClass && FPClass);
	Body->SetAnimInstanceClass(TPClass);
	FirstPersonArms->SetAnimInstanceClass(FPClass);
	Body->AddTickPrerequisiteActor(this);
	FirstPersonArms->AddTickPrerequisiteComponent(Body);
	bPreviewLowerOnly = FParse::Param(FCommandLine::Get(), TEXT("StanceNativeRaw"));
	bStationary = FParse::Param(FCommandLine::Get(), TEXT("StanceNativeStationary"));
	bCapture = FParse::Param(FCommandLine::Get(), TEXT("StanceNativeCapture"));
	bPitchSweep = FParse::Param(FCommandLine::Get(), TEXT("StanceNativePitchSweep"));
	bTransitionCapture = FParse::Param(FCommandLine::Get(), TEXT("StanceTransitions"));
	FParse::Value(FCommandLine::Get(), TEXT("StanceNativeDirections="), CaptureDirections);
	check(CaptureDirections >= 8 && CaptureDirections <= 64);
	if (bCapture)
	{
		FString Relative;
		FParse::Value(FCommandLine::Get(), TEXT("StanceNativeOutput="), Relative);
		check(!Relative.IsEmpty());
		CaptureOutput = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / Relative);
		check(FPaths::IsUnderDirectory(CaptureOutput, FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir())));
		check(!FPaths::FileExists(CaptureOutput));
		CaptureReport = TEXT("time,case,stable,mode,low,raw,case_time,forward,right,pitch,yaw,")
			TEXT("idle_time,move_phase,move_length,mesh,bone,x,y,z,wx,wy,wz,qx,qy,qz,qw,ao_y,pitch_age,crouch_alpha\n");
		CaptureViewReport = TEXT("time,case,kind,pitch,alpha,wx,wy,wz,qx,qy,qz,qw\n");
		if (APlayerController* PC = Cast<APlayerController>(Controller))
		{
			PC->SetIgnoreLookInput(true);
			PC->SetControlRotation(FRotator::ZeroRotator);
		}
	}
	UE_LOG(LogStancePreview, Display, TEXT("STANCE_NATIVE TP=%s FP=%s Raw=%d"),
		*TPClass->GetPathName(), *FPClass->GetPathName(), bPreviewLowerOnly ? 1 : 0);
	UpdatePresentation();
}

void AShooterStancePreviewPawn::Stand() { PreviewMode = 0; }
void AShooterStancePreviewPawn::Crouch() { PreviewMode = 1; }
void AShooterStancePreviewPawn::Walk() { PreviewMode = 2; }
void AShooterStancePreviewPawn::Slide() { PreviewMode = 3; SlideClock = 0; }
void AShooterStancePreviewPawn::ToggleReady() { bPreviewLowReady = !bPreviewLowReady; }
void AShooterStancePreviewPawn::ToggleView()
{
	View = bFourDirection ? View % 3 + 1 : (View + 1) % 4;
	UpdatePresentation();
}
void AShooterStancePreviewPawn::ToggleDirectionDemo() { bDirectionDemo = !bDirectionDemo; }
void AShooterStancePreviewPawn::ToggleLowerOnly() { bPreviewLowerOnly = !bPreviewLowerOnly; }
void AShooterStancePreviewPawn::LookYaw(float Value) { AddControllerYawInput(Value); }
void AShooterStancePreviewPawn::LookPitch(float Value) { AddControllerPitchInput(-Value); }

void AShooterStancePreviewPawn::TogglePause()
{
	bPaused = !bPaused;
	Body->bPauseAnims = bPaused;
	FirstPersonArms->bPauseAnims = bPaused;
}

void AShooterStancePreviewPawn::SetupPlayerInputComponent(UInputComponent* Input)
{
	Super::SetupPlayerInputComponent(Input);
	if (!GetWorld()->GetAuthGameMode<AShooterFourDirectionPreviewGameMode>())
	{
		Input->BindKey(EKeys::One, IE_Pressed, this, &AShooterStancePreviewPawn::Stand);
		Input->BindKey(EKeys::Two, IE_Pressed, this, &AShooterStancePreviewPawn::Crouch);
		Input->BindKey(EKeys::Three, IE_Pressed, this, &AShooterStancePreviewPawn::Walk);
		Input->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AShooterStancePreviewPawn::Slide);
		Input->BindKey(EKeys::R, IE_Pressed, this, &AShooterStancePreviewPawn::ToggleReady);
	}
	Input->BindKey(EKeys::V, IE_Pressed, this, &AShooterStancePreviewPawn::ToggleView);
	Input->BindKey(EKeys::T, IE_Pressed, this, &AShooterStancePreviewPawn::TogglePause);
	Input->BindKey(EKeys::O, IE_Pressed, this, &AShooterStancePreviewPawn::ToggleDirectionDemo);
	if (!GetWorld()->GetAuthGameMode<AShooterFourDirectionPreviewGameMode>())
	{
		Input->BindKey(EKeys::L, IE_Pressed, this, &AShooterStancePreviewPawn::ToggleLowerOnly);
	}
	Input->BindAxisKey(EKeys::MouseX, this, &AShooterStancePreviewPawn::LookYaw);
	Input->BindAxisKey(EKeys::MouseY, this, &AShooterStancePreviewPawn::LookPitch);
}

void AShooterStancePreviewPawn::UpdatePresentation()
{
	Body->SetOwnerNoSee(View == 0);
	Rifle->SetOwnerNoSee(View == 0);
	FirstPersonArms->SetVisibility(View == 0);
	FirstPersonRifle->SetVisibility(View == 0);
}

void AShooterStancePreviewPawn::TickCaptureInputs(float DeltaSeconds)
{
	CaptureClock += DeltaSeconds;
	CaseClock += DeltaSeconds;
	const double Duration = bTransitionCapture ? 3.0 : CaptureCase < 2 ? 14.0 : 4.0;
	if (CaseClock >= Duration)
	{
		++CaptureCase;
		CaseClock = 0;
		SetActorLocation(FVector::ZeroVector);
	}
	bCaptureComplete = CaptureCase >= (bTransitionCapture ? 58 : 2 + 2 * CaptureDirections);
	if (bCaptureComplete) { return; }
	bPreviewLowReady = CaptureCase == 1 || CaptureCase >= 2 + CaptureDirections;
	PreviewMode = CaptureCase < 2 ? 1 : 2;
	PreviewForward = PreviewRight = 0;
	if (CaptureCase >= 2)
	{
		const double Angle = ((CaptureCase - 2) % CaptureDirections) * 2.0 * PI / CaptureDirections;
		PreviewForward = FMath::Cos(Angle);
		PreviewRight = FMath::Sin(Angle);
	}
	PreviewYaw = 0;
	constexpr float Pitches[] = {-90, -45, -10, -1, -0.5f, 0, 0.5f, 1, 10, 45, 90};
	PreviewPitch = bPitchSweep ? Pitches[FMath::FloorToInt(CaptureClock / 0.5) % UE_ARRAY_COUNT(Pitches)] : 0;
	if (bTransitionCapture)
	{
		bPreviewLowReady = CaptureCase >= 28 && CaptureCase < 36;
		PreviewMode = CaptureCase == 0 || CaptureCase == 2 || (CaptureCase >= 36 && CaptureCase < 47) ? 0 : 1;
		PreviewForward = PreviewRight = 0;
		int32 DirectionIndex = -1;
		if (CaptureCase >= 4 && CaptureCase < 20 && CaptureCase % 2 == 0)
		{
			DirectionIndex = (CaptureCase - 4) / 2;
		}
		if (CaptureCase >= 20 && CaptureCase < 36) { DirectionIndex = (CaptureCase - 20) % 8; }
		if (DirectionIndex >= 0)
		{
			PreviewMode = 2;
			PreviewForward = FMath::Cos(DirectionIndex * PI / 4.0);
			PreviewRight = FMath::Sin(DirectionIndex * PI / 4.0);
		}
		PreviewPitch = CaptureCase >= 36 ? Pitches[(CaptureCase - 36) % UE_ARRAY_COUNT(Pitches)] : 0;
	}
	if (Controller) { Controller->SetControlRotation(FRotator(PreviewPitch, 0, 0)); }
}

void AShooterStancePreviewPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFourDirection)
	{
		TickFourDirection(DeltaSeconds);
		return;
	}
	if (bCapture)
	{
		TickCaptureInputs(DeltaSeconds);
		GetWorld()->GetTimerManager().SetTimerForNextTick(this, &AShooterStancePreviewPawn::CaptureFrame);
	}
	else if (!bPaused)
	{
		CaptureClock += DeltaSeconds;
		FVector2D Input = FVector2D::ZeroVector;
		if (const APlayerController* PC = Cast<APlayerController>(Controller))
		{
			Input.X = (PC->IsInputKeyDown(EKeys::W) ? 1.0f : 0.0f) - (PC->IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f);
			Input.Y = (PC->IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f) - (PC->IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f);
		}
		if (bDirectionDemo)
		{
			const double Angle = FMath::FloorToInt(CaptureClock / 3.0) * PI / 4.0;
			Input = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle));
		}
		Input = Input.GetClampedToMaxSize(1);
		PreviewForward = Input.X;
		PreviewRight = Input.Y;
		if (PreviewMode == 1 || PreviewMode == 2) { PreviewMode = Input.IsNearlyZero() ? 1 : 2; }
		if (PreviewMode == 3)
		{
			SlideClock += DeltaSeconds;
			if (SlideClock >= 1.6f) { PreviewMode = 1; }
		}
		const FRotator Control = GetControlRotation();
		SetActorRotation(FRotator(0, Control.Yaw, 0));
		PreviewPitch = FRotator::NormalizeAxis(Control.Pitch);
		PreviewYaw = 0;
	}
	const float CrouchTarget = PreviewMode == 1 || PreviewMode == 2 ? 1.0f : 0.0f;
	PreviewCrouchAlpha = FMath::FInterpConstantTo(PreviewCrouchAlpha, CrouchTarget, DeltaSeconds, 4.0f);
	if (!bPaused && !bStationary && PreviewMode == 2)
	{
		const FVector Direction = GetActorForwardVector() * PreviewForward + GetActorRightVector() * PreviewRight;
		const float Speed = bBaseline ? StanceNative::MoveSpeed : StanceNative::AwareMoveSpeed;
		AddActorWorldOffset(Direction * Speed * DeltaSeconds);
	}
	if (GEngine && !bCapture)
	{
		GEngine->AddOnScreenDebugMessage(uint64(GetUniqueID()), 0.2f, FColor::Cyan,
			FString::Printf(TEXT("NATIVE ANIMBP | 1 Stand  2 Crouch  WASD Move  R Ready  V View  L Lower only\n")
				TEXT("Space Slide  T Pause  O Direction demo | Mode %d | %s"),
				PreviewMode, bPreviewLowerOnly ? TEXT("Lower") : TEXT("Layered")), false);
	}
}

void AShooterStancePreviewPawn::CaptureFrame()
{
	if (bCaptureComplete)
	{
		check(FFileHelper::SaveStringToFile(CaptureReport, *CaptureOutput));
		check(FFileHelper::SaveStringToFile(CaptureViewReport, *FPaths::ChangeExtension(CaptureOutput, TEXT("view.csv"))));
		UE_LOG(LogStancePreview, Display, TEXT("STANCE_NATIVE_CAPTURE_COMPLETE Cases=%d File=%s"),
			bTransitionCapture ? 58 : 2 + 2 * CaptureDirections, *CaptureOutput);
		FPlatformMisc::RequestExit(false);
		return;
	}
	if (CaptureFrameIndex++ % 3 != 0) { return; }
	float IdleTime, MovePhase, MoveLength, AimY;
	StanceNative::ReadPlayerTimes(Body->GetAnimInstance(), IdleTime, MovePhase, MoveLength, AimY);
	TArray<FName> Names;
	Body->GetBoneNames(Names);
	for (USkeletalMeshComponent* Component : {Body.Get(), FirstPersonArms.Get()})
	{
		const FTransform ComponentWorld = Component->GetComponentTransform();
		for (const FName Name : Names)
		{
			const int32 Index = Component->GetBoneIndex(Name);
			check(Index != INDEX_NONE);
			const FTransform World = Component->GetBoneTransform(Index);
			const FTransform Local = World.GetRelativeTransform(ComponentWorld);
			const FVector P = Local.GetLocation();
			const FVector W = World.GetLocation();
			const FQuat Q = Local.GetRotation();
			CaptureReport += FString::Printf(
				TEXT("%.6f,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.3f,%.3f,"),
				CaptureClock,
				CaptureCase,
				CaseClock > 0.6 ? 1 : 0,
				PreviewMode,
				bPreviewLowReady ? 1 : 0,
				bPreviewLowerOnly ? 1 : 0,
				CaseClock,
				PreviewForward,
				PreviewRight,
				PreviewPitch,
				PreviewYaw);
			CaptureReport += FString::Printf(TEXT("%.6f,%.6f,%.6f,%s,%s,"),
				IdleTime, MovePhase, MoveLength, Component == Body ? TEXT("body") : TEXT("fp"), *Name.ToString());
			CaptureReport += FString::Printf(TEXT("%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.8f,%.8f,%.8f,%.8f,%.8f,%.6f,%.6f\n"),
				P.X, P.Y, P.Z, W.X, W.Y, W.Z, Q.X, Q.Y, Q.Z, Q.W, AimY, FMath::Fmod(CaptureClock, 0.5), PreviewCrouchAlpha);
		}
	}
	const FTransform Views[] = {Camera->GetComponentTransform(), Rifle->GetSocketTransform(MuzzleSocketName),
		FirstPersonRifle->GetSocketTransform(MuzzleSocketName)};
	const TCHAR* Labels[] = {TEXT("camera"), TEXT("tp_muzzle"), TEXT("fp_muzzle")};
	for (int32 I = 0; I < UE_ARRAY_COUNT(Views); ++I)
	{
		const FVector P = Views[I].GetLocation();
		const FQuat Q = Views[I].GetRotation();
		CaptureViewReport += FString::Printf(TEXT("%.6f,%d,%s,%.3f,%.6f,%.6f,%.6f,%.6f,%.8f,%.8f,%.8f,%.8f\n"),
			CaptureClock, CaptureCase, Labels[I], PreviewPitch, PreviewCrouchAlpha, P.X, P.Y, P.Z, Q.X, Q.Y, Q.Z, Q.W);
	}
}

void AShooterStancePreviewPawn::CalcCamera(float DeltaTime, FMinimalViewInfo& Out)
{
	if (View == 0)
	{
		Camera->GetCameraView(DeltaTime, Out);
		Out.PerspectiveNearClipPlane = 1;
		return;
	}
	Out.FOV = 58;
	Out.PerspectiveNearClipPlane = 1;
	Out.bUseFirstPersonParameters = false;
	const FVector Target = GetActorLocation() + FVector(15, 0, PreviewMode == 3 ? 60 : 85);
	const FVector Offset = View == 1 ? FVector(70, 310, 25)
		: View == 2 ? FVector(290, 110, 35) : FVector(-280, -100, 35);
	Out.Location = Target + GetActorRotation().RotateVector(Offset * (bFourDirection ? 2.0f : 1.0f));
	Out.Rotation = (Target - Out.Location).Rotation();
}


void AShooterStancePreviewPawn::TickFourDirection(float DeltaSeconds)
{
	if (!bPaused) { CaptureClock += DeltaSeconds; }
	FVector2D Input = FVector2D::ZeroVector;
	if (const APlayerController* PC = Cast<APlayerController>(Controller))
	{
		Input.X = float(PC->IsInputKeyDown(EKeys::W)) - float(PC->IsInputKeyDown(EKeys::S));
		Input.Y = float(PC->IsInputKeyDown(EKeys::D)) - float(PC->IsInputKeyDown(EKeys::A));
	}
	if (bCapture || bDirectionDemo)
	{
		const double DemoClock = bCapture ? CaptureClock - 1.0 : CaptureClock;
		if (DemoClock >= 0 && (!bCapture || DemoClock < 24))
		{
			const double Angle = FMath::FloorToInt(DemoClock / 3.0) * PI / 4.0;
			Input = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle));
		}
		else { Input = FVector2D::ZeroVector; }
	}
	// 菱形边界只表示方向比例；零输入停在最后移动姿态，不采样中心或构造 Idle。
	const double Magnitude = FMath::Abs(Input.X) + FMath::Abs(Input.Y);
	if (Magnitude > UE_SMALL_NUMBER)
	{
		PreviewForward = Input.X / Magnitude;
		PreviewRight = Input.Y / Magnitude;
	}
	Body->GlobalAnimRateScale = bPaused || Magnitude <= UE_SMALL_NUMBER ? 0.0f : 1.0f;
	Body->bPauseAnims = false;
	if (Controller) { SetActorRotation(FRotator(0, GetControlRotation().Yaw, 0)); }
	if (!bPaused && Magnitude > UE_SMALL_NUMBER)
	{
		// 动画输入保持菱形比例；位移单独归一化，确保斜向与主方向等速。
		const FVector Direction = (GetActorForwardVector() * PreviewForward + GetActorRightVector() * PreviewRight).GetSafeNormal();
		AddActorWorldOffset(Direction * StanceNative::MoveSpeed * DeltaSeconds);
	}
	if (bCapture)
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick(this, &AShooterStancePreviewPawn::CaptureFourDirectionFrame);
	}
	else if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(uint64(GetUniqueID()), 0.2f, FColor::Cyan,
			FString::Printf(TEXT("EIGHT DIRECTION | WASD: Move | Release: Stop | V: View | T: Pause | O: Demo\n")
				TEXT("Forward %.2f  Right %.2f"), PreviewForward, PreviewRight), false);
	}
}

void AShooterStancePreviewPawn::CaptureFourDirectionFrame()
{
	if (CaptureClock >= 26.0)
	{
		check(FFileHelper::SaveStringToFile(CaptureReport, *CaptureOutput));
		UE_LOG(LogStancePreview, Display, TEXT("STANCE_FOUR_CAPTURE_COMPLETE %s"), *CaptureOutput);
		FPlatformMisc::RequestExit(false);
		return;
	}
	if (CaptureFrameIndex++ % 2 != 0) { return; }
	const UAnimInstance* Instance = Body->GetAnimInstance();
	const FAnimNode_BlendSpacePlayer* Player = nullptr;
	for (TFieldIterator<FStructProperty> It(Instance->GetClass()); It; ++It)
	{
		if (It->Struct == FAnimNode_BlendSpacePlayer::StaticStruct())
		{
			Player = It->ContainerPtrToValuePtr<FAnimNode_BlendSpacePlayer>(Instance);
			break;
		}
	}
	check(Player && Player->GetBlendSpace());
	const FVector Filtered = Player->GetFilteredPosition();
	const int32 Case = CaptureClock < 1 ? -1 : CaptureClock >= 25 ? 8 : int32((CaptureClock - 1) / 3);
	const FTransform ComponentWorld = Body->GetComponentTransform();
	TArray<FName> Names;
	Body->GetBoneNames(Names);
	for (FName Name : Names)
	{
		const FTransform World = Body->GetBoneTransform(Body->GetBoneIndex(Name));
		const FTransform Pose = World.GetRelativeTransform(ComponentWorld);
		const FVector P = Pose.GetLocation();
		const FVector W = World.GetLocation();
		const FQuat Q = Pose.GetRotation();
		CaptureReport += FString::Printf(
			TEXT("%.6f,%d,%.6f,%.6f,%.6f,%.6f,%.8f,%.6f,%s,"),
			CaptureClock,
			Case,
			PreviewForward,
			PreviewRight,
			Filtered.X,
			Filtered.Y,
			Player->GetAccumulatedTime(),
			Player->GetCurrentAssetLength(),
			*Name.ToString());
		CaptureReport += FString::Printf(TEXT("%.6f,%.6f,%.6f,%.8f,%.8f,%.8f,%.8f,%.6f,%.6f,%.6f\n"),
			P.X, P.Y, P.Z, Q.X, Q.Y, Q.Z, Q.W, W.X, W.Y, W.Z);
	}
}

AShooterFourDirectionPreviewGameMode::AShooterFourDirectionPreviewGameMode()
{
	DefaultPawnClass = AShooterStancePreviewPawn::StaticClass();
	PreviewAnimClass = FSoftObjectPath(TEXT("/Game/Experiments/Stance_20261022/ABP_Smoothing03_Crouch.ABP_Smoothing03_Crouch_C"));
}

AShooterStancePreviewGameMode::AShooterStancePreviewGameMode()
{
	DefaultPawnClass = AShooterStancePreviewPawn::StaticClass();
}

namespace StanceNative
{
	/** 以下函数只在 Editor Commandlet 建立资产节点和连线，从不求值或写骨骼。 */
	template <typename T>
	T* NewGraphNode(UEdGraph* Graph, int32 X, int32 Y, TFunctionRef<void(T*)> Configure)
	{
		FGraphNodeCreator<T> Creator(*Graph);
		T* Node = Creator.CreateNode(false);
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Configure(Node);
		Creator.Finalize();
		return Node;
	}

	template <typename T>
	void SetEditorValue(T& Node, FName Name, const TCHAR* Value)
	{
		FProperty* Property = T::StaticStruct()->FindPropertyByName(Name);
		checkf(Property, TEXT("缺少原生节点配置 %s"), *Name.ToString());
		check(Property->ImportText_Direct(Value, Property->ContainerPtrToValuePtr<void>(&Node), nullptr, PPF_None));
	}

	void Connect(UEdGraphNode* From, FName Output, UEdGraphNode* To, FName Input)
	{
		UEdGraphPin* OutPin = From->FindPin(Output, EGPD_Output);
		UEdGraphPin* InPin = To->FindPin(Input, EGPD_Input);
		if (!OutPin || !InPin)
		{
			for (const UEdGraphNode* Node : {From, To})
			{
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					UE_LOG(LogStancePreview, Error, TEXT("PIN %s %s %d"),
						*Node->GetClass()->GetName(), *Pin->PinName.ToString(), int32(Pin->Direction));
				}
			}
		}
		checkf(OutPin && InPin, TEXT("缺少引脚 %s -> %s"), *Output.ToString(), *Input.ToString());
		check(From->GetGraph()->GetSchema()->TryCreateConnection(OutPin, InPin));
	}

	UEdGraph* FindGraph(UBlueprint* Blueprint, FName Name)
	{
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph->GetFName() == Name) { return Graph; }
		}
		checkf(false, TEXT("缺少图 %s"), *Name.ToString());
		return nullptr;
	}

	UK2Node_VariableGet* ReadVariable(UEdGraph* Graph, FName Name, int32 X, int32 Y)
	{
		return NewGraphNode<UK2Node_VariableGet>(Graph, X, Y,
			[Name](UK2Node_VariableGet* Node) { Node->VariableReference.SetSelfMember(Name); });
	}

	void BindInput(UAnimGraphNode_Base* Node, FName Input, FName Variable)
	{
		for (int32 I = 0; I < Node->ShowPinForProperties.Num(); ++I)
		{
			if (Node->ShowPinForProperties[I].PropertyName == Input)
			{
				Node->SetPinVisibility(true, I);
				break;
			}
		}
		UK2Node_VariableGet* Value = ReadVariable(Node->GetGraph(), Variable, Node->NodePosX - 220, Node->NodePosY);
		Connect(Value, Variable, Node, Input);
	}

	UAnimGraphNode_SequencePlayer* Sequence(UEdGraph* Graph, const FString& Path, int32 X, int32 Y, bool bLoop = true)
	{
		UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *Path);
		checkf(Clip, TEXT("缺少动画 %s"), *Path);
		return NewGraphNode<UAnimGraphNode_SequencePlayer>(Graph, X, Y,
			[Clip, bLoop](UAnimGraphNode_SequencePlayer* Node)
			{
				Node->SetAnimationAsset(Clip);
				Node->Node.SetLoopAnimation(bLoop);
			});
	}

	UAnimBlueprint* NewAnimBlueprint(const FString& Name, USkeleton* Skeleton, USkeletalMesh* Mesh, bool bAware = false)
	{
		const FString Path = (bAware ? AwareFolder : Folder) / Name;
		checkf(!FPackageName::DoesPackageExist(Path), TEXT("拒绝覆盖 %s"), *Path);
		UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
		Factory->ParentClass = UAnimInstance::StaticClass();
		Factory->TargetSkeleton = Skeleton;
		Factory->PreviewSkeletalMesh = Mesh;
		return CastChecked<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),
			CreatePackage(*Path), *Name, RF_Public | RF_Standalone, nullptr, GWarn));
	}

	void AddInputVariables(UAnimBlueprint* Blueprint, bool bMovementOnly = false)
	{
		const TCHAR* Names[] = {TEXT("Mode"), TEXT("Forward"), TEXT("Right"), TEXT("Pitch"), TEXT("Yaw"),
			TEXT("LowReady"), TEXT("LowerOnly"), TEXT("CrouchAlpha")};
		for (int32 I = 0; I < UE_ARRAY_COUNT(Names); ++I)
		{
			if (bMovementOnly && I != 1 && I != 2) { continue; }
			FEdGraphPinType Type;
			Type.PinCategory = I == 0 ? UEdGraphSchema_K2::PC_Int
				: I == 5 || I == 6 ? UEdGraphSchema_K2::PC_Boolean : UEdGraphSchema_K2::PC_Real;
			if ((I > 0 && I < 5) || I == 7) { Type.PinSubCategory = UEdGraphSchema_K2::PC_Float; }
			check(FBlueprintEditorUtils::AddMemberVariable(Blueprint, Names[I], Type, I == 0 ? TEXT("1") : TEXT("0")));
		}
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		UEdGraph* Graph = FindGraph(Blueprint, TEXT("EventGraph"));
		for (UEdGraphNode* Node : TArray<TObjectPtr<UEdGraphNode>>(Graph->Nodes))
		{
			FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
		}
		UK2Node_Event* Update = NewGraphNode<UK2Node_Event>(Graph, -800, 0, [](UK2Node_Event* Node)
			{
				Node->EventReference.SetExternalMember(TEXT("BlueprintUpdateAnimation"), UAnimInstance::StaticClass());
				Node->bOverrideFunction = true;
			});
		UK2Node_CallFunction* Owner = NewGraphNode<UK2Node_CallFunction>(Graph, -800, 240,
			[](UK2Node_CallFunction* Node)
			{
				Node->SetFromFunction(UAnimInstance::StaticClass()->FindFunctionByName(TEXT("TryGetPawnOwner")));
			});
		UK2Node_DynamicCast* Cast = NewGraphNode<UK2Node_DynamicCast>(Graph, -500, 0, [](UK2Node_DynamicCast* Node)
			{
				Node->TargetType = AShooterStancePreviewPawn::StaticClass();
				Node->SetPurity(false);
			});
		check(Graph->GetSchema()->TryCreateConnection(
			Owner->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue), Cast->GetCastSourcePin()));
		Connect(Update, UEdGraphSchema_K2::PN_Then, Cast, UEdGraphSchema_K2::PN_Execute);
		UEdGraphNode* Previous = Cast;
		for (int32 I = 0; I < UE_ARRAY_COUNT(Names); ++I)
		{
			if (bMovementOnly && I != 1 && I != 2) { continue; }
			const FName Target = Names[I];
			const FName Source = I == 5 ? TEXT("bPreviewLowReady") : I == 6 ? TEXT("bPreviewLowerOnly")
				: FName(*(FString(TEXT("Preview")) + Names[I]));
			UK2Node_VariableGet* Read = NewGraphNode<UK2Node_VariableGet>(Graph, I * 250, 230,
				[Source](UK2Node_VariableGet* Node)
				{
					Node->VariableReference.SetExternalMember(Source, AShooterStancePreviewPawn::StaticClass());
				});
			check(Graph->GetSchema()->TryCreateConnection(
				Cast->GetCastResultPin(), Read->FindPinChecked(UEdGraphSchema_K2::PN_Self)));
			UK2Node_VariableSet* Set = NewGraphNode<UK2Node_VariableSet>(Graph, I * 250, 0,
				[Target](UK2Node_VariableSet* Node) { Node->VariableReference.SetSelfMember(Target); });
			Connect(Read, Source, Set, Target);
			Connect(Previous, UEdGraphSchema_K2::PN_Then, Set, UEdGraphSchema_K2::PN_Execute);
			Previous = Set;
		}
	}

	UAnimGraphNode_Root* RootNode(UEdGraph* Graph)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UAnimGraphNode_Root* Root = Cast<UAnimGraphNode_Root>(Node)) { return Root; }
		}
		checkNoEntry();
		return nullptr;
	}

	UAnimBlueprint* MakeTP(USkeleton* Skeleton, USkeletalMesh* Mesh, bool bAware)
	{
		UAnimBlueprint* Blueprint = NewAnimBlueprint(
			bAware ? TEXT("ABP_StanceAware_TP") : TEXT("ABP_StanceNative_TP"), Skeleton, Mesh, bAware);
		AddInputVariables(Blueprint);
		UEdGraph* Graph = FindGraph(Blueprint, TEXT("AnimGraph"));
		UAnimGraphNode_Root* Root = RootNode(Graph);

		UAnimGraphNode_Base* Stand;
		UAnimGraphNode_Base* Idle;
		if (bAware)
		{
			UBlendSpace* StanceSpace = LoadObject<UBlendSpace>(nullptr, *(AwareFolder / TEXT("BS_StanceHeight")));
			check(StanceSpace);
			UAnimGraphNode_BlendSpacePlayer* StandSpace = NewGraphNode<UAnimGraphNode_BlendSpacePlayer>(
				Graph, -1200, -500, [StanceSpace](UAnimGraphNode_BlendSpacePlayer* Node) { Node->SetAnimationAsset(StanceSpace); });
			UAnimGraphNode_BlendSpacePlayer* IdleSpace = NewGraphNode<UAnimGraphNode_BlendSpacePlayer>(
				Graph, -1200, -300, [StanceSpace](UAnimGraphNode_BlendSpacePlayer* Node) { Node->SetAnimationAsset(StanceSpace); });
			BindInput(StandSpace, TEXT("X"), TEXT("CrouchAlpha"));
			BindInput(IdleSpace, TEXT("X"), TEXT("CrouchAlpha"));
			Stand = StandSpace;
			Idle = IdleSpace;
		}
		else
		{
			Stand = Sequence(Graph, TEXT("/Game/Characters/Mannequins/Anims/Rifle/MF_Rifle_Idle_ADS"), -1200, -500);
			Idle = Sequence(Graph, SourceFolder / TEXT("AS_SourceLayer_CrouchIdle"), -1200, -300);
		}
		const FString SpacePath = bAware ? AwareFolder / TEXT("BS_StanceAware_Lower")
			: SourceFolder / TEXT("BS_SourceLayer_Lower");
		UBlendSpace* Space = LoadObject<UBlendSpace>(nullptr, *SpacePath);
		check(Space);
		UAnimGraphNode_BlendSpacePlayer* Move = NewGraphNode<UAnimGraphNode_BlendSpacePlayer>(Graph, -1200, -100,
			[Space](UAnimGraphNode_BlendSpacePlayer* Node) { Node->SetAnimationAsset(Space); });
		BindInput(Move, TEXT("X"), TEXT("Forward"));
		BindInput(Move, TEXT("Y"), TEXT("Right"));
		UAnimGraphNode_SequencePlayer* Slide = Sequence(Graph,
			TEXT("/Game/Experiments/Stance_20261022/AS_Slide_Aim"), -1200, 100, false);
		UAnimGraphNode_BlendListByInt* Modes = NewGraphNode<UAnimGraphNode_BlendListByInt>(Graph, -700, -300,
			[](UAnimGraphNode_BlendListByInt* Node)
			{
				Node->Node.AddPose();
				Node->Node.AddPose();
			});
		SetEditorValue(Modes->Node, TEXT("ChildUpateMode"), TEXT("ResetChildOnActivate"));
		if (bAware) { SetEditorValue(Modes->Node, TEXT("BlendTime"), TEXT("(0.25,0.25,0.25,0.25)")); }
		BindInput(Modes, TEXT("ActiveChildIndex"), TEXT("Mode"));
		Connect(Stand, TEXT("Pose"), Modes, TEXT("BlendPose_0"));
		Connect(Idle, TEXT("Pose"), Modes, TEXT("BlendPose_1"));
		Connect(Move, TEXT("Pose"), Modes, TEXT("BlendPose_2"));
		Connect(Slide, TEXT("Pose"), Modes, TEXT("BlendPose_3"));
		UAnimGraphNode_SaveCachedPose* Cache = NewGraphNode<UAnimGraphNode_SaveCachedPose>(Graph, -350, -300,
			[](UAnimGraphNode_SaveCachedPose* Node) { Node->CacheName = TEXT("NativeLower"); });
		Connect(Modes, TEXT("Pose"), Cache, TEXT("Pose"));
		UAnimGraphNode_UseCachedPose* Base = NewGraphNode<UAnimGraphNode_UseCachedPose>(Graph, 100, -350,
			[Cache](UAnimGraphNode_UseCachedPose* Node) { Node->SaveCachedPoseNode = Cache; });
		UAnimGraphNode_UseCachedPose* Raw = NewGraphNode<UAnimGraphNode_UseCachedPose>(Graph, 650, -500,
			[Cache](UAnimGraphNode_UseCachedPose* Node) { Node->SaveCachedPoseNode = Cache; });
		UAnimGraphNode_SequencePlayer* Aim = Sequence(Graph,
			TEXT("/Game/Characters/Mannequins/Anims/Rifle/MF_Rifle_Idle_ADS"), -1000, 500);
		UAnimGraphNode_SequencePlayer* Low = Sequence(Graph,
			TEXT("/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_LowReady_Compact"), -1000, 700);
		UAnimGraphNode_BlendListByBool* Hold = NewGraphNode<UAnimGraphNode_BlendListByBool>(Graph, -600, 500,
			[](UAnimGraphNode_BlendListByBool*) {});
		BindInput(Hold, TEXT("bActiveValue"), TEXT("LowReady"));
		Connect(Low, TEXT("Pose"), Hold, TEXT("BlendPose_0"));
		Connect(Aim, TEXT("Pose"), Hold, TEXT("BlendPose_1"));
		UAnimGraphNode_Base* WeaponBase = Hold;
		if (bAware)
		{
			UAnimGraphNode_SequencePlayer* CrouchAim = Sequence(Graph,
				AwareFolder / TEXT("AS_StanceAware_CrouchADS"), -1100, 1000);
			UAnimGraphNode_SequencePlayer* CrouchLow = Sequence(Graph,
				AwareFolder / TEXT("AS_StanceAware_CrouchLowReady"), -1100, 1200);
			UAnimGraphNode_BlendListByBool* CrouchHold =
				NewGraphNode<UAnimGraphNode_BlendListByBool>(Graph, -650, 1000, [](UAnimGraphNode_BlendListByBool*) {});
			BindInput(CrouchHold, TEXT("bActiveValue"), TEXT("LowReady"));
			Connect(CrouchLow, TEXT("Pose"), CrouchHold, TEXT("BlendPose_0"));
			Connect(CrouchAim, TEXT("Pose"), CrouchHold, TEXT("BlendPose_1"));
			UAnimGraphNode_TwoWayBlend* StanceHold = NewGraphNode<UAnimGraphNode_TwoWayBlend>(
				Graph, -200, 900, [](UAnimGraphNode_TwoWayBlend*) {});
			BindInput(StanceHold, TEXT("Alpha"), TEXT("CrouchAlpha"));
			Connect(Hold, TEXT("Pose"), StanceHold, TEXT("A"));
			Connect(CrouchHold, TEXT("Pose"), StanceHold, TEXT("B"));
			WeaponBase = StanceHold;
		}
		UBlendSpace* AimSpace = LoadObject<UBlendSpace>(nullptr,
			bAware ? *(AwareFolder / TEXT("AO_StanceWeapon"))
				: TEXT("/Game/Characters/Mannequins/Anims/Rifle/AIM/AO_Rifle"));
		check(AimSpace);
		using UAimOffsetNode = UAnimGraphNode_RotationOffsetBlendSpace;
		UAimOffsetNode* AO = NewGraphNode<UAimOffsetNode>(Graph, -100, 500,
			[AimSpace](UAimOffsetNode* Node) { Node->SetAnimationAsset(AimSpace); });
		BindInput(AO, TEXT("X"), bAware ? TEXT("CrouchAlpha") : TEXT("Yaw"));
		UK2Node_CallFunction* PitchSin = NewGraphNode<UK2Node_CallFunction>(Graph, -300, 680,
			[](UK2Node_CallFunction* Node)
			{
				Node->SetFromFunction(UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("DegSin")));
			});
		UK2Node_VariableGet* PitchValue = ReadVariable(Graph, TEXT("Pitch"), -520, 680);
		Connect(PitchValue, TEXT("Pitch"), PitchSin, TEXT("A"));
		Connect(PitchSin, TEXT("ReturnValue"), AO, TEXT("Y"));
		if (!bAware) { Connect(Hold, TEXT("Pose"), AO, TEXT("BasePose")); }
		UAnimGraphNode_LayeredBoneBlend* Layer = NewGraphNode<UAnimGraphNode_LayeredBoneBlend>(Graph, 400, 0,
			[bAware](UAnimGraphNode_LayeredBoneBlend* Node)
			{
				Node->Node.LayerSetup.SetNum(1);
				FBranchFilter& Branch = Node->Node.LayerSetup[0].BranchFilters.AddDefaulted_GetRef();
				Branch.BoneName = bAware ? TEXT("spine_03") : TEXT("spine_01");
				Branch.BlendDepth = bAware ? 4 : 1;
				Node->Node.bMeshSpaceRotationBlend = !bAware;
			});
		Connect(Base, TEXT("Pose"), Layer, TEXT("BasePose"));
		UAnimGraphNode_Base* FinalPose = Layer;
		if (bAware)
		{
			Connect(WeaponBase, TEXT("Pose"), Layer, TEXT("BlendPoses_0"));
			UAnimGraphNode_SaveCachedPose* WeaponCache = NewGraphNode<UAnimGraphNode_SaveCachedPose>(
				Graph, 650, 100, [](UAnimGraphNode_SaveCachedPose* Node) { Node->CacheName = TEXT("StanceWeaponBase"); });
			Connect(Layer, TEXT("Pose"), WeaponCache, TEXT("Pose"));
			UAnimGraphNode_UseCachedPose* AimBase = NewGraphNode<UAnimGraphNode_UseCachedPose>(
				Graph, 800, 400, [WeaponCache](UAnimGraphNode_UseCachedPose* Node) { Node->SaveCachedPoseNode = WeaponCache; });
			UAnimGraphNode_UseCachedPose* AimLower = NewGraphNode<UAnimGraphNode_UseCachedPose>(
				Graph, 800, 200, [WeaponCache](UAnimGraphNode_UseCachedPose* Node) { Node->SaveCachedPoseNode = WeaponCache; });
			Connect(AimBase, TEXT("Pose"), AO, TEXT("BasePose"));
			AO->NodePosX = 1050;
			UAnimGraphNode_LayeredBoneBlend* AimScope = NewGraphNode<UAnimGraphNode_LayeredBoneBlend>(
				Graph, 1350, 200, [](UAnimGraphNode_LayeredBoneBlend* Node)
				{
					Node->Node.LayerSetup.SetNum(1);
					FBranchFilter& Branch = Node->Node.LayerSetup[0].BranchFilters.AddDefaulted_GetRef();
					Branch.BoneName = TEXT("spine_01");
					Branch.BlendDepth = 1;
					Node->Node.bMeshSpaceRotationBlend = false;
				});
			Connect(AimLower, TEXT("Pose"), AimScope, TEXT("BasePose"));
			Connect(AO, TEXT("Pose"), AimScope, TEXT("BlendPoses_0"));
			FinalPose = AimScope;
		}
		else { Connect(AO, TEXT("Pose"), Layer, TEXT("BlendPoses_0")); }
		UAnimGraphNode_BlendListByBool* Debug = NewGraphNode<UAnimGraphNode_BlendListByBool>(Graph, 1000, 0,
			[](UAnimGraphNode_BlendListByBool*) {});
		BindInput(Debug, TEXT("bActiveValue"), TEXT("LowerOnly"));
		Connect(Raw, TEXT("Pose"), Debug, TEXT("BlendPose_0"));
		Connect(FinalPose, TEXT("Pose"), Debug, TEXT("BlendPose_1"));
		Debug->NodePosX = bAware ? 1700 : 1000;
		Root->NodePosX = bAware ? 2050 : 1400;
		Connect(Debug, TEXT("Pose"), Root, TEXT("Result"));
		return Blueprint;
	}

	UAnimBlueprint* MakeFP(USkeleton* Skeleton, USkeletalMesh* Mesh, bool bAware)
	{
		UAnimBlueprint* Blueprint = NewAnimBlueprint(
			bAware ? TEXT("ABP_StanceAware_FP") : TEXT("ABP_StanceNative_FP"), Skeleton, Mesh, bAware);
		UEdGraph* Graph = FindGraph(Blueprint, TEXT("AnimGraph"));
		UAnimGraphNode_CopyPoseFromMesh* Copy = NewGraphNode<UAnimGraphNode_CopyPoseFromMesh>(Graph, -400, 0,
			[](UAnimGraphNode_CopyPoseFromMesh* Node) { Node->Node.bUseAttachedParent = true; });
		Connect(Copy, TEXT("Pose"), RootNode(Graph), TEXT("Result"));
		return Blueprint;
	}

	bool SaveBlueprint(UObject* Blueprint)
	{
		const FString Path = Blueprint->GetOutermost()->GetName();
		if (!Path.StartsWith(Folder + TEXT("/")) && !Path.StartsWith(AwareFolder + TEXT("/")) &&
			!Path.StartsWith(FourFolder + TEXT("/"))) { return false; }
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint,
			*FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension()), Args);
	}


	/** 仅建立四个新序列的原生混合；不采样或改写动画轨道。 */
	void MakeFourDirection(USkeletalMesh* Mesh)
	{
		USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *(FourFolder / TEXT("SK_FourDirection")));
		check(Skeleton);
		const FString SpaceName = TEXT("BS_FourDirection_Crouch");
		const FString SpacePath = FourFolder / SpaceName;
		check(!FPackageName::DoesPackageExist(SpacePath));
		UBlendSpace* Space = NewObject<UBlendSpace>(CreatePackage(*SpacePath), *SpaceName, RF_Public | RF_Standalone);
		Space->SetSkeleton(Skeleton);
		Space->SetPreviewMesh(Mesh);
		FStructProperty* ParameterProperty = FindFProperty<FStructProperty>(UBlendSpace::StaticClass(), TEXT("BlendParameters"));
		check(ParameterProperty);
		for (int32 I = 0; I < 2; ++I)
		{
			FBlendParameter* Axis = ParameterProperty->ContainerPtrToValuePtr<FBlendParameter>(Space, I);
			Axis->DisplayName = I == 0 ? TEXT("Forward") : TEXT("Right");
			Axis->Min = -1;
			Axis->Max = 1;
			Axis->GridNum = 4;
			Space->InterpolationParam[I].InterpolationTime = 0.2f;
		}
		Space->bAllowMarkerBasedSync = false;
		const TCHAR* Names[] = {TEXT("Forward"), TEXT("Back"), TEXT("Left"), TEXT("Right")};
		const FVector Points[] = {FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, -1, 0), FVector(0, 1, 0)};
		for (int32 I = 0; I < 4; ++I)
		{
			UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *(FourFolder / (FString(TEXT("AS_NewCrouch_")) + Names[I])));
			check(Clip && Clip->GetSkeleton() == Skeleton);
			check(Space->AddSample(Clip, Points[I]) != INDEX_NONE);
		}
		Space->ValidateSampleData();
		Space->ResampleData();
		Space->PostEditChange();
		Space->MarkPackageDirty();
		check(SaveBlueprint(Space));

		const FString GraphName = TEXT("ABP_FourDirection");
		const FString GraphPath = FourFolder / GraphName;
		check(!FPackageName::DoesPackageExist(GraphPath));
		UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
		Factory->ParentClass = UAnimInstance::StaticClass();
		Factory->TargetSkeleton = Skeleton;
		Factory->PreviewSkeletalMesh = Mesh;
		UAnimBlueprint* Blueprint = CastChecked<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),
			CreatePackage(*GraphPath), *GraphName, RF_Public | RF_Standalone, nullptr, GWarn));
		AddInputVariables(Blueprint, true);
		UEdGraph* Graph = FindGraph(Blueprint, TEXT("AnimGraph"));
		UAnimGraphNode_BlendSpacePlayer* Move = NewGraphNode<UAnimGraphNode_BlendSpacePlayer>(Graph, -400, 0,
			[Space](UAnimGraphNode_BlendSpacePlayer* Node) { Node->SetAnimationAsset(Space); });
		BindInput(Move, TEXT("X"), TEXT("Forward"));
		BindInput(Move, TEXT("Y"), TEXT("Right"));
		Connect(Move, TEXT("Pose"), RootNode(Graph), TEXT("Result"));
		FCompilerResultsLog Results;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Results);
		check(Results.NumErrors == 0 && Blueprint->GeneratedClass);
		check(SaveBlueprint(Blueprint));

		FString Weights = TEXT("forward,right,sample,weight\n");
		for (int32 I = 0; I < 8; ++I)
		{
			const float Angle = I * PI / 4;
			FVector Input(FMath::Cos(Angle), FMath::Sin(Angle), 0);
			Input /= FMath::Abs(Input.X) + FMath::Abs(Input.Y);
			TArray<FBlendSampleData> Samples;
			int32 Triangle = INDEX_NONE;
			check(Space->GetSamplesFromBlendInput(Input, Samples, Triangle, true));
			for (const FBlendSampleData& Sample : Samples)
			{
				Weights += FString::Printf(TEXT("%.6f,%.6f,%s,%.8f\n"), Input.X, Input.Y,
					*Space->GetBlendSamples()[Sample.SampleDataIndex].Animation->GetName(), Sample.GetClampedWeight());
			}
		}
		const FString Output = FPaths::ProjectSavedDir() / TEXT("FourDirectionBlend_20261117_01");
		check(FFileHelper::SaveStringToFile(Weights, *(Output / TEXT("Weights.csv"))));
		UE_LOG(LogStancePreview, Display, TEXT("STANCE_FOUR_GRAPH_COMPLETE %s"), *GraphPath);
	}

	/** 复制既有输入设置，只替换新候选序列，并由引擎重建三角化数据。 */
	void MakeAwareSpace()
	{
		const FString Name = TEXT("BS_StanceAware_Lower");
		const FString Path = AwareFolder / Name;
		checkf(!FPackageName::DoesPackageExist(Path), TEXT("拒绝覆盖 %s"), *Path);
		UBlendSpace* Original = LoadObject<UBlendSpace>(nullptr, *(SourceFolder / TEXT("BS_SourceLayer_Lower")));
		check(Original);
		UBlendSpace* Space = CastChecked<UBlendSpace>(StaticDuplicateObject(Original, CreatePackage(*Path), *Name));
		const TArray<FBlendSample> Samples = Space->GetBlendSamples();
		check(Samples.Num() == 9);
		for (int32 I = 0; I < Samples.Num(); ++I)
		{
			const FString SourceName = Samples[I].Animation->GetName();
			const int32 Start = SourceName.Find(TEXT("Crouch"));
			check(Start != INDEX_NONE);
			const FString AnimationPath = AwareFolder / (TEXT("AS_StanceAware_") + SourceName.Mid(Start));
			UAnimSequence* Animation = LoadObject<UAnimSequence>(nullptr, *AnimationPath);
			check(Animation && Space->ReplaceSampleAnimation(I, Animation));
		}
		Space->ValidateSampleData();
		Space->ResampleData();
		Space->PostEditChange();
		Space->MarkPackageDirty();
		check(SaveBlueprint(Space));
	}


	/** 密集的静态接地采样承接站蹲变化，避免跨越大幅屈膝时直接插值。 */
	void MakeStanceSpace(USkeleton* Skeleton)
	{
		const FString Name = TEXT("BS_StanceHeight");
		const FString Path = AwareFolder / Name;
		checkf(!FPackageName::DoesPackageExist(Path), TEXT("拒绝覆盖 %s"), *Path);
		UBlendSpace1D* Space = NewObject<UBlendSpace1D>(CreatePackage(*Path), *Name, RF_Public | RF_Standalone);
		Space->SetSkeleton(Skeleton);
		FStructProperty* Property = FindFProperty<FStructProperty>(UBlendSpace::StaticClass(), TEXT("BlendParameters"));
		check(Property);
		FBlendParameter* Parameter = Property->ContainerPtrToValuePtr<FBlendParameter>(Space, 0);
		Parameter->DisplayName = TEXT("CrouchAlpha");
		Parameter->Min = 0;
		Parameter->Max = 1;
		Parameter->GridNum = 8;
		for (int32 I = 0; I <= 8; ++I)
		{
			const FString ClipPath = AwareFolder / FString::Printf(TEXT("AS_StanceAware_Stance_%d"), I);
			UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *ClipPath);
			check(Clip && Space->AddSample(Clip, FVector(I / 8.0f, 0, 0)) != INDEX_NONE);
		}
		Space->ValidateSampleData();
		Space->ResampleData();
		Space->PostEditChange();
		Space->MarkPackageDirty();
		check(SaveBlueprint(Space));
	}


	/** 同一 AO 同时以站蹲参数和俯仰取样；每条输出只应用一次增量。 */
	void MakeAimSpace(USkeleton* Skeleton)
	{
		const FString Name = TEXT("AO_StanceWeapon");
		const FString Path = AwareFolder / Name;
		checkf(!FPackageName::DoesPackageExist(Path), TEXT("拒绝覆盖 %s"), *Path);
		UAimOffsetBlendSpace* Space = NewObject<UAimOffsetBlendSpace>(
			CreatePackage(*Path), *Name, RF_Public | RF_Standalone);
		Space->SetSkeleton(Skeleton);
		FStructProperty* Property = FindFProperty<FStructProperty>(UBlendSpace::StaticClass(), TEXT("BlendParameters"));
		check(Property);
		for (int32 I = 0; I < 2; ++I)
		{
			FBlendParameter* Parameter = Property->ContainerPtrToValuePtr<FBlendParameter>(Space, I);
			Parameter->DisplayName = I == 0 ? TEXT("CrouchAlpha") : TEXT("AimPitch");
			Parameter->Min = I == 0 ? 0 : -1;
			Parameter->Max = 1;
			Parameter->GridNum = 8;
		}
		FBoolProperty* GridProperty = FindFProperty<FBoolProperty>(UBlendSpace::StaticClass(), TEXT("bInterpolateUsingGrid"));
		check(GridProperty);
		GridProperty->SetPropertyValue_InContainer(Space, false);
		const TCHAR* StandNames[] = {
			TEXT("MM_Rifle_Idle_ADS_AO_CD_Fixed"), TEXT("MM_Rifle_Idle_ADS_AO_CC"), TEXT("MM_Rifle_Idle_ADS_AO_CU")};
		for (int32 I = 0; I < 3; ++I)
		{
			const FString ClipPath = FString(TEXT("/Game/Characters/Mannequins/Anims/Rifle/AIM/")) + StandNames[I];
			UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *ClipPath);
			check(Clip && Space->AddSample(Clip, FVector(0, I - 1, 0)) != INDEX_NONE);
		}
		const TCHAR* CrouchNames[] = {
			TEXT("CrouchAO_m90"), TEXT("CrouchAO_m45"), TEXT("CrouchAO_zero"), TEXT("CrouchAO_p45"), TEXT("CrouchAO_p90")};
		for (int32 I = 0; I < 5; ++I)
		{
			const FString ClipPath = AwareFolder / (FString(TEXT("AS_StanceAware_")) + CrouchNames[I]);
			UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *ClipPath);
			const float Pitch = FMath::Sin(FMath::DegreesToRadians((I - 2) * 45.0f));
			check(Clip && Space->AddSample(Clip, FVector(1, Pitch, 0)) != INDEX_NONE);
		}
		Space->ValidateSampleData();
		Space->ResampleData();
		Space->PostEditChange();
		Space->MarkPackageDirty();
		check(SaveBlueprint(Space));
	}

}

UShooterStancePreviewCommandlet::UShooterStancePreviewCommandlet()
{
	IsClient = false;
	IsServer = true;
	IsEditor = true;
	LogToConsole = true;
}

int32 UShooterStancePreviewCommandlet::Main(const FString& Params)
{
	if (!FParse::Param(*Params, TEXT("NativeGraph")))
	{
		UE_LOG(LogStancePreview, Error, TEXT("旧 C++ 动画制作与姿态处理已取消；仅接受 -NativeGraph 建立原生 AnimBP"));
		return 2;
	}
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"));
	check(Mesh);
	if (FParse::Param(*Params, TEXT("FourDirection")))
	{
		StanceNative::MakeFourDirection(Mesh);
		return 0;
	}
	if (FParse::Param(*Params, TEXT("StanceAimSpace")))
	{
		StanceNative::MakeAimSpace(Mesh->GetSkeleton());
		return 0;
	}
	const bool bAware = FParse::Param(*Params, TEXT("StanceAwareGraph"));
	if (bAware)
	{
		StanceNative::MakeAwareSpace();
		StanceNative::MakeStanceSpace(Mesh->GetSkeleton());
		StanceNative::MakeAimSpace(Mesh->GetSkeleton());
	}
	UAnimBlueprint* TP = StanceNative::MakeTP(Mesh->GetSkeleton(), Mesh, bAware);
	UAnimBlueprint* FP = StanceNative::MakeFP(Mesh->GetSkeleton(), Mesh, bAware);
	FString Nodes = TEXT("asset,parent,skeleton,graph,node_class,node\n");
	FString Links = TEXT("asset,graph,from,output,to,input\n");
	for (UAnimBlueprint* Blueprint : {TP, FP})
	{
		FCompilerResultsLog Results;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Results);
		if (Results.NumErrors > 0 || !Blueprint->GeneratedClass) { return 3; }
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (const UEdGraph* Graph : Graphs)
		{
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				Nodes += FString::Printf(
					TEXT("%s,%s,%s,%s,%s,%s\n"),
					*Blueprint->GetPathName(),
					*Blueprint->ParentClass->GetPathName(),
					*Blueprint->TargetSkeleton->GetPathName(),
					*Graph->GetName(),
					*Node->GetClass()->GetPathName(),
					*Node->GetName());
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin->Direction != EGPD_Output) { continue; }
					for (const UEdGraphPin* To : Pin->LinkedTo)
					{
						Links += FString::Printf(
							TEXT("%s,%s,%s,%s,%s,%s\n"),
							*Blueprint->GetName(),
							*Graph->GetName(),
							*Node->GetName(),
							*Pin->PinName.ToString(),
							*To->GetOwningNode()->GetName(),
							*To->PinName.ToString());
					}
				}
			}
		}
	}
	for (UAnimBlueprint* Blueprint : {TP, FP})
	{
		if (!StanceNative::SaveBlueprint(Blueprint)) { return 4; }
	}
	const FString Output = FPaths::ProjectSavedDir() / (bAware ? TEXT("StanceAware_20261116") : TEXT("StanceNativeGraph_20261115"));
	FFileHelper::SaveStringToFile(Nodes, *(Output / TEXT("GraphNodes.csv")));
	FFileHelper::SaveStringToFile(Links, *(Output / TEXT("GraphLinks.csv")));
	UE_LOG(LogStancePreview, Display, TEXT("STANCE_NATIVE_GRAPH_COMPLETE TP=%s FP=%s"),
		*TP->GetPathName(), *FP->GetPathName());
	return 0;
}
