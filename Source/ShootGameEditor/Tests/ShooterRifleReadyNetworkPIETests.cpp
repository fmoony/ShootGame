// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/ShooterNPC.h"
#include "Characters/Animation/ShooterRifleReadyPreviewComponent.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
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
		int32 Preview;
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
			IConsoleManager::Get().FindConsoleVariable(TEXT("ShootGame.RifleReady.NetPreview"))->Set(Saved.Preview);
			return true;
		}
	private:
		FSettings Saved;
	};

	class FValidate : public IAutomationLatentCommand
	{
	public:
		FValidate(FAutomationTestBase* InTest, FString InMode)
			: Test(InTest), Mode(MoveTemp(InMode)), Started(FPlatformTime::Seconds()) {}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 100.0) { return Fail(TEXT("network preview timeout")); }
			if (!bPrepared) { return Prepare(); }
			if (!Target.IsValid() || !Weapon.IsValid()) { return Fail(TEXT("stable target / weapon disappeared")); }
			TArray<AShooterCharacter*> Copies = FindCopies();
			AShooterCharacter* Owner = nullptr;
			int32 Views = 0;
			bool bAllLow = true, bAllAim = true;
			for (AShooterCharacter* Copy : Copies)
			{
				// 仅冻结 PIE 取证角色的移动，避免地图边缘跌落导致稳定对象消失。
				Copy->GetCharacterMovement()->DisableMovement();
				if (Copy->IsLocallyControlled()) { Owner = Copy; }
				auto* Mesh = Copy->GetMesh();
				Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				Mesh->bEnableUpdateRateOptimizations = false;
				auto* Anim = Cast<UShooterThirdPersonAnimInstance>(Mesh->GetAnimInstance());
				auto* Preview = Copy->FindComponentByClass<UShooterRifleReadyPreviewComponent>();
				const auto* State = Anim ? Anim->GetStateMachineInstanceFromName(TEXT("RifleReady")) : nullptr;
				const auto* Player = Anim ? Anim->GetRifleTransitionPlayer() : nullptr;
				const auto* Equipped = Copy->GetCurrentWeaponActor();
				if (!Preview || !Equipped || Equipped->GetWeaponId() != TEXT("Rifle"))
				{
					return false;
				}
				if (Copy->GetNetMode() == NM_DedicatedServer) { continue; }
				if (!Anim || !State || !Player) { return false; }
				++Views;
				const bool bExpectedGoal = Mode == TEXT("Off") ? Copy->IsLocallyControlled() : true;
				bAllLow &= State->GetCurrentStateName() == (bExpectedGoal ? TEXT("LowReady") : TEXT("ArmedAim"));
				bAllAim &= State->GetCurrentStateName() == TEXT("ArmedAim");
				Rows += FString::Printf(TEXT("%s,%d,%.6f,%d,%d,%d,%s,%.9f,%.3f,%d,%d,%.3f\r\n"),
					*Mode,
					Phase,
					FPlatformTime::Seconds() - Started,
					Copy->GetWorld()->GetNetMode(),
					Copy->GetLocalRole(),
					Copy->IsLocallyControlled(),
					*State->GetCurrentStateName().ToString(),
					Player->GetAccumulatedTime(),
					Player->GetPlayRate(),
					Preview->GetLowReadyRequested(),
					Anim->bDebugRifleLowReady,
					Anim->AimPitchDegrees);
			}
			if (!Owner || Views != 2 || Copies.Num() != (Mode == TEXT("Dedicated") ? 3 : 2)) { return false; }
			const double Elapsed = FPlatformTime::Seconds() - PhaseStarted;
			auto* Anim = CastChecked<UShooterThirdPersonAnimInstance>(Owner->GetMesh()->GetAnimInstance());
			const auto* Player = Anim->GetRifleTransitionPlayer();
			const auto* State = Anim->GetStateMachineInstanceFromName(TEXT("RifleReady"));
			if (bRelease)
			{
				Input(Owner, IE_Released);
				bRelease = false;
			}
			if (Phase == 0 && bAllAim && Elapsed > 1.0)
			{
				Test->TestTrue(TEXT("Actual owning client with server connection"),
					Owner->GetNetMode() == NM_Client && Owner->GetWorld()->GetNetDriver()->ServerConnection != nullptr);
				BaselineAmmo = Weapon->GetBulletCount();
				BaselineShots = Weapon->GetAuthorityShotCountForAutomationTest();
				Input(Owner, IE_Pressed);
				Next(1);
			}
			else if (Phase == 1 && bAllLow)
			{
				Test->TestEqual(TEXT("Server owns final pose target"), ServerGoal(), Mode != TEXT("Off"));
				CheckNoShots();
				Input(Owner, IE_Pressed);
				Next(2);
			}
			else if (Phase == 2 && bAllAim && Elapsed > 0.3)
			{
				CheckNoShots();
				if (Mode == TEXT("Off")) { return Finish(); }
				CastChecked<APlayerController>(Owner->GetController())->SetControlRotation(FRotator(-60, 0, 0));
				Input(Owner, IE_Pressed);
				Next(3);
			}
			else if (Phase == 3 && State->GetCurrentStateName() == TEXT("AimTransition") &&
				Player->GetAccumulatedTime() >= 0.25f && Player->GetAccumulatedTime() < 0.65f)
			{
				Input(Owner, IE_Pressed);
				PreviousTime = Player->GetAccumulatedTime();
				Next(4);
			}
			else if (Phase == 4)
			{
				if (Player->GetPlayRate() < 0 && Player->GetAccumulatedTime() > 0 &&
					Player->GetAccumulatedTime() < PreviousTime && State->GetCurrentStateName() == TEXT("AimTransition"))
				{
					bReverseSeen = true;
				}
				PreviousTime = Player->GetAccumulatedTime();
				if (bAllAim && Elapsed > 0.3)
				{
					Test->TestTrue(TEXT("Actual mid-transition reverse keeps time"), bReverseSeen);
					CheckNoShots();
					Input(Owner, IE_Pressed);
					Next(5);
				}
			}
			else if (Phase == 5 && bAllLow)
			{
				// 非拥有者通过同一个蓝图入口尝试影响目标，不能改变服务器状态。
				for (AShooterCharacter* Copy : Copies)
				{
					if (!Copy->IsLocallyControlled()) { UShooterThirdPersonAnimInstance::ToggleDebugRifleLowReady(Copy); }
				}
				Next(6);
			}
			else if (Phase == 6 && Elapsed > 0.4)
			{
				Test->TestTrue(TEXT("Foreign preview entry cannot toggle authoritative target"), ServerGoal());
				CheckNoShots();
				Owner->DoStartFiring();
				Next(7);
			}
			else if (Phase == 7)
			{
				if (Elapsed > 0.03) { Owner->DoStopFiring(); }
				if (Elapsed > 1.5 && bAllAim && !ServerGoal())
				{
					Test->TestTrue(TEXT("Existing network Fire still commits on authority"),
						Weapon->GetAuthorityShotCountForAutomationTest() > BaselineShots);
					Input(Owner, IE_Pressed);
					Next(8);
				}
			}
			else if (Phase == 8 && bAllLow)
			{
				Owner->DoReload();
				Next(9);
			}
			else if (Phase == 9)
			{
				const auto* Action = Anim->GetStateMachineInstanceFromName(TEXT("WeaponAction"));
				bReloadSeen |= Action && Action->GetCurrentStateName() == TEXT("Reload");
				if (Elapsed > 3.5 && bAllAim && !ServerGoal())
				{
					Test->TestTrue(TEXT("Reload keeps original state and clears network preview"), bReloadSeen);
					if (Mode == TEXT("Dedicated")) { return Finish(); }
					for (AShooterCharacter* C : AuthorityPlayers)
					{
						if (C->IsLocallyControlled()) { Target = C; break; }
					}
					Weapon = Target->GetCurrentWeaponActor();
					BaselineAmmo = Weapon->GetBulletCount();
					BaselineShots = Weapon->GetAuthorityShotCountForAutomationTest();
					Input(Target.Get(), IE_Pressed);
					Next(10);
				}
			}
			else if (Phase == 10 && bAllLow)
			{
				Test->TestTrue(TEXT("Listen host has authoritative local animation"), Owner->HasAuthority());
				CheckNoShots();
				Input(Owner, IE_Pressed);
				Next(11);
			}
			else if (Phase == 11 && bAllAim && Elapsed > 0.3) { CheckNoShots(); return Finish(); }
			return false;
		}

	private:
		bool Prepare()
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (Context.WorldType != EWorldType::PIE || !World || World->GetNetMode() == NM_Client) { continue; }
				AuthorityPlayers.Reset();
				for (TActorIterator<AShooterCharacter> It(World); It; ++It)
				{
					if (Cast<APlayerController>(It->GetController()) && It->GetPlayerState()) { AuthorityPlayers.Add(*It); }
				}
				if (AuthorityPlayers.Num() != 2 || !World->GetNetDriver()) { continue; }
				const int32 ExpectedConnections = Mode == TEXT("Dedicated") ? 2 : 1;
				if (World->GetNetDriver()->ClientConnections.Num() != ExpectedConnections) { continue; }
				auto* Runtime = World->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
				if (!Runtime || !Runtime->IsRuntimeInitialized()) { continue; }
				for (TActorIterator<AShooterNPC> It(World); It; ++It) { It->Destroy(); }
				for (int32 I = 0; I < AuthorityPlayers.Num(); ++I)
				{
					auto* C = AuthorityPlayers[I];
					auto* Rifle = C->GetInventoryComponent()->FindWeaponByWeaponId(TEXT("Rifle"));
					if (!Rifle)
					{
						Rifle = Runtime->AcquireWeapon(TEXT("Rifle"), C, C);
						if (!Rifle || C->GetInventoryComponent()->AddWeapon(Rifle) != EShooterInventoryAddResult::Added)
						{
							return Fail(TEXT("real Rifle grant failed"));
						}
					}
					C->GetEquipmentComponent()->EquipWeapon(Rifle);
					C->GetCharacterMovement()->DisableMovement();
					C->SetActorLocation(FVector(-534, 1506 + I * 400, 98.21), false, nullptr, ETeleportType::TeleportPhysics);
					if (!C->IsLocallyControlled()) { Target = C; }
				}
				Weapon = Target->GetCurrentWeaponActor();
