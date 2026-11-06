// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/ShooterAIController.h"
#include "AI/ShooterNPC.h"
#include "Components/StateTreeAIComponent.h"
#include "Editor.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Misc/AutomationTest.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationEditorCommon.h"

DEFINE_LATENT_AUTOMATION_COMMAND_FOUR_PARAMETER(FRestoreShooterAIPlaySettings,
	EPlayNetMode, NetMode, int32, ClientCount, bool, bOneProcess, bool, bSeparateServer);

bool FRestoreShooterAIPlaySettings::Update()
{
	if (GEditor->PlayWorld)
	{
		return false;
	}
	ULevelEditorPlaySettings* Play = GetMutableDefault<ULevelEditorPlaySettings>();
	Play->SetPlayNetMode(NetMode);
	Play->SetPlayNumberOfClients(ClientCount);
	Play->SetRunUnderOneProcess(bOneProcess);
	Play->bLaunchSeparateServer = bSeparateServer;
	return true;
}

namespace ShooterAILifecyclePIE
{
	class FValidateLifecycle final : public IAutomationLatentCommand
	{
	public:
		explicit FValidateLifecycle(FAutomationTestBase& InTest) : Test(InTest) {}

		bool Update() override
		{
			UWorld* World = GEditor->PlayWorld;
			if (!World || !World->HasBegunPlay())
			{
				if (FPlatformTime::Seconds() < Deadline)
				{
					return false;
				}
				Test.AddError(TEXT("Lvl_Shooter PIE did not begin play"));
				return true;
			}

			AShooterNPC* NPC = nullptr;
			for (TActorIterator<AShooterNPC> It(World); It; ++It)
			{
				if (It->HasAuthority() && Cast<AShooterAIController>(It->GetController()))
				{
					NPC = *It;
					break;
				}
			}
			if (!NPC && FPlatformTime::Seconds() < Deadline)
			{
				return false;
			}
			if (!Test.TestNotNull(TEXT("Lvl_Shooter has an authoritative controlled NPC"), NPC))
			{
				return true;
			}

			AShooterAIController* Controller = CastChecked<AShooterAIController>(NPC->GetController());
			UStateTreeAIComponent* Tree = Controller->FindComponentByClass<UStateTreeAIComponent>();
			if (!Test.TestNotNull(TEXT("Controller owns a StateTree"), Tree))
			{
				return true;
			}

			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0);
			if (Test.TestNotNull(TEXT("Authority world has a navigation system"), Nav) &&
				Test.TestNotNull(TEXT("Standalone player pawn exists"), Player))
			{
				const FVector Extent(100.0f, 100.0f, 300.0f);
				const FNavAgentProperties& Agent = NPC->GetNavAgentPropertiesRef();
				FNavLocation Projected;
				Test.TestTrue(TEXT("NPC start projects to its agent's navmesh"),
					Nav->ProjectPointToNavigation(NPC->GetActorLocation(), Projected, Extent, &Agent));
				Test.TestTrue(TEXT("Player goal projects to the NPC agent's navmesh"),
					Nav->ProjectPointToNavigation(Player->GetActorLocation(), Projected, Extent, &Agent));

				const FVector Start = NPC->GetActorLocation();
				UNavigationPath* Route = UNavigationSystemV1::FindPathToLocationSynchronously(
					World, Start, Player->GetActorLocation(), Controller);
				Test.TestTrue(TEXT("Loaded Lvl_Shooter yields a complete NPC route"),
					Route && Route->IsValid() && !Route->IsPartial());
				UNavigationPath* Unreachable = UNavigationSystemV1::FindPathToLocationSynchronously(
					World, Start, Start + FVector(100000.0f), Controller);
				Test.TestTrue(TEXT("Goal outside the level has no complete route"),
					!Unreachable || !Unreachable->IsValid() || Unreachable->IsPartial());
			}

			Test.TestTrue(TEXT("Placed NPC starts its StateTree"), Tree->IsRunning());
			Test.TestTrue(TEXT("Running tree binds perception"), Controller->OnShooterPerceptionForgotten.IsBound());
			Controller->UnPossess();
			Test.TestFalse(TEXT("Unpossess stops the StateTree"), Tree->IsRunning());
			Test.TestFalse(TEXT("Stop unbinds the updated callback"), Controller->OnShooterPerceptionUpdated.IsBound());
			Test.TestFalse(TEXT("Stop unbinds the forgotten callback"), Controller->OnShooterPerceptionForgotten.IsBound());

			Controller->Possess(NPC);
			Test.TestTrue(TEXT("Repossessing restarts the StateTree"), Tree->IsRunning());
			Test.TestTrue(TEXT("New execution rebinds perception"), Controller->OnShooterPerceptionForgotten.IsBound());

			// 真实伤害入口，死亡后使用生产代码的计时销毁流程。
			NPC->TakeDamage(100000.0f, FDamageEvent(), nullptr, Controller);
			Test.TestTrue(TEXT("Authoritative damage kills the NPC"), NPC->IsDead());
			Test.TestNull(TEXT("Death releases possession"), NPC->GetController());
			Test.TestFalse(TEXT("Death stops StateTree"), Tree->IsRunning());
			Test.TestFalse(TEXT("Death unbinds perception"), Controller->OnShooterPerceptionForgotten.IsBound());

			// 另一个在运行的 NPC 直接销毁，覆盖不经过死亡委托的入口。
			for (TActorIterator<AShooterNPC> It(World); It; ++It)
			{
				if (It->HasAuthority() && *It != NPC)
				{
					AShooterAIController* OtherController = Cast<AShooterAIController>(It->GetController());
					if (!OtherController)
					{
						continue;
					}
					UStateTreeAIComponent* OtherTree = OtherController->FindComponentByClass<UStateTreeAIComponent>();
					if (Test.TestNotNull(TEXT("Second NPC has a StateTree"), OtherTree))
					{
						Test.TestTrue(TEXT("Second NPC is running before destruction"), OtherTree->IsRunning());
						It->Destroy();
						Test.TestFalse(TEXT("Direct destruction stops StateTree"), OtherTree->IsRunning());
						Test.TestFalse(TEXT("Direct destruction unbinds perception"),
							OtherController->OnShooterPerceptionForgotten.IsBound());
					}
					return true;
				}
			}
			Test.AddError(TEXT("Lvl_Shooter is missing a second authoritative NPC"));
			return true;
		}

	private:
		FAutomationTestBase& Test;
		const double Deadline = FPlatformTime::Seconds() + 45.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAILifecyclePIETest, "ShootGame.AI.StateTree.PIE.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAILifecyclePIETest::RunTest(const FString& Parameters)
{
	ULevelEditorPlaySettings* Play = GetMutableDefault<ULevelEditorPlaySettings>();
	EPlayNetMode NetMode = PIE_Standalone;
	int32 ClientCount = 1;
	bool bOneProcess = true;
	const bool bSeparateServer = Play->bLaunchSeparateServer;
	Play->GetPlayNetMode(NetMode);
	Play->GetPlayNumberOfClients(ClientCount);
	Play->GetRunUnderOneProcess(bOneProcess);
	Play->SetPlayNetMode(PIE_Standalone);
	Play->SetPlayNumberOfClients(1);
	Play->SetRunUnderOneProcess(true);
	Play->bLaunchSeparateServer = false;

	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Shooter/Maps/Lvl_Shooter")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<ShooterAILifecyclePIE::FValidateLifecycle>(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FRestoreShooterAIPlaySettings(NetMode, ClientCount, bOneProcess, bSeparateServer));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
