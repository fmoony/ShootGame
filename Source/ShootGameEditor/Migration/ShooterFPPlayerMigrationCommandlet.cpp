// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShootGameEditor/Migration/ShooterFPPlayerMigrationCommandlet.h"

#include "AnimGraphNode_CopyPoseFromMesh.h"
#include "AnimGraphNode_Root.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "AssetToolsModule.h"
#include "BlueprintEditorLibrary.h"
#include "Characters/Animation/ShooterFirstPersonAnimInstance.h"
#include "Characters/ShooterCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogShooterFPPlayerMigration, Log, All);

namespace ShooterFPPlayerMigration
{
	const TCHAR* const SourcePath = TEXT("/Game/Shooter/Animation/FirstPerson/ABP_FP_Copy.ABP_FP_Copy");
	const TCHAR* const TargetFolder = TEXT("/Game/Shooter/Animation/FirstPerson");
	const TCHAR* const TargetName = TEXT("ABP_FP_Player");
	const TCHAR* const TargetPath = TEXT("/Game/Shooter/Animation/FirstPerson/ABP_FP_Player.ABP_FP_Player");
	const TCHAR* const CharacterPath =
		TEXT("/Game/Shooter/Blueprints/Characters/BP_ShooterCharacter.BP_ShooterCharacter");

	bool Compile(UBlueprint* Blueprint)
	{
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		UE_LOG(LogShooterFPPlayerMigration, Display, TEXT("FP_PLAYER_COMPILE Asset=%s Errors=%d Warnings=%d"),
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

	UEdGraph* FindGraph(UBlueprint* Blueprint, const FName GraphName)
	{
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph->GetFName() == GraphName)
			{
				return Graph;
			}
		}
		return nullptr;
	}

	/** 删除除 Root 与 CopyPose 之外的全部 AnimGraph 节点，返回是否发生了变化。 */
	bool PruneToCopyPose(UAnimBlueprint* Blueprint, UAnimGraphNode_CopyPoseFromMesh*& OutCopyPose, UAnimGraphNode_Root*& OutRoot)
	{
		UEdGraph* AnimGraph = FindGraph(Blueprint, TEXT("AnimGraph"));
		if (!AnimGraph)
		{
			return false;
		}
		OutCopyPose = nullptr;
		OutRoot = nullptr;
		TArray<UEdGraphNode*> ToRemove;
		for (UEdGraphNode* Node : AnimGraph->Nodes)
		{
			if (UAnimGraphNode_CopyPoseFromMesh* CopyPose = Cast<UAnimGraphNode_CopyPoseFromMesh>(Node))
			{
				OutCopyPose = CopyPose;
			}
			else if (UAnimGraphNode_Root* Root = Cast<UAnimGraphNode_Root>(Node))
			{
				OutRoot = Root;
			}
			else
			{
				ToRemove.Add(Node);
			}
		}
		for (UEdGraphNode* Node : ToRemove)
		{
			UE_LOG(LogShooterFPPlayerMigration, Display, TEXT("FP_PLAYER_REMOVE_NODE %s"), *Node->GetName());
			FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
		}
		return ToRemove.Num() > 0;
	}

	/** 保证 CopyPose 直接接到 Output Pose；已连接时返回 true 且不重复改线。 */
	bool ConnectCopyPoseToOutput(UEdGraph* AnimGraph, UAnimGraphNode_CopyPoseFromMesh* CopyPose, UAnimGraphNode_Root* Root)
	{
		UEdGraphPin* Source = CopyPose ? CopyPose->FindPin(TEXT("Pose")) : nullptr;
		UEdGraphPin* Target = Root ? Root->FindPin(TEXT("Result")) : nullptr;
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
		return AnimGraph->GetSchema()->TryCreateConnection(Source, Target);
	}

	/** 只读诊断：节点清单与关键复制设置，证明有效链与复制源一致。 */
	void LogGraphState(UAnimBlueprint* Blueprint, UAnimGraphNode_CopyPoseFromMesh* CopyPose)
	{
		UEdGraph* AnimGraph = FindGraph(Blueprint, TEXT("AnimGraph"));
		const int32 NodeCount = AnimGraph ? AnimGraph->Nodes.Num() : -1;
		UE_LOG(LogShooterFPPlayerMigration, Display, TEXT("FP_PLAYER_GRAPH Nodes=%d"), NodeCount);
		if (AnimGraph)
		{
			for (UEdGraphNode* Node : AnimGraph->Nodes)
			{
				UE_LOG(LogShooterFPPlayerMigration, Display, TEXT("FP_PLAYER_GRAPH_NODE %s"),
					*Node->GetClass()->GetName());
			}
		}
		if (CopyPose)
		{
			UE_LOG(LogShooterFPPlayerMigration, Display, TEXT("FP_PLAYER_COPY_POSE UseAttachedParent=%d"),
				CopyPose->Node.bUseAttachedParent ? 1 : 0);
			UE_LOG(LogShooterFPPlayerMigration, Display, TEXT("FP_PLAYER_COPY_POSE CopyCustomAttributes=%d"),
				CopyPose->Node.bCopyCustomAttributes ? 1 : 0);
		}
	}
}

