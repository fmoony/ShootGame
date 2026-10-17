// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/ShooterNPC.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Animation/AnimSequence.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/Movement/ShooterCharacterMovementComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
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

namespace RifleReadyNetworkPIE
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
			Play->SetPlayNetMode(Saved.Mode); Play->SetPlayNumberOfClients(Saved.Clients);
			Play->SetRunUnderOneProcess(Saved.bOneProcess); Play->bLaunchSeparateServer = Saved.bSeparateServer;
			return true;
		}
	private:
		FSettings Saved;
	};

	class FValidate : public IAutomationLatentCommand
	{
	public:
		FValidate(FAutomationTestBase* InTest, FString InMode, FName InWeaponId)
			: Test(InTest), Mode(MoveTemp(InMode)), WeaponId(InWeaponId), Started(FPlatformTime::Seconds()) {}
		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 80) { return Fail(TEXT("formal network timeout")); }
			if (!Target.IsValid()) { return Prepare(); }
			if (!Weapon.IsValid()) { return Fail(TEXT("weapon disappeared")); }
			auto Copies = FindCopies();
			AShooterCharacter* Owner = nullptr;
			bool bAllLow = true, bAllAim = true;
			int32 Views = 0;
			for (auto* Copy : Copies)
			{
				if (Copy->IsLocallyControlled()) { Owner = Copy; }
				else
				{
					Copy->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
					Copy->GetMesh()->bEnableUpdateRateOptimizations = false;
				}
				if (!Copy->GetCurrentWeaponActor() || Copy->GetCurrentWeaponActor()->GetWeaponId() != WeaponId) { return false; }
				if (Copy->GetNetMode() == NM_DedicatedServer) { continue; }
				auto* Anim = Cast<UShooterThirdPersonAnimInstance>(Copy->GetMesh()->GetAnimInstance());
				const auto* State = Anim ? Anim->GetStateMachineInstanceFromName(TEXT("RifleReady")) : nullptr;
				const auto* Player = Anim ? Anim->GetRifleTransitionPlayer() : nullptr;
				if (!State || !Player) { return false; }
				++Views;
				bAllLow &= State->GetCurrentStateName() == TEXT("LowReady") && Copy->GetShooterMovement()->IsSprinting();
				bAllAim &= State->GetCurrentStateName() == TEXT("ArmedAim") &&
					!Copy->GetShooterMovement()->IsSprinting();
				Rows += FString::Printf(TEXT("%d,%d,%d,%s,%.6f,%.3f,%.6f,%d,%.1f,%d,%d\r\n"),
					Phase,
					int32(Copy->GetNetMode()),
					Copy->IsLocallyControlled(),
					*State->GetCurrentStateName().ToString(),
					Player->GetAccumulatedTime(),
					Player->GetPlayRate(),
					Player->GetSequence() ? Player->GetSequence()->RateScale : 0.0f,
					Copy->GetShooterMovement()->IsSprinting(),
					Copy->GetVelocity().Size2D(),
					Copy->GetShooterMovement()->IsSprintHeld(),
					Anim->GetRifleReadyAbortRequested());
			}
			if (!Owner || Views != 2 || Copies.Num() != (Mode == TEXT("Listen") ? 2 : 3)) { return false; }
			auto* Anim = CastChecked<UShooterThirdPersonAnimInstance>(Owner->GetMesh()->GetAnimInstance());
			const auto* Player = Anim->GetRifleTransitionPlayer();
			const auto* State = Anim->GetStateMachineInstanceFromName(TEXT("RifleReady"));
			const float Time = Player->GetAccumulatedTime();
			const double Elapsed = FPlatformTime::Seconds() - PhaseStarted;
			if (Elapsed > 15) { return Fail(TEXT("formal stage timeout")); }
			if (Phase == 0 && bAllAim && Elapsed > 0.5)
			{
				Test->TestTrue(TEXT("Real owning client connection"),
					Owner->GetNetMode() == NM_Client && Owner->GetWorld()->GetNetDriver()->ServerConnection != nullptr);
				Test->TestNull(TEXT("Preview CVar removed"),
					IConsoleManager::Get().FindConsoleVariable(TEXT("ShootGame.RifleReady.NetPreview")));
				for (const auto* Component : Owner->GetComponents())
				{
					Test->TestTrue(TEXT("Preview component removed"), Component->GetFName() != TEXT("RifleReadyNetworkPreview"));
				}
				const auto* Sequence = Anim->RifleTransitionCorrected.Get();
				if (!Sequence) { return Fail(TEXT("configured transition sequence missing")); }
				const float Duration = Sequence->GetPlayLength() / Sequence->RateScale;
				Test->TestTrue(TEXT("Native resource duration is 0.25 to 0.4 seconds"), Duration >= 0.25f && Duration <= 0.4f);
				Ammo = Weapon->GetBulletCount(); Shots = Weapon->GetAuthorityShotCountForAutomationTest();
				Owner->GetController()->SetControlRotation(FRotator(-45, 0, 0));
				Key(Owner, EKeys::W, true); Next(10);
			}
			else if (Phase == 10 && Owner->GetVelocity().Size2D() > 200)
			{
				Key(Owner, EKeys::LeftShift, true); Next(1);
			}
			else if (Phase == 1 && bAllLow)
			{
				CheckNoShot(); Key(Owner, EKeys::LeftShift, false); Next(2);
			}
			else if (Phase == 2 && State->GetCurrentStateName() == TEXT("AimTransition") && Time > 0.25f && Time < 0.65f)
			{
				BeforeReverse = Time; Key(Owner, EKeys::LeftShift, true); Next(3);
			}
			else if (Phase == 3)
			{
				bReverseSeen |= State->GetCurrentStateName() == TEXT("AimTransition") && Time > BeforeReverse && Time < 0.82f;
				if (bAllLow)
				{
					Test->TestTrue(TEXT("Real Shift reverses in place"), bReverseSeen); CheckNoShot();
					Feedback = Owner->GetOwnerLocalMontageCountForAutomationTest();
					Owner->DoStartFiring(); Owner->DoStopFiring();
					Test->TestTrue(TEXT("Released click waits for actual pose"), Owner->GetShooterMovement()->IsFireReadyPending());
					Next(4);
				}
			}
			else if (Phase == 4)
			{
				if (Owner->GetShooterMovement()->IsFireReadyPending() && !Owner->IsRifleReadyForFire())
				{
					Test->TestEqual(TEXT("No early owner feedback"),
						Owner->GetOwnerLocalMontageCountForAutomationTest(), Feedback);
					CheckNoShot();
				}
				if (Elapsed > 1.3 && bAllLow)
				{
					Test->TestEqual(TEXT("One released first shot"),
						Weapon->GetAuthorityShotCountForAutomationTest() - Shots, 1);
					Test->TestEqual(TEXT("One ammo consumed"), Ammo - Weapon->GetBulletCount(), 1);
					Test->TestEqual(TEXT("One owner montage"),
						Owner->GetOwnerLocalMontageCountForAutomationTest() - Feedback, 1);
					Shots = Weapon->GetAuthorityShotCountForAutomationTest(); Owner->DoReload(); Next(5);
				}
			}
			else if (Phase == 5)
			{
				bReloadSeen |= Anim->bIsReloading && !Owner->GetShooterMovement()->IsSprinting();
				if (Elapsed > 0.1) { Owner->DoStopReload(); }
				if (Elapsed > 3.8 && bAllLow)
				{
					Test->TestTrue(TEXT("Reload overrides actual sprint"), bReloadSeen);
					Test->TestEqual(TEXT("Reload creates no extra shot"),
						Weapon->GetAuthorityShotCountForAutomationTest(), Shots);
					Test->TestEqual(TEXT("Reload restores initial magazine"), Weapon->GetBulletCount(), Ammo);
					Key(Owner, EKeys::LeftShift, false); Next(6);
				}
			}
			else if (Phase == 6 && bAllAim && Elapsed > 0.4)
			{
				Key(Owner, EKeys::W, false); return Finish();
			}
			return false;
		}
	private:
		bool Prepare()
		{
			UWorld* Server = nullptr; TArray<UWorld*> Worlds; TArray<AShooterCharacter*> Players;
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType != EWorldType::PIE || !Context.World()) { continue; }
				Worlds.Add(Context.World());
				if (Context.World()->GetNetMode() < NM_Client) { Server = Context.World(); }
			}
			if (!Server || Worlds.Num() != (Mode == TEXT("Listen") ? 2 : 3) || !Server->GetNetDriver()) { return false; }
			for (TActorIterator<AShooterCharacter> It(Server); It; ++It)
			{
				auto* PC = Cast<APlayerController>(It->GetController());
				if (PC && It->GetPlayerState()) { Players.Add(*It); }
			}
			if (Players.Num() != 2) { return false; }
			auto* Runtime = Server->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
			if (!Runtime || !Runtime->IsRuntimeInitialized()) { return false; }
			for (auto* Pawn : Players)
			{
				if (!CastChecked<APlayerController>(Pawn->GetController())->IsLocalController()) { Target = Pawn; break; }
			}
			if (!Target.IsValid()) { return Fail(TEXT("remote authority player missing")); }
			for (TActorIterator<AShooterNPC> It(Server); It; ++It) { It->Destroy(); }
			auto* Floor = Server->SpawnActor<AStaticMeshActor>();
			Floor->bAlwaysRelevant = true; Floor->SetReplicates(true); Floor->SetReplicateMovement(true);
			auto* Mesh = Floor->GetStaticMeshComponent(); Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetIsReplicated(true);
			Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Mesh->SetCollisionProfileName(TEXT("BlockAll"));
			Floor->SetActorScale3D(FVector(1000, 1000, 1)); Floor->SetActorLocation(FVector(0, 0, 9900));
			for (int32 Index = 0; Index < Players.Num(); ++Index)
			{
				Players[Index]->SetActorLocation(FVector(0, Index * 500, 10048), false, nullptr, ETeleportType::TeleportPhysics);
			}
