#if WITH_DEV_AUTOMATION_TESTS

#include "AudioDevice.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Characters/Animation/ShooterAnimNotify_Footstep.h"
#include "Components/SkeletalMeshComponent.h"
#include "Characters/ShooterCharacter.h"
#include "ShootGame.h"

/** 显式控制台命令启用的测试；通过正常移动输入观察 TP Notify，不手动触发播放。 */
namespace ShooterFootstepNetworkProbe
{
	struct FObservation
	{
		float Time = 0.0f;
		int32 OwnerRequests = 0;
		int32 RemoteRequests = 0;
		int32 OwnerLeftRequests = 0;
		int32 OwnerRightRequests = 0;
		int32 DuplicateFrames = 0;
		float LastOwnerTime = -1.0f;
		int32 StoppedOwnerRequests = INDEX_NONE;
		bool bFinished = false;
		bool bRecording = false;
	};

	TMap<TWeakObjectPtr<UWorld>, FObservation> Worlds;
	FDelegateHandle TickHandle;
	FDelegateHandle NotifyHandle;

	void ObserveNotify(USkeletalMeshComponent* Mesh, FName Foot)
	{
		AShooterCharacter* Character = Mesh ? Cast<AShooterCharacter>(Mesh->GetOwner()) : nullptr;
		FObservation* State = Character ? Worlds.Find(Character->GetWorld()) : nullptr;
		if (!State)
		{
			return;
		}
		if (Character->GetNetMode() == NM_DedicatedServer || Mesh != Character->GetMesh())
		{
			UE_LOG(LogShootGame, Error, TEXT("AUTOMATION_TEST_FAILURE: Non-TP or dedicated footstep"));
			return;
		}
		if (State->bFinished)
		{
			return;
		}
		if (Character->IsLocallyControlled())
		{
			++State->OwnerRequests;
			State->OwnerLeftRequests += Foot == TEXT("foot_l") ? 1 : 0;
			State->OwnerRightRequests += Foot == TEXT("foot_r") ? 1 : 0;
			const float Now = Character->GetWorld()->GetTimeSeconds();
			State->DuplicateFrames += Now == State->LastOwnerTime ? 1 : 0;
			State->LastOwnerTime = Now;
		}
		else
		{
			++State->RemoteRequests;
		}
	}

	void Tick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
	{
		if (!World || !World->IsGameWorld())
		{
			return;
		}
		FObservation& Observation = Worlds.FindOrAdd(World);
		if (Observation.bFinished)
		{
			return;
		}
		AShooterCharacter* Local = Cast<AShooterCharacter>(UGameplayStatics::GetPlayerPawn(World, 0));
		bool bHasRemote = false;
		int32 CharacterCount = 0;
		for (TActorIterator<AShooterCharacter> It(World); It; ++It)
		{
			AShooterCharacter* Character = *It;
			bHasRemote |= Character != Local;
			++CharacterCount;
		}
		if (World->GetNetMode() == NM_DedicatedServer)
		{
			if (CharacterCount >= 2)
			{
				UE_LOG(LogShootGame, Display, TEXT("FOOTSTEP_SERVER_OBSERVING Characters=%d"), CharacterCount);
				Observation.bFinished = true;
			}
			return;
		}
		if (!Local || (World->GetNetMode() != NM_Standalone && !bHasRemote))
		{
			return;
		}
		if (Observation.Time == 0.0f && FParse::Param(FCommandLine::Get(), TEXT("ShootGameFootstepRecord")))
		{
			if (FAudioDeviceHandle Device = World->GetAudioDevice())
			{
				Device->StartRecording(nullptr, 10.0f);
				Observation.bRecording = true;
			}
		}
		Observation.Time += DeltaSeconds;
		if (Observation.Time < 8.0f)
		{
			// 小范围往返：使用拥有者正常的 AddMovementInput 和 CharacterMovement 网络路径。
			// 避免换向间隔与动画左右脚周期锁相，导致某一脚总在速度过零时被正确过滤。
			const float Direction = (FMath::FloorToInt(Observation.Time / 0.85f) % 2) ? -1.0f : 1.0f;
			Local->AddMovementInput(Local->GetActorForwardVector(), Direction);
		}
		else if (Observation.Time >= 9.0f && Observation.StoppedOwnerRequests == INDEX_NONE)
		{
			Observation.StoppedOwnerRequests = Observation.OwnerRequests;
		}
		else if (Observation.Time >= 10.0f)
		{
			Observation.bFinished = true;
			if (Observation.bRecording)
			{
				if (FAudioDeviceHandle Device = World->GetAudioDevice())
				{
					float Channels = 0.0f;
					float SampleRate = 0.0f;
					const Audio::FAlignedFloatBuffer& Samples = Device->StopRecording(nullptr, Channels, SampleRate);
					const FString Root = FPaths::ProjectSavedDir() / TEXT("Footsteps/Capture");
					IFileManager::Get().MakeDirectory(*FPaths::GetPath(Root), true);
					FFileHelper::SaveArrayToFile(TArrayView<const uint8>(
						reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(float)),
						*(Root + TEXT(".f32")));
					FFileHelper::SaveStringToFile(FString::Printf(TEXT("%.0f %.0f"), Channels, SampleRate), *(Root + TEXT(".txt")));
					UE_LOG(LogShootGame, Display, TEXT("FOOTSTEP_AUDIO_CAPTURE Samples=%d Channels=%.0f Rate=%.0f"),
						Samples.Num(), Channels, SampleRate);
				}
			}
			if (FParse::Param(FCommandLine::Get(), TEXT("ShootGameFootstepRecord")))
			{
				// 无窗口录音进程在写完文件后正常退出，避免依赖外部强制回收。
				FPlatformMisc::RequestExit(false);
			}
			const bool bRemoteObserved = World->GetNetMode() == NM_Standalone || Observation.RemoteRequests >= 2;
			const FString Counts = FString::Printf(
				TEXT("Owner=%d Remote=%d Left=%d Right=%d Duplicates=%d"),
				Observation.OwnerRequests,
				Observation.RemoteRequests,
				Observation.OwnerLeftRequests,
				Observation.OwnerRightRequests,
				Observation.DuplicateFrames);
			if (Observation.OwnerLeftRequests < 2 || Observation.OwnerRightRequests < 2 || !bRemoteObserved ||
				Observation.OwnerRequests != Observation.StoppedOwnerRequests || Observation.DuplicateFrames != 0)
			{
				UE_LOG(LogShootGame, Error, TEXT("AUTOMATION_TEST_FAILURE: Footsteps %s Stopped=%d"),
					*Counts, Observation.StoppedOwnerRequests);
				return;
			}
			UE_LOG(LogShootGame, Display, TEXT("FOOTSTEP_PROBE_SUCCESS NetMode=%d %s StopSilent=true"),
				static_cast<int32>(World->GetNetMode()), *Counts);
		}
	}

	FAutoConsoleCommand StartCommand(TEXT("ShootGame.Footsteps.Probe"),
		TEXT("Dev test: walk for eight seconds, then verify owner/remote footsteps and stop silence."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (!TickHandle.IsValid())
			{
				TickHandle = FWorldDelegates::OnWorldPostActorTick.AddStatic(&Tick);
				NotifyHandle = UShooterAnimNotify_Footstep::PlaybackObserved.AddStatic(&ObserveNotify);
			}
		}), ECVF_Cheat);
}

#endif
