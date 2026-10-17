// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterRifleSegmentedCommandlet.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"

#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogRifleSegmented, Log, All);

UAnimSequence* UShooterRifleSegmentedAnimInstance::ResolveSequence(float Pitch) const
{
	return Pitch < Down60Threshold ? Down60Sequence.Get()
		: Pitch <= -30.0f ? Down30Sequence.Get() : OriginalSequence.Get();
}

void UShooterRifleSegmentedAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	if (bProductionGraphTest)
	{
		bRifleReadyAvailable = true;
		bRifleLowReadyRequested = RifleTestGoal == ERifleTransitionTestGoal::LowReady;
		const auto* Ready = GetStateMachineInstanceFromName(TEXT("RifleReady"));
		if (Ready && ((Ready->GetCurrentStateName() == TEXT("ArmedAim") && GetRifleLowerRequested()) ||
			(Ready->GetCurrentStateName() == TEXT("LowReady") && GetRifleRaiseRequested())))
		{
			LatchedPitch = RifleTestPitch;
			++SelectionCount;
		}
		UpdateRifleReadyAnimation();
		return;
	}
	const FAnimNode_StateMachine* Machine = GetStateMachineInstanceFromName(TEXT("WeaponAction"));
	if (!Machine)
	{
		return;
	}
	const FName State = Machine->GetCurrentStateName();
	const bool bEnterLower = State == TEXT("ArmedAim") && GetRifleTestLowerRequested();
	const bool bEnterRaise = State == TEXT("LowReady") && GetRifleTestRaiseRequested();
	if (bEnterLower || bEnterRaise)
	{
		auto* Player = const_cast<FAnimNode_SequencePlayer*>(GetTransitionPlayer());
		UAnimSequence* Sequence = ResolveSequence(RifleTestPitch);
		if (Player && Sequence)
		{
			// 下一次正常状态入场初始化时间；这里不写当前累计时间。
			Player->SetSequence(Sequence);
			LatchedPitch = RifleTestPitch;
			++SelectionCount;
		}
	}
}

void AShooterRifleSegmentedPreview::CycleSegmentPitch()
{
	const TArray<float> Pitches{0.0f, -15.0f, -30.0f, -45.0f, -50.0f, -50.01f, -55.0f, -60.0f, -60.01f, -75.0f, 30.0f};
	const int32 Current = Pitches.IndexOfByPredicate([this](float Pitch)
	{
		return FMath::IsNearlyEqual(Pitch, RifleTestPitch, 0.001f);
	});
	RifleTestPitch = Pitches[(Current + 1) % Pitches.Num()];
	Tick(0.0f);
}

FString AShooterRifleSegmentedPreview::GetSegmentStatus() const
{
	const auto* Instance = Cast<UShooterRifleSegmentedAnimInstance>(ThirdPersonMesh->GetAnimInstance());
	const auto* Player = Instance ? Instance->GetTransitionPlayer() : nullptr;
	return Player && Player->GetSequence()
		? FString::Printf(TEXT("%s | selected at %.3f | now %.3f | time %.6f | rate %.1f"),
			*Player->GetSequence()->GetPathName(), Instance->LatchedPitch, RifleTestPitch,
			Player->GetAccumulatedTime(), Player->GetPlayRate()) : TEXT("Invalid");
}

namespace RifleSegmented
{
	const FString Folder = TEXT("/Game/Experiments/RifleTransition/Segmented50_20261011");
	const FString Name = TEXT("ABP_RifleSegmented_LowToReady");
	const FString Output = FPaths::ProjectSavedDir() / TEXT("RifleTools/Measure");

	/** 正式图的只读测量编译；不写入接入配置或资产。 */
	namespace Production
	{
		bool Compile(UBlueprint* BP)
		{
			FCompilerResultsLog Results;
			FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
			UE_LOG(LogRifleSegmented, Display, TEXT("RIFLE_PRODUCTION_COMPILE %s errors=%d warnings=%d"),
				*BP->GetName(), Results.NumErrors, Results.NumWarnings);
			return Results.NumErrors == 0 && BP->Status != BS_Error;
		}
	}

