// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/Aim/ShooterAimPresentationComponent.h"

#include "Camera/CameraComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "ShootGame.h"
#include "TimerManager.h"
#include "Weapons/ShooterAimMath.h"
#include "Weapons/ShooterWeapon.h"

namespace ShooterAimPresentationDebug
{
	constexpr uint64 LocalViewMessageKey = 0x53484F4F5441494Dull;

	TAutoConsoleVariable<int32> CVarDrawAim(
		TEXT("ShootGame.Aim.DebugDraw"),
		0,
		TEXT("Aim debug mode. 0=off, 1=single remote pose audit, 2=legacy full aim chain."),
		ECVF_Cheat);

	const AShooterCharacter* FindPoseDebugSubject(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}

		const AShooterCharacter* LocalCharacter = nullptr;
		for (TActorIterator<AShooterCharacter> It(World); It; ++It)
		{
			if (It->IsLocallyControlled())
			{
				LocalCharacter = *It;
				break;
			}
		}
		if (!LocalCharacter)
		{
			return nullptr;
		}

		const UCameraComponent* Camera = LocalCharacter->GetFirstPersonCameraComponent();
		const FVector ViewLocation = Camera
			? Camera->GetComponentLocation()
			: LocalCharacter->GetPawnViewLocation();
		const FVector ViewForward = Camera
			? Camera->GetForwardVector()
			: LocalCharacter->GetControlRotation().Vector();

		const AShooterCharacter* BestSubject = nullptr;
		double BestDot = -2.0;
		double BestDistanceSquared = TNumericLimits<double>::Max();
		for (TActorIterator<AShooterCharacter> It(World); It; ++It)
		{
			const AShooterCharacter* Candidate = *It;
			if (Candidate == LocalCharacter || Candidate->IsLocallyControlled())
			{
				continue;
			}

			const FVector ToCandidate =
				Candidate->GetActorLocation() + FVector(0.0, 0.0, 80.0) - ViewLocation;
			const double DistanceSquared = ToCandidate.SizeSquared();
			const double ViewDot = FVector::DotProduct(ViewForward, ToCandidate.GetSafeNormal());
			if (ViewDot > BestDot + UE_SMALL_NUMBER ||
				(FMath::IsNearlyEqual(ViewDot, BestDot) && DistanceSquared < BestDistanceSquared))
			{
				BestSubject = Candidate;
				BestDot = ViewDot;
				BestDistanceSquared = DistanceSquared;
			}
		}

		return BestSubject;
	}

	double AngleBetweenDegrees(const FVector& A, const FVector& B)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(A, B), -1.0, 1.0)));
	}
}

UShooterAimPresentationComponent::UShooterAimPresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);
}

void UShooterAimPresentationComponent::BeginPlay()
{
	Super::BeginPlay();
	StartPresentationAimSampling();
}

void UShooterAimPresentationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PresentationAimTimer);
	}

	Super::EndPlay(EndPlayReason);
}

void UShooterAimPresentationComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdatePresentationAimSmoothing(DeltaTime);
	DrawAimDebug();
}

bool UShooterAimPresentationComponent::IsValidPresentationAimTargetValue(const FVector& Target)
{
	return FMath::IsFinite(Target.X) &&
		FMath::IsFinite(Target.Y) &&
		FMath::IsFinite(Target.Z) &&
		!Target.IsNearlyZero();
}

