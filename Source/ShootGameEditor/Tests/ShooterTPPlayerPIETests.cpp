// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimNode_StateMachine.h"
#include "Animation/AnimSequence.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Characters/Animation/ShooterAnimNotify_Footstep.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "Misc/AutomationTest.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationEditorCommon.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/Subsystems/ShooterWeaponRuntimeSubsystem.h"

namespace ShooterTPPlayerPIE
{
	struct FSettingsSnapshot
	{
		EPlayNetMode Mode = PIE_Standalone;
		int32 Clients = 1;
		bool bOneProcess = true;
	};

	class FRestoreSettings final : public IAutomationLatentCommand
	{
	public:
		explicit FRestoreSettings(FSettingsSnapshot InSettings) : Settings(InSettings) {}
		virtual bool Update() override
		{
			if (GEditor->PlayWorld)
			{
				return false;
			}
			ULevelEditorPlaySettings* Play = GetMutableDefault<ULevelEditorPlaySettings>();
			Play->SetPlayNetMode(Settings.Mode);
			Play->SetPlayNumberOfClients(Settings.Clients);
			Play->SetRunUnderOneProcess(Settings.bOneProcess);
			return true;
		}
	private:
		FSettingsSnapshot Settings;
	};

	/** 真实 PIE 中观察固定实例、节点时间和复制到达；不调用 InitAnim 代替正常动画求值。 */
	class FValidatePlayer final : public IAutomationLatentCommand
	{
	public:
		FValidatePlayer(FAutomationTestBase* InTest, bool bInNetwork)
			: Test(InTest), bNetwork(bInNetwork), Started(FPlatformTime::Seconds())
		{
			FootstepHandle = UShooterAnimNotify_Footstep::PlaybackObserved.AddLambda(
				[this](USkeletalMeshComponent* Mesh, FName Foot) { ObserveFootstep(Mesh, Foot); });
		}

