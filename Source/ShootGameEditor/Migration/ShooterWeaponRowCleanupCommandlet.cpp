// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShootGameEditor/Migration/ShooterWeaponRowCleanupCommandlet.h"

#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "Weapons/Data/ShooterWeaponConfigRow.h"

DEFINE_LOG_CATEGORY_STATIC(LogShooterWeaponRowCleanup, Log, All);

namespace ShooterWeaponRowCleanup
{
	const TCHAR* const TableObjectPath = TEXT("/Game/Shooter/Data/DT_WeaponData.DT_WeaponData");

	/**
	 * 默认允许从基线中消失的列（历史调用未传 -Removed 时的行为保持不变）。
	 * 需要移除别的列时用 -Removed=<逗号列表> 显式声明，对照只放行列表内的列。
	 */
	const TCHAR* const DefaultRemovedPropertyName = TEXT("ThirdPersonAnimInstanceClass");

	/** 迁移后不该再出现在 DT_WeaponData 里的资源前缀。 */
	const TCHAR* const ForbiddenFragments[] = {TEXT("ABP_TP_")};

	/** 导出文本可能带引号或括号，去掉换行与制表符，保证一列一行、可逐行比较。 */
	FString Sanitize(const FString& In)
	{
		FString Out = In;
		Out.ReplaceInline(TEXT("\r\n"), TEXT("\\n"));
		Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Out.ReplaceInline(TEXT("\t"), TEXT("\\t"));
		return Out;
	}

	/** 反射导出：STRUCT 行 + 排序后的 PROP 行 + 排序后的 ROW 行，格式稳定可 diff。 */
	bool DumpTable(const UDataTable* Table, FString& OutText)
	{
		if (!Table)
		{
			return false;
		}
		const UScriptStruct* Struct = Table->GetRowStruct();
		if (!Struct)
		{
			return false;
		}

		TMap<FString, const FProperty*> Properties;
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			Properties.Add(It->GetName(), *It);
		}
		TArray<FString> PropertyNames;
		Properties.GetKeys(PropertyNames);
		PropertyNames.Sort();

		TArray<FString> Lines;
		Lines.Add(FString::Printf(TEXT("STRUCT\t%s"), *Struct->GetStructPathName().ToString()));
		for (const FString& PropertyName : PropertyNames)
		{
			Lines.Add(FString::Printf(TEXT("PROP\t%s"), *PropertyName));
		}

		const TMap<FName, uint8*>& RowMap = Table->GetRowMap();
		TArray<FName> RowNames;
		RowMap.GetKeys(RowNames);
		RowNames.Sort([](const FName& Left, const FName& Right)
		{
			return Left.LexicalLess(Right);
		});

		for (const FName& RowName : RowNames)
		{
			const uint8* RowData = RowMap.FindRef(RowName);
			if (!RowData)
			{
				continue;
			}
			uint8* MutableRow = const_cast<uint8*>(RowData);
			for (const FString& PropertyName : PropertyNames)
			{
				const FProperty* Property = Properties.FindRef(PropertyName);
				FString Value;
				Property->ExportTextItem_Direct(Value, Property->ContainerPtrToValuePtr<void>(MutableRow),
					nullptr, nullptr, PPF_None);
				Lines.Add(FString::Printf(TEXT("ROW\t%s\t%s\t%s"), *RowName.ToString(), *PropertyName, *Sanitize(Value)));
			}
		}

