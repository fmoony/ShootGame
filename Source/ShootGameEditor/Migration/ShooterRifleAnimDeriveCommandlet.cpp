// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShootGameEditor/Migration/ShooterRifleAnimDeriveCommandlet.h"

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetToolsModule.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogShooterRifleAnimDerive, Log, All);

namespace ShooterRifleAnimDerive
{
	const TCHAR* const SourceFolder = TEXT("/Game/Characters/Mannequins/Anims/Rifle");
	const TCHAR* const TargetFolder = TEXT("/Game/Shooter/Animation/ThirdPerson/Rifle");
	const TCHAR* const ExpectedSkeleton = TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin");

	/**
	 * 一份派生计划。
	 * KeptLastFrame 是保留的最后一个零基帧。原始资产满足
	 * NumberOfFrames == 最后一个零基帧，NumberOfKeys == NumberOfFrames + 1。
	 * MirrorSourceTarget 非空时表示镜像来源是同批已生成的目标。
	 */
	struct FDeriveSpec
	{
		const TCHAR* TargetName;
		const TCHAR* SourceName;
		int32 SourceNumberOfFrames;
		int32 KeptLastFrame;
		const TCHAR* MirrorSourceTarget;
	};

	const FDeriveSpec Specs[] = {
		{TEXT("MM_Rifle_LowReady"), TEXT("RifleIdle"), 257, 257, nullptr},
		{TEXT("MM_Rifle_AimToLowReady"), TEXT("RifleAimToDown"), 45, 25, nullptr},
		{TEXT("MM_Rifle_Holster"), TEXT("PutBackRifleBehindShoulder"), 119, 100, nullptr},
		{TEXT("MM_Rifle_Draw"), TEXT("MM_Rifle_Holster"), 100, 100, TEXT("MM_Rifle_Holster")},
	};

	const int32 SpecCount = UE_ARRAY_COUNT(Specs);

	/** 源包在磁盘上的可比较指纹，用于证明原始资产未被改写。FNV-1a 免依赖且确定。 */
	struct FFileFingerprint
	{
		int64 Size = -1;
		uint64 Checksum = 0;
		FDateTime TimeStamp;
		bool bValid = false;
	};

