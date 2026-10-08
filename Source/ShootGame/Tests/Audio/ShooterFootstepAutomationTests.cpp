#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimSequence.h"
#include "Components/ActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWave.h"
#include "UObject/UnrealType.h"

#include "Characters/Animation/ShooterAnimNotify_Footstep.h"
#include "Characters/Audio/ShooterFootstepSoundSet.h"
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
	const UShooterFootstepSoundSet* SoundSet = Character->GetFootstepSoundSet();
	if (!TestNotNull(TEXT("Character 配置共享脚步 DataAsset"), SoundSet))
	{
		return false;
	}
	TestEqual(TEXT("共享配置保存五个 Concrete 音源"), SoundSet->ConcreteSounds.Num(), 5);
	for (const USoundBase* Sound : SoundSet->ConcreteSounds)
	{
		const USoundWave* Wave = Cast<USoundWave>(Sound);
		if (TestNotNull(TEXT("Footstep variant is a SoundWave"), Wave))
		{
			TestEqual(TEXT("Spatial footstep source is mono"), Wave->NumChannels, 1);
			TestTrue(TEXT("Footstep is a short one-shot"), Wave->Duration > 0.05f && Wave->Duration < 0.3f);
			TestFalse(TEXT("Footstep is not looping"), Wave->bLooping);
		}
	}
	const UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd"));
	const UShooterAnimNotify_Footstep* Notify = nullptr;
	if (Sequence)
	{
		for (const FAnimNotifyEvent& Event : Sequence->Notifies)
		{
			Notify = Cast<UShooterAnimNotify_Footstep>(Event.Notify);
			if (Notify)
			{
				break;
			}
		}
	}
	if (!TestNotNull(TEXT("现有移动序列保留脚步 Notify"), Notify))
	{
		return false;
	}
	TestTrue(TEXT("旧移动 Notify 默认仍为 Step"), Notify->EventType == EShooterFootstepEvent::Step);
	TestNull(TEXT("Notify 不再保存旧音源数组"),
		FindFProperty<FArrayProperty>(UShooterAnimNotify_Footstep::StaticClass(), TEXT("Sounds")));
	TestNull(TEXT("Notify 不再保存衰减字段"),
		FindFProperty<FObjectPropertyBase>(UShooterAnimNotify_Footstep::StaticClass(), TEXT("Attenuation")));
	TestTrue(TEXT("三种地面使用独立衰减资产"),
		SoundSet->ConcreteAttenuation != SoundSet->MetalAttenuation &&
		SoundSet->ConcreteAttenuation != SoundSet->DirtAttenuation &&
		SoundSet->MetalAttenuation != SoundSet->DirtAttenuation);
	for (const USoundAttenuation* Attenuation :
		{ SoundSet->ConcreteAttenuation.Get(), SoundSet->MetalAttenuation.Get(), SoundSet->DirtAttenuation.Get() })
	{
		if (TestNotNull(TEXT("DataAsset 地面衰减有效"), Attenuation))
		{
			const FSoundAttenuationSettings& Settings = Attenuation->Attenuation;
			TestTrue(TEXT("Distance attenuation enabled"), Settings.bAttenuate != 0);
			TestTrue(TEXT("Spatialization enabled"), Settings.bSpatialize != 0);
			TestEqual(TEXT("Inner radius is 150cm"), Settings.AttenuationShapeExtents.X, 150.0);
			TestEqual(TEXT("Falloff distance is 1800cm"), Settings.FalloffDistance, 1800.0f);
		}
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
		// 只替换测试角色的瞬态配置，不改共享资产；覆盖空项、配置缺失和全部无效。
		const UShooterFootstepSoundSet* ProductionSet = Character->GetFootstepSoundSet();
		FObjectPropertyBase* ConfigProperty = FindFProperty<FObjectPropertyBase>(Class, TEXT("FootstepSoundSet"));
		USoundBase* ValidSound = ProductionSet && !ProductionSet->ConcreteSounds.IsEmpty()
			? ProductionSet->ConcreteSounds[0].Get() : nullptr;
		if (TestNotNull(TEXT("生产音源有效"), ValidSound) && TestNotNull(TEXT("共享配置字段可读"), ConfigProperty))
		{
			UObject* ProductionConfig = ConfigProperty->GetObjectPropertyValue_InContainer(Character);
			UShooterFootstepSoundSet* FixtureSet = NewObject<UShooterFootstepSoundSet>();
			FixtureSet->ConcreteSounds = { nullptr, ValidSound, nullptr };
			FixtureSet->ConcreteAttenuation = ProductionSet->ConcreteAttenuation;
			ConfigProperty->SetObjectPropertyValue_InContainer(Character, FixtureSet);
			Movement->Velocity = FVector(600.0f, 0.0f, 0.0f);
			for (int32 Index = 0; Index < 8; ++Index)
			{
				Notify->Notify(Character->GetMesh(), nullptr, FAnimNotifyEventReference());
			}
			TestEqual(TEXT("Concrete 中的空项不会随机造成静音"), Requests, 9);
			FixtureSet->ConcreteAttenuation = nullptr;
			Notify->Notify(Character->GetMesh(), nullptr, FAnimNotifyEventReference());
			TestEqual(TEXT("衰减缺失时安全静音，不播放无衰减声音"), Requests, 9);
			FixtureSet->ConcreteAttenuation = ProductionSet->ConcreteAttenuation;
			FixtureSet->ConcreteSounds = { nullptr };
			Notify->Notify(Character->GetMesh(), nullptr, FAnimNotifyEventReference());
			TestEqual(TEXT("Concrete 全部无效时安全静音，无旧音源兜底"), Requests, 9);
			ConfigProperty->SetObjectPropertyValue_InContainer(Character, nullptr);
			Notify->Notify(Character->GetMesh(), nullptr, FAnimNotifyEventReference());
			TestEqual(TEXT("缺少 DataAsset 时安全静音，无旧音源兜底"), Requests, 9);
			// 同一播放路径：Landing 允许静止，但仍拒绝 FP 与空中事件。
			ConfigProperty->SetObjectPropertyValue_InContainer(Character, ProductionConfig);
			Notify->EventType = EShooterFootstepEvent::Landing;
			Movement->Velocity = FVector::ZeroVector;
			Notify->Notify(Character->GetMesh(), nullptr, FAnimNotifyEventReference());
			TestEqual(TEXT("原地 Landing 产生一次播放请求"), Requests, 10);
			Notify->Notify(Character->GetFirstPersonMesh(), nullptr, FAnimNotifyEventReference());
			TestEqual(TEXT("Landing 不从 FP 重复播放"), Requests, 10);
			Movement->SetMovementMode(MOVE_Falling);
			Notify->Notify(Character->GetMesh(), nullptr, FAnimNotifyEventReference());
			TestEqual(TEXT("空中 Landing 保持静音"), Requests, 10);
			Movement->SetMovementMode(MOVE_Walking);
			Movement->Velocity = FVector(600.0f, 0.0f, 0.0f);
			Notify->Notify(Character->GetMesh(), nullptr, FAnimNotifyEventReference());
			TestEqual(TEXT("移动 Landing 产生一次播放请求"), Requests, 11);
		}
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