bool UShooterAimPresentationComponent::ShouldSubmitPresentationAimTarget(
	const FVector& NewTarget,
	const FVector& PreviousTarget,
	const FVector& ViewLocation,
	float SecondsSinceLastSubmit,
	float MinChangeDistance,
	float MinChangeAngle,
	float KeepAliveInterval)
{
	if (!IsValidPresentationAimTargetValue(NewTarget) ||
		!FMath::IsFinite(ViewLocation.X) ||
		!FMath::IsFinite(ViewLocation.Y) ||
		!FMath::IsFinite(ViewLocation.Z))
	{
		return false;
	}

	if (!IsValidPresentationAimTargetValue(PreviousTarget) || SecondsSinceLastSubmit < 0.0f)
	{
		return true;
	}

	if (SecondsSinceLastSubmit >= FMath::Max(0.0f, KeepAliveInterval))
	{
		return true;
	}

	const FVector OldDirection = (PreviousTarget - ViewLocation).GetSafeNormal();
	const FVector NewDirection = (NewTarget - ViewLocation).GetSafeNormal();
	const float DistanceDelta = FVector::Distance(NewTarget, PreviousTarget);
	const float AngleDelta = !OldDirection.IsNearlyZero() && !NewDirection.IsNearlyZero()
		? FMath::RadiansToDegrees(FMath::Acos(
			FMath::Clamp(FVector::DotProduct(OldDirection, NewDirection), -1.0f, 1.0f)))
		: 180.0f;

	return DistanceDelta >= FMath::Max(0.0f, MinChangeDistance) ||
		AngleDelta >= FMath::Max(0.0f, MinChangeAngle);
}

bool UShooterAimPresentationComponent::IsClientPresentationAimTargetWithinBounds(
	const FVector& Target,
	const FVector& ServerViewLocation,
	float MaxDistance,
	float DistanceTolerance)
{
	if (!IsValidPresentationAimTargetValue(Target) ||
		!FMath::IsFinite(ServerViewLocation.X) ||
		!FMath::IsFinite(ServerViewLocation.Y) ||
		!FMath::IsFinite(ServerViewLocation.Z))
	{
		return false;
	}

	const float AllowedDistance = FMath::Max(0.0f, MaxDistance) + FMath::Max(0.0f, DistanceTolerance);
	return FVector::DistSquared(Target, ServerViewLocation) <= FMath::Square(AllowedDistance);
}

bool UShooterAimPresentationComponent::IsNewerPresentationAimSequence(uint16 Candidate, uint16 Previous)
{
	const uint16 Delta = Candidate - Previous;
	return Delta != 0 && Delta < 32768;
}

bool UShooterAimPresentationComponent::ShouldRunPresentationAimSmoothing(
	ENetRole LocalRole,
	ENetMode NetMode,
	bool bLocallyControlled)
{
	// 普通客户端观察其他玩家：SimulatedProxy。
	if (LocalRole == ROLE_SimulatedProxy)
	{
		return true;
	}

	// Listen Server 观察远端客户端 Pawn：Authority 但不是 LocallyControlled。
	if (LocalRole == ROLE_Authority && !bLocallyControlled)
	{
		return NetMode == NM_ListenServer;
	}

	return false;
}

void UShooterAimPresentationComponent::ComputeAimPresentationAnglesForState(
	bool bUseSmoothedTarget,
	const FVector& ViewLocation,
	const FVector& SmoothedTarget,
	const FRotator& BaseAimRotation,
	const FTransform& MeshReferenceTransform,
	float& OutAimYaw,
	float& OutAimPitch)
{
	FVector WorldDirection;
	if (bUseSmoothedTarget)
	{
		// 观察端：世界方向 = Normalized(目标 - AimPivot)。
		WorldDirection = SmoothedTarget - ViewLocation;
	}
	else
	{
		// 拥有者 / 回退：本地视角方向或远端 GetBaseAimRotation。
		WorldDirection = BaseAimRotation.Vector();
	}

	FShooterAimMath::WorldDirectionToLocalAngles(
		WorldDirection,
		MeshReferenceTransform,
		OutAimYaw,
		OutAimPitch);
}

void UShooterAimPresentationComponent::GetAimPresentationAngles(float& OutAimYaw, float& OutAimPitch) const
{
	const bool bShouldUseSmoothedTarget =
		ShouldRunPresentationAimSmoothingForContext() &&
		bPresentationAimTargetValid &&
		!SmoothedPresentationAimTarget.IsNearlyZero();

	ComputeAimPresentationAnglesForState(
		bShouldUseSmoothedTarget,
		GetPresentationPawnViewLocation(),
		SmoothedPresentationAimTarget,
		GetPresentationBaseAimRotation(),
		GetPresentationMeshReferenceTransform(),
		OutAimYaw,
		OutAimPitch);
}

