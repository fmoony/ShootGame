// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/ShooterCharacter.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/Animation/ShooterAnimInstanceBase.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Weapons/ShooterWeapon.h"
#include "../Equipment/ShooterWeaponPresentationTestTypes.h"
#include "ShootGame.h"
#include "UObject/UnrealType.h"

namespace ShooterFirstPersonCapture
{
	// 仅显式测试启动参数允许此命令：会替换独立测试角色的背包、控制视角并触发换弹。
	FAutoConsoleCommandWithWorldAndArgs CaptureCommand(
		TEXT("ShootGame.FirstPerson.Capture"),
		TEXT("Standalone only, requires -ShootGameFirstPersonCapture. Args: output folder label [exercise]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!FParse::Param(FCommandLine::Get(), TEXT("ShootGameFirstPersonCapture")) ||
				!World || World->GetNetMode() != NM_Standalone)
			{
				return;
			}
			struct FCaptureState
			{
				TWeakObjectPtr<UWorld> World;
				FString Folder;
				FString CSV = TEXT("weapon,pitch,time,reloading,near_clip,left_depth,right_depth,muzzle_depth,view_pitch,ammo,left_upper_ratio,right_upper_ratio,left_forearm_ratio,right_forearm_ratio\n");
				int32 Case = -1;
				int32 Frame = 0;
				float Time = 0.0f;
				float NextSample = 0.0f;
				bool bReloadSent = false;
				bool bExercise = false;
				bool bPistolReview = false;
				bool bFireSent = false;
				bool bStopSent = false;
				FGuid WeaponId;
			};
			TSharedRef<FCaptureState> State = MakeShared<FCaptureState>();
			State->World = World;
			State->bExercise = Args.Num() > 1 && Args[1] == TEXT("exercise");
			State->bPistolReview = Args.Num() > 1 && Args[1] == TEXT("pistolreview");
			State->Folder = FPaths::ProjectSavedDir() / TEXT("Automation/FirstPersonCapture") /
				(Args.IsEmpty() ? TEXT("Current") : FPaths::MakeValidFileName(Args[0]));
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float DeltaTime)
			{
				UWorld* TestWorld = State->World.Get();
				if (!TestWorld)
				{
					return false;
				}
				APlayerController* PC = TestWorld->GetFirstPlayerController();
				AShooterCharacter* Character = PC ? Cast<AShooterCharacter>(PC->GetPawn()) : nullptr;
				if (!Character || !Character->IsLocallyControlled())
				{
					return true;
				}
				if (Character->IsDead())
				{
					UE_LOG(LogShootGame, Error, TEXT("FIRST_PERSON_CAPTURE_FAILED: test pawn died"));
					PC->ConsoleCommand(TEXT("quit"));
					return false;
				}
				if (State->Case < 0)
				{
					// 仅清理本次独立测试世界的 AI，避免自动射击把换弹样本变成死亡相机。
					for (TActorIterator<APawn> It(TestWorld); It; ++It)
					{
						if (!It->IsPlayerControlled())
						{
							It->Destroy();
						}
					}
				}
				const TCHAR* Weapons[] = { TEXT("Pistol"), TEXT("Rifle"), TEXT("AWP"), TEXT("GrenadeLauncher") };
				const TArray<float> Pitches = State->bPistolReview
					? TArray<float>{ 0.0f, 30.0f, 40.0f, 50.0f, 60.0f, 70.0f, 80.0f, -20.0f, -45.0f, -70.0f }
					: TArray<float>{ 0.0f, 45.0f, 65.0f, 80.0f, -20.0f, -45.0f, -70.0f };
				const int32 PitchCount = Pitches.Num();
				// 使用游戏动画时钟，避免首次加载或渲染卡顿的墙钟间隔跳过整个换弹用例。
				State->Time += TestWorld->GetDeltaSeconds();
				if (State->Case < 0 || State->Time >= 5.0f)
				{
					++State->Case;
					if (State->Case >= (State->bExercise ? UE_ARRAY_COUNT(Weapons) : (State->bPistolReview ? 1 : UE_ARRAY_COUNT(Weapons)) * PitchCount))
					{
						FFileHelper::SaveStringToFile(State->CSV, *(State->Folder / TEXT("Depth.csv")));
						UE_LOG(LogShootGame, Display, TEXT("FIRST_PERSON_CAPTURE_COMPLETE %s"), *State->Folder);
						PC->ConsoleCommand(TEXT("quit"));
						return false;
					}
					State->Time = 0.0f;
					State->Frame = 0;
					State->NextSample = 0.75f;
					State->bReloadSent = false;
					State->bFireSent = false;
					State->bStopSent = false;
					const TCHAR* WeaponRowName = Weapons[State->bExercise ? State->Case : State->Case / PitchCount];
					UShooterInventoryComponent* CaptureInventory = Character->GetInventoryComponent();
					CaptureInventory->ClearInventory();
					// 单表纠偏后授予只接受武器模板行名：Capture 直接使用 DT_WeaponData 的正式行，
					// 与生产路径读取完全相同的配置（含网格、AnimClass 与构图下沉量）。
					if (CaptureInventory->TryAddWeaponRow(FName(WeaponRowName), State->WeaponId)
						!= EShooterInventoryAddResult::Added)
					{
						UE_LOG(
							LogShootGame,
							Warning,
							TEXT("FIRST_PERSON_CAPTURE_GRANT_REJECTED Row=%s"),
							WeaponRowName);
					}
					Character->GetEquipmentComponent()->EquipWeapon(State->WeaponId);
					CaptureInventory->ConsumeMagazineAmmo(State->WeaponId, 1);
					AShooterWeapon* EquippedWeapon = Character->GetCurrentWeapon();
					float DropOverride = -1.0f;
					if (EquippedWeapon && FParse::Value(FCommandLine::Get(), TEXT("ShootGameCaptureDrop="), DropOverride) && DropOverride >= 0.0f)
					{
						// 只改本独立测试世界的 Actor 实例；不改 CDO、资产或用户编辑器。
						if (FFloatProperty* Property = FindFProperty<FFloatProperty>(EquippedWeapon->GetClass(), TEXT("FirstPersonCompositionDrop")))
						{
							Property->SetPropertyValue_InContainer(EquippedWeapon, DropOverride);
						}
					}
					if (EquippedWeapon)
					{
						UE_LOG(LogShootGame, Display, TEXT("FIRST_PERSON_REVIEW Weapon=%s Drop=%.2f HasSupportGrip=%d"),
							*EquippedWeapon->GetClass()->GetName(), EquippedWeapon->GetFirstPersonCompositionDrop(), EquippedWeapon->HasThirdPersonLeftHandGripSocket());
					}
				}
				const int32 WeaponIndex = State->bExercise ? State->Case : State->Case / PitchCount;
				const int32 PitchIndex = State->bExercise ? 0 : State->Case % PitchCount;
				const float ViewPitch = State->bExercise
					? FMath::Clamp(80.0f * FMath::Sin(State->Time * PI), -70.0f, 80.0f) : Pitches[PitchIndex];
				PC->SetControlRotation(FRotator(ViewPitch, 0.0f, 0.0f));
				if (State->bExercise && !State->bFireSent && State->Time >= 1.0f)
				{
					Character->DoStartFiring();
					State->bFireSent = true;
				}
				if (State->bExercise && !State->bStopSent && State->Time >= 1.4f)
				{
					Character->DoStopFiring();
					State->bStopSent = true;
				}
				if (!State->bReloadSent && State->Time >= (State->bExercise ? 2.0f : 1.0f))
				{
					Character->DoReload();
					State->bReloadSent = true;
				}
				if (State->Time >= State->NextSample)
				{
					State->NextSample += State->bExercise ? 0.1f : 0.2f;
					FMinimalViewInfo View;
					Character->CalcCamera(DeltaTime, View);
					const FVector Forward = View.Rotation.Vector();
					auto Depth = [&View, &Forward](const FVector& Point)
					{
						return FVector::DotProduct(Point - View.Location, Forward) * View.FirstPersonScale;
					};
					USkeletalMeshComponent* Mesh = Character->GetFirstPersonMesh();
					// 使用实际世界关节间距对照参考骨长，捕捉父子骨骼平移造成的拉伸。
					auto BoneLengthRatio = [Mesh](FName Parent, FName Child)
					{
						const FVector ReferenceOffset = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton()
							.GetRefBonePose()[Mesh->GetBoneIndex(Child)].GetTranslation();
						const double ReferenceLength = Mesh->GetComponentTransform().TransformVector(ReferenceOffset).Size();
						return FVector::Distance(Mesh->GetSocketLocation(Parent), Mesh->GetSocketLocation(Child)) / ReferenceLength;
					};
					AShooterWeapon* Weapon = Character->GetCurrentWeapon();
					const UShooterAnimInstanceBase* Anim = Cast<UShooterAnimInstanceBase>(Mesh->GetAnimInstance());
					State->CSV += FString::Printf(TEXT("%s,%.0f,%.3f,%d,%.2f,%.3f,%.3f,%.3f,%.3f,%d,%.5f,%.5f,%.5f,%.5f\n"),
						Weapons[WeaponIndex], Pitches[PitchIndex], State->Time,
						Anim && Anim->bIsReloading, View.GetFinalPerspectiveNearClipPlane(),
						Depth(Mesh->GetSocketLocation(TEXT("hand_l"))), Depth(Mesh->GetSocketLocation(TEXT("hand_r"))),
						Weapon ? Depth(Weapon->GetFirstPersonMesh()->GetSocketLocation(Weapon->GetMuzzleSocketName())) : 0.0,
						View.Rotation.Pitch, Character->GetInventoryComponent()->GetMagazineAmmo(State->WeaponId),
						BoneLengthRatio(TEXT("upperarm_l"), TEXT("lowerarm_l")),
						BoneLengthRatio(TEXT("upperarm_r"), TEXT("lowerarm_r")),
						BoneLengthRatio(TEXT("lowerarm_l"), TEXT("hand_l")),
						BoneLengthRatio(TEXT("lowerarm_r"), TEXT("hand_r")));
					const FString Filename = FString::Printf(TEXT("%s_%02d_%02d.png"),
						Weapons[WeaponIndex], PitchIndex, State->Frame++);
					FScreenshotRequest::RequestScreenshot(State->Folder / Filename, false, false);
				}
				return true;
			}));
		}));
}

#endif