#if DO_ENABLE_NET_TEST
			if (Mode == TEXT("Emulated"))
			{
				for (auto* World : Worlds)
				{
					FPacketSimulationSettings Packets; Packets.PktLag = 100; Packets.PktLoss = 2;
					World->GetNetDriver()->SetPacketSimulationSettings(Packets);
					Test->TestEqual(TEXT("Actual lag"), World->GetNetDriver()->PacketSimulationSettings.PktLag, 100);
					Test->TestEqual(TEXT("Actual loss"), World->GetNetDriver()->PacketSimulationSettings.PktLoss, 2);
				}
			}
#else
			if (Mode == TEXT("Emulated")) { return Fail(TEXT("network emulation unavailable")); }
#endif
			Weapon = Target->GetInventoryComponent()->FindWeaponByWeaponId(WeaponId);
			if (!Weapon.IsValid())
			{
				Weapon = Runtime->AcquireWeapon(WeaponId, Target.Get(), Target.Get());
				if (!Weapon.IsValid() || Target->GetInventoryComponent()->AddWeapon(Weapon.Get()) != EShooterInventoryAddResult::Added)
				{
					return Fail(TEXT("weapon grant failed"));
				}
			}
			Target->GetEquipmentComponent()->EquipWeapon(Weapon.Get()); Next(0); return false;
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
		void Key(AShooterCharacter* Pawn, FKey Key, bool bHeld)
		{
			CastChecked<APlayerController>(Pawn->GetController())->InputKey(FInputKeyEventArgs(nullptr,
				FInputDeviceId::CreateFromInternalId(0), Key, bHeld ? IE_Pressed : IE_Released,
				bHeld ? 1.0f : 0.0f, false, FPlatformTime::Cycles64()));
		}
		void Next(int32 Value)
		{
			Phase = Value; PhaseStarted = FPlatformTime::Seconds();
			UE_LOG(LogAutomationTestFramework, Display, TEXT("RIFLE_GAMEPLAY_STAGE weapon=%s mode=%s phase=%d"),
				*WeaponId.ToString(), *Mode, Phase);
		}
		void CheckNoShot()
		{
			Test->TestEqual(TEXT("No premature authority shot"), Weapon->GetAuthorityShotCountForAutomationTest(), Shots);
			Test->TestEqual(TEXT("No premature ammo consumption"), Weapon->GetBulletCount(), Ammo);
		}
		bool Finish()
		{
			const FString Folder = FPaths::ProjectSavedDir() / TEXT("RifleReadyIntegration") /
				FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"));
			IFileManager::Get().MakeDirectory(*Folder, true);
			FFileHelper::SaveStringToFile(Rows, *(Folder / (WeaponId.ToString() + TEXT("_") + Mode + TEXT(".csv"))));
			Test->AddInfo(FString::Printf(
				TEXT("RIFLE_GAMEPLAY_NETWORK weapon=%s mode=%s reverse=%d reload=%d errors=%d"),
				*WeaponId.ToString(),
				*Mode,
				bReverseSeen,
				bReloadSeen,
				Test->HasAnyErrors()));
			return true;
		}
		bool Fail(const TCHAR* Reason)
		{
			Test->AddError(FString::Printf(TEXT("RIFLE_GAMEPLAY_NETWORK_FAIL weapon=%s mode=%s phase=%d reason=%s"),
				*WeaponId.ToString(), *Mode, Phase, Reason)); return Finish();
		}
		FAutomationTestBase* Test;
		FString Mode; FName WeaponId;
		double Started, PhaseStarted = 0;
		int32 Phase = 0, Ammo = 0, Shots = 0, Feedback = 0;
		float BeforeReverse = 0;
		bool bReverseSeen = false, bReloadSeen = false;
		TWeakObjectPtr<AShooterCharacter> Target;
		TWeakObjectPtr<AShooterWeapon> Weapon;
		FString Rows = TEXT("phase,net,local,state,time,node_rate,asset_rate,sprint,speed,sprint_held,abort\r\n");
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FShooterRifleReadyNetworkPIETest, "ShootGame.Animation.RifleReady.GameplayNetwork",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FShooterRifleReadyNetworkPIETest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* WeaponId : {TEXT("Rifle"), TEXT("AWP"), TEXT("GrenadeLauncher")})
	{
		for (const TCHAR* Mode : {TEXT("Listen"), TEXT("Dedicated"), TEXT("Emulated")})
		{
			Names.Add(FString(WeaponId) + TEXT(".") + Mode); Commands.Add(FString(Mode) + TEXT("|") + WeaponId);
		}
	}
}

bool FShooterRifleReadyNetworkPIETest::RunTest(const FString& Parameters)
{
	using namespace RifleReadyNetworkPIE;
	FString Mode, WeaponName; Parameters.Split(TEXT("|"), &Mode, &WeaponName);
	auto* Play = GetMutableDefault<ULevelEditorPlaySettings>(); FSettings Original;
	Play->GetPlayNetMode(Original.Mode); Play->GetPlayNumberOfClients(Original.Clients);
	Play->GetRunUnderOneProcess(Original.bOneProcess); Original.bSeparateServer = Play->bLaunchSeparateServer;
	Play->SetPlayNetMode(Mode == TEXT("Listen") ? PIE_ListenServer : PIE_Client);
	Play->SetPlayNumberOfClients(2); Play->SetRunUnderOneProcess(true); Play->bLaunchSeparateServer = false;
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Shooter/Maps/Lvl_Shooter")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FValidate>(this, Mode, FName(*WeaponName)));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FRestore>(Original));
	return true;
}

#endif