#if DO_ENABLE_NET_TEST
				if (Mode == TEXT("Emulated"))
				{
					for (const FWorldContext& C : GEngine->GetWorldContexts())
					{
						if (C.WorldType != EWorldType::PIE || !C.World() || !C.World()->GetNetDriver()) { continue; }
						FPacketSimulationSettings Packets;
						Packets.PktLag = 80;
						Packets.PktLoss = 2;
						C.World()->GetNetDriver()->SetPacketSimulationSettings(Packets);
					}
				}
#endif
				bPrepared = true;
				Next(0);
				Test->AddInfo(FString::Printf(TEXT("RIFLE_NET_START mode=%s server_mode=%d connections=%d"),
					*Mode, World->GetNetMode(), ExpectedConnections));
				return false;
			}
			return false;
		}

		TArray<AShooterCharacter*> FindCopies() const
		{
			TArray<AShooterCharacter*> Result;
			const int32 Id = Target->GetPlayerState()->GetPlayerId();
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType != EWorldType::PIE || !Context.World()) { continue; }
				for (TActorIterator<AShooterCharacter> It(Context.World()); It; ++It)
				{
					if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId() == Id) { Result.Add(*It); }
				}
			}
			return Result;
		}
		bool ServerGoal() const
		{
			return Target->FindComponentByClass<UShooterRifleReadyPreviewComponent>()->GetLowReadyRequested();
		}
		void Input(AShooterCharacter* Owner, EInputEvent Event)
		{
			auto* PC = CastChecked<APlayerController>(Owner->GetController());
			const FInputDeviceId Device = FInputDeviceId::CreateFromInternalId(0);
			PC->InputKey(FInputKeyEventArgs(nullptr, Device, EKeys::K, Event, Event == IE_Pressed ? 1.0f : 0.0f,
				false, FPlatformTime::Cycles64()));
			bRelease = Event == IE_Pressed;
		}
		void Next(int32 Value) { Phase = Value; PhaseStarted = FPlatformTime::Seconds(); }
		void CheckNoShots()
		{
			Test->TestEqual(TEXT("K does not consume authority ammo"), Weapon->GetBulletCount(), BaselineAmmo);
			Test->TestEqual(TEXT("K has zero extra authority shots"), Weapon->GetAuthorityShotCountForAutomationTest(), BaselineShots);
		}
		bool Finish()
		{
			const FString Folder = FPaths::ProjectSavedDir() / TEXT("RifleReadyNetworkPreview_20261013");
			IFileManager::Get().MakeDirectory(*Folder, true);
			FFileHelper::SaveStringToFile(Rows, *(Folder / (Mode + TEXT(".csv"))));
			Test->AddInfo(FString::Printf(TEXT("RIFLE_NET_RESULT mode=%s errors=%d reverse=%d reload=%d"),
				*Mode, Test->HasAnyErrors(), bReverseSeen, bReloadSeen));
			return true;
		}
		bool Fail(const TCHAR* Reason)
		{
			Test->AddError(FString::Printf(TEXT("RIFLE_NET_FAIL mode=%s phase=%d reason=%s"), *Mode, Phase, Reason));
			Finish();
			return true;
		}
		FAutomationTestBase* Test;
		FString Mode;
		double Started, PhaseStarted = 0;
		int32 Phase = 0, BaselineAmmo = 0, BaselineShots = 0;
		float PreviousTime = 0;
		bool bPrepared = false, bRelease = false, bReverseSeen = false, bReloadSeen = false;
		TWeakObjectPtr<AShooterCharacter> Target;
		TWeakObjectPtr<AShooterWeapon> Weapon;
		TArray<AShooterCharacter*> AuthorityPlayers;
		FString Rows = TEXT("mode,phase,seconds,net_mode,role,local,state,time,rate,component_goal,anim_goal,pitch\r\n");
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FShooterRifleReadyNetworkPIETest, "ShootGame.Animation.RifleReady.NetworkPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FShooterRifleReadyNetworkPIETest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Mode : {TEXT("Listen"), TEXT("Dedicated"), TEXT("Emulated"), TEXT("Off")})
	{
		Names.Add(Mode);
		Commands.Add(Mode);
	}
}

bool FShooterRifleReadyNetworkPIETest::RunTest(const FString& Mode)
{
	using namespace RifleReadyNetworkPIE;
	auto* Play = GetMutableDefault<ULevelEditorPlaySettings>();
	FSettings Original;
	Play->GetPlayNetMode(Original.Mode);
	Play->GetPlayNumberOfClients(Original.Clients);
	Play->GetRunUnderOneProcess(Original.bOneProcess);
	Original.bSeparateServer = Play->bLaunchSeparateServer;
	auto* Toggle = IConsoleManager::Get().FindConsoleVariable(TEXT("ShootGame.RifleReady.NetPreview"));
	Original.Preview = Toggle->GetInt();
	Toggle->Set(Mode == TEXT("Off") ? 0 : 1);
	Play->SetPlayNetMode(Mode == TEXT("Dedicated") ? PIE_Client : PIE_ListenServer);
	Play->SetPlayNumberOfClients(2);
	Play->SetRunUnderOneProcess(true);
	Play->bLaunchSeparateServer = false;
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Shooter/Maps/Lvl_Shooter")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FValidate>(this, Mode));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FRestore>(Original));
	return true;
}

#endif
