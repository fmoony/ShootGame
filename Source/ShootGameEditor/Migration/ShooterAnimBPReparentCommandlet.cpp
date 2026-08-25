// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShootGameEditor/Migration/ShooterAnimBPReparentCommandlet.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "BlueprintEditorLibrary.h"
#include "Characters/Animation/ShooterFirstPersonAnimInstance.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogShooterAnimBPReparent, Log, All);

namespace ShooterAnimBPReparent
{
	struct FReparentTarget
	{
		const TCHAR* PackagePath;
		const TCHAR* AssetName;
		UClass* (*GetParentClass)();
		const TCHAR* LegacyVariableToRename = nullptr;
		const TCHAR* LegacyVariableNewName = nullptr;
	};

	UClass* GetFirstPersonParentClass()
	{
		return UShooterFirstPersonAnimInstance::StaticClass();
	}

	UClass* GetThirdPersonParentClass()
	{
		return UShooterThirdPersonAnimInstance::StaticClass();
	}

	bool ReparentCompileAndSave(const FReparentTarget& Target, int32& OutFailureCount)
	{
		const FString ObjectPath = FString::Printf(
			TEXT("%s.%s"),
			Target.PackagePath,
			Target.AssetName);
		UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(
			nullptr,
			*ObjectPath);
		if (!AnimBP)
		{
			UE_LOG(
				LogShooterAnimBPReparent,
				Error,
				TEXT("AnimBP reparent failed to load: %s"),
				*ObjectPath);
			++OutFailureCount;
			return false;
		}

		UClass* NewParentClass = Target.GetParentClass();
		if (!NewParentClass || !NewParentClass->IsChildOf(UAnimInstance::StaticClass()))
		{
			UE_LOG(
				LogShooterAnimBPReparent,
				Error,
				TEXT("AnimBP reparent invalid parent for: %s"),
				*ObjectPath);
			++OutFailureCount;
			return false;
		}

		UE_LOG(
			LogShooterAnimBPReparent,
			Display,
			TEXT("AnimBP reparent: Asset=%s OldParent=%s NewParent=%s"),
			*ObjectPath,
			AnimBP->ParentClass ? *AnimBP->ParentClass->GetPathName() : TEXT("<none>"),
			*NewParentClass->GetPathName());

		// R6.2 冲突处理：旧蓝图变量与新基类属性同名时，先把蓝图变量改名，
		// 后续 R6.3 会把这些节点迁到基类快照或删除。
		if (Target.LegacyVariableToRename && Target.LegacyVariableNewName)
		{
			const FName OldVariableName(Target.LegacyVariableToRename);
			if (FBlueprintEditorUtils::FindNewVariableIndex(AnimBP, OldVariableName) != INDEX_NONE)
			{
				const FName NewVariableName(Target.LegacyVariableNewName);
				FBlueprintEditorUtils::RenameMemberVariable(AnimBP, OldVariableName, NewVariableName);
				UE_LOG(
					LogShooterAnimBPReparent,
					Display,
					TEXT("AnimBP legacy variable renamed: Asset=%s Old=%s New=%s"),
					*ObjectPath,
					*OldVariableName.ToString(),
					*NewVariableName.ToString());
			}
		}

		if (AnimBP->ParentClass != NewParentClass)
		{
			UBlueprintEditorLibrary::ReparentBlueprint(AnimBP, NewParentClass);
			if (AnimBP->ParentClass != NewParentClass)
			{
				UE_LOG(
					LogShooterAnimBPReparent,
					Error,
					TEXT("AnimBP reparent did not take effect: %s"),
					*ObjectPath);
				++OutFailureCount;
				return false;
			}
		}

		FCompilerResultsLog CompilerResults;
		FKismetEditorUtilities::CompileBlueprint(
			AnimBP,
			EBlueprintCompileOptions::SkipGarbageCollection,
			&CompilerResults);
		// 迁移 Commandlet 中 BS_BeingCreated 可能是编译管理器正在重建生成类，
		// 只要编译器返回 0 Error 且不是 BS_Error，就允许保存；下一次加载会收敛状态。
		if (CompilerResults.NumErrors > 0 || AnimBP->Status == BS_Error)
		{
			UE_LOG(
				LogShooterAnimBPReparent,
				Error,
				TEXT("AnimBP compile failed: Asset=%s Errors=%d Status=%d"),
				*ObjectPath,
				CompilerResults.NumErrors,
				static_cast<int32>(AnimBP->Status));
			++OutFailureCount;
			return false;
		}
		UE_LOG(
			LogShooterAnimBPReparent,
			Display,
			TEXT("AnimBP compile accepted: Asset=%s Errors=%d Status=%d"),
			*ObjectPath,
			CompilerResults.NumErrors,
			static_cast<int32>(AnimBP->Status));

		UPackage* Package = AnimBP->GetOutermost();
		FString PackageFilename;
		if (!Package ||
			!FPackageName::TryConvertLongPackageNameToFilename(
				Package->GetName(),
				PackageFilename,
				FPackageName::GetAssetPackageExtension()))
		{
			UE_LOG(
				LogShooterAnimBPReparent,
				Error,
				TEXT("AnimBP save path resolution failed: %s"),
				*ObjectPath);
			++OutFailureCount;
			return false;
		}

		Package->MarkPackageDirty();
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const bool bSaved = UPackage::SavePackage(
			Package,
			AnimBP,
			*PackageFilename,
			SaveArgs);
		if (!bSaved)
		{
			UE_LOG(
				LogShooterAnimBPReparent,
				Error,
				TEXT("AnimBP save failed: Asset=%s File=%s"),
				*ObjectPath,
				*PackageFilename);
			++OutFailureCount;
			return false;
		}

		UE_LOG(
			LogShooterAnimBPReparent,
			Display,
			TEXT("AUTOMATION_ANIMBP_REPARENT_SUCCESS Asset=%s Parent=%s File=%s"),
			*ObjectPath,
			*AnimBP->ParentClass->GetPathName(),
			*PackageFilename);
		return true;
	}
}

