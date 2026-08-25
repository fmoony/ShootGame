// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShootGameEditor/Migration/ShooterAnimBPGraphAuditCommandlet.h"

#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogShooterAnimBPGraphAudit, Log, All);

namespace ShooterAnimBPGraphAudit
{
	void LogVariableList(const UAnimBlueprint* AnimBP)
	{
		for (const FBPVariableDescription& Variable : AnimBP->NewVariables)
		{
			const FString SubCategoryObject = Variable.VarType.PinSubCategoryObject.IsValid()
				? Variable.VarType.PinSubCategoryObject->GetPathName()
				: TEXT("<none>");
			UE_LOG(
				LogShooterAnimBPGraphAudit,
				Display,
				TEXT("AUTOMATION_ANIMBP_GRAPH_AUDIT Variable Asset=%s Name=%s Category=%s SubObject=%s Container=%d"),
				*AnimBP->GetPathName(),
				*Variable.VarName.ToString(),
				*Variable.VarType.PinCategory.ToString(),
				*SubCategoryObject,
				static_cast<int32>(Variable.VarType.ContainerType));
		}
	}

	void LogNodeList(const UAnimBlueprint* AnimBP, const UEdGraph* Graph)
	{
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			const FString NodeTitle = Node
				? Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Replace(TEXT("\n"), TEXT(" "))
				: TEXT("<null>");
			FString VariableName = TEXT("<not-a-variable-node>");
			if (const UK2Node_VariableGet* VariableGet = Cast<UK2Node_VariableGet>(Node))
			{
				VariableName = VariableGet->VariableReference.GetMemberName().ToString();
			}
			else if (const UK2Node_VariableSet* VariableSet = Cast<UK2Node_VariableSet>(Node))
			{
				VariableName = VariableSet->VariableReference.GetMemberName().ToString();
			}
			else if (const UK2Node_CallFunction* CallFunction = Cast<UK2Node_CallFunction>(Node))
			{
				if (CallFunction->GetTargetFunction())
				{
					VariableName = FString::Printf(
						TEXT("call:%s"),
						*CallFunction->GetTargetFunction()->GetPathName());
				}
			}

			UE_LOG(
				LogShooterAnimBPGraphAudit,
				Display,
				TEXT("AUTOMATION_ANIMBP_GRAPH_AUDIT Node Asset=%s Graph=%s GraphClass=%s NodeClass=%s VariableOrCall=%s Title=%s"),
				*AnimBP->GetPathName(),
				*Graph->GetName(),
				*Graph->GetClass()->GetName(),
				*Node->GetClass()->GetName(),
				*VariableName,
				*NodeTitle);

			for (const UEdGraphPin* Pin : Node->Pins)
			{
				const FString SubObject = Pin->PinType.PinSubCategoryObject.IsValid()
					? Pin->PinType.PinSubCategoryObject->GetName()
					: TEXT("<none>");
				UE_LOG(
					LogShooterAnimBPGraphAudit,
					Display,
					TEXT("AUTOMATION_ANIMBP_GRAPH_AUDIT Pin Asset=%s Graph=%s NodeClass=%s Node=%s Pin=%s Direction=%s Category=%s SubObject=%s"),
					*AnimBP->GetPathName(),
					*Graph->GetName(),
					*Node->GetClass()->GetName(),
					*NodeTitle,
					*Pin->GetName(),
					Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"),
					*Pin->PinType.PinCategory.ToString(),
					*SubObject);

				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin || !LinkedPin->GetOwningNode())
					{
						continue;
					}
					const FString LinkedNodeTitle = LinkedPin->GetOwningNode()
						->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Replace(TEXT("\n"), TEXT(" "));
					UE_LOG(
						LogShooterAnimBPGraphAudit,
						Display,
						TEXT("AUTOMATION_ANIMBP_GRAPH_AUDIT Link Asset=%s Graph=%s FromNode=%s FromPin=%s ToNodeClass=%s ToNode=%s ToPin=%s"),
						*AnimBP->GetPathName(),
						*Graph->GetName(),
						*NodeTitle,
						*Pin->GetName(),
						*LinkedPin->GetOwningNode()->GetClass()->GetName(),
						*LinkedNodeTitle,
						*LinkedPin->GetName());
				}
			}
		}
	}

	bool AuditAsset(const FString& ObjectPath, int32& OutFailureCount)
	{
		UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath);
		if (!AnimBP)
		{
			UE_LOG(
				LogShooterAnimBPGraphAudit,
				Error,
				TEXT("AUTOMATION_ANIMBP_GRAPH_AUDIT LoadFailed Asset=%s"),
				*ObjectPath);
			++OutFailureCount;
			return false;
		}

		UE_LOG(
			LogShooterAnimBPGraphAudit,
			Display,
			TEXT("AUTOMATION_ANIMBP_GRAPH_AUDIT Begin Asset=%s Parent=%s Status=%d"),
			*AnimBP->GetPathName(),
			AnimBP->ParentClass ? *AnimBP->ParentClass->GetPathName() : TEXT("<none>"),
			static_cast<int32>(AnimBP->Status));

		LogVariableList(AnimBP);

		TArray<UEdGraph*> Graphs;
		Graphs.Append(AnimBP->UbergraphPages);
		Graphs.Append(AnimBP->EventGraphs);
		Graphs.Append(AnimBP->FunctionGraphs);
		Graphs.Append(AnimBP->MacroGraphs);
		Graphs.Append(AnimBP->DelegateSignatureGraphs);

		TSet<const UEdGraph*> VisitedGraphs;
		while (Graphs.Num() > 0)
		{
			UEdGraph* Graph = Graphs.Pop(EAllowShrinking::No);
			if (!Graph || VisitedGraphs.Contains(Graph))
			{
				continue;
			}
			VisitedGraphs.Add(Graph);

			UE_LOG(
				LogShooterAnimBPGraphAudit,
				Display,
				TEXT("AUTOMATION_ANIMBP_GRAPH_AUDIT Graph Asset=%s Graph=%s GraphClass=%s NodeCount=%d"),
				*AnimBP->GetPathName(),
				*Graph->GetName(),
				*Graph->GetClass()->GetName(),
				Graph->Nodes.Num());
			LogNodeList(AnimBP, Graph);

			for (UEdGraph* SubGraph : Graph->SubGraphs)
			{
				if (SubGraph && !VisitedGraphs.Contains(SubGraph))
				{
					Graphs.Add(SubGraph);
				}
			}
		}

		UE_LOG(
			LogShooterAnimBPGraphAudit,
			Display,
			TEXT("AUTOMATION_ANIMBP_GRAPH_AUDIT End Asset=%s"),
			*AnimBP->GetPathName());
		return true;
	}
}

