#include "ShooterAnimNotify_Footstep.h"

#include "Animation/AnimSequenceBase.h"
#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

#include "Characters/Audio/ShooterFootstepSoundSet.h"
#include "Characters/ShooterCharacter.h"
#include "ShootGame.h"

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
static TAutoConsoleVariable<int32> CVarFootstepSurfaceDebug(TEXT("ShootGame.Footsteps.SurfaceDebug"), 0,
	TEXT("启用脚步表面命中与回退日志；0 关闭，1 开启。"), ECVF_Cheat);
#endif

#if WITH_DEV_AUTOMATION_TESTS
UShooterAnimNotify_Footstep::FPlaybackObserved UShooterAnimNotify_Footstep::PlaybackObserved;
#endif

namespace ShooterFootstepAudio
{
	USoundBase* PickValidSound(const TArray<TObjectPtr<USoundBase>>& Candidates)
	{
		// 对有效引用均匀随机抽样，不分配临时数组，也不因空项而漏播。
		USoundBase* Selected = nullptr;
		int32 ValidCount = 0;
		for (USoundBase* Candidate : Candidates)
		{
			if (IsValid(Candidate) && FMath::RandHelper(++ValidCount) == 0)
			{
				Selected = Candidate;
			}
		}
		return Selected;
	}
}

void UShooterAnimNotify_Footstep::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	const AShooterCharacter* Character = MeshComp ? Cast<AShooterCharacter>(MeshComp->GetOwner()) : nullptr;
	if (!Character || MeshComp != Character->GetMesh() || Character->GetNetMode() == NM_DedicatedServer ||
		Character->IsDead() || !MeshComp->DoesSocketExist(FootBone))
	{
		return;
	}
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!Movement || !Movement->IsMovingOnGround() ||
		(EventType != EShooterFootstepEvent::Landing && Movement->Velocity.SizeSquared2D() < FMath::Square(10.0f)))
	{
		return;
	}
	const FVector FootLocation = MeshComp->GetSocketLocation(FootBone);
	FHitResult GroundHit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ShooterFootstepSurface), false, Character);
	QueryParams.bReturnPhysicalMaterial = true;
	const bool bGroundHit = MeshComp->GetWorld()->LineTraceSingleByChannel(
		GroundHit,
		FootLocation + FVector(0.0f, 0.0f, 25.0f),
		FootLocation - FVector(0.0f, 0.0f, 100.0f),
		ECC_Visibility,
		QueryParams);
	const EPhysicalSurface Surface = bGroundHit && GroundHit.PhysMaterial.IsValid()
		? UPhysicalMaterial::DetermineSurfaceType(GroundHit.PhysMaterial.Get()) : SurfaceType_Default;
	const UShooterFootstepSoundSet* SoundSet = Character->GetFootstepSoundSet();
	const TArray<TObjectPtr<USoundBase>>* SurfaceSounds = nullptr;
	USoundAttenuation* Attenuation = SoundSet ? SoundSet->ConcreteAttenuation.Get() : nullptr;
	const TCHAR* SurfaceName = TEXT("Default");
	const TCHAR* AudioName = TEXT("Concrete");
	const TCHAR* AudioSource = TEXT("DataAsset");
	bool bSurfaceFallback = false;
	switch (Surface)
	{
	case SurfaceType1:
		SurfaceName = TEXT("Concrete");
		SurfaceSounds = SoundSet ? &SoundSet->ConcreteSounds : nullptr;
		break;
	case SurfaceType2:
		SurfaceName = TEXT("Metal");
		SurfaceSounds = SoundSet ? &SoundSet->MetalSounds : nullptr;
		Attenuation = SoundSet ? SoundSet->MetalAttenuation.Get() : nullptr;
		break;
	case SurfaceType3:
		SurfaceName = TEXT("Dirt");
		SurfaceSounds = SoundSet ? &SoundSet->DirtSounds : nullptr;
		Attenuation = SoundSet ? SoundSet->DirtAttenuation.Get() : nullptr;
		break;
	default:
		bSurfaceFallback = true;
		break;
	}
	if (!IsValid(Attenuation))
	{
		Attenuation = SoundSet ? SoundSet->ConcreteAttenuation.Get() : nullptr;
	}
	USoundBase* Sound = SurfaceSounds ? ShooterFootstepAudio::PickValidSound(*SurfaceSounds) : nullptr;
	const bool bAudioFallback = Sound == nullptr;
	if (Sound)
	{
		AudioName = SurfaceName;
	}
	else
	{
		Sound = SoundSet ? ShooterFootstepAudio::PickValidSound(SoundSet->ConcreteSounds) : nullptr;
	}
	if (!Sound)
	{
		AudioName = TEXT("None");
		AudioSource = TEXT("None");
	}
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (CVarFootstepSurfaceDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogShootGame, Log,
			TEXT("FootstepSurface Event=%s Character=%s Local=%s Foot=%s Actor=%s Component=%s PM=%s "
				"Surface=%s(%d) Hit=%s SurfaceFallback=%s AudioFallback=%s Audio=%s "
				"Source=%s Sound=%s SoundSet=%s Attenuation=%s"),
			EventType == EShooterFootstepEvent::Landing ? TEXT("Landing") : TEXT("Step"),
			*GetNameSafe(Character),
			Character->IsLocallyControlled() ? TEXT("true") : TEXT("false"),
			*FootBone.ToString(),
			*GetNameSafe(GroundHit.GetActor()),
			*GetNameSafe(GroundHit.GetComponent()),
			*GetNameSafe(GroundHit.PhysMaterial.Get()),
			SurfaceName,
			static_cast<int32>(Surface),
			bGroundHit ? TEXT("true") : TEXT("false"),
			bSurfaceFallback ? TEXT("true") : TEXT("false"),
			bAudioFallback ? TEXT("true") : TEXT("false"),
			AudioName,
			AudioSource,
			*GetNameSafe(Sound),
			*GetNameSafe(SoundSet),
			*GetNameSafe(Attenuation));
	}
#endif
	if (!Sound || !IsValid(Attenuation))
	{
		return;
	}
	// Notify 对象由所有使用此资源的角色共享；不在其中保存角色的步频或上次播放状态。
	UGameplayStatics::PlaySoundAtLocation(MeshComp, Sound, FootLocation,
		FRotator::ZeroRotator, Volume, FMath::FRandRange(0.96f, 1.04f), 0.0f, Attenuation);
#if WITH_DEV_AUTOMATION_TESTS
	PlaybackObserved.Broadcast(MeshComp, FootBone);
#endif
}

FString UShooterAnimNotify_Footstep::GetNotifyName_Implementation() const
{
	return FString::Printf(TEXT("%s:%s"),
		EventType == EShooterFootstepEvent::Landing ? TEXT("Landing") : TEXT("Footstep"), *FootBone.ToString());
}

#if WITH_EDITOR
bool UShooterAnimNotify_Footstep::ConfigureEditorEvent(FName InFootBone)
{
	UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(GetOuter());
	if (!Sequence || (InFootBone != TEXT("foot_l") && InFootBone != TEXT("foot_r")))
	{
		return false;
	}
	for (FAnimNotifyEvent& Event : Sequence->Notifies)
	{
		if (Event.Notify == this)
		{
			Sequence->Modify();
			Modify();
			FootBone = InFootBone;
			Event.NotifyName = FName(*GetNotifyName());
			Event.bTriggerOnDedicatedServer = false;
			Event.TriggerWeightThreshold = 0.1f;
			Sequence->RefreshCacheData();
			return true;
		}
	}
	return false;
}
#endif
