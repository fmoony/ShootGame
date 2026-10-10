// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShootGameEditor/Migration/ShooterFPSlotWiringCommandlet.h"

#include "AnimGraphNode_CopyPoseFromMesh.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_Slot.h"
#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogShooterFPSlotWiring, Log, All);

namespace ShooterFPSlotWiring
{
	const TCHAR* const TargetPath = TEXT("/Game/Shooter/Animation/FirstPerson/ABP_FP_Player.ABP_FP_Player");
	const TCHAR* const SlotName = TEXT("Arms");
	const TCHAR* const GraphName = TEXT("AnimGraph");

	bool Compile(UBlueprint* Blueprint)
	{
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		UE_LOG(LogShooterFPSlotWiring, Display, TEXT("FP_SLOT_COMPILE Asset=%s Errors=%d Warnings=%d"),
			*Blueprint->GetPathName(), Results.NumErrors, Results.NumWarnings);
		return Results.NumErrors == 0 && Blueprint->Status != BS_Error;
	}

	bool Save(UObject* Asset)
	{
		FString Filename;
		UPackage* Package = Asset->GetOutermost();
		if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Filename,
			FPackageName::GetAssetPackageExtension()))
		{
			return false;
		}
		Package->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Package, Asset, *Filename, Args);
	}

	UEdGraph* FindAnimGraph(UBlueprint* Blueprint)
	{
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph->GetFName() == FName(GraphName))
			{
				return Graph;
			}
		}
		return nullptr;
	}

	/** 只在目标输入引脚未连到期望输出时改线；已正确连接时保持原状。 */
	bool EnsureLink(UEdGraph* Graph, UEdGraphPin* Source, UEdGraphPin* Target)
	{
		if (!Source || !Target)
		{
			return false;
		}
		if (Target->LinkedTo.Num() == 1 && Target->LinkedTo[0] == Source)
		{
			return true;
		}
		if (Target->LinkedTo.Num() > 0)
		{
			Target->BreakAllPinLinks();
		}
		return Graph->GetSchema()->TryCreateConnection(Source, Target);
	}

	/** 单条连线的可读形式；与日志语句分开，避免多参数 UE_LOG 被版式规则拆成多行。 */
	FString DescribeLink(const UEdGraphNode* Node, const UEdGraphPin* Pin, const UEdGraphPin* Linked)
	{
		const UEdGraphNode* LinkedNode = Linked ? Linked->GetOwningNode() : nullptr;
		return FString::Printf(TEXT("%s.%s -> %s.%s"),
			*Node->GetClass()->GetName(),
			*Pin->PinName.ToString(),
			LinkedNode ? *LinkedNode->GetClass()->GetName() : TEXT("None"),
			Linked ? *Linked->PinName.ToString() : TEXT("None"));
	}

	void LogGraphState(UAnimBlueprint* Blueprint, const UAnimGraphNode_Slot* SlotNode)
	{
		UEdGraph* AnimGraph = FindAnimGraph(Blueprint);
		UE_LOG(LogShooterFPSlotWiring, Display, TEXT("FP_SLOT_GRAPH Nodes=%d"),
			AnimGraph ? AnimGraph->Nodes.Num() : -1);
		if (AnimGraph)
		{
			for (const UEdGraphNode* Node : AnimGraph->Nodes)
			{
				UE_LOG(LogShooterFPSlotWiring, Display, TEXT("FP_SLOT_GRAPH_NODE %s"), *Node->GetClass()->GetName());
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					for (const UEdGraphPin* Linked : Pin->LinkedTo)
					{
						UE_LOG(LogShooterFPSlotWiring, Display, TEXT("FP_SLOT_LINK %s"), *DescribeLink(Node, Pin, Linked));
					}
				}
			}
		}
		if (SlotNode)
		{
			UE_LOG(LogShooterFPSlotWiring, Display, TEXT("FP_SLOT_NODE SlotName=%s AlwaysUpdateSourcePose=%d"),
				*SlotNode->Node.SlotName.ToString(), SlotNode->Node.bAlwaysUpdateSourcePose ? 1 : 0);
		}
	}
}

