#include "ShooterAnimNotify_Footstep.h"

#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

#include "Characters/ShooterCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS
UShooterAnimNotify_Footstep::FPlaybackObserved UShooterAnimNotify_Footstep::PlaybackObserved;
#endif

UShooterAnimNotify_Footstep::UShooterAnimNotify_Footstep()
{
	for (int32 Index = 0; Index < 5; ++Index)
	{
		const FString Path = FString::Printf(TEXT("/Game/Shooter/Audio/Footsteps/SW_Footstep_Concrete_%03d"), Index);
		ConstructorHelpers::FObjectFinder<USoundBase> Sound(*Path);
		if (Sound.Succeeded())
		{
			Sounds.Add(Sound.Object);
		}
	}
	static ConstructorHelpers::FObjectFinder<USoundAttenuation> DefaultAttenuation(TEXT("/Game/Shooter/Audio/Footsteps/SA_Footsteps"));
	Attenuation = DefaultAttenuation.Object;
}

void UShooterAnimNotify_Footstep::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	const AShooterCharacter* Character = MeshComp ? Cast<AShooterCharacter>(MeshComp->GetOwner()) : nullptr;
	if (!Character || MeshComp != Character->GetMesh() || Character->GetNetMode() == NM_DedicatedServer ||
		Character->IsDead() || Sounds.IsEmpty() || !MeshComp->DoesSocketExist(FootBone))
	{
		return;
	}
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!Movement || !Movement->IsMovingOnGround() || Movement->Velocity.SizeSquared2D() < FMath::Square(10.0f))
	{
		return;
	}
	USoundBase* Sound = Sounds[FMath::RandRange(0, Sounds.Num() - 1)];
	if (!Sound)
	{
		return;
	}
	// Notify 对象由所有使用此资源的角色共享；不在其中保存角色的步频或上次播放状态。
	UGameplayStatics::PlaySoundAtLocation(MeshComp, Sound, MeshComp->GetSocketLocation(FootBone),
		FRotator::ZeroRotator, Volume, FMath::FRandRange(0.96f, 1.04f), 0.0f, Attenuation);
#if WITH_DEV_AUTOMATION_TESTS
	PlaybackObserved.Broadcast(MeshComp, FootBone);
#endif
}

FString UShooterAnimNotify_Footstep::GetNotifyName_Implementation() const
{
	return FString::Printf(TEXT("Footstep:%s"), *FootBone.ToString());
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