	FString PackageToFilename(const FString& PackageName)
	{
		const FString Extension = FPackageName::GetAssetPackageExtension();
		FString Filename;
		if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, Filename, Extension))
		{
			return FString();
		}
		return Filename;
	}

	bool FingerprintFile(const FString& PackageName, FFileFingerprint& OutFingerprint)
	{
		const FString Filename = PackageToFilename(PackageName);
		TArray<uint8> Bytes;
		if (Filename.IsEmpty() || !FFileHelper::LoadFileToArray(Bytes, *Filename))
		{
			return false;
		}
		uint64 Checksum = 1469598103934665603ULL;
		for (const uint8 Byte : Bytes)
		{
			Checksum ^= static_cast<uint64>(Byte);
			Checksum *= 1099511628211ULL;
		}
		OutFingerprint.Size = Bytes.Num();
		OutFingerprint.Checksum = Checksum;
		OutFingerprint.TimeStamp = IFileManager::Get().GetTimeStamp(*Filename);
		OutFingerprint.bValid = true;
		return true;
	}

	/** 数据模型侧的可读指纹：帧数、键数、时长、轨道数与全部轨道键数总和。 */
	FString DescribeModel(const UAnimSequence* Sequence)
	{
		const IAnimationDataModel* Model = Sequence->GetDataModel();
		if (!Model)
		{
			return TEXT("model=<null>");
		}
		TArray<FName> TrackNames;
		Model->GetBoneTrackNames(TrackNames);
		int32 KeyTotal = 0;
		for (const FName TrackName : TrackNames)
		{
			TArray<FTransform> Keys;
			Model->GetBoneTrackTransforms(TrackName, Keys);
			KeyTotal += Keys.Num();
		}
		const double PlayLength = Sequence->GetPlayLength();
		return FString::Printf(TEXT("Frames=%d Keys=%d Tracks=%d KeyTotal=%d Length=%.6f"),
			Model->GetNumberOfFrames(), Model->GetNumberOfKeys(), TrackNames.Num(), KeyTotal, PlayLength);
	}

	bool SaveAsset(UObject* Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		const FString Filename = PackageToFilename(Package->GetName());
		if (Filename.IsEmpty())
		{
			return false;
		}
		Package->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Package, Asset, *Filename, Args);
	}

	/**
	 * 原生截断：把序列帧数设为 KeptLastFrame，删除其后的帧区间。
	 * 引擎侧 T0/T1 由当前帧数与目标帧数之差推出，不做任何重采样。
	 */
	bool TruncateToKeptLastFrame(UAnimSequence* Sequence, int32 KeptLastFrame)
	{
		IAnimationDataController& Controller = Sequence->GetController();
		Controller.OpenBracket(NSLOCTEXT("ShooterRifleAnimDerive", "Truncate", "Rifle 派生裁切"), true);
		Controller.SetNumberOfFrames(FFrameNumber(KeptLastFrame), true);
		Controller.CloseBracket(true);
		return Sequence->GetDataModel()->GetNumberOfFrames() == KeptLastFrame;
	}

	/** 读取一条轨道在模型里实际存储的全部键。 */
	void ReadTrackKeys(const IAnimationDataModel* Model, FName TrackName, TArray<FTransform>& OutKeys)
	{
		OutKeys.Reset();
		Model->GetBoneTrackTransforms(TrackName, OutKeys);
	}

	/** 按时间镜像重写目标的全部骨骼轨道：Target[i] = Reference[N-1-i]。 */
	bool MirrorTracks(UAnimSequence* Target, const UAnimSequence* Reference)
	{
		const IAnimationDataModel* SourceModel = Reference->GetDataModel();
		TArray<FName> SourceTracks;
		SourceModel->GetBoneTrackNames(SourceTracks);
		TArray<FName> TargetTracks;
		Target->GetDataModel()->GetBoneTrackNames(TargetTracks);
		if (SourceTracks.Num() == 0 || SourceTracks.Num() != TargetTracks.Num())
		{
			UE_LOG(LogShooterRifleAnimDerive, Error, TEXT("RIFLE_DERIVE_MIRROR_TRACK_MISMATCH Source=%d Target=%d"),
				SourceTracks.Num(), TargetTracks.Num());
			return false;
		}
		IAnimationDataController& Controller = Target->GetController();
		Controller.OpenBracket(NSLOCTEXT("ShooterRifleAnimDerive", "Mirror", "Rifle 派生镜像"), true);
		int32 Mirrored = 0;
		int32 MinKeyCount = MAX_int32;
		int32 MaxKeyCount = 0;
		for (const FName TrackName : SourceTracks)
		{
			TArray<FTransform> SourceKeys;
			ReadTrackKeys(SourceModel, TrackName, SourceKeys);
			const int32 KeyCount = SourceKeys.Num();
			if (KeyCount == 0)
			{
				UE_LOG(LogShooterRifleAnimDerive, Error, TEXT("RIFLE_DERIVE_MIRROR_KEYS_INVALID Track=%s"), *TrackName.ToString());
				Controller.CloseBracket(true);
				return false;
			}
			MinKeyCount = FMath::Min(MinKeyCount, KeyCount);
			MaxKeyCount = FMath::Max(MaxKeyCount, KeyCount);
			TArray<FVector> Positions;
			TArray<FQuat> Rotations;
			TArray<FVector> Scales;
			Positions.SetNum(KeyCount);
			Rotations.SetNum(KeyCount);
			Scales.SetNum(KeyCount);
			for (int32 Index = 0; Index < KeyCount; ++Index)
			{
				const FTransform& Source = SourceKeys[KeyCount - 1 - Index];
				Positions[Index] = Source.GetTranslation();
				Rotations[Index] = Source.GetRotation();
				Scales[Index] = Source.GetScale3D();
			}
			if (!Controller.SetBoneTrackKeys(TrackName, Positions, Rotations, Scales, true))
			{
				UE_LOG(LogShooterRifleAnimDerive, Error, TEXT("RIFLE_DERIVE_MIRROR_SET_FAILED Track=%s"), *TrackName.ToString());
				Controller.CloseBracket(true);
				return false;
			}
			++Mirrored;
		}
		Controller.CloseBracket(true);
		UE_LOG(LogShooterRifleAnimDerive, Display, TEXT("RIFLE_DERIVE_MIRROR_DONE Tracks=%d Min=%d Max=%d"),
			Mirrored, MinKeyCount, MaxKeyCount);
		return Mirrored == SourceTracks.Num();
	}

	/** 读回镜像结果：逐轨比较 Target[i] 与 Reference[N-1-i]，返回不匹配的轨道数。 */
	int32 VerifyMirror(const UAnimSequence* Target, const UAnimSequence* Reference, int32& OutTrackCount)
	{
		const IAnimationDataModel* SourceModel = Reference->GetDataModel();
		const IAnimationDataModel* TargetModel = Target->GetDataModel();
		TArray<FName> SourceTracks;
		SourceModel->GetBoneTrackNames(SourceTracks);
		OutTrackCount = SourceTracks.Num();
		int32 Mismatches = 0;
		for (const FName TrackName : SourceTracks)
		{
			const FString TrackLabel = TrackName.ToString();
			TArray<FTransform> SourceKeys;
			ReadTrackKeys(SourceModel, TrackName, SourceKeys);
			TArray<FTransform> TargetKeys;
			ReadTrackKeys(TargetModel, TrackName, TargetKeys);
			const int32 KeyCount = SourceKeys.Num();
			if (TargetKeys.Num() != KeyCount)
			{
				++Mismatches;
				continue;
			}
			for (int32 Index = 0; Index < KeyCount; ++Index)
			{
				const FTransform& Source = SourceKeys[KeyCount - 1 - Index];
				const FTransform& Result = TargetKeys[Index];
				const bool bPosOk = Result.GetTranslation().Equals(Source.GetTranslation(), 0.01);
				const bool bScaleOk = Result.GetScale3D().Equals(Source.GetScale3D(), 0.001);
				const bool bRotOk = Result.GetRotation().Equals(Source.GetRotation(), 0.001);
				if (!bPosOk || !bScaleOk || !bRotOk)
				{
					UE_LOG(LogShooterRifleAnimDerive, Warning, TEXT("RIFLE_DERIVE_MIRROR_DIFF %s #%d"), *TrackLabel, Index);
					++Mismatches;
					break;
				}
			}
		}
		return Mismatches;
	}

	UAnimSequence* LoadSourceSequence(const TCHAR* SourceName)
	{
		const FString Path = FString::Printf(TEXT("%s/%s.%s"), SourceFolder, SourceName, SourceName);
		UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn);
		if (!Sequence || Sequence->IsValidAdditive())
		{
			return nullptr;
		}
		const USkeleton* Skeleton = Sequence->GetSkeleton();
		if (!Skeleton || Skeleton->GetPathName() != FString(ExpectedSkeleton))
		{
			return nullptr;
		}
		return Sequence;
	}
}

