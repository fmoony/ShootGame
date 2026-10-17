// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/ShooterNPC.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/Movement/ShooterCharacterMovementComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/DamageEvents.h"
#include "Engine/NetDriver.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState/ShooterPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "InputKeyEventArgs.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationEditorCommon.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/Subsystems/ShooterWeaponRuntimeSubsystem.h"

namespace ShooterSprintPIE
{
	struct FSettings
	{
		EPlayNetMode Mode;
		int32 Clients;
		bool bOneProcess;
		bool bSeparateServer;
	};

	class FRestore : public IAutomationLatentCommand
	{
	public:
		explicit FRestore(FSettings In) : Saved(In) {}
		virtual bool Update() override
		{
			if (GEditor->PlayWorld) { return false; }
			auto* Play = GetMutableDefault<ULevelEditorPlaySettings>();
			Play->SetPlayNetMode(Saved.Mode);
			Play->SetPlayNumberOfClients(Saved.Clients);
			Play->SetRunUnderOneProcess(Saved.bOneProcess);
			Play->bLaunchSeparateServer = Saved.bSeparateServer;
			return true;
		}
	private:
		FSettings Saved;
	};

	class FValidate : public IAutomationLatentCommand
	{
	public:
		FValidate(FAutomationTestBase* InTest, bool bInLag)
			: Test(InTest), bLag(bInLag), Started(FPlatformTime::Seconds()) {}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 120.0) { return Fail(TEXT("timeout")); }
			if (!Target.IsValid()) { return Prepare(); }
			Copies = FindCopies();
			AShooterCharacter* Owner = nullptr;
			for (auto* Copy : Copies)
			{
				if (Copy->IsLocallyControlled()) { Owner = Copy; }
				else
				{
					// 无头观察夹具显式更新远端；拥有端保留生产隐藏 Mesh 的更新策略。
					Copy->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
					Copy->GetMesh()->bEnableUpdateRateOptimizations = false;
				}
			}
			if (!Owner || Copies.Num() != 3 || !Rifle.IsValid()) { return false; }
			auto* Movement = Owner->GetShooterMovement();
			auto* Anim = Cast<UShooterThirdPersonAnimInstance>(Owner->GetMesh()->GetAnimInstance());
			const auto* State = Anim ? Anim->GetStateMachineInstanceFromName(TEXT("RifleReady")) : nullptr;
			const auto* Player = Anim ? Anim->GetRifleTransitionPlayer() : nullptr;
			if (!State || !Player) { return false; }
			const double Elapsed = FPlatformTime::Seconds() - PhaseStarted;
			if (Elapsed > 20.0)
			{
				Test->AddInfo(FString::Printf(TEXT("SPRINT_STUCK held=%d fire=%d mode=%d accel=%.1f weapon=%s"),
					Movement->IsSprintHeld(), Movement->IsFireHeld(), int32(Movement->MovementMode),
					Movement->GetCurrentAcceleration().Size(), *GetNameSafe(Owner->GetCurrentWeaponActor())));
				return Fail(TEXT("stage timeout"));
			}
			const float Time = Player->GetAccumulatedTime();
			Rows += FString::Printf(TEXT("%d,%.5f,%.3f,%.3f,%d,%d,%s,%.5f,%.1f,%d,%d,%d\r\n"),
				Phase,
				FPlatformTime::Seconds() - Started,
				Movement->Velocity.Size2D(),
				Target->GetVelocity().Size2D(),
				Movement->IsSprinting(),
				Target->GetShooterMovement()->IsSprinting(),
				*State->GetCurrentStateName().ToString(),
				Time,
				Player->GetPlayRate(),
				Rifle->GetAuthorityShotCountForAutomationTest(),
				Rifle->GetBulletCount(),
				Movement->CorrectionReplayCount);
			if (Phase == 0)
			{
				const auto* PS = Owner->GetPlayerState<AShooterPlayerState>();
				if (!Owner->GetCurrentWeaponActor() || !PS ||
					!PS->FindFireAbilitySpecForWeapon(Owner->GetCurrentWeaponActor()) || Elapsed < 1) { return false; }
				Test->TestEqual(TEXT("Real dedicated server"), Target->GetNetMode(), NM_DedicatedServer);
				Test->TestEqual(TEXT("Two actual client connections"),
					Target->GetWorld()->GetNetDriver()->ClientConnections.Num(), 2);
				Ammo = Rifle->GetBulletCount();
				Test->AddInfo(FString::Printf(TEXT("SPRINT_OWNER_PRODUCTION_TICK option=%d uro=%d"),
					int32(Owner->GetMesh()->VisibilityBasedAnimTickOption), Owner->GetMesh()->bEnableUpdateRateOptimizations));
				Shots = Rifle->GetAuthorityShotCountForAutomationTest();
				CastChecked<APlayerController>(Owner->GetController())->SetControlRotation(FRotator::ZeroRotator);
				Key(Owner, EKeys::W, true);
				Key(Owner, EKeys::K, true);
				Next(1);
			}
			else if (Phase == 1 && Elapsed > 0.8 && AllMoving(false, TEXT("ArmedAim")))
			{
				Key(Owner, EKeys::K, false);
				Test->TestFalse(TEXT("K cannot change the formal pose target"), Anim->bRifleLowReadyRequested);
				CheckNoShots();
				Key(Owner, EKeys::LeftShift, true);
				Next(2);
			}
			else if (Phase == 2 && AllMoving(true, TEXT("LowReady")))
			{
				Test->TestEqual(TEXT("Runtime lowering only selects direction"), Anim->RifleTransitionPlayRate, 1.0f);
				Test->TestTrue(TEXT("Native asset supplies acceleration"), Player->GetSequence()->RateScale > 2.0f);
				CheckNoShots();
				Key(Owner, EKeys::LeftShift, false);
				Next(3);
			}
			else if (Phase == 3 && State->GetCurrentStateName() == TEXT("AimTransition") && Time > 0.25 && Time < 0.65)
			{
				BeforeReverse = Time;
				Key(Owner, EKeys::LeftShift, true);
				Next(4);
			}
			else if (Phase == 4)
			{
				bReverseSeen |= State->GetCurrentStateName() == TEXT("AimTransition") &&
					Player->GetPlayRate() > 0 && Time > BeforeReverse && Time < 0.82;
				if (bReverseSeen)
				{
					RapidPreviousTime = Time;
					RapidPreviousWallTime = FPlatformTime::Seconds();
					RapidSequence = Player->GetSequence();
					Next(40);
				}
			}
			else if (Phase == 40)
			{
				const double Now = FPlatformTime::Seconds();
				const float Step = FMath::Max(float(Now - RapidPreviousWallTime), Owner->GetWorld()->GetDeltaSeconds());
				const float EffectiveRate = FMath::Abs(Player->GetPlayRate() * Player->GetSequence()->RateScale);
				Test->TestTrue(TEXT("Rapid Shift preserves the latched clip"), RapidSequence == Player->GetSequence());
				if (FMath::Abs(Time - RapidPreviousTime) > EffectiveRate * Step + 0.025f)
				{
					return Fail(TEXT("Rapid Shift reset the player clock"));
				}
				RapidPreviousTime = Time;
				RapidPreviousWallTime = Now;
				if (RapidEdges < 8 && Elapsed > 0.045 * (RapidEdges + 1))
				{
					++RapidEdges;
					Key(Owner, EKeys::LeftShift, RapidEdges % 2 == 0);
				}
				if (RapidEdges == 8 && Elapsed > 0.5) { Next(41); }
			}
			else if (Phase == 41 && AllMoving(true, TEXT("LowReady")))
			{
				Test->TestTrue(TEXT("Raise reverses in place using existing player"), bReverseSeen);
				CheckNoShots();
				Feedback = Owner->GetOwnerLocalMontageCountForAutomationTest();
				Owner->DoStartFiring();
				Test->TestFalse(TEXT("Fire input immediately exits sprint"), Movement->IsSprinting());
				Next(5);
			}
			else if (Phase == 5)
			{
				if (Owner->GetOwnerLocalMontageCountForAutomationTest() > Feedback &&
					State->GetCurrentStateName() == TEXT("AimTransition") && Time > 0)
				{
					return Fail(TEXT("Shot occurred before actual raise endpoint"));
				}
				bReadyShot |= Owner->GetOwnerLocalMontageCountForAutomationTest() > Feedback && Owner->IsRifleReadyForFire();
				if (Movement->IsSprinting()) { return Fail(TEXT("Fire Held allowed sprint")); }
				if (Elapsed > 1.2 && Rifle->GetAuthorityShotCountForAutomationTest() - Shots >= 3)
				{
					Test->TestTrue(TEXT("Held first shot waits for the real raise endpoint"), bReadyShot);
					Test->TestEqual(TEXT("Runtime raising only selects direction"), Anim->RifleTransitionPlayRate, -1.0f);
					Test->TestEqual(TEXT("One ammo per authority shot"), Ammo - Rifle->GetBulletCount(),
						Rifle->GetAuthorityShotCountForAutomationTest() - Shots);
					Owner->DoStopFiring();
					Next(6);
				}
			}
			else if (Phase == 6 && AllMoving(true, TEXT("LowReady")))
			{
				Shots = Rifle->GetAuthorityShotCountForAutomationTest();
				Ammo = Rifle->GetBulletCount();
				Feedback = Owner->GetOwnerLocalMontageCountForAutomationTest();
				// 三次点击共享同一首发窗口；全部提前松开，也只能执行一次。
				for (int32 Click = 0; Click < 3; ++Click) { Owner->DoStartFiring(); Owner->DoStopFiring(); }
				Test->TestTrue(TEXT("Released click retains pending first shot"), Movement->IsFireReadyPending());
				Test->TestFalse(TEXT("Pending first shot prevents Shift resuming sprint"), Movement->IsSprinting());
				Owner->SetActorLocation(Owner->GetActorLocation() + FVector(0, 180, 0), false,
					nullptr, ETeleportType::TeleportPhysics);
				Next(60);
			}
			else if (Phase == 60)
			{
				if (Movement->IsFireReadyPending() && !Owner->IsRifleReadyForFire())
				{
					Test->TestEqual(TEXT("No feedback before endpoint"),
						Owner->GetOwnerLocalMontageCountForAutomationTest(), Feedback);
					CheckNoShots();
				}
				if (Elapsed > 1.3 && AllMoving(true, TEXT("LowReady")))
				{
					Test->TestEqual(TEXT("Released first shot exactly once"),
						Rifle->GetAuthorityShotCountForAutomationTest() - Shots, 1);
					Test->TestEqual(TEXT("Released first shot consumes one ammo"), Ammo - Rifle->GetBulletCount(), 1);
					Test->TestEqual(TEXT("Released first shot owner feedback once"),
						Owner->GetOwnerLocalMontageCountForAutomationTest() - Feedback, 1);
					Test->TestFalse(TEXT("Correction cannot resurrect consumed first shot"), Movement->IsFireReadyPending());
					Shots = Rifle->GetAuthorityShotCountForAutomationTest();
					Ammo = Rifle->GetBulletCount();
					Owner->DoStartFiring(); Owner->DoStopFiring();
					Owner->GetMesh()->SetComponentTickEnabled(false); // 夹具暂停动画，验证真实窗口过期。
					Next(61);
				}
			}
			else if (Phase == 61)
			{
				if (Elapsed > 0.35 && !bExpiryRepeat)
				{
					Owner->DoStartFiring(); Owner->DoStopFiring();
					bExpiryRepeat = true;
				}
				if (Elapsed > 0.85)
				{
					Test->TestFalse(TEXT("Repeated press does not extend first shot lifetime"), Movement->IsFireReadyPending());
					CheckNoShots();
					Owner->GetMesh()->SetComponentTickEnabled(true);
					Next(62);
				}
			}
			else if (Phase == 62 && Elapsed > 0.8 && AllMoving(true, TEXT("LowReady")))
			{
				CheckNoShots();
				Owner->DoStartFiring(); Owner->DoStopFiring();
				Test->TestTrue(TEXT("Weapon invalidation fixture has a pending first shot"), Movement->IsFireReadyPending());
				Target->GetEquipmentComponent()->ClearEquippedWeapon();
				Next(63);
			}
			else if (Phase == 63 && Elapsed > 0.8 && !Owner->GetCurrentWeaponActor())
			{
				Test->TestFalse(TEXT("Weapon invalidation cancels pending first shot"), Movement->IsFireReadyPending());
				CheckNoShots();
				Target->GetEquipmentComponent()->EquipWeapon(Rifle.Get());
				Next(64);
			}
			else if (Phase == 64 && Elapsed > 0.8 && AllMoving(true, TEXT("LowReady")))
			{
				CheckNoShots();
				Replays = bLag ? Movement->CorrectionReplayCount : Movement->CorrectionCount;
				// 只污染拥有端位置，要求真实 ServerMove 校验和原生纠正重放恢复。
				Owner->SetActorLocation(Owner->GetActorLocation() + FVector(0, 200, 0), false,
					nullptr, ETeleportType::TeleportPhysics);
				Next(7);
			}
			else if (Phase == 7 && Elapsed > 1 &&
				(bLag ? Movement->CorrectionReplayCount : Movement->CorrectionCount) > Replays &&
				FMath::Abs(Owner->GetActorLocation().Y - Target->GetActorLocation().Y) < 10)
			{
				Test->TestTrue(TEXT("Correction restores current held sprint input"), Movement->IsSprintHeld());
				Test->TestFalse(TEXT("Correction does not resurrect fire"), Movement->IsFireHeld());
				CheckNoShots();
				Owner->DoStartFiring(); Owner->DoStopFiring();
				Test->TestTrue(TEXT("Reload fixture has a pending first shot"), Movement->IsFireReadyPending());
				Owner->DoReload();
				Test->TestFalse(TEXT("Reload cancels pending first shot immediately"), Movement->IsFireReadyPending());
				Next(8);
			}
			else if (Phase == 8)
			{
				bReloadSeen |= Anim->bIsReloading && !Movement->IsSprinting();
				if (Elapsed > 0.1) { Owner->DoStopReload(); }
				if (Elapsed > 3.5 && AllMoving(true, TEXT("LowReady")))
				{
					Test->TestTrue(TEXT("Reload overrides sprint during its actual window"), bReloadSeen);
					Test->TestEqual(TEXT("Reload creates no authority shot"),
						Rifle->GetAuthorityShotCountForAutomationTest(), Shots);
					Owner->DoStartFiring(); Owner->DoStopFiring();
					Test->TestTrue(TEXT("Equip fixture has a pending first shot"), Movement->IsFireReadyPending());
					Owner->DoSwitchWeapon();
					Test->TestFalse(TEXT("Equip cancels pending first shot immediately"), Movement->IsFireReadyPending());
					Next(9);
				}
			}
			else if (Phase == 9)
			{
				bEquipSeen |= Movement->IsSprintBlockedByServerAction() && !Movement->IsSprinting();
				if (Elapsed > 1 && Owner->GetCurrentWeaponActor() &&
					Owner->GetCurrentWeaponActor()->GetWeaponId() == TEXT("Pistol") && AllMoving(true, TEXT("ArmedAim")))
				{
					Test->TestTrue(TEXT("Equip overrides sprint during its actual window"), bEquipSeen);
					Test->TestFalse(TEXT("Pistol does not request Rifle LowReady"), Anim->bRifleLowReadyRequested);
					Target->GetEquipmentComponent()->ClearEquippedWeapon();
					Next(10);
				}
			}
			else if (Phase == 10 && Elapsed > 0.5 && !Owner->GetCurrentWeaponActor() && Movement->IsSprinting())
			{
				Test->TestFalse(TEXT("Unarmed never requests Rifle pose"), Anim->GetRifleLowerRequested());
				Key(Owner, EKeys::LeftShift, false);
				Next(11);
			}
			else if (Phase == 11 && Elapsed > 0.5 && !Movement->IsSprinting() && !Target->GetShooterMovement()->IsSprinting())
			{
				Test->TestTrue(TEXT("Shift release restores walk speed"),
					FMath::IsNearlyEqual(Movement->Velocity.Size2D(), 250, 5));
				Owner->DoStartSprint();
				Target->GetEquipmentComponent()->EquipWeapon(Rifle.Get());
				Next(19);
			}
			else if (Phase == 19 && Elapsed > 0.8 && AllMoving(true, TEXT("LowReady")))
			{
				Owner->DoStartFiring(); Owner->DoStopFiring();
				Test->TestTrue(TEXT("Death fixture has a pending first shot"), Movement->IsFireReadyPending());
				Shots = Rifle->GetAuthorityShotCountForAutomationTest();
				Target->TakeDamage(100000, FDamageEvent(), nullptr, nullptr);
				Next(12);
			}
			else if (Phase == 12 && Owner->IsDead())
			{
				Test->TestFalse(TEXT("Death prevents sprint"), Movement->CanSprint());
				Test->TestFalse(TEXT("Death clears Shift intent"), Movement->IsSprintHeld());
				Test->TestFalse(TEXT("Death clears Fire intent"), Movement->IsFireHeld());
				Test->TestFalse(TEXT("Death cancels pending first shot"), Movement->IsFireReadyPending());
				Test->TestEqual(TEXT("Death creates no pending authority shot"),
					Rifle->GetAuthorityShotCountForAutomationTest(), Shots);
				Test->TestFalse(TEXT("Authority death clears sprint"), Target->GetShooterMovement()->IsSprinting());
				return Finish();
			}
			return false;
		}

	private:
		bool Prepare()
		{
			UWorld* Server = nullptr;
			TArray<UWorld*> Worlds;
			TArray<AShooterCharacter*> Players;
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType != EWorldType::PIE || !Context.World()) { continue; }
				Worlds.Add(Context.World());
				if (Context.World()->GetNetMode() == NM_DedicatedServer) { Server = Context.World(); }
			}
			if (!Server || Worlds.Num() != 3 || !Server->GetNetDriver() ||
				Server->GetNetDriver()->ClientConnections.Num() != 2) { return false; }
			for (TActorIterator<AShooterCharacter> It(Server); It; ++It)
			{
				if (It->GetPlayerState() && Cast<APlayerController>(It->GetController())) { Players.Add(*It); }
			}
			if (Players.Num() != 2) { return false; }
			auto* Runtime = Server->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
			if (!Runtime || !Runtime->IsRuntimeInitialized()) { return false; }
			const auto* BlendSpace = LoadObject<UBlendSpace>(nullptr,
				TEXT("/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run.BS_Idle_Walk_Run"));
			if (!BlendSpace) { return Fail(TEXT("production locomotion blendspace missing")); }
			Test->AddInfo(FString::Printf(TEXT("SPRINT_BLENDSPACE speed_min=%.1f speed_max=%.1f samples=%d"),
				BlendSpace->GetBlendParameter(1).Min, BlendSpace->GetBlendParameter(1).Max,
				BlendSpace->GetBlendSamples().Num()));
			for (TActorIterator<AShooterNPC> It(Server); It; ++It) { It->Destroy(); }
			// 真实复制的地板让 CMC 可解析 MovementBase；不能用各世界独立的匿名组件。
			auto* Floor = Server->SpawnActor<AStaticMeshActor>();
			Floor->bAlwaysRelevant = true;
			Floor->SetReplicates(true);
			Floor->SetReplicateMovement(true);
			auto* Mesh = Floor->GetStaticMeshComponent();
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetIsReplicated(true);
			Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Mesh->SetCollisionProfileName(TEXT("BlockAll"));
			Floor->SetActorScale3D(FVector(800, 800, 1));
			Floor->SetActorLocation(FVector(0, 0, 9900));
			Floor->ForceNetUpdate();
			for (auto* World : Worlds)
			{
#if DO_ENABLE_NET_TEST
				if (bLag)
				{
					FPacketSimulationSettings Packets;
					Packets.PktLag = 100;
					Packets.PktLoss = 2;
					World->GetNetDriver()->SetPacketSimulationSettings(Packets);
					const auto& Actual = World->GetNetDriver()->PacketSimulationSettings;
					Test->TestEqual(TEXT("Actual NetDriver lag is 100 ms"), Actual.PktLag, 100);
					Test->TestEqual(TEXT("Actual NetDriver packet loss is 2 percent"), Actual.PktLoss, 2);
					Test->AddInfo(FString::Printf(TEXT("SPRINT_EMULATION mode=%d lag=%d loss=%d"),
						int32(World->GetNetMode()), Actual.PktLag, Actual.PktLoss));
				}
#else
				if (bLag) { return Fail(TEXT("network emulation is not compiled in")); }
#endif
			}
			Target = Players[0];
			for (int32 Index = 0; Index < Players.Num(); ++Index)
			{
				Players[Index]->SetActorLocation(FVector(0, Index * 500, 10048), false,
					nullptr, ETeleportType::TeleportPhysics);
			}
			for (const TCHAR* Id : {TEXT("Rifle"), TEXT("Pistol")})
			{
				auto* Weapon = Target->GetInventoryComponent()->FindWeaponByWeaponId(Id);
				if (!Weapon)
				{
					Weapon = Runtime->AcquireWeapon(Id, Target.Get(), Target.Get());
					if (!Weapon || Target->GetInventoryComponent()->AddWeapon(Weapon) != EShooterInventoryAddResult::Added)
					{
						return Fail(TEXT("weapon grant failed"));
					}
				}
				if (FName(Id) == TEXT("Rifle")) { Rifle = Weapon; }
			}
			Target->GetEquipmentComponent()->EquipWeapon(Rifle.Get());
			Next(0);
			return false;
		}

		TArray<AShooterCharacter*> FindCopies() const
		{
			TArray<AShooterCharacter*> Result;
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType != EWorldType::PIE || !Context.World()) { continue; }
				for (TActorIterator<AShooterCharacter> It(Context.World()); It; ++It)
				{
					if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId() == Target->GetPlayerState()->GetPlayerId())
					{
						Result.Add(*It);
					}
				}
			}
			return Result;
		}

		bool AllMoving(bool bSprint, FName Pose) const
		{
			for (auto* Copy : Copies)
			{
				if (Copy->GetShooterMovement()->IsSprinting() != bSprint ||
					!FMath::IsNearlyEqual(Copy->GetVelocity().Size2D(), bSprint ? 600.0f : 250.0f, 8.0f)) { return false; }
				if (Copy->GetNetMode() == NM_DedicatedServer) { continue; }
				const auto* Anim = Cast<UShooterThirdPersonAnimInstance>(Copy->GetMesh()->GetAnimInstance());
				const auto* State = Anim ? Anim->GetStateMachineInstanceFromName(TEXT("RifleReady")) : nullptr;
				if (!State || State->GetCurrentStateName() != Pose) { return false; }
			}
			return true;
		}

		void Key(AShooterCharacter* Character, FKey Key, bool bPressed)
		{
			auto* PC = CastChecked<APlayerController>(Character->GetController());
			PC->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), Key,
				bPressed ? IE_Pressed : IE_Released, bPressed ? 1.0f : 0.0f, false, FPlatformTime::Cycles64()));
		}
		void Next(int32 NewPhase)
		{
			Phase = NewPhase;
			PhaseStarted = FPlatformTime::Seconds();
			Test->AddInfo(FString::Printf(TEXT("SPRINT_STAGE lag=%d phase=%d"), bLag, Phase));
			UE_LOG(LogAutomationTestFramework, Display, TEXT("SPRINT_STAGE lag=%d phase=%d"), bLag, Phase);
		}
		void CheckNoShots()
		{
			Test->TestEqual(TEXT("Locomotion creates no shot"), Rifle->GetAuthorityShotCountForAutomationTest(), Shots);
			Test->TestEqual(TEXT("Locomotion consumes no ammo"), Rifle->GetBulletCount(), Ammo);
		}
		bool Finish()
		{
			const FString Folder = FPaths::ProjectSavedDir() / TEXT("SprintGameplay") / FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"));
			IFileManager::Get().MakeDirectory(*Folder, true);
			const FString CsvPath = Folder / (bLag ? TEXT("emulated.csv") : TEXT("dedicated.csv"));
			FFileHelper::SaveStringToFile(Rows, *CsvPath);
			Test->AddInfo(FString::Printf(TEXT("SPRINT_CSV %s"), *CsvPath));
			Test->AddInfo(FString::Printf(
				TEXT("SPRINT_RESULT lag=%d errors=%d reverse=%d ready_shot=%d reload=%d equip=%d"),
				bLag, Test->HasAnyErrors(), bReverseSeen, bReadyShot, bReloadSeen, bEquipSeen));
			return true;
		}
		bool Fail(const TCHAR* Reason)
		{
			Test->AddError(FString::Printf(TEXT("SPRINT_FAIL lag=%d phase=%d reason=%s"), bLag, Phase, Reason));
			return Finish();
		}
		FAutomationTestBase* Test;
		bool bLag;
		double Started, PhaseStarted = 0;
		int32 Phase = 0, Shots = 0, Ammo = 0, Feedback = 0, Replays = 0;
		float BeforeReverse = 0;
		float RapidPreviousTime = 0;
		double RapidPreviousWallTime = 0;
		int32 RapidEdges = 0;
		const UAnimSequenceBase* RapidSequence = nullptr;
		bool bReverseSeen = false, bReadyShot = false, bReloadSeen = false, bEquipSeen = false, bExpiryRepeat = false;
		TWeakObjectPtr<AShooterCharacter> Target;
		TWeakObjectPtr<AShooterWeapon> Rifle;
		TArray<AShooterCharacter*> Copies;
		FString Rows = TEXT("phase,seconds,owner_speed,server_speed,owner_sprint,server_sprint,state,time,rate,shots,ammo,replays\r\n");
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FShooterSprintNetworkTest, "ShootGame.Movement.Sprint.Network",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FShooterSprintNetworkTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	Names = {TEXT("Dedicated"), TEXT("Emulated")};
	Commands = Names;
}

bool FShooterSprintNetworkTest::RunTest(const FString& Parameters)
{
	using namespace ShooterSprintPIE;
	auto* Play = GetMutableDefault<ULevelEditorPlaySettings>();
	FSettings Original;
	Play->GetPlayNetMode(Original.Mode);
	Play->GetPlayNumberOfClients(Original.Clients);
	Play->GetRunUnderOneProcess(Original.bOneProcess);
	Original.bSeparateServer = Play->bLaunchSeparateServer;
	Play->SetPlayNetMode(PIE_Client);
	Play->SetPlayNumberOfClients(2);
	Play->SetRunUnderOneProcess(true);
	Play->bLaunchSeparateServer = false;
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Shooter/Maps/Lvl_Shooter")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FValidate>(this, Parameters == TEXT("Emulated")));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FRestore>(Original));
	return true;
}

#endif