float UShooterAimPresentationComponent::GetAimPitchN() const
{
	float AimYaw = 0.0f;
	float AimPitch = 0.0f;
	GetAimPresentationAngles(AimYaw, AimPitch);
	return FMath::Sin(FMath::DegreesToRadians(AimPitch));
}

void UShooterAimPresentationComponent::ResolveAimPresentationInput(
	FVector& OutAimDirectionWorld,
	FVector& OutAimTargetWorld,
	bool& bOutAimTargetWorldValid,
	float MinimumTargetDistanceFromView) const
{
	OutAimDirectionWorld = FVector::ZeroVector;
	OutAimTargetWorld = FVector::ZeroVector;
	bOutAimTargetWorldValid = false;

	const bool bLocallyControlled = IsPresentationOwnerLocallyControlled();
	const FVector BaseAimDirection = GetPresentationBaseAimRotation().Vector().GetSafeNormal();

	// 本地拥有者：即时基础方向，永远不消费远端表现目标。
	if (bLocallyControlled)
	{
		OutAimDirectionWorld = BaseAimDirection;
		return;
	}

	// 观察端：SimulatedProxy 与 Listen Server 观察远端 Pawn 都使用同一份平滑目标。
	if (!ShouldRunPresentationAimSmoothingForContext() ||
		!bPresentationAimTargetValid ||
		!FMath::IsFinite(BaseAimDirection.X) ||
		!FMath::IsFinite(BaseAimDirection.Y) ||
		!FMath::IsFinite(BaseAimDirection.Z) ||
		BaseAimDirection.IsNearlyZero())
	{
		return;
	}

	const FVector ViewWorldLocation = GetPresentationPawnViewLocation();
	FVector SafeTargetWorld = SmoothedPresentationAimTarget;
	if (!FMath::IsFinite(ViewWorldLocation.X) ||
		!FMath::IsFinite(ViewWorldLocation.Y) ||
		!FMath::IsFinite(ViewWorldLocation.Z) ||
		!FMath::IsFinite(SafeTargetWorld.X) ||
		!FMath::IsFinite(SafeTargetWorld.Y) ||
		!FMath::IsFinite(SafeTargetWorld.Z))
	{
		return;
	}

	// 距视点最小安全深度：近点目标沿基础视线向前投影，保留横向偏移。
	const float TargetDepthFromView = FVector::DotProduct(
		SafeTargetWorld - ViewWorldLocation,
		BaseAimDirection);
	if (TargetDepthFromView < MinimumTargetDistanceFromView)
	{
		SafeTargetWorld += BaseAimDirection *
			(MinimumTargetDistanceFromView - TargetDepthFromView);
	}

	OutAimDirectionWorld = BaseAimDirection;
	OutAimTargetWorld = SafeTargetWorld;
	bOutAimTargetWorldValid = true;
}

ENetRole UShooterAimPresentationComponent::GetPresentationLocalRole() const
{
	return GetOwner() ? GetOwner()->GetLocalRole() : ROLE_None;
}

ENetMode UShooterAimPresentationComponent::GetPresentationNetMode() const
{
	return GetOwner() && GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone;
}

bool UShooterAimPresentationComponent::IsPresentationOwnerLocallyControlled() const
{
	const APawn* Pawn = GetPresentationPawn();
	return Pawn && Pawn->IsLocallyControlled();
}

bool UShooterAimPresentationComponent::IsPresentationOwnerDead() const
{
	const AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwner());
	return Character && Character->IsDead();
}

FVector UShooterAimPresentationComponent::GetPresentationPawnViewLocation() const
{
	const APawn* Pawn = GetPresentationPawn();
	return Pawn ? Pawn->GetPawnViewLocation() : FVector::ZeroVector;
}

FRotator UShooterAimPresentationComponent::GetPresentationBaseAimRotation() const
{
	const APawn* Pawn = GetPresentationPawn();
	return Pawn ? Pawn->GetBaseAimRotation() : FRotator::ZeroRotator;
}

FTransform UShooterAimPresentationComponent::GetPresentationMeshReferenceTransform() const
{
	if (const AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwner()))
	{
		if (Character->GetMesh())
		{
			return Character->GetMesh()->GetComponentTransform();
		}
	}

	return GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity;
}

