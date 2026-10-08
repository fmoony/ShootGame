#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/ActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWave.h"

#include "Characters/Animation/ShooterAnimNotify_Footstep.h"
#include "Characters/ShooterCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFootstepAssetsTest, "ShootGame.Audio.Footsteps.ProductionConfiguration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFootstepAssetsTest::RunTest(const FString& Parameters)
{
	const UClass* CharacterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Shooter/Blueprints/Characters/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!TestNotNull(TEXT("Production Character class resolves"), CharacterClass))
	{
		return false;
	}
	const AShooterCharacter* Character = CharacterClass->GetDefaultObject<AShooterCharacter>();
	TArray<UActorComponent*> Components;
	Character->GetComponents(Components);
	for (const UActorComponent* Component : Components)
	{
		TestNotEqual(TEXT("Production Character has no distance footstep component"),
			Component->GetFName(), FName(TEXT("FootstepComponent")));
	}
	TestNull(TEXT("Removed distance component class is not registered"),
		FindObject<UClass>(nullptr, TEXT("/Script/ShootGame.ShooterFootstepComponent")));
	const UShooterAnimNotify_Footstep* Notify = GetDefault<UShooterAnimNotify_Footstep>();
	TestEqual(TEXT("Notify references the same five production sound variants"), Notify->Sounds.Num(), 5);
	for (const USoundBase* Sound : Notify->Sounds)
	{
		const USoundWave* Wave = Cast<USoundWave>(Sound);
		if (TestNotNull(TEXT("Footstep variant is a SoundWave"), Wave))
		{
			TestEqual(TEXT("Spatial footstep source is mono"), Wave->NumChannels, 1);
			TestTrue(TEXT("Footstep is a short one-shot"), Wave->Duration > 0.05f && Wave->Duration < 0.3f);
			TestFalse(TEXT("Footstep is not looping"), Wave->bLooping);
		}
	}
	if (TestNotNull(TEXT("Notify spatial attenuation resolves"), Notify->Attenuation.Get()))
	{
		const FSoundAttenuationSettings& Settings = Notify->Attenuation->Attenuation;
		TestTrue(TEXT("Distance attenuation enabled"), Settings.bAttenuate != 0);
		TestTrue(TEXT("Spatialization enabled"), Settings.bSpatialize != 0);
		TestEqual(TEXT("Inner radius is 150cm"), Settings.AttenuationShapeExtents.X, 150.0);
		TestEqual(TEXT("Falloff distance is 1800cm"), Settings.FalloffDistance, 1800.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFootstepNotifyGuardsTest, "ShootGame.Audio.Footsteps.NotifyGuards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFootstepNotifyGuardsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world created"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	UClass* Class = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Shooter/Blueprints/Characters/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	AShooterCharacter* Character = Class ? World->SpawnActor<AShooterCharacter>(Class) : nullptr;
	if (Character && Class)
	{
		UShooterAnimNotify_Footstep* Notify = NewObject<UShooterAnimNotify_Footstep>();
		int32 Requests = 0;
		const FDelegateHandle Handle = UShooterAnimNotify_Footstep::PlaybackObserved.AddLambda(
			[&Requests](USkeletalMeshComponent*, FName) { ++Requests; });
		UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		Movement->SetMovementMode(MOVE_Walking);
		Movement->Velocity = FVector(600.0f, 0.0f, 0.0f);
		Notify->Notify(Character->GetMesh(), nullptr, FAnimNotifyEventReference());
		TestEqual(TEXT("TP ground movement produces one request"), Requests, 1);
		Notify->Notify(Character->GetFirstPersonMesh(), nullptr, FAnimNotifyEventReference());
		TestEqual(TEXT("Shared FP animation cannot produce a second request"), Requests, 1);
		Movement->SetMovementMode(MOVE_Falling);
		Notify->Notify(Character->GetMesh(), nullptr, FAnimNotifyEventReference());
		TestEqual(TEXT("Airborne Notify is silent"), Requests, 1);
		Movement->SetMovementMode(MOVE_Walking);
		Movement->Velocity = FVector::ZeroVector;
		Notify->Notify(Character->GetMesh(), nullptr, FAnimNotifyEventReference());
		TestEqual(TEXT("Residual Notify after stopping is silent"), Requests, 1);
		UShooterAnimNotify_Footstep::PlaybackObserved.Remove(Handle);
	}
	else
	{
		AddError(TEXT("Footstep guard fixture could not load its character or production mesh"));
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
