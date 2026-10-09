// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShootGameEditor/Migration/ShooterTPPlayerMigrationCommandlet.h"

#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "Animation/AimOffsetBlendSpace.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "AssetToolsModule.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Characters/ShooterCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/DataTable.h"
#include "IAssetTools.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "Weapons/Data/ShooterWeaponConfigRow.h"

DEFINE_LOG_CATEGORY_STATIC(LogShooterTPPlayerMigration, Log, All);

namespace ShooterTPPlayerMigration
{
	bool BindInput(UAnimBlueprint* Blueprint, FName GraphName, FName NodeName, FName PinName, FName VariableName)
	{
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph->GetFName() != GraphName)
			{
				continue;
			}
			for (UEdGraphNode* GraphNode : Graph->Nodes)
			{
				UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(GraphNode);
				if (!Node || Node->GetFName() != NodeName)
				{
					continue;
				}
				for (int32 Index = 0; Index < Node->ShowPinForProperties.Num(); ++Index)
				{
					if (Node->ShowPinForProperties[Index].PropertyName == PinName)
					{
						Node->SetPinVisibility(true, Index);
						break;
					}
				}
				UEdGraphPin* Input = Node->FindPin(PinName);
				if (!Input)
				{
					return false;
				}
				if (Input->LinkedTo.Num() == 1)
				{
					const UK2Node_VariableGet* Existing = Cast<UK2Node_VariableGet>(Input->LinkedTo[0]->GetOwningNode());
					return Existing && Existing->VariableReference.GetMemberName() == VariableName;
				}
				FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
				UK2Node_VariableGet* Getter = Creator.CreateNode();
				Getter->VariableReference.SetSelfMember(VariableName);
				Getter->NodePosX = Node->NodePosX - 260;
				Getter->NodePosY = Node->NodePosY;
				Creator.Finalize();
				return Graph->GetSchema()->TryCreateConnection(Getter->FindPin(VariableName), Input);
			}
		}
		return false;
	}

	bool Compile(UBlueprint* Blueprint)
	{
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		UE_LOG(LogShooterTPPlayerMigration, Display, TEXT("TP_PLAYER_COMPILE Asset=%s Errors=%d Warnings=%d"),
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
}

UShooterTPPlayerMigrationCommandlet::UShooterTPPlayerMigrationCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UShooterTPPlayerMigrationCommandlet::Main(const FString& Params)
{
	using namespace ShooterTPPlayerMigration;
	if (!FParse::Param(*Params, TEXT("Apply")))
	{
		UE_LOG(LogShooterTPPlayerMigration, Error, TEXT("Migration requires explicit -Apply."));
		return 1;
	}
	UAnimBlueprint* Source = LoadObject<UAnimBlueprint>(nullptr,
		TEXT("/Game/Shooter/Animation/ThirdPerson/ABP_TP_Rifle.ABP_TP_Rifle"));
	UDataTable* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/Shooter/Data/DT_WeaponData.DT_WeaponData"));
	UBlueprint* CharacterBP = LoadObject<UBlueprint>(nullptr,
		TEXT("/Game/Shooter/Blueprints/Characters/BP_ShooterCharacter.BP_ShooterCharacter"));
	if (!Source || Source->ParentClass != UShooterThirdPersonAnimInstance::StaticClass() || !Source->TargetSkeleton ||
		!Table || Table->GetRowStruct() != FShooterWeaponConfigRow::StaticStruct() || !CharacterBP)
	{
		return 1;
	}
	// 先验证全部行与资源，失败时不创建或保存任何目标资产。
	const FName WeaponIds[] = {TEXT("Rifle"), TEXT("Pistol"), TEXT("AWP"), TEXT("GrenadeLauncher")};
	TArray<FShooterWeaponConfigRow*> Rows;
	TArray<UAnimSequence*> Holds;
	TArray<UAimOffsetBlendSpace*> Offsets;
	TArray<UAnimSequence*> Reloads;
	for (FName Id : WeaponIds)
	{
		FShooterWeaponConfigRow* Row = Table->FindRow<FShooterWeaponConfigRow>(Id, TEXT("TPPlayerMigration"));
		const FString Family = Id == TEXT("Pistol") ? TEXT("Pistol") : TEXT("Rifle");
		const FString Root = TEXT("/Game/Characters/Mannequins/Anims/") + Family;
		UAnimSequence* Hold = LoadObject<UAnimSequence>(nullptr, *(Root + TEXT("/MF_") + Family + TEXT("_Idle_ADS")));
		const FString AimFolder = Id == TEXT("Pistol") ? TEXT("/Aim/AO_") : TEXT("/AIM/AO_");
		UAimOffsetBlendSpace* Offset = LoadObject<UAimOffsetBlendSpace>(nullptr, *(Root + AimFolder + Family));
		UAnimSequence* Reload = LoadObject<UAnimSequence>(nullptr, *(Root + TEXT("/MM_") + Family + TEXT("_Reload")));
		if (!Row || !Hold || !Offset || !Reload || Hold->GetSkeleton() != Source->TargetSkeleton ||
			Offset->GetSkeleton() != Source->TargetSkeleton || Reload->GetSkeleton() != Source->TargetSkeleton ||
			Hold->IsValidAdditive() || Reload->IsValidAdditive())
		{
			UE_LOG(LogShooterTPPlayerMigration, Error, TEXT("Resource validation failed: %s"), *Id.ToString());
			return 1;
		}
		Rows.Add(Row);
		Holds.Add(Hold);
		Offsets.Add(Offset);
		Reloads.Add(Reload);
	}
	const TCHAR* TargetPath = TEXT("/Game/Shooter/Animation/ThirdPerson/ABP_TP_Player.ABP_TP_Player");
	UAnimBlueprint* Target = LoadObject<UAnimBlueprint>(nullptr, TargetPath, nullptr, LOAD_NoWarn);
	if (!Target)
	{
		IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		Target = Cast<UAnimBlueprint>(Tools.DuplicateAsset(TEXT("ABP_TP_Player"),
			TEXT("/Game/Shooter/Animation/ThirdPerson"), Source));
	}
	if (!Target || Target->ParentClass != Source->ParentClass || Target->TargetSkeleton != Source->TargetSkeleton)
	{
		return 1;
	}
	bool bBound = BindInput(Target, TEXT("AnimGraph"), TEXT("AnimGraphNode_SequencePlayer_0"),
		TEXT("Sequence"), TEXT("ThirdPersonHoldSequence"));
	bBound &= BindInput(Target, TEXT("AnimGraph"), TEXT("AnimGraphNode_RotationOffsetBlendSpace_0"),
		TEXT("BlendSpace"), TEXT("ThirdPersonAimOffset"));
	bBound &= BindInput(Target, TEXT("Reload"), TEXT("AnimGraphNode_SequencePlayer_2"),
		TEXT("Sequence"), TEXT("ThirdPersonReloadSequence"));
	bBound &= BindInput(Target, TEXT("AnimGraph"), TEXT("AnimGraphNode_LayeredBoneBlend_1"),
		TEXT("BlendWeights_0"), TEXT("WeaponUpperBodyWeight"));
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target);
	if (!bBound || !Compile(Target))
	{
		return 1;
	}
	UShooterThirdPersonAnimInstance* Defaults = Cast<UShooterThirdPersonAnimInstance>(Target->GeneratedClass->GetDefaultObject());
	Defaults->ThirdPersonHoldSequence = Holds[0];
	Defaults->ThirdPersonAimOffset = Offsets[0];
	Defaults->ThirdPersonReloadSequence = Reloads[0];
	Defaults->MinimumRemoteAimTargetDistanceFromMuzzle = 100.0f;
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		Rows[Index]->ThirdPersonHoldSequence = Holds[Index];
		Rows[Index]->ThirdPersonAimOffset = Offsets[Index];
		Rows[Index]->ThirdPersonReloadSequence = Reloads[Index];
		Rows[Index]->ThirdPersonMinimumAimTargetDistanceFromMuzzle = Index == 1 ? 50.0f : 100.0f;
	}
	AShooterCharacter* CharacterDefaults = Cast<AShooterCharacter>(CharacterBP->GeneratedClass->GetDefaultObject());
	FClassProperty* ClassProperty = FindFProperty<FClassProperty>(AShooterCharacter::StaticClass(),
		TEXT("PlayerThirdPersonAnimInstanceClass"));
	if (!CharacterDefaults || !ClassProperty)
	{
		return 1;
	}
	ClassProperty->SetObjectPropertyValue_InContainer(CharacterDefaults, Target->GeneratedClass);
	CharacterDefaults->GetMesh()->SetAnimInstanceClass(Target->GeneratedClass);
	if (!Compile(CharacterBP) || !Save(Target) || !Save(Table) || !Save(CharacterBP))
	{
		return 1;
	}
	UE_LOG(LogShooterTPPlayerMigration, Display, TEXT("TP_PLAYER_MIGRATION_SUCCESS Assets=3 Weapons=4"));
	return 0;
}