		virtual ~FValidatePlayer() override
		{
			UShooterAnimNotify_Footstep::PlaybackObserved.Remove(FootstepHandle);
		}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 75.0)
			{
				Test->AddError(FString::Printf(TEXT("TP PIE timed out at phase %d"), Phase));
				return true;
			}
			if (Phase == 0)
			{
				return Setup();
			}
			if (!Target.IsValid())
			{
				Test->AddError(TEXT("PIE target disappeared"));
				return true;
			}
			TArray<AShooterCharacter*> Copies = FindCopies();
			AShooterCharacter* Owner = nullptr;
			for (AShooterCharacter* Copy : Copies)
			{
				if (Copy->IsLocallyControlled())
				{
					Owner = Copy;
				}
				UShooterThirdPersonAnimInstance* Anim = Cast<UShooterThirdPersonAnimInstance>(Copy->GetMesh()->GetAnimInstance());
				if (!Anim)
				{
					return false;
				}
				if (InitialInstances.Contains(Copy))
				{
					if (!Test->TestTrue(TEXT("TP instance survives all switches"), InitialInstances[Copy].Get() == Anim))
					{
						return true;
					}
				}
				else
				{
					InitialInstances.Add(Copy, Anim);
				}
			}
			if (!Owner || Copies.Num() < (bNetwork ? 2 : 1))
			{
				return false;
			}
			const double Elapsed = FPlatformTime::Seconds() - PhaseStarted;
			if (Phase <= 5)
			{
				Owner->AddMovementInput(FVector(0.0f, -1.0f, 0.0f));
			}
			if (Phase == 1 || Phase == 2)
			{
				const FAnimNode_StateMachine* Machine = GetLocomotion(Target.Get());
				if (FPlatformTime::Seconds() - LastDiagnosticTime > 5.0)
				{
					LastDiagnosticTime = FPlatformTime::Seconds();
					UE_LOG(
						LogTemp,
						Display,
						TEXT("TP_PIE_WAIT Phase=%d State=%s Anim=%s Velocity=%s Ready=%d Copies=%d"),
						Phase,
						Machine ? *Machine->GetCurrentStateName().ToString() : TEXT("NoMachine"),
						*GetNameSafe(Target->GetMesh()->GetAnimInstance()),
						*Target->GetVelocity().ToString(),
						CopiesReady(Copies),
						Copies.Num());
				}
				if (!Machine || Machine->GetCurrentStateName() != TEXT("FPP_Run") || Elapsed < 0.35)
				{
					return false;
				}
				if (!CopiesReady(Copies))
				{
					return false;
				}
				if (GroundSwitchIndex < 4)
				{
					if (GroundSwitchIndex == 3 && !GrantWeapon(3, TEXT("GrenadeLauncher"), TEXT("Pistol")))
					{
						return true;
					}
					SwitchWithoutReset(Weapons[GroundSwitchIndex++].Get());
					Next(2);
					return false;
				}
				if (!GrantWeapon(1, TEXT("Pistol"), TEXT("AWP")))
				{
					return true;
				}
				for (AShooterCharacter* Copy : Copies)
				{
					Copy->GetCharacterMovement()->JumpZVelocity = 900.0f;
				}
				Owner->Jump();
				Next(3);
			}
			else if (Phase == 3 || Phase == 4)
			{
				const FAnimNode_StateMachine* Machine = GetLocomotion(Target.Get());
				const FName Expected = Phase == 3 ? FName(TEXT("FPP_JumpStart")) : FName(TEXT("FPP_JumpLoop"));
				if (!Machine || Machine->GetCurrentStateName() != Expected)
				{
					return false;
				}
				SwitchWithoutReset(Weapons[Phase == 3 ? 1 : 0].Get());
				Next(Phase + 1);
			}
			else if (Phase == 5)
			{
				Owner->StopJumping();
				if (Target->GetCharacterMovement()->IsFalling() || !CopiesReady(Copies))
				{
					return false;
				}
				SwitchWithoutReset(Weapons[1].Get());
				Next(6);
			}
			else if (Phase == 6 && CopiesReady(Copies) && Elapsed > 0.5)
			{
				for (AShooterCharacter* Copy : Copies)
				{
					Test->TestTrue(TEXT("Ground run requests left steps on every view"), StepLeft.FindRef(Copy) > 0);
					Test->TestTrue(TEXT("Ground run requests right steps on every view"), StepRight.FindRef(Copy) > 0);
					LandingRequests.Add(Copy, 0);
				}
				Test->TestEqual(TEXT("No duplicate same-foot requests in a frame"), DuplicateStepRequests, 0);
				Test->TestEqual(TEXT("FP never submits footsteps"), FirstPersonRequests, 0);
				Owner->Jump();
				Next(20);
			}
			else if (Phase == 20 || Phase == 21)
			{
				const FAnimNode_StateMachine* Machine = GetLocomotion(Target.Get());
				const FName Expected = Phase == 20 ? FName(TEXT("FPP_JumpStart")) : FName(TEXT("FPP_JumpLoop"));
				if (!Machine || Machine->GetCurrentStateName() != Expected)
				{
					return false;
				}
				SwitchWithoutReset(Weapons[Phase == 20 ? 0 : 1].Get());
				Next(Phase + 1);
			}
			else if (Phase == 22)
			{
				Owner->StopJumping();
				if (Target->GetCharacterMovement()->IsFalling() || !CopiesReady(Copies))
				{
					return false;
				}
				SwitchWithoutReset(Weapons[0].Get());
				Next(23);
			}
			else if (Phase == 23 && Elapsed > 0.5 && CopiesReady(Copies))
			{
				for (AShooterCharacter* Copy : Copies)
				{
					Test->TestEqual(TEXT("Stationary second landing requests exactly once per view"),
						LandingRequests.FindRef(Copy), 1);
					Test->AddInfo(FString::Printf(TEXT("TP_AUDIO View=%s Left=%d Right=%d Landing=%d"),
						Copy->IsLocallyControlled() ? TEXT("Owner") : TEXT("Observer"),
						StepLeft.FindRef(Copy), StepRight.FindRef(Copy), LandingRequests.FindRef(Copy)));
				}
				SwitchWithoutReset(Weapons[1].Get());
				Next(24);
			}
			else if (Phase == 24 && CopiesReady(Copies) && Elapsed > 0.3)
			{
				ShotBaseline = Weapons[1]->GetAuthorityShotCountForAutomationTest();
				for (AShooterCharacter* Copy : Copies)
				{
					MontageBaselines.Add(Copy, Copy->IsLocallyControlled()
						? Copy->GetOwnerLocalMontageCountForAutomationTest()
						: Copy->GetRemoteConfirmedMontageCountForAutomationTest());
				}
				Owner->DoStartFiring();
				Next(7);
			}
			else if (Phase == 7 && Elapsed > 0.2)
			{
				Owner->DoStopFiring();
				Next(8);
			}
			else if (Phase == 8 && Elapsed > 0.7)
			{
				Test->TestEqual(TEXT("One pistol input produces one authority shot"),
					Weapons[1]->GetAuthorityShotCountForAutomationTest() - ShotBaseline, 1);
				for (AShooterCharacter* Copy : Copies)
				{
					if (Copy->IsLocallyControlled())
					{
						Test->TestEqual(TEXT("Owner feedback remains once"),
							Copy->GetOwnerLocalMontageCountForAutomationTest() - MontageBaselines.FindRef(Copy), 1);
					}
					else
					{
						Test->TestEqual(TEXT("Observer TP confirmed montage remains once"),
						Copy->GetRemoteConfirmedMontageCountForAutomationTest() - MontageBaselines.FindRef(Copy), 1);
					}
				}
				Owner->DoReload();
				Next(9);
			}
			else if (Phase == 9)
			{
				if (Elapsed > 0.2)
				{
					Owner->DoStopReload();
				}
				for (AShooterCharacter* Copy : Copies)
				{
					UShooterThirdPersonAnimInstance* Anim =
						CastChecked<UShooterThirdPersonAnimInstance>(Copy->GetMesh()->GetAnimInstance());
					const FAnimNode_StateMachine* Machine = Anim->GetStateMachineInstanceFromName(TEXT("WeaponAction"));
					if (Machine && Machine->GetCurrentStateName() == TEXT("Reload"))
					{
						ReloadSeen.Add(Copy);
					}
					if (Anim->bReloadPresentationRecovering)
					{
						RecoverySeen.Add(Copy);
					}
					AShooterWeapon* Weapon = Copy->GetCurrentWeaponActor();
					if (Weapon && Weapon->GetThirdPersonMagazineProxy()->GetAttachParent() == Copy->GetMesh())
					{
						MagazineSeen.Add(Copy);
					}
				}
				if (Elapsed > 3.1)
				{
					Test->TestEqual(TEXT("Every view evaluates Reload"), ReloadSeen.Num(), Copies.Num());
					Test->TestEqual(TEXT("Every view receives ReloadRecovery notify"), RecoverySeen.Num(), Copies.Num());
					Test->TestEqual(TEXT("Every view detaches magazine via sequence notify"), MagazineSeen.Num(), Copies.Num());
					Target->GetEquipmentComponent()->ClearEquippedWeapon();
					Next(10);
				}
			}
			else if (Phase == 10 && Elapsed > 0.5)
			{
				for (AShooterCharacter* Copy : Copies)
				{
					const UShooterThirdPersonAnimInstance* Anim =
						CastChecked<UShooterThirdPersonAnimInstance>(Copy->GetMesh()->GetAnimInstance());
					Test->TestEqual(TEXT("Unarmed uses only locomotion base pose"), Anim->WeaponUpperBodyWeight, 0.0f);
					Test->TestFalse(TEXT("Unarmed clears Aim IK"), Anim->bAimIKEnabled);
					Test->TestFalse(TEXT("Unarmed clears LeftHand IK"), Anim->bLeftHandIKEnabled);
				}
				Test->AddInfo(TEXT("TP_PLAYER_PIE_SUCCESS GroundSwitches=4 Jumps=2 JumpStartSwitch=2 JumpLoopSwitch=2 Fire=1 Reload=1"));
				return true;
			}
			return false;
		}

	private:
		void ObserveFootstep(USkeletalMeshComponent* Mesh, FName Foot)
		{
			AShooterCharacter* Copy = Mesh ? Cast<AShooterCharacter>(Mesh->GetOwner()) : nullptr;
			if (!Copy || !InitialInstances.Contains(Copy))
			{
				return;
			}
			if (Mesh != Copy->GetMesh())
			{
				++FirstPersonRequests;
				return;
			}
			if (Phase <= 2)
			{
				TMap<FName, uint64>& Frames = LastStepFrames.FindOrAdd(Copy);
				const uint64* Previous = Frames.Find(Foot);
				DuplicateStepRequests += Previous && *Previous == GFrameCounter ? 1 : 0;
				Frames.Add(Foot, GFrameCounter);
				if (Foot == TEXT("foot_l"))
				{
					++StepLeft.FindOrAdd(Copy);
				}
				else if (Foot == TEXT("foot_r"))
				{
					++StepRight.FindOrAdd(Copy);
				}
			}
			// 第二跳不添加水平输入；静止时 Step 被 Notify 守卫拒绝，只接受 Landing 请求。
			if (Phase >= 20 && Phase <= 23)
			{
				++LandingRequests.FindOrAdd(Copy);
			}
		}

		bool Setup()
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (Context.WorldType != EWorldType::PIE || !World || World->GetNetMode() == NM_Client)
				{
					continue;
				}
				for (TActorIterator<AShooterCharacter> It(World); It; ++It)
				{
					if (It->GetPlayerState() && (!bNetwork || !It->IsLocallyControlled()))
					{
						Target = *It;
						break;
					}
				}
			}
			if (!Target.IsValid() || !Target->GetMesh()->GetAnimInstance())
			{
				return false;
			}
			UShooterWeaponRuntimeSubsystem* Runtime = Target->GetWorld()->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
			if (!Runtime || !Runtime->IsRuntimeInitialized())
			{
				return false;
			}
			// 随机出生点前方可能紧邻掩体；测试统一从本地图已核对的畅通通道开始。
			Target->SetActorLocation(FVector(-534.0f, 1506.0f, 98.21f), false, nullptr, ETeleportType::TeleportPhysics);
			Target->ForceNetUpdate();
			const FName Ids[] = {TEXT("Rifle"), TEXT("Pistol"), TEXT("AWP")};
			Weapons.SetNum(4);
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Ids); ++Index)
			{
				if (!GrantWeapon(Index, Ids[Index]))
				{
					return true;
				}
			}
			Target->GetEquipmentComponent()->EquipWeapon(Weapons[0].Get());
			Next(1);
			return false;
		}

		/** 保留三个 Slot 的正式容量；第四种武器进入前只归还一把未装备武器。 */
		bool GrantWeapon(int32 Index, FName Id, FName EvictedId = NAME_None)
		{
			UShooterInventoryComponent* Inventory = Target->GetInventoryComponent();
			AShooterWeapon* Weapon = Inventory->FindWeaponByWeaponId(Id);
			if (!Weapon)
			{
				if (Inventory->FindFreeSlotIndex() == INDEX_NONE)
				{
					AShooterWeapon* Evicted = Inventory->FindWeaponByWeaponId(EvictedId);
					if (!Evicted || Evicted == Target->GetCurrentWeaponActor() || !Inventory->RemoveWeapon(Evicted))
					{
						Test->AddError(TEXT("PIE fixture could not free a holstered slot"));
						return false;
					}
				}
				UShooterWeaponRuntimeSubsystem* Runtime = Target->GetWorld()->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
				Weapon = Runtime->AcquireWeapon(Id, Target.Get(), Target.Get());
				if (!Weapon || Inventory->AddWeapon(Weapon) != EShooterInventoryAddResult::Added)
				{
					if (Weapon)
					{
						Runtime->ReleaseWeapon(Weapon);
					}
					Test->AddError(TEXT("PIE fixture could not grant weapon"));
					return false;
				}
				Weapon->SetActorHiddenInGame(true);
			}
			Weapons[Index] = Weapon;
			return true;
		}

		TArray<AShooterCharacter*> FindCopies() const
		{
			TArray<AShooterCharacter*> Result;
			const int32 PlayerId = Target->GetPlayerState()->GetPlayerId();
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType != EWorldType::PIE || !Context.World())
				{
					continue;
				}
				for (TActorIterator<AShooterCharacter> It(Context.World()); It; ++It)
				{
					if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId() == PlayerId)
					{
						Result.Add(*It);
					}
				}
			}
			return Result;
		}

		bool CopiesReady(const TArray<AShooterCharacter*>& Copies)
		{
			for (AShooterCharacter* Copy : Copies)
			{
				const AShooterWeapon* Weapon = Copy->GetCurrentWeaponActor();
				const UShooterThirdPersonAnimInstance* Anim = Cast<UShooterThirdPersonAnimInstance>(Copy->GetMesh()->GetAnimInstance());
				if (!Weapon || Weapon->GetWeaponId() != Target->GetCurrentWeaponActor()->GetWeaponId() ||
					!Anim || !Anim->HasWeaponAnimationResources(Weapon))
				{
					return false;
				}
				Test->TestEqual(
					TEXT("Per-weapon muzzle distance is retained"),
					Anim->MinimumRemoteAimTargetDistanceFromMuzzle,
					Weapon->GetWeaponId() == TEXT("Pistol") ? 50.0f : 100.0f);
			}
			return true;
		}

		static const FAnimNode_StateMachine* GetLocomotion(AShooterCharacter* Character)
		{
			return Character->GetMesh()->GetAnimInstance()->GetStateMachineInstanceFromName(TEXT("LocomotioStateMachine"));
		}

		void SwitchWithoutReset(AShooterWeapon* Weapon)
		{
			UAnimInstance* Anim = Target->GetMesh()->GetAnimInstance();
			const FAnimNode_StateMachine* Machine = GetLocomotion(Target.Get());
			const FName State = Machine->GetCurrentStateName();
			const float Elapsed = Machine->GetCurrentStateElapsedTime();
			const int32 PlayerIndex = Anim->GetInstanceAssetPlayerIndex(TEXT("LocomotioStateMachine"), State);
			Test->TestTrue(TEXT("Locomotion player index is valid"), PlayerIndex != INDEX_NONE);
			const float Time = Anim->GetInstanceAssetPlayerTime(PlayerIndex);
			Test->TestTrue(TEXT("PIE authority equips target"), Target->GetEquipmentComponent()->EquipWeapon(Weapon));
			Test->TestTrue(TEXT("Switch keeps exact TP instance"), Target->GetMesh()->GetAnimInstance() == Anim);
			Test->TestTrue(TEXT("Switch keeps locomotion node"), GetLocomotion(Target.Get()) == Machine);
			Test->TestEqual(TEXT("Switch keeps locomotion state"), Machine->GetCurrentStateName(), State);
			Test->TestEqual(TEXT("Switch keeps state clock"), Machine->GetCurrentStateElapsedTime(), Elapsed);
			Test->TestEqual(TEXT("Switch keeps asset player clock"), Anim->GetInstanceAssetPlayerTime(PlayerIndex), Time);
			const FString Diagnostic = FString::Printf(
				TEXT("TP_SWITCH Weapon=%s State=%s StateTime=%.4f PlayerTime=%.4f Instance=%s"),
				*Weapon->GetWeaponId().ToString(),
				*State.ToString(),
				Elapsed,
				Time,
				*Anim->GetPathName());
			Test->AddInfo(Diagnostic);
		}

		void Next(int32 InPhase)
		{
			Phase = InPhase;
			PhaseStarted = FPlatformTime::Seconds();
		}

		FAutomationTestBase* Test;
		bool bNetwork;
		double Started;
		double PhaseStarted = 0.0;
		double LastDiagnosticTime = 0.0;
		int32 Phase = 0;
		int32 GroundSwitchIndex = 0;
		int32 ShotBaseline = 0;
		FDelegateHandle FootstepHandle;
		int32 DuplicateStepRequests = 0;
		int32 FirstPersonRequests = 0;
		TMap<AShooterCharacter*, TMap<FName, uint64>> LastStepFrames;
		TMap<AShooterCharacter*, int32> StepLeft;
		TMap<AShooterCharacter*, int32> StepRight;
		TMap<AShooterCharacter*, int32> LandingRequests;
		TMap<AShooterCharacter*, int32> MontageBaselines;
		TWeakObjectPtr<AShooterCharacter> Target;
		TArray<TWeakObjectPtr<AShooterWeapon>> Weapons;
		TMap<AShooterCharacter*, TWeakObjectPtr<UAnimInstance>> InitialInstances;
		TSet<AShooterCharacter*> ReloadSeen;
		TSet<AShooterCharacter*> RecoverySeen;
		TSet<AShooterCharacter*> MagazineSeen;
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FShooterTPPlayerPIETest, "ShootGame.Animation.TPPlayer.PIE",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FShooterTPPlayerPIETest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	Names.Add(TEXT("Standalone"));
	Commands.Add(TEXT("Standalone"));
	Names.Add(TEXT("ListenOwnerObserver"));
	Commands.Add(TEXT("Listen"));
}

bool FShooterTPPlayerPIETest::RunTest(const FString& Parameters)
{
	using namespace ShooterTPPlayerPIE;
	ULevelEditorPlaySettings* Play = GetMutableDefault<ULevelEditorPlaySettings>();
	FSettingsSnapshot Original;
	Play->GetPlayNetMode(Original.Mode);
	Play->GetPlayNumberOfClients(Original.Clients);
	Play->GetRunUnderOneProcess(Original.bOneProcess);
	const bool bNetwork = Parameters == TEXT("Listen");
	Play->SetPlayNetMode(bNetwork ? PIE_ListenServer : PIE_Standalone);
	Play->SetPlayNumberOfClients(bNetwork ? 2 : 1);
	Play->SetRunUnderOneProcess(true);
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Shooter/Maps/Lvl_Shooter")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FValidatePlayer>(this, bNetwork));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FRestoreSettings>(Original));
	return true;
}

#endif