UShooterAnimBPGraphAuditCommandlet::UShooterAnimBPGraphAuditCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UShooterAnimBPGraphAuditCommandlet::Main(const FString& Params)
{
	using namespace ShooterAnimBPGraphAudit;

	const TCHAR* TargetAssets[] = {
		TEXT("/Game/Shooter/Animation/FirstPerson/ABP_FP_Weapon.ABP_FP_Weapon"),
		TEXT("/Game/Shooter/Animation/FirstPerson/ABP_FP_Pistol.ABP_FP_Pistol"),
		TEXT("/Game/Shooter/Animation/ThirdPerson/ABP_TP_Rifle.ABP_TP_Rifle"),
		TEXT("/Game/Shooter/Animation/ThirdPerson/ABP_TP_Pistol.ABP_TP_Pistol"),
	};

	int32 FailureCount = 0;
	for (const TCHAR* TargetAsset : TargetAssets)
	{
		AuditAsset(TargetAsset, FailureCount);
	}

	UE_LOG(
		LogShooterAnimBPGraphAudit,
		Display,
		TEXT("AUTOMATION_ANIMBP_GRAPH_AUDIT_SUMMARY Total=%d Failures=%d"),
		static_cast<int32>(UE_ARRAY_COUNT(TargetAssets)),
		FailureCount);

	return FailureCount == 0 ? 0 : 1;
}
