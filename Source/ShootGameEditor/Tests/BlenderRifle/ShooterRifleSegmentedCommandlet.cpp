// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterRifleSegmentedCommandlet.h"

#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimStateNode.h"
#include "AnimStateEntryNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_InputKey.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "K2Node_Self.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

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
		bDebugRifleReadyAvailable = true;
		bDebugRifleLowReady = RifleTestGoal == ERifleTransitionTestGoal::LowReady;
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

	/** 限定两个正式蓝图的可重复接入工具；不创建 Skeleton、地图或 Gameplay 资产。 */
	namespace Production
	{
		template <typename T> T* Add(UEdGraph* Graph, int32 X, int32 Y)
		{
			FGraphNodeCreator<T> Creator(*Graph);
			T* Node = Creator.CreateNode();
			Node->NodePosX = X;
			Node->NodePosY = Y;
			Creator.Finalize();
			return Node;
		}

		bool Link(UEdGraphPin* A, UEdGraphPin* B)
		{
			return A && B && A->GetOwningNode()->GetGraph()->GetSchema()->TryCreateConnection(A, B);
		}

		UK2Node_CallFunction* Call(UEdGraph* Graph, UFunction* Function, int32 X, int32 Y)
		{
			FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
			auto* Node = Creator.CreateNode();
			Node->SetFromFunction(Function);
			Node->NodePosX = X;
			Node->NodePosY = Y;
			Creator.Finalize();
			return Node;
		}

		bool Bind(UAnimGraphNode_Base* Node, FName Pin, FName Variable)
		{
			for (int32 I = 0; I < Node->ShowPinForProperties.Num(); ++I)
			{
				if (Node->ShowPinForProperties[I].PropertyName == Pin)
				{
					Node->SetPinVisibility(true, I);
					break;
				}
			}
			FGraphNodeCreator<UK2Node_VariableGet> Creator(*Node->GetGraph());
			auto* Getter = Creator.CreateNode();
			Getter->VariableReference.SetSelfMember(Variable);
			Getter->NodePosX = Node->NodePosX - 300;
			Getter->NodePosY = Node->NodePosY + Node->GetGraph()->Nodes.Num() * 32;
			Creator.Finalize();
			return Link(Getter->FindPin(Variable), Node->FindPin(Pin));
		}

		UAnimStateNode* State(UEdGraph* Graph, const TCHAR* StateName, int32 X)
		{
			auto* Node = Add<UAnimStateNode>(Graph, X, 0);
			Node->BoundGraph->Rename(StateName);
			Node->bAlwaysResetOnEntry = true;
			return Node;
		}

		bool Rule(UAnimStateNode* From, UAnimStateNode* To, const TCHAR* Function, float Duration, int32 Priority)
		{
			auto* Node = Add<UAnimStateTransitionNode>(From->GetGraph(), From->NodePosX + 80, Priority * 60);
			Node->CrossfadeDuration = Duration;
			Node->PriorityOrder = Priority;
			Node->CreateConnections(From, To);
			auto* FunctionNode = Call(Node->BoundGraph,
				UShooterThirdPersonAnimInstance::StaticClass()->FindFunctionByName(Function), -300, 0);
			for (UEdGraphNode* Result : Node->BoundGraph->Nodes)
			{
				if (Cast<UAnimGraphNode_TransitionResult>(Result))
				{
					return Link(FunctionNode->GetReturnValuePin(), Result->FindPin(TEXT("bCanEnterTransition")));
				}
			}
			return false;
		}

		bool Compile(UBlueprint* BP)
		{
			FCompilerResultsLog Results;
			FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
			UE_LOG(LogRifleSegmented, Display, TEXT("RIFLE_PRODUCTION_COMPILE %s errors=%d warnings=%d"),
				*BP->GetName(), Results.NumErrors, Results.NumWarnings);
			return Results.NumErrors == 0 && BP->Status != BS_Error;
		}

		bool Save(UObject* Asset)
		{
			UPackage* Package = Asset->GetOutermost();
			const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), TEXT(".uasset"));
			FSavePackageArgs Args;
			Args.TopLevelFlags = RF_Public | RF_Standalone;
			Package->MarkPackageDirty();
			return UPackage::SavePackage(Package, Asset, *File, Args);
		}

		bool Apply()
		{
			auto* BP = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Shooter/Animation/ThirdPerson/ABP_TP_Player"));
			auto* CharacterBP = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Shooter/Blueprints/Characters/BP_ShooterCharacter"));
			const FString Root = TEXT("/Game/Characters/Mannequins/Anims/Rifle/");
			auto* Corrected = LoadObject<UAnimSequence>(nullptr,
				*(Root + TEXT("MM_Rifle_AimToLowReady_CorrectedGrip")));
			auto* Down30 = LoadObject<UAnimSequence>(nullptr, *(Root + TEXT("MM_Rifle_AimToLowReady_Down30")));
			auto* Down60 = LoadObject<UAnimSequence>(nullptr, *(Root + TEXT("MM_Rifle_AimToLowReady_Down60")));
			auto* LowSequence = LoadObject<UAnimSequence>(nullptr,
				TEXT("/Game/Shooter/Animation/ThirdPerson/Rifle/MM_Rifle_LowReady"));
			if (!BP || !CharacterBP || !Corrected || !Down30 || !Down60 || !LowSequence ||
				BP->ParentClass != UShooterThirdPersonAnimInstance::StaticClass() ||
				Corrected->GetSkeleton() != BP->TargetSkeleton || Down30->GetSkeleton() != BP->TargetSkeleton ||
				Down60->GetSkeleton() != BP->TargetSkeleton || LowSequence->GetSkeleton() != BP->TargetSkeleton)
			{
				return false;
			}
			TArray<UEdGraph*> Graphs;
			BP->GetAllGraphs(Graphs);
			UEdGraph* Armed = nullptr;
			UAnimGraphNode_SaveCachedPose* Cache = nullptr;
			bool bInstalled = false;
			for (UEdGraph* Graph : Graphs)
			{
				bInstalled |= Graph->GetFName() == TEXT("RifleReady");
				if (Graph->GetFName() == TEXT("ArmedAim")) { Armed = Graph; }
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (auto* Cached = Cast<UAnimGraphNode_SaveCachedPose>(Node))
					{
						if (Cached->CacheName == TEXT("ArmedAimPose")) { Cache = Cached; }
					}
				}
			}
			if (!Armed || !Cache) { return false; }
			if (!bInstalled)
			{
				auto* Machine = Add<UAnimGraphNode_StateMachine>(Armed, 64, 400);
				Machine->EditorStateMachineGraph->Rename(TEXT("RifleReady"));
				UAnimationStateMachineGraph* Graph = Machine->EditorStateMachineGraph;
				auto* Aim = State(Graph, TEXT("ArmedAim"), 200);
				auto* Transition = State(Graph, TEXT("AimTransition"), 500);
				auto* Low = State(Graph, TEXT("LowReady"), 800);
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (Cast<UAnimStateEntryNode>(Node))
					{
						if (!Link(Node->FindPin(TEXT("Entry")), Aim->GetInputPin())) { return false; }
					}
				}
				auto* Use = Add<UAnimGraphNode_UseCachedPose>(Aim->BoundGraph, -100, 0);
				Use->SaveCachedPoseNode = Cache;
				Use->ReconstructNode();
				if (!Link(Use->FindPin(TEXT("Pose")), Aim->GetPoseSinkPinInsideState())) { return false; }
				auto* Player = Add<UAnimGraphNode_SequencePlayer>(Transition->BoundGraph, 0, 0);
				Player->Node.SetSequence(Corrected);
				Player->Node.SetLoopAnimation(false);
				if (!Bind(Player, TEXT("Sequence"), TEXT("RifleTransitionSequence")) ||
					!Bind(Player, TEXT("PlayRate"), TEXT("RifleTransitionPlayRate")) ||
					!Bind(Player, TEXT("StartPosition"), TEXT("RifleTransitionStartPosition")) ||
					!Link(Player->FindPin(TEXT("Pose")), Transition->GetPoseSinkPinInsideState())) { return false; }
				auto* LowPlayer = Add<UAnimGraphNode_SequencePlayer>(Low->BoundGraph, 0, 0);
				LowPlayer->Node.SetSequence(LowSequence);
				if (!Link(LowPlayer->FindPin(TEXT("Pose")), Low->GetPoseSinkPinInsideState())) { return false; }
				if (!Rule(Aim, Transition, TEXT("GetRifleLowerRequested"), 0.0f, 2) ||
					!Rule(Low, Transition, TEXT("GetRifleRaiseRequested"), 0.0f, 2) ||
					!Rule(Transition, Low, TEXT("GetRifleAtLowEndpoint"), 0.12f, 2) ||
					!Rule(Transition, Aim, TEXT("GetRifleAtAimEndpoint"), 0.12f, 2) ||
					!Rule(Transition, Aim, TEXT("GetRifleReadyAbortRequested"), 0.10f, 1) ||
					!Rule(Low, Aim, TEXT("GetRifleReadyAbortRequested"), 0.10f, 1)) { return false; }
				for (UEdGraphNode* Node : Armed->Nodes)
				{
					if (Cast<UAnimGraphNode_StateResult>(Node))
					{
						Node->FindPin(TEXT("Result"))->BreakAllPinLinks();
						if (!Link(Machine->FindPin(TEXT("Pose")), Node->FindPin(TEXT("Result")))) { return false; }
					}
				}
			}
			UEdGraph* Event = CharacterBP->UbergraphPages[0];
			bool bKeyInstalled = false;
			for (UEdGraphNode* Node : Event->Nodes)
			{
				if (auto* Key = Cast<UK2Node_InputKey>(Node)) { bKeyInstalled |= Key->InputKey == EKeys::K; }
			}
			if (!bKeyInstalled)
			{
				FGraphNodeCreator<UK2Node_InputKey> Creator(*Event);
				auto* Key = Creator.CreateNode();
				Key->InputKey = EKeys::K;
				Key->bConsumeInput = false;
				Key->NodePosY = 480;
				Key->NodeComment = TEXT("临时 Rifle 动画切换：本地表现，可在过渡中反向；不改变 Gameplay。");
				Creator.Finalize();
				auto* Pawn = Add<UK2Node_Self>(Event, 240, 650);
				UFunction* ToggleFunction = UShooterThirdPersonAnimInstance::StaticClass()->FindFunctionByName(
					TEXT("ToggleDebugRifleLowReady"));
				auto* Toggle = Call(Event, ToggleFunction, 520, 480);
				if (!Link(Pawn->FindPin(UEdGraphSchema_K2::PN_Self), Toggle->FindPin(TEXT("Pawn"))) ||
					!Link(Key->FindPin(TEXT("Pressed")), Toggle->FindPin(UEdGraphSchema_K2::PN_Execute)))
				{
					return false;
				}
			}
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(CharacterBP);
			if (!Compile(BP) || !Compile(CharacterBP)) { return false; }
			auto* Defaults = CastChecked<UShooterThirdPersonAnimInstance>(BP->GeneratedClass->GetDefaultObject());
			Defaults->RifleTransitionCorrected = Corrected;
			Defaults->RifleTransitionDown30 = Down30;
			Defaults->RifleTransitionDown60 = Down60;
			Defaults->RifleTransitionSequence = Corrected;
			return Save(BP) && Save(CharacterBP);
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
		const bool bSuccess = RifleSegmented::Production::Apply();
		UE_LOG(LogRifleSegmented, Display, TEXT("RIFLE_PRODUCTION_APPLY success=%d"), bSuccess);
		return bSuccess ? 0 : 1;
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
