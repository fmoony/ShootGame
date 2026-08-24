// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterCharacter.h"

#include "ShooterWeapon.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

namespace ShooterAimDebug
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

void AShooterCharacter::DrawAimDebug() const
{
	UWorld* World = GetWorld();
	const int32 DebugMode = ShooterAimDebug::CVarDrawAim.GetValueOnGameThread();
	if (DebugMode <= 0 || !World || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const FVector RawTarget = (FVector)PresentationAimTarget;
	const bool bRawTargetValid = IsValidPresentationAimTargetValue(RawTarget);
	const bool bSmoothedTargetValid =
		bPresentationAimTargetValid && IsValidPresentationAimTargetValue(SmoothedPresentationAimTarget);
	const AShooterWeapon* Weapon = GetCurrentWeapon();
	const bool bHasMuzzle = Weapon && Weapon->HasThirdPersonMuzzleSocket();

	// 模式 1：只选择本地视野最接近中心的一个远端角色，避免多世界、多角色调试线互相覆盖。
	if (DebugMode == 1)
	{
		if (ShooterAimDebug::FindPoseDebugSubject(World) != this)
		{
			return;
		}

		FString PoseText = FString::Printf(TEXT("POSE AUDIT  %s"), *GetName());
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
				const double MuzzleAngle = ShooterAimDebug::AngleBetweenDegrees(
					ActualDirection, DesiredDirection);
				const double MissDistance = FVector::Distance(ActualEnd, AimTarget);
				float AimYaw = 0.0f;
				float AimPitch = 0.0f;
				GetAimPresentationAngles(AimYaw, AimPitch);

				// 青色：目标方向；蓝色：可见第三人称枪口 +X。线宽保持最细。
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
			GetActorLocation() + FVector(0.0, 0.0, 220.0),
			PoseText,
			nullptr,
			FColor::White,
			0.0f);
		return;
	}

	// 模式 2：保留完整链路，供后续 Pistol 和网络目标回归使用。
	const FVector PawnViewLocation = GetPawnViewLocation();
	FString DebugText = FString::Printf(
		TEXT("%s  Role=%d Local=%d  Raw=%d Smooth=%d"),
		*GetName(), static_cast<int32>(GetLocalRole()), IsLocallyControlled() ? 1 : 0,
		bRawTargetValid ? 1 : 0, bSmoothedTargetValid ? 1 : 0);

	if (IsLocallyControlled() && FirstPersonCameraComponent)
	{
		const FVector CameraStart = FirstPersonCameraComponent->GetComponentLocation();
		const FVector CameraForward = FirstPersonCameraComponent->GetForwardVector();
		const FVector ControlForward = GetControlRotation().Vector();
		const FVector CameraFromPawnView = CameraStart - PawnViewLocation;
		const FVector ViewLocalDelta = GetControlRotation().UnrotateVector(CameraFromPawnView);
		const double ForwardAngleDegrees = ShooterAimDebug::AngleBetweenDegrees(
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
				ShooterAimDebug::LocalViewMessageKey,
				0.1f,
				FColor::White,
				LocalViewText,
				false,
				FVector2D(0.75f, 0.75f));
		}

		const FVector CameraTraceEnd = CameraStart + CameraForward * MaxAimDistance;
		FHitResult CameraHit;
		bool bCameraPawnHit = false;
		const FVector LocalCameraTarget = TracePreSpreadAimTarget(
			World, CameraStart, CameraTraceEnd, this, &CameraHit, &bCameraPawnHit);

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
			const double AngleDegrees = ShooterAimDebug::AngleBetweenDegrees(
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
		GetActorLocation() + FVector(0.0, 0.0, 220.0),
		DebugText,
		nullptr,
		FColor::White,
		0.0f);
}