float UShooterAimPresentationComponent::GetPresentationMaxAimDistance() const
{
	const AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwner());
	return Character ? Character->GetMaxAimDistance() : 10000.0f;
}

const APawn* UShooterAimPresentationComponent::GetPresentationPawn() const
{
	return Cast<APawn>(GetOwner());
}

bool UShooterAimPresentationComponent::ShouldRunPresentationAimSmoothingForContext() const
{
	return ShouldRunPresentationAimSmoothing(
		GetPresentationLocalRole(),
		GetPresentationNetMode(),
		IsPresentationOwnerLocallyControlled());
}

void UShooterAimPresentationComponent::StartPresentationAimSampling()
{
	if (!IsPresentationOwnerLocallyControlled() || GetPresentationNetMode() == NM_Standalone || !GetWorld())
	{
		return;
	}

	// 本地拥有者以 20Hz 起点采样；SetTimer 对同一 Handle 幂等替换，兼容 BeginPlay 与输入初始化先后顺序。
	GetWorld()->GetTimerManager().SetTimer(
		PresentationAimTimer,
		this,
		&UShooterAimPresentationComponent::SamplePresentationAimTarget,
		PresentationAimSampleInterval,
		true,
		0.0f);
}

void UShooterAimPresentationComponent::SamplePresentationAimTarget()
{
	if (!IsPresentationOwnerLocallyControlled() || IsPresentationOwnerDead() || !GetWorld())
	{
		return;
	}

	const APawn* Pawn = GetPresentationPawn();
	if (!Pawn)
	{
		return;
	}

	const FVector NewTarget = AShooterCharacter::ComputePreSpreadAimTarget(
		Pawn,
		GetPresentationMaxAimDistance());
	const FVector ViewLocation = GetPresentationPawnViewLocation();
	const float Now = GetWorld()->GetTimeSeconds();
	const float SecondsSinceLastSubmit = LastPresentationAimSubmitTime >= 0.0f
		? Now - LastPresentationAimSubmitTime
		: -1.0f;
	if (!ShouldSubmitPresentationAimTarget(
		NewTarget,
		LastSubmittedPresentationAimTarget,
		ViewLocation,
		SecondsSinceLastSubmit,
		PresentationAimMinChangeDistance,
		PresentationAimMinChangeAngle,
		PresentationAimKeepAliveInterval))
	{
		return;
	}

	LastSubmittedPresentationAimTarget = NewTarget;
	LastPresentationAimSubmitTime = Now;
	++NextPresentationAimSequence;

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		// Listen Server 本地玩家无需绕 RPC；仍只写表现属性并转发给其他连接。
		PresentationAimTarget = NewTarget;
		GetOwner()->ForceNetUpdate();
	}
	else
	{
		ServerUpdatePresentationAimTarget(NewTarget, NextPresentationAimSequence);
	}
}

void UShooterAimPresentationComponent::ServerUpdatePresentationAimTarget_Implementation(
	FVector_NetQuantize NewTarget,
	uint16 ClientSequence)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || IsPresentationOwnerDead() || !GetWorld())
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	constexpr float MinimumServerReceiveInterval = 1.0f / 40.0f;
	constexpr float TargetDistanceTolerance = 500.0f;
	if ((LastAcceptedPresentationAimServerTime >= 0.0f &&
		Now - LastAcceptedPresentationAimServerTime < MinimumServerReceiveInterval) ||
		(bHasAcceptedPresentationAimSequence &&
			!IsNewerPresentationAimSequence(ClientSequence, LastAcceptedPresentationAimSequence)) ||
		!IsClientPresentationAimTargetWithinBounds(
			NewTarget,
			GetPresentationPawnViewLocation(),
			GetPresentationMaxAimDistance(),
			TargetDistanceTolerance))
	{
		return;
	}

	LastAcceptedPresentationAimServerTime = Now;
	LastAcceptedPresentationAimSequence = ClientSequence;
	bHasAcceptedPresentationAimSequence = true;
	PresentationAimTarget = NewTarget;
	GetOwner()->ForceNetUpdate();
}