	FString Fields(const FTransform& Transform)
	{
		const FVector P = Transform.GetLocation();
		const FQuat Q = Transform.GetRotation();
		return FString::Printf(TEXT("%.9f,%.9f,%.9f,%.10f,%.10f,%.10f,%.10f"), P.X, P.Y, P.Z, Q.X, Q.Y, Q.Z, Q.W);
	}

	bool Measure(bool bProduction = false)
	{
		const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
			.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("RifleSegmentedWorld"),
			nullptr, true, ERHIFeatureLevel::Num, &Values);
		UClass* Class = LoadClass<UAnimInstance>(nullptr, *(Folder / Name + TEXT(".") + Name + TEXT("_C")));
		if (bProduction)
		{
			auto* Source = LoadObject<UAnimBlueprint>(nullptr,
				TEXT("/Game/Shooter/Animation/ThirdPerson/ABP_TP_Player"));
			auto* Copy = Source ? DuplicateObject<UAnimBlueprint>(Source,
				GetTransientPackage(), TEXT("RifleProductionMeasure"))
				: nullptr;
			if (!Copy) { return false; }
			Copy->ParentClass = UShooterRifleSegmentedAnimInstance::StaticClass();
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Copy);
			if (!Production::Compile(Copy)) { return false; }
			Class = Copy->GeneratedClass;
		}
		if (!World || !Class)
		{
			return false;
		}
		FString Metrics = TEXT("case_pitch,strength,scenario,frame,pitch,goal,state,time,rate,latched,selections,sequence\r\n");
		FString Poses = TEXT("case_pitch,strength,scenario,frame,bone,px,py,pz,qx,qy,qz,qw\r\n");
		bool bValid = true;
		const TArray<float> Pitches{0, 30, 60, -15, -29.999f, -30, -45, -49.999f, -50, -50.001f, -55,
			-59.999f, -60, -60.001f, -75};
		const TArray<FString> Scenarios{TEXT("Full"), TEXT("Reverse25"), TEXT("Reverse50"), TEXT("Reverse75"),
			TEXT("ChangeDuringLower"), TEXT("ChangeDuringRaise"), TEXT("ChangeDuringReverse")};
		for (float Pitch : Pitches)
		{
			for (float Strength : bProduction ? TArray<float>{0.0f} : TArray<float>{0.0f, 1.0f})
			{
				for (const FString& Scenario : Scenarios)
				{
					const bool bDynamic = Scenario.StartsWith(TEXT("Change"));
					if (bDynamic && Pitch != -45)
					{
						continue;
					}
					auto* Actor = World->SpawnActor<AShooterRifleSegmentedPreview>();
					Actor->ThirdPersonMesh->SetAnimInstanceClass(Class);
					auto* Instance = Cast<UShooterRifleSegmentedAnimInstance>(Actor->ThirdPersonMesh->GetAnimInstance());
					const int32 Cut = Scenario == TEXT("Reverse25") ? 73 : Scenario == TEXT("Reverse50") ? 85 : 98;
					Instance->bProductionGraphTest = bProduction;
					for (int32 Frame = 0; Frame < 260; ++Frame)
					{
						float CurrentPitch = Pitch;
						bool bLow = Frame >= 60 && Frame < 140;
						if (Scenario.StartsWith(TEXT("Reverse")))
						{
							bLow = Frame >= 60 && Frame < Cut;
						}
						if (bDynamic)
						{
							const int32 Change = Scenario == TEXT("ChangeDuringRaise") ? 160 : 85;
							CurrentPitch = Frame >= Change ? -75.0f : Pitch;
							if (Scenario == TEXT("ChangeDuringReverse"))
							{
								bLow = Frame >= 60 && Frame < 85;
							}
						}
						++GFrameCounter;
						Actor->SetRiflePreviewGoal(bLow, CurrentPitch, Strength);
						Instance->bRifleTestIK = false;
						Actor->ThirdPersonMesh->TickAnimation(1.0f / 60.0f, false);
						Actor->ThirdPersonMesh->RefreshBoneTransforms();
						Actor->RifleMesh->UpdateComponentToWorld();
						const auto* Machine = Instance->GetStateMachineInstanceFromName(
							bProduction ? TEXT("RifleReady") : TEXT("WeaponAction"));
						const auto* Player = bProduction ? Instance->GetRifleTransitionPlayer() : Instance->GetTransitionPlayer();
						const UAnimSequenceBase* Sequence = Player && Player->GetSequence() ? Player->GetSequence()
							: Instance->RifleTransitionCorrected.Get();
						if (!Machine || !Player || !Sequence)
						{
							bValid = false;
							continue;
						}
						const FString Prefix = FString::Printf(TEXT("%.3f,%.0f,%s,%d,"), Pitch, Strength, *Scenario, Frame);
						Metrics += Prefix + FString::Printf(TEXT("%.3f,%d,%s,%.9f,%.3f,%.3f,%d,%s\r\n"),
							CurrentPitch,
							bLow,
							*Machine->GetCurrentStateName().ToString(),
							Player->GetAccumulatedTime(),
							Player->GetPlayRate(),
							Instance->LatchedPitch,
							Instance->SelectionCount,
							*Sequence->GetPathName());
						for (FName Bone : {FName(TEXT("hand_r")), FName(TEXT("hand_l")), FName(TEXT("upperarm_r")),
							FName(TEXT("lowerarm_r")), FName(TEXT("spine_05")), FName(TEXT("Muzzle"))})
						{
							USkeletalMeshComponent* Mesh = Bone == TEXT("Muzzle") ? Actor->RifleMesh : Actor->ThirdPersonMesh;
							Poses += Prefix + Bone.ToString() + TEXT(",") + Fields(Mesh->GetSocketTransform(Bone)
								.GetRelativeTransform(Actor->ThirdPersonMesh->GetComponentTransform())) + TEXT("\r\n");
						}
					}
					Actor->Destroy();
				}
			}
		}
		const FString Destination = bProduction ? FPaths::ProjectSavedDir() / TEXT("RifleTools/ProductionMeasure") : Output;
		IFileManager::Get().MakeDirectory(*Destination, true);
		bValid &= FFileHelper::SaveStringToFile(Metrics, *(Destination / TEXT("runtime_metrics.csv")));
		bValid &= FFileHelper::SaveStringToFile(Poses, *(Destination / TEXT("runtime_poses.csv")));
		World->DestroyWorld(false);
		return bValid;
	}
}

UShooterRifleSegmentedCommandlet::UShooterRifleSegmentedCommandlet()
{
	IsClient = true;
	IsEditor = true;
	LogToConsole = true;
}

int32 UShooterRifleSegmentedCommandlet::Main(const FString& Params)
{
	if (FParse::Param(*Params, TEXT("ProductionApply")))
	{
		UE_LOG(LogRifleSegmented, Error, TEXT("旧分段接入入口已移除；正式方案为 Local，工具只保留测量。"));
		return 1;
	}
	if (FParse::Param(*Params, TEXT("Prepare")))
	{
		UE_LOG(LogRifleSegmented, Error, TEXT("Only measurement of the retained isolated scheme is supported."));
		return 1;
	}
	const bool bSuccess = RifleSegmented::Measure(FParse::Param(*Params, TEXT("ProductionMeasure")));
	UE_LOG(LogRifleSegmented, Display, TEXT("SEGMENTED_COMPLETE success=%d"), bSuccess);
	return bSuccess ? 0 : 1;
}