UShooterRifleAnimDeriveCommandlet::UShooterRifleAnimDeriveCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UShooterRifleAnimDeriveCommandlet::Main(const FString& Params)
{
	using namespace ShooterRifleAnimDerive;

	const bool bApply = FParse::Param(*Params, TEXT("Apply"));
	UE_LOG(LogShooterRifleAnimDerive, Display, TEXT("RIFLE_DERIVE_BEGIN Apply=%d Folder=%s Specs=%d"),
		bApply ? 1 : 0, TargetFolder, SpecCount);

	// 第一遍：校验源资产与目标冲突；任一失败都不创建或保存任何资产。
	TArray<UAnimSequence*> Sources;
	Sources.SetNumZeroed(SpecCount);
	TMap<FString, FFileFingerprint> Before;
	for (int32 Index = 0; Index < SpecCount; ++Index)
	{
		const FDeriveSpec& Spec = Specs[Index];
		if (Spec.MirrorSourceTarget)
		{
			continue;
		}
		UAnimSequence* Source = LoadSourceSequence(Spec.SourceName);
		if (!Source)
		{
			UE_LOG(LogShooterRifleAnimDerive, Error, TEXT("RIFLE_DERIVE_SOURCE_INVALID Name=%s"), Spec.SourceName);
			return 1;
		}
		const int32 SourceFrames = Source->GetDataModel()->GetNumberOfFrames();
		if (SourceFrames != Spec.SourceNumberOfFrames)
		{
			UE_LOG(LogShooterRifleAnimDerive, Error, TEXT("RIFLE_DERIVE_SOURCE_BAD_FRAMES Name=%s Want=%d Got=%d"),
				Spec.SourceName, Spec.SourceNumberOfFrames, SourceFrames);
			return 1;
		}
		const FString SourcePackage = FString::Printf(TEXT("%s/%s"), SourceFolder, Spec.SourceName);
		FFileFingerprint Fingerprint;
		if (!FingerprintFile(SourcePackage, Fingerprint))
		{
			UE_LOG(LogShooterRifleAnimDerive, Error, TEXT("RIFLE_DERIVE_SOURCE_READ_FAILED Package=%s"), *SourcePackage);
			return 1;
		}
		Before.Add(SourcePackage, Fingerprint);
		Sources[Index] = Source;
		UE_LOG(LogShooterRifleAnimDerive, Display, TEXT("RIFLE_DERIVE_SOURCE Name=%s Package=%s Bytes=%lld Fnv1a=%llu"),
			Spec.SourceName, *SourcePackage, Fingerprint.Size, Fingerprint.Checksum);
		UE_LOG(LogShooterRifleAnimDerive, Display, TEXT("RIFLE_DERIVE_SOURCE_MODEL Name=%s %s"), Spec.SourceName, *DescribeModel(Source));
	}

	for (int32 Index = 0; Index < SpecCount; ++Index)
	{
		const FDeriveSpec& Spec = Specs[Index];
		const FString TargetPackage = FString::Printf(TEXT("%s/%s"), TargetFolder, Spec.TargetName);
		if (FPackageName::DoesPackageExist(TargetPackage))
		{
			UE_LOG(LogShooterRifleAnimDerive, Error, TEXT("RIFLE_DERIVE_TARGET_EXISTS Refusing=1 Package=%s"), *TargetPackage);
			return 1;
		}
		const TCHAR* const MirrorName = Spec.MirrorSourceTarget ? Spec.MirrorSourceTarget : TEXT("<none>");
		UE_LOG(LogShooterRifleAnimDerive, Display, TEXT("RIFLE_DERIVE_PLAN %s <- %s Last=%d Keys=%d"),
			Spec.TargetName, Spec.SourceName, Spec.KeptLastFrame, Spec.KeptLastFrame + 1);
		UE_LOG(LogShooterRifleAnimDerive, Display, TEXT("RIFLE_DERIVE_PLAN_MIRROR %s <- %s"), Spec.TargetName, MirrorName);
	}

	if (!bApply)
	{
		UE_LOG(LogShooterRifleAnimDerive, Display, TEXT("RIFLE_DERIVE_DRYRUN_OK Apply=0 Targets=%d"), SpecCount);
		return 0;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	TMap<FString, UAnimSequence*> Created;
	for (int32 Index = 0; Index < SpecCount; ++Index)
	{
		const FDeriveSpec& Spec = Specs[Index];
		UAnimSequence* CloneSource = Spec.MirrorSourceTarget
			? Created.FindRef(FString(Spec.MirrorSourceTarget))
			: Sources[Index];
		if (!CloneSource)
		{
			UE_LOG(LogShooterRifleAnimDerive, Error, TEXT("RIFLE_DERIVE_CLONE_SOURCE_MISSING Target=%s"), Spec.TargetName);
			return 1;
		}
		UAnimSequence* Target = Cast<UAnimSequence>(
			AssetTools.DuplicateAsset(Spec.TargetName, TargetFolder, CloneSource));
		if (!Target)
		{
			UE_LOG(LogShooterRifleAnimDerive, Error, TEXT("RIFLE_DERIVE_DUPLICATE_FAILED Target=%s"), Spec.TargetName);
			return 1;
		}
		// 复制的数据模型必须是独立对象：与来源共享模型会让裁切直接改写原始资产。
		if (Target->GetDataModel() == CloneSource->GetDataModel())
		{
			UE_LOG(LogShooterRifleAnimDerive, Error, TEXT("RIFLE_DERIVE_MODEL_SHARED Target=%s"), Spec.TargetName);
			return 1;
		}
		// 复制的数据模型必须可独立编辑：先确认键数与来源一致，再做裁切。
		if (Target->GetDataModel()->GetNumberOfKeys() != CloneSource->GetDataModel()->GetNumberOfKeys())
		{
			UE_LOG(LogShooterRifleAnimDerive, Error, TEXT("RIFLE_DERIVE_MODEL_COPY_MISMATCH Target=%s"), Spec.TargetName);
			return 1;
		}
		if (!TruncateToKeptLastFrame(Target, Spec.KeptLastFrame))
		{
			UE_LOG(LogShooterRifleAnimDerive, Error, TEXT("RIFLE_DERIVE_TRUNCATE_FAILED Target=%s Frame=%d"),
				Spec.TargetName, Spec.KeptLastFrame);
			return 1;
		}
		if (Spec.MirrorSourceTarget && !MirrorTracks(Target, CloneSource))
		{
			UE_LOG(LogShooterRifleAnimDerive, Error, TEXT("RIFLE_DERIVE_MIRROR_FAILED Target=%s"), Spec.TargetName);
			return 1;
		}
		if (!SaveAsset(Target))
		{
			UE_LOG(LogShooterRifleAnimDerive, Error, TEXT("RIFLE_DERIVE_SAVE_FAILED Target=%s"), Spec.TargetName);
			return 1;
		}
		Created.Add(Spec.TargetName, Target);
		UE_LOG(LogShooterRifleAnimDerive, Display, TEXT("RIFLE_DERIVE_TARGET Name=%s Saved=1 %s"), Spec.TargetName, *DescribeModel(Target));
	}

	// 镜像读回：Draw 必须逐键等于 Holster 的时间反转。
	UAnimSequence* Holster = Created.FindRef(TEXT("MM_Rifle_Holster"));
	UAnimSequence* Draw = Created.FindRef(TEXT("MM_Rifle_Draw"));
	int32 TrackCount = 0;
	const int32 MirrorMismatches = (Holster && Draw) ? VerifyMirror(Draw, Holster, TrackCount) : -1;
	UE_LOG(LogShooterRifleAnimDerive, Display, TEXT("RIFLE_DERIVE_MIRROR_VERIFY Tracks=%d Mismatches=%d"), TrackCount, MirrorMismatches);
	if (MirrorMismatches != 0)
	{
		UE_LOG(LogShooterRifleAnimDerive, Error, TEXT("RIFLE_DERIVE_MIRROR_VERIFY_FAILED"));
		return 1;
	}

	// 源资产只读证明：包文件大小、校验和与时间戳都必须与运行前一致。
	int32 UnchangedCount = 0;
	for (int32 Index = 0; Index < SpecCount; ++Index)
	{
		const FDeriveSpec& Spec = Specs[Index];
		if (Spec.MirrorSourceTarget)
		{
			continue;
		}
		const FString Package = FString::Printf(TEXT("%s/%s"), SourceFolder, Spec.SourceName);
		FFileFingerprint After;
		FingerprintFile(Package, After);
		const FFileFingerprint& Pre = Before.FindRef(Package);
		const bool bSame = Pre.bValid && After.bValid && Pre.Size == After.Size &&
			Pre.Checksum == After.Checksum && Pre.TimeStamp == After.TimeStamp;
		UE_LOG(LogShooterRifleAnimDerive, Display, TEXT("RIFLE_DERIVE_SOURCE_CHECK %s Bytes=%lld/%lld Fnv=%llu/%llu"),
			*Package, Pre.Size, After.Size, Pre.Checksum, After.Checksum);
		UE_LOG(LogShooterRifleAnimDerive, Display, TEXT("RIFLE_DERIVE_SOURCE_CHECK %s Unchanged=%d"), *Package, bSame ? 1 : 0);
		if (!bSame)
		{
			UE_LOG(LogShooterRifleAnimDerive, Error, TEXT("RIFLE_DERIVE_SOURCE_MODIFIED Package=%s"), *Package);
			return 1;
		}
		++UnchangedCount;
	}

	UE_LOG(LogShooterRifleAnimDerive, Display, TEXT("RIFLE_DERIVE_SUCCESS Targets=%d SourcesUnchanged=%d"),
		Created.Num(), UnchangedCount);
	return 0;
}