void UShooterAimPresentationComponent::OnRep_PresentationAimTarget()
{
	// 只把“收到有效目标”当作建立有效状态的依据；不再刷新无变化超时计时。
	if (GetPresentationLocalRole() == ROLE_SimulatedProxy)
	{
		if (IsValidPresentationAimTargetValue(PresentationAimTarget))
		{
			const bool bNeedsReset = !bPresentationAimTargetValid ||
				SmoothedPresentationAimTarget.IsNearlyZero();
			bPresentationAimTargetValid = true;
			if (bNeedsReset)
			{
				ResetPresentationAimSmoothing();
			}
		}
	}

	UE_LOG(
		LogShootGame,
		VeryVerbose,
		TEXT("OnRep PresentationAimTarget Actor=%s Target=%s Valid=%s"),
		*GetNameSafe(GetOwner()),
		*PresentationAimTarget.ToString(),
		bPresentationAimTargetValid ? TEXT("true") : TEXT("false"));
}

void UShooterAimPresentationComponent::UpdatePresentationAimSmoothing(float DeltaSeconds)
{
	if (!ShouldRunPresentationAimSmoothingForContext())
	{
		return;
	}

	const FVector ViewLocation = GetPresentationPawnViewLocation();

	// 死亡是显式生命周期重置：旧表现目标立即失效，不插值、不回退复用。
	if (IsPresentationOwnerDead())
	{
		bPresentationAimTargetValid = false;
		SmoothedPresentationAimTarget = FVector::ZeroVector;
		LastPresentationAimViewLocation = ViewLocation;
		return;
	}

	// 首次建立有效状态：直接采用最新目标，不从旧位置插值。
	if (!bPresentationAimTargetValid && IsValidPresentationAimTargetValue(PresentationAimTarget))
	{
		bPresentationAimTargetValid = true;
		SmoothedPresentationAimTarget = (FVector)PresentationAimTarget;
		LastPresentationAimViewLocation = ViewLocation;
		return;
	}

	// 尚未建立有效状态、平滑目标缺失、或视点发生传送级跳变时显式重置。
	if (!bPresentationAimTargetValid ||
		SmoothedPresentationAimTarget.IsNearlyZero() ||
		FVector::DistSquared(ViewLocation, LastPresentationAimViewLocation) > FMath::Square(500.0f))
	{
		ResetPresentationAimSmoothing();
		LastPresentationAimViewLocation = ViewLocation;
		return;
	}

	LastPresentationAimViewLocation = ViewLocation;

	// 已建立的有效目标在稳态下持续有效；服务器不重复复制未变化的值也不回退。
	if (!IsValidPresentationAimTargetValue(PresentationAimTarget))
	{
		return;
	}

	// 指数插值向最新网络/权威目标收敛（世界空间位置插值；角度环绕在局部角度计算时处理）。
	const float Alpha = 1.0f - FMath::Exp(-PresentationAimSmoothingRate * DeltaSeconds);
	SmoothedPresentationAimTarget = FMath::Lerp(
		SmoothedPresentationAimTarget,
		(FVector)PresentationAimTarget,
		Alpha);
}

void UShooterAimPresentationComponent::ResetPresentationAimSmoothing()
{
	// 目标可用时直接采用；否则用回退方向（角色 Forward + 远端 Pitch）。
	bPresentationAimTargetValid = IsValidPresentationAimTargetValue(PresentationAimTarget);
	if (bPresentationAimTargetValid)
	{
		SmoothedPresentationAimTarget = (FVector)PresentationAimTarget;
	}
	else
	{
		SmoothedPresentationAimTarget =
			GetPresentationPawnViewLocation() +
			GetPresentationBaseAimRotation().Vector() * GetPresentationMaxAimDistance();
	}
}

void UShooterAimPresentationComponent::ClearPresentationAimSmoothing()
{
	bPresentationAimTargetValid = false;
	SmoothedPresentationAimTarget = FVector::ZeroVector;
	LastPresentationAimViewLocation = FVector::ZeroVector;
}

void UShooterAimPresentationComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 表现目标只复制给非拥有者：拥有者继续使用本地即时视角，不被远端表现数据覆盖。
	DOREPLIFETIME_CONDITION(UShooterAimPresentationComponent, PresentationAimTarget, COND_SkipOwner);
}

void UShooterAimPresentationComponent::DrawAimDebug() const
{
	UWorld* World = GetWorld();
	const AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwner());
	const int32 DebugMode = ShooterAimPresentationDebug::CVarDrawAim.GetValueOnGameThread();
	if (DebugMode <= 0 || !World || GetPresentationNetMode() == NM_DedicatedServer || !Character)
	{
		return;
	}

	const FVector RawTarget = (FVector)PresentationAimTarget;
	const bool bRawTargetValid = IsValidPresentationAimTargetValue(RawTarget);
	const bool bSmoothedTargetValid =
		bPresentationAimTargetValid && IsValidPresentationAimTargetValue(SmoothedPresentationAimTarget);
	const AShooterWeapon* Weapon = Character->GetCurrentWeapon();
	const bool bHasMuzzle = Weapon && Weapon->HasThirdPersonMuzzleSocket();

	// 模式 1：只选择本地视野最接近中心的一个远端角色，避免多世界、多角色调试线互相覆盖。
	if (DebugMode == 1)
	{
		if (ShooterAimPresentationDebug::FindPoseDebugSubject(World) != Character)
		{
			return;
		}

		FString PoseText = FString::Printf(TEXT("POSE AUDIT  %s"), *Character->GetName());
		if (bSmoothedTargetValid && bHasMuzzle)
		{
			const FVector AimTarget = SmoothedPresentationAimTarget;
			const FTransform MuzzleTransform = Weapon->GetThirdPersonMuzzleWorldTransform();
			const FVector MuzzleLocation = MuzzleTransform.GetLocation();
			const FVector TargetDelta = AimTarget - MuzzleLocation;
			const double TargetRange = TargetDelta.Size();
			const FVector DesiredDirection = TargetDelta.GetSafeNormal();
			const FVector ActualDirection = MuzzleTransform.GetUnitAxis(EAxis::X).GetSafeNormal();

			if (TargetRange > UE_SMALL_NUMBER && !ActualDirection.IsNearlyZero())
			{
				const FVector ActualEnd = MuzzleLocation + ActualDirection * TargetRange;
				const double MuzzleAngle = ShooterAimPresentationDebug::AngleBetweenDegrees(
					ActualDirection, DesiredDirection);
				const double MissDistance = FVector::Distance(ActualEnd, AimTarget);
				float AimYaw = 0.0f;
				float AimPitch = 0.0f;
				GetAimPresentationAngles(AimYaw, AimPitch);

				DrawDebugLine(
					World, MuzzleLocation, AimTarget,
					FColor::Cyan, false, 0.0f, 0, 0.0f);
				DrawDebugLine(
					World, MuzzleLocation, ActualEnd,
					FColor::Blue, false, 0.0f, 0, 0.0f);

				PoseText += FString::Printf(
					TEXT("\nDemand Yaw/Pitch=(%.1f, %.1f) deg")
					TEXT("\nFinalMuzzle Angle=%.2f deg  Miss=%.1f cm  Range=%.1f cm"),
					AimYaw, AimPitch, MuzzleAngle, MissDistance, TargetRange);
			}
		}
		else
		{
			PoseText += FString::Printf(
				TEXT("\nTarget=%d Weapon=%d Muzzle=%d"),
				bSmoothedTargetValid ? 1 : 0, Weapon ? 1 : 0, bHasMuzzle ? 1 : 0);
		}

		DrawDebugString(
			World,
			Character->GetActorLocation() + FVector(0.0, 0.0, 220.0),
			PoseText,
			nullptr,
			FColor::White,
			0.0f);
		return;
	}

	// 模式 2：保留完整链路，供后续 Pistol 和网络目标回归使用。
	const FVector PawnViewLocation = GetPresentationPawnViewLocation();
	FString DebugText = FString::Printf(
		TEXT("%s  Role=%d Local=%d  Raw=%d Smooth=%d"),
		*Character->GetName(),
		static_cast<int32>(GetPresentationLocalRole()),
		IsPresentationOwnerLocallyControlled() ? 1 : 0,
		bRawTargetValid ? 1 : 0,
		bSmoothedTargetValid ? 1 : 0);

	const UCameraComponent* FirstPersonCameraComponent = Character->GetFirstPersonCameraComponent();
	const USkeletalMeshComponent* FirstPersonMesh = Character->GetFirstPersonMesh();
	if (IsPresentationOwnerLocallyControlled() && FirstPersonCameraComponent)
	{
		const FVector CameraStart = FirstPersonCameraComponent->GetComponentLocation();
		const FVector CameraForward = FirstPersonCameraComponent->GetForwardVector();
		const FVector ControlForward = Character->GetControlRotation().Vector();
		const FVector CameraFromPawnView = CameraStart - PawnViewLocation;
		const FVector ViewLocalDelta = Character->GetControlRotation().UnrotateVector(CameraFromPawnView);
		const double ForwardAngleDegrees = ShooterAimPresentationDebug::AngleBetweenDegrees(
			CameraForward, ControlForward);

		const FName CameraAttachSocket = FirstPersonCameraComponent->GetAttachSocketName();
		const bool bHasCameraAttachSocket =
			FirstPersonMesh &&
			!CameraAttachSocket.IsNone() &&
			FirstPersonMesh->DoesSocketExist(CameraAttachSocket);
		const FVector CameraAttachSocketLocation = bHasCameraAttachSocket
			? FirstPersonMesh->GetSocketLocation(CameraAttachSocket)
			: CameraStart;

		DrawDebugPoint(World, CameraStart, 20.0f, FColor::White, false, 0.0f);
		DrawDebugPoint(World, PawnViewLocation, 20.0f, FColor::Orange, false, 0.0f);
		DrawDebugLine(World, CameraStart, PawnViewLocation, FColor::Orange, false, 0.0f, 0, 0.0f);
		DrawDebugLine(
			World, CameraStart, CameraStart + CameraForward * 100.0f,
			FColor::White, false, 0.0f, 0, 0.0f);
		DrawDebugLine(
			World, PawnViewLocation, PawnViewLocation + ControlForward * 100.0f,
			FColor::Orange, false, 0.0f, 0, 0.0f);
		if (bHasCameraAttachSocket)
		{
			DrawDebugPoint(World, CameraAttachSocketLocation, 20.0f, FColor::Silver, false, 0.0f);
			DrawDebugLine(
				World, PawnViewLocation, CameraAttachSocketLocation,
				FColor::Silver, false, 0.0f, 0, 0.0f);
			DrawDebugLine(
				World, CameraAttachSocketLocation, CameraStart,
				FColor::White, false, 0.0f, 0, 0.0f);
		}

		if (GEngine)
		{
			const FString LocalViewText = FString::Printf(
				TEXT("LOCAL VIEW  Camera-PawnEye=%.1fcm  Camera-Head=%.1fcm  Head-PawnEye=%.1fcm\n")
				TEXT("DeltaView F/R/U=(%.1f, %.1f, %.1f)cm  ForwardAngle=%.3fdeg"),
				CameraFromPawnView.Size(),
				bHasCameraAttachSocket
					? FVector::Distance(CameraStart, CameraAttachSocketLocation)
					: -1.0,
				bHasCameraAttachSocket
					? FVector::Distance(CameraAttachSocketLocation, PawnViewLocation)
					: -1.0,
				ViewLocalDelta.X, ViewLocalDelta.Y, ViewLocalDelta.Z,
				ForwardAngleDegrees);
			GEngine->AddOnScreenDebugMessage(
				ShooterAimPresentationDebug::LocalViewMessageKey,
				0.1f,
				FColor::White,
				LocalViewText,
				false,
				FVector2D(0.75f, 0.75f));
		}

		const float MaxAimDistance = GetPresentationMaxAimDistance();
		const FVector CameraTraceEnd = CameraStart + CameraForward * MaxAimDistance;
		FHitResult CameraHit;
		bool bCameraPawnHit = false;
		const FVector LocalCameraTarget = AShooterCharacter::TracePreSpreadAimTarget(
			World, CameraStart, CameraTraceEnd, Character, &CameraHit, &bCameraPawnHit);

		DrawDebugLine(World, CameraStart, LocalCameraTarget, FColor::Green, false, 0.0f, 0, 0.0f);
		DrawDebugPoint(World, LocalCameraTarget, 20.0f, FColor::Green, false, 0.0f);
		DebugText += FString::Printf(
			TEXT("\nCameraEye=%.1f cm  LocalTarget=(%.0f, %.0f, %.0f)  Hit=%s/%s %.1fcm"),
			FVector::Distance(CameraStart, PawnViewLocation),
			LocalCameraTarget.X, LocalCameraTarget.Y, LocalCameraTarget.Z,
			CameraHit.bBlockingHit
				? (bCameraPawnHit ? TEXT("Pawn") : TEXT("Visibility"))
				: TEXT("Fallback"),
			CameraHit.bBlockingHit ? *GetNameSafe(CameraHit.GetActor()) : TEXT("None"),
			CameraHit.bBlockingHit ? CameraHit.Distance : MaxAimDistance);
		if (bRawTargetValid)
		{
			DebugText += FString::Printf(
				TEXT("  LocalVsRaw=%.1f cm"),
				FVector::Distance(LocalCameraTarget, RawTarget));
		}
	}

	if (bRawTargetValid)
	{
		DrawDebugLine(World, PawnViewLocation, RawTarget, FColor::Magenta, false, 0.0f, 0, 0.0f);
		DrawDebugPoint(World, RawTarget, 20.0f, FColor::Yellow, false, 0.0f);
		DebugText += FString::Printf(
			TEXT("\nRawTarget=(%.0f, %.0f, %.0f)"),
			RawTarget.X, RawTarget.Y, RawTarget.Z);
	}

	if (bSmoothedTargetValid && bHasMuzzle)
	{
		const FVector AimTarget = SmoothedPresentationAimTarget;
		const FTransform MuzzleTransform = Weapon->GetThirdPersonMuzzleWorldTransform();
		const FVector MuzzleLocation = MuzzleTransform.GetLocation();
		const FVector TargetDelta = AimTarget - MuzzleLocation;
		const double TargetRange = TargetDelta.Size();
		const FVector DesiredDirection = TargetDelta.GetSafeNormal();
		const FVector ActualDirection = MuzzleTransform.GetUnitAxis(EAxis::X).GetSafeNormal();

		if (TargetRange > UE_SMALL_NUMBER && !ActualDirection.IsNearlyZero())
		{
			const double AlongDistance =
				FMath::Max(FVector::DotProduct(TargetDelta, ActualDirection), 0.0);
			const FVector ClosestPoint = MuzzleLocation + ActualDirection * AlongDistance;
			const double AngleDegrees = ShooterAimPresentationDebug::AngleBetweenDegrees(
				ActualDirection, DesiredDirection);
			const double MissDistance = FVector::Distance(ClosestPoint, AimTarget);

			DrawDebugLine(World, MuzzleLocation, AimTarget, FColor::Cyan, false, 0.0f, 0, 0.0f);
			DrawDebugLine(World, MuzzleLocation, ClosestPoint, FColor::Blue, false, 0.0f, 0, 0.0f);
			DrawDebugLine(World, ClosestPoint, AimTarget, FColor::Red, false, 0.0f, 0, 0.0f);
			DebugText += FString::Printf(
				TEXT("\nRawVsSmooth=%.1f cm  Angle=%.2f deg  Miss=%.1f cm  Range=%.1f cm"),
				bRawTargetValid ? FVector::Distance(RawTarget, AimTarget) : -1.0,
				AngleDegrees, MissDistance, TargetRange);
		}
	}
	else
	{
		DebugText += FString::Printf(
			TEXT("\nWeapon=%d Muzzle=%d"), Weapon ? 1 : 0, bHasMuzzle ? 1 : 0);
	}

	DrawDebugString(
		World,
		Character->GetActorLocation() + FVector(0.0, 0.0, 220.0),
		DebugText,
		nullptr,
		FColor::White,
		0.0f);
}