		OutText = FString::Join(Lines, TEXT("\n")) + TEXT("\n");
		return true;
	}

	struct FParsedDump
	{
		FString StructName;
		TSet<FString> Properties;
		TMap<FString, FString> Values;
		TSet<FString> RowNames;
	};

	bool ParseDump(const FString& Text, FParsedDump& Out)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, true);
		for (const FString& Line : Lines)
		{
			TArray<FString> Parts;
			Line.ParseIntoArray(Parts, TEXT("\t"), false);
			if (Parts.Num() == 0)
			{
				continue;
			}
			if (Parts[0] == TEXT("STRUCT") && Parts.Num() >= 2)
			{
				Out.StructName = Parts[1];
			}
			else if (Parts[0] == TEXT("PROP") && Parts.Num() >= 2)
			{
				Out.Properties.Add(Parts[1]);
			}
			else if (Parts[0] == TEXT("ROW") && Parts.Num() >= 4)
			{
				Out.RowNames.Add(Parts[1]);
				Out.Values.Add(Parts[1] + TEXT("\t") + Parts[2], Parts[3]);
			}
		}
		return Out.Properties.Num() > 0 && Out.Values.Num() > 0;
	}

	/** 逐行逐列对照；只有 ExpectedRemoved 内的列允许消失，其他任何差异都算错误。 */
	bool CompareDumps(const FParsedDump& Baseline, const FParsedDump& Current,
		const TSet<FString>& ExpectedRemoved, int32& OutErrors)
	{
		int32 Errors = 0;

		if (Baseline.StructName != Current.StructName)
		{
			UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("CLEANUP_STRUCT_CHANGED Baseline=%s Current=%s"),
				*Baseline.StructName, *Current.StructName);
			++Errors;
		}

		for (const FString& PropertyName : Baseline.Properties)
		{
			if (!Current.Properties.Contains(PropertyName))
			{
				const bool bExpected = ExpectedRemoved.Contains(PropertyName);
				UE_LOG(LogShooterWeaponRowCleanup, Display, TEXT("CLEANUP_PROPERTY_ABSENT %s Expected=%d"),
					*PropertyName, bExpected ? 1 : 0);
				if (!bExpected)
				{
					++Errors;
				}
			}
		}

		for (const FString& PropertyName : Current.Properties)
		{
			if (!Baseline.Properties.Contains(PropertyName))
			{
				UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("CLEANUP_PROPERTY_ADDED %s"), *PropertyName);
				++Errors;
			}
		}

		for (const FString& RowName : Baseline.RowNames)
		{
			if (!Current.RowNames.Contains(RowName))
			{
				UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("CLEANUP_ROW_MISSING %s"), *RowName);
				++Errors;
			}
		}
		for (const FString& RowName : Current.RowNames)
		{
			if (!Baseline.RowNames.Contains(RowName))
			{
				UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("CLEANUP_ROW_ADDED %s"), *RowName);
				++Errors;
			}
		}

		int32 ComparedValues = 0;
		for (const TPair<FString, FString>& Pair : Current.Values)
		{
			const FString* Expected = Baseline.Values.Find(Pair.Key);
			if (!Expected)
			{
				UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("CLEANUP_VALUE_UNEXPECTED %s = %s"),
					*Pair.Key.Replace(TEXT("\t"), TEXT(".")), *Pair.Value);
				++Errors;
			}
			else if (*Expected != Pair.Value)
			{
				UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("CLEANUP_VALUE_CHANGED %s Baseline=%s Current=%s"),
					*Pair.Key.Replace(TEXT("\t"), TEXT(".")), **Expected, *Pair.Value);
				++Errors;
			}
			else
			{
				++ComparedValues;
			}
		}

		for (const TPair<FString, FString>& Pair : Baseline.Values)
		{
			if (Current.Values.Contains(Pair.Key))
			{
				continue;
			}
			FString RowName;
			FString PropertyName;
			Pair.Key.Split(TEXT("\t"), &RowName, &PropertyName);
			if (!ExpectedRemoved.Contains(PropertyName))
			{
				UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("CLEANUP_VALUE_LOST %s = %s"),
					*Pair.Key.Replace(TEXT("\t"), TEXT(".")), *Pair.Value);
				++Errors;
			}
		}

		const FString Summary = FString::Printf(TEXT("Rows=%d Properties=%d ComparedValues=%d Errors=%d"),
			Current.RowNames.Num(), Current.Properties.Num(), ComparedValues, Errors);
		UE_LOG(LogShooterWeaponRowCleanup, Display, TEXT("CLEANUP_COMPARE %s"), *Summary);
		OutErrors = Errors;
		return Errors == 0;
	}

	bool WriteTextFile(const FString& Filename, const FString& Text)
	{
		return FFileHelper::SaveStringToFile(Text, *Filename);
	}

	bool ReadTextFile(const FString& Filename, FString& OutText)
	{
		return FFileHelper::LoadFileToString(OutText, *Filename);
	}

	/** 在已保存的资产字节里搜索不应再出现的 ASCII 片段。 */
	void ScanSavedFile(const FString& Filename, int32& OutHits)
	{
		OutHits = 0;
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Filename))
		{
			UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("CLEANUP_SCAN_READ_FAILED %s"), *Filename);
			++OutHits;
			return;
		}

		for (const TCHAR* FragmentText : ForbiddenFragments)
		{
			const FString Fragment(FragmentText);
			const FTCHARToUTF8 Encoded(*Fragment);
			const int32 Length = Encoded.Length();
			bool bFound = false;
			for (int32 Index = 0; Index + Length <= Bytes.Num() && !bFound; ++Index)
			{
				bFound = FMemory::Memcmp(Bytes.GetData() + Index, Encoded.Get(), Length) == 0;
			}
			if (bFound)
			{
				UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("CLEANUP_FILE_FRAGMENT_PRESENT %s"), *Fragment);
				++OutHits;
			}
			else
			{
				UE_LOG(LogShooterWeaponRowCleanup, Display, TEXT("CLEANUP_FILE_FRAGMENT_ABSENT %s"), *Fragment);
			}
		}
	}

	bool SaveAsset(UObject* Asset)
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

	/** 打印关键列，作为“四把武器资源映射不变”的直接证据。 */
	void LogKeyColumns(const FParsedDump& Dump)
	{
		const TCHAR* KeyProperties[] = {
			TEXT("ThirdPersonHoldSequence"),
			TEXT("ThirdPersonAimOffset"),
			TEXT("ThirdPersonReloadSequence"),
			TEXT("ThirdPersonMinimumAimTargetDistanceFromMuzzle"),
			TEXT("MagazineSize"),
			TEXT("ReloadDuration"),
			TEXT("EquipDuration"),
		};
		TArray<FString> RowNames = Dump.RowNames.Array();
		RowNames.Sort();
		for (const FString& RowName : RowNames)
		{
			for (const TCHAR* PropertyName : KeyProperties)
			{
				const FString* Value = Dump.Values.Find(RowName + TEXT("\t") + PropertyName);
				UE_LOG(LogShooterWeaponRowCleanup, Display, TEXT("CLEANUP_KEY %s %s = %s"),
					*RowName, PropertyName, Value ? **Value : TEXT("<absent>"));
			}
		}
	}
}