UShooterFPPlayerMigrationCommandlet::UShooterFPPlayerMigrationCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UShooterFPPlayerMigrationCommandlet::Main(const FString& Params)
{
	using namespace ShooterFPPlayerMigration;

	if (!FParse::Param(*Params, TEXT("Apply")))
	{
		UE_LOG(LogShooterFPPlayerMigration, Error, TEXT("Migration requires explicit -Apply."));
		return 1;
	}

	UAnimBlueprint* Source = LoadObject<UAnimBlueprint>(nullptr, SourcePath);
	UBlueprint* CharacterBP = LoadObject<UBlueprint>(nullptr, CharacterPath);
	if (!Source || !Source->TargetSkeleton || !Source->ParentClass ||
		!Source->ParentClass->IsChildOf(UAnimInstance::StaticClass()) || !CharacterBP)
	{
		UE_LOG(LogShooterFPPlayerMigration, Error, TEXT("FP_PLAYER_SOURCE_INVALID"));
		return 1;
	}

	UAnimBlueprint* Target = LoadObject<UAnimBlueprint>(nullptr, TargetPath, nullptr, LOAD_NoWarn);
	if (!Target)
	{
		IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		Target = Cast<UAnimBlueprint>(Tools.DuplicateAsset(TargetName, TargetFolder, Source));
	}
	if (!Target || Target->TargetSkeleton != Source->TargetSkeleton)
	{
		UE_LOG(LogShooterFPPlayerMigration, Error, TEXT("FP_PLAYER_TARGET_INVALID"));
		return 1;
	}

	// 只保留 CopyPose → Output；先裁剪再改父类，避免旧图引用旧父类成员。
	UAnimGraphNode_CopyPoseFromMesh* CopyPose = nullptr;
	UAnimGraphNode_Root* Root = nullptr;
	PruneToCopyPose(Target, CopyPose, Root);
	if (!CopyPose || !Root)
	{
		UE_LOG(LogShooterFPPlayerMigration, Error, TEXT("FP_PLAYER_GRAPH_SHAPE_UNEXPECTED"));
		return 1;
	}
	const bool bConnected = ConnectCopyPoseToOutput(FindGraph(Target, TEXT("AnimGraph")), CopyPose, Root);
	if (!bConnected)
	{
		UE_LOG(LogShooterFPPlayerMigration, Error, TEXT("FP_PLAYER_CONNECT_FAILED"));
		return 1;
	}

	if (Target->ParentClass != UShooterFirstPersonAnimInstance::StaticClass())
	{
		UBlueprintEditorLibrary::ReparentBlueprint(Target, UShooterFirstPersonAnimInstance::StaticClass());
	}
	if (Target->ParentClass != UShooterFirstPersonAnimInstance::StaticClass())
	{
		UE_LOG(LogShooterFPPlayerMigration, Error, TEXT("FP_PLAYER_REPARENT_FAILED Parent=%s"),
			*GetNameSafe(Target->ParentClass));
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
	LogGraphState(Target, CopyPose);
	UE_LOG(LogShooterFPPlayerMigration, Display, TEXT("FP_PLAYER_MAIN_GRAPH Parent=%s"),
		*Target->ParentClass->GetName());

	AShooterCharacter* CharacterDefaults = Cast<AShooterCharacter>(CharacterBP->GeneratedClass->GetDefaultObject());
	FClassProperty* ClassProperty = FindFProperty<FClassProperty>(AShooterCharacter::StaticClass(),
		TEXT("PlayerFirstPersonAnimInstanceClass"));
	if (!CharacterDefaults || !ClassProperty)
	{
		UE_LOG(LogShooterFPPlayerMigration, Error, TEXT("FP_PLAYER_CHARACTER_PROPERTY_MISSING"));
		return 1;
	}
	ClassProperty->SetObjectPropertyValue_InContainer(CharacterDefaults, Target->GeneratedClass);

	// 出生必须继续使用初始 FP 主图：这里只读回，不写入 FP Mesh 默认类。
	USkeletalMeshComponent* FirstPersonMesh = CharacterDefaults->GetFirstPersonMesh();
	UE_LOG(LogShooterFPPlayerMigration, Display, TEXT("FP_PLAYER_DEFAULTS FPDefaultClass=%s"),
		*GetNameSafe(FirstPersonMesh ? FirstPersonMesh->GetAnimClass() : nullptr));
	UE_LOG(LogShooterFPPlayerMigration, Display, TEXT("FP_PLAYER_DEFAULTS TPClass=%s"),
		*GetNameSafe(CharacterDefaults->GetMesh() ? CharacterDefaults->GetMesh()->GetAnimClass() : nullptr));

	if (!Compile(CharacterBP) || !Save(CharacterBP))
	{
		return 1;
	}
	UE_LOG(LogShooterFPPlayerMigration, Display, TEXT("FP_PLAYER_MIGRATION_SUCCESS Assets=2"));
	return 0;
}