UShooterAnimBPReparentCommandlet::UShooterAnimBPReparentCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UShooterAnimBPReparentCommandlet::Main(const FString& Params)
{
	using namespace ShooterAnimBPReparent;

	const FReparentTarget Targets[] = {
		{
			TEXT("/Game/Shooter/Animation/FirstPerson/ABP_FP_Weapon"),
			TEXT("ABP_FP_Weapon"),
			&GetFirstPersonParentClass,
		},
		{
			TEXT("/Game/Shooter/Animation/FirstPerson/ABP_FP_Pistol"),
			TEXT("ABP_FP_Pistol"),
			&GetFirstPersonParentClass,
		},
		{
			TEXT("/Game/Shooter/Animation/ThirdPerson/ABP_TP_Rifle"),
			TEXT("ABP_TP_Rifle"),
			&GetThirdPersonParentClass,
			TEXT("GroundSpeed"),
			TEXT("LegacyGroundSpeed"),
		},
		{
			TEXT("/Game/Shooter/Animation/ThirdPerson/ABP_TP_Pistol"),
			TEXT("ABP_TP_Pistol"),
			&GetThirdPersonParentClass,
		},
	};

	int32 FailureCount = 0;
	for (const FReparentTarget& Target : Targets)
	{
		ReparentCompileAndSave(Target, FailureCount);
	}

	UE_LOG(
		LogShooterAnimBPReparent,
		Display,
		TEXT("AUTOMATION_ANIMBP_REPARENT_SUMMARY Total=%d Failures=%d"),
		static_cast<int32>(UE_ARRAY_COUNT(Targets)),
		FailureCount);

	return FailureCount == 0 ? 0 : 1;
}