UShooterWeaponRowCleanupCommandlet::UShooterWeaponRowCleanupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UShooterWeaponRowCleanupCommandlet::Main(const FString& Params)
{
	using namespace ShooterWeaponRowCleanup;

	const bool bDump = FParse::Param(*Params, TEXT("Dump"));
	const bool bApply = FParse::Param(*Params, TEXT("Apply"));
	const bool bVerify = FParse::Param(*Params, TEXT("Verify"));
	if (bDump == bApply && bApply == bVerify)
	{
		UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("Specify exactly one of -Dump / -Apply / -Verify."));
		return 1;
	}

	FString OutPath;
	FParse::Value(*Params, TEXT("Out="), OutPath);
	if (OutPath.IsEmpty())
	{
		UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("Missing -Out=<file>."));
		return 1;
	}

	UDataTable* Table = LoadObject<UDataTable>(nullptr, TableObjectPath);
	if (!Table || Table->GetRowStruct() != FShooterWeaponConfigRow::StaticStruct())
	{
		UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("DT_WeaponData 与 FShooterWeaponConfigRow 不匹配。"));
		return 1;
	}

	FString CurrentText;
	if (!DumpTable(Table, CurrentText))
	{
		UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("导出当前表数据失败。"));
		return 1;
	}
	FParsedDump Current;
	if (!ParseDump(CurrentText, Current))
	{
		UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("解析当前表数据失败。"));
		return 1;
	}
	UE_LOG(LogShooterWeaponRowCleanup, Display, TEXT("CLEANUP_MODE Dump=%d Apply=%d Verify=%d"),
		bDump ? 1 : 0, bApply ? 1 : 0, bVerify ? 1 : 0);
	UE_LOG(LogShooterWeaponRowCleanup, Display, TEXT("CLEANUP_STRUCT %s"), *Current.StructName);
	UE_LOG(LogShooterWeaponRowCleanup, Display, TEXT("CLEANUP_TABLE Rows=%d Properties=%d Values=%d"),
		Current.RowNames.Num(), Current.Properties.Num(), Current.Values.Num());
	LogKeyColumns(Current);

	if (!WriteTextFile(OutPath, CurrentText))
	{
		UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("写入 %s 失败。"), *OutPath);
		return 1;
	}

	if (bDump)
	{
		UE_LOG(LogShooterWeaponRowCleanup, Display, TEXT("CLEANUP_DUMP_WRITTEN %s"), *OutPath);
		return 0;
	}

	FString BaselinePath;
	FParse::Value(*Params, TEXT("Baseline="), BaselinePath);
	if (BaselinePath.IsEmpty())
	{
		UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("Missing -Baseline=<file>."));
		return 1;
	}
	FString BaselineText;
	if (!ReadTextFile(BaselinePath, BaselineText))
	{
		UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("读取基线 %s 失败。"), *BaselinePath);
		return 1;
	}
	FParsedDump Baseline;
	if (!ParseDump(BaselineText, Baseline))
	{
		UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("解析基线 %s 失败。"), *BaselinePath);
		return 1;
	}

	// -Removed=<逗号列表> 显式声明本轮允许从基线消失的列；缺省保持历史单列行为。
	FString RemovedList;
	FParse::Value(*Params, TEXT("Removed="), RemovedList);
	TSet<FString> ExpectedRemoved;
	if (RemovedList.IsEmpty())
	{
		ExpectedRemoved.Add(DefaultRemovedPropertyName);
	}
	else
	{
		TArray<FString> RemovedNames;
		RemovedList.ParseIntoArray(RemovedNames, TEXT(","), true);
		for (const FString& RemovedName : RemovedNames)
		{
			ExpectedRemoved.Add(RemovedName);
		}
	}
	UE_LOG(LogShooterWeaponRowCleanup, Display, TEXT("CLEANUP_EXPECTED_REMOVED %s"),
		*FString::Join(ExpectedRemoved.Array(), TEXT(",")));

	int32 Errors = 0;
	if (!CompareDumps(Baseline, Current, ExpectedRemoved, Errors))
	{
		UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("对照失败，不保存任何资产。Errors=%d"), Errors);
		return 1;
	}

	if (bVerify)
	{
		UE_LOG(LogShooterWeaponRowCleanup, Display, TEXT("CLEANUP_VERIFY_SUCCESS %s"), *OutPath);
		return 0;
	}

	FString Filename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(Table->GetOutermost()->GetName(), Filename,
		FPackageName::GetAssetPackageExtension()))
	{
		UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("解析 DT_WeaponData 磁盘路径失败。"));
		return 1;
	}
	if (!SaveAsset(Table))
	{
		UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("保存 DT_WeaponData 失败。"));
		return 1;
	}
	UE_LOG(LogShooterWeaponRowCleanup, Display, TEXT("CLEANUP_SAVED %s"), *Filename);

	int32 Hits = 0;
	ScanSavedFile(Filename, Hits);
	if (Hits > 0)
	{
		UE_LOG(LogShooterWeaponRowCleanup, Error, TEXT("保存后仍存在废弃引用。Hits=%d"), Hits);
		return 1;
	}

	UE_LOG(LogShooterWeaponRowCleanup, Display, TEXT("CLEANUP_APPLY_SUCCESS %s"), *OutPath);
	return 0;
}