UShooterFPSlotWiringCommandlet::UShooterFPSlotWiringCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UShooterFPSlotWiringCommandlet::Main(const FString& Params)
{
	using namespace ShooterFPSlotWiring;

	if (!FParse::Param(*Params, TEXT("Apply")))
	{
		UE_LOG(LogShooterFPSlotWiring, Error, TEXT("Slot wiring requires explicit -Apply."));
		return 1;
	}

	UAnimBlueprint* Target = LoadObject<UAnimBlueprint>(nullptr, TargetPath);
	if (!Target)
	{
		UE_LOG(LogShooterFPSlotWiring, Error, TEXT("FP_SLOT_TARGET_MISSING %s"), TargetPath);
		return 1;
	}

	UEdGraph* AnimGraph = FindAnimGraph(Target);
	if (!AnimGraph)
	{
		UE_LOG(LogShooterFPSlotWiring, Error, TEXT("FP_SLOT_ANIMGRAPH_MISSING"));
		return 1;
	}

	UAnimGraphNode_CopyPoseFromMesh* CopyPose = nullptr;
	UAnimGraphNode_Root* Root = nullptr;
	UAnimGraphNode_Slot* SlotNode = nullptr;
	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		if (UAnimGraphNode_CopyPoseFromMesh* Found = Cast<UAnimGraphNode_CopyPoseFromMesh>(Node))
		{
			CopyPose = Found;
		}
		else if (UAnimGraphNode_Root* FoundRoot = Cast<UAnimGraphNode_Root>(Node))
		{
			Root = FoundRoot;
		}
		else if (UAnimGraphNode_Slot* FoundSlot = Cast<UAnimGraphNode_Slot>(Node))
		{
			if (FoundSlot->Node.SlotName == FName(SlotName))
			{
				SlotNode = FoundSlot;
			}
		}
	}
	if (!CopyPose || !Root)
	{
		UE_LOG(LogShooterFPSlotWiring, Error, TEXT("FP_SLOT_GRAPH_SHAPE_UNEXPECTED CopyPose=%d Root=%d"),
			CopyPose ? 1 : 0, Root ? 1 : 0);
		return 1;
	}

	if (!SlotNode)
	{
		// 节点 Outer 必须是图本身：UEdGraph::AddNode 会断言 NodeToAdd->GetOuter() == this。
		SlotNode = NewObject<UAnimGraphNode_Slot>(AnimGraph, UAnimGraphNode_Slot::StaticClass(), NAME_None,
			RF_Transactional);
		if (!SlotNode)
		{
			UE_LOG(LogShooterFPSlotWiring, Error, TEXT("FP_SLOT_NODE_CREATE_FAILED"));
			return 1;
		}
		SlotNode->Node.SlotName = FName(SlotName);
		SlotNode->Node.bAlwaysUpdateSourcePose = false;
		SlotNode->CreateNewGuid();
		SlotNode->PostPlacedNewNode();
		SlotNode->NodePosX = CopyPose->NodePosX + 320;
		SlotNode->NodePosY = CopyPose->NodePosY;
		AnimGraph->AddNode(SlotNode, true, false);
		if (SlotNode->Pins.Num() == 0)
		{
			SlotNode->AllocateDefaultPins();
		}
		UE_LOG(LogShooterFPSlotWiring, Display, TEXT("FP_SLOT_NODE_CREATED SlotName=%s Pins=%d"), SlotName,
			SlotNode->Pins.Num());
	}

	// CopyPose → Slot('Arms') → Output。
	if (!EnsureLink(AnimGraph, CopyPose->FindPin(TEXT("Pose")), SlotNode->FindPin(TEXT("Source"))))
	{
		UE_LOG(LogShooterFPSlotWiring, Error, TEXT("FP_SLOT_LINK_IN_FAILED"));
		return 1;
	}
	if (!EnsureLink(AnimGraph, SlotNode->FindPin(TEXT("Pose")), Root->FindPin(TEXT("Result"))))
	{
		UE_LOG(LogShooterFPSlotWiring, Error, TEXT("FP_SLOT_LINK_OUT_FAILED"));
		return 1;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target);
	if (!Compile(Target))
	{
		return 1;
	}
	if (!Save(Target))
	{
		return 1;
	}

	LogGraphState(Target, SlotNode);
	UE_LOG(LogShooterFPSlotWiring, Display, TEXT("FP_SLOT_WIRING_SUCCESS Asset=1"));
	return 0;
}
