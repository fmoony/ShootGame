// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterAIController.h"
#include "AI/ShooterNPC.h"
#include "Components/StateTreeAIComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "AI/Navigation/PathFollowingAgentInterface.h"

AShooterAIController::AShooterAIController()
{
	// create the StateTree component
	StateTreeAI = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAI"));
	StateTreeAI->SetStartLogicAutomatically(false);

	// create the AI perception component. It will be configured in BP
	AIPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));

	// subscribe to the AI perception delegates
	AIPerception->OnTargetPerceptionUpdated.AddDynamic(this, &AShooterAIController::OnPerceptionUpdated);
	AIPerception->OnTargetPerceptionForgotten.AddDynamic(this, &AShooterAIController::OnPerceptionForgotten);
}

void AShooterAIController::BeginPlay()
{
	Super::BeginPlay();

	// 地图中预放置的 NPC 可能在 Controller BeginPlay 之前就已被占有。
	if (GetPawn() && !StateTreeAI->IsRunning())
	{
		StateTreeAI->StartLogic();
	}
}

void AShooterAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	AShooterNPC* NPC = Cast<AShooterNPC>(GetPawn());
	if (!NPC || NPC != InPawn)
	{
		return;
	}

	NPC->Tags.AddUnique(TeamTag);
	NPC->OnPawnDeath.AddUniqueDynamic(this, &AShooterAIController::OnPawnDeath);

	if (HasActorBegunPlay() && !StateTreeAI->IsRunning())
	{
		StateTreeAI->StartLogic();
	}
}

void AShooterAIController::OnUnPossess()
{
	if (AShooterNPC* NPC = Cast<AShooterNPC>(GetPawn()))
	{
		NPC->OnPawnDeath.RemoveDynamic(this, &AShooterAIController::OnPawnDeath);
	}

	// ExitState 依赖当前 Pawn 上下文，必须在基类清除占有关系前运行。
	StateTreeAI->StopLogic(TEXT("UnPossess"));
	ClearCurrentTarget();

	Super::OnUnPossess();
}

void AShooterAIController::OnPawnDeath()
{
	// stop movement
	GetPathFollowingComponent()->AbortMove(*this, FPathFollowingResultFlags::UserAbort);

	// unpossess the pawn
	UnPossess();

	// destroy this controller
	Destroy();
}

void AShooterAIController::SetCurrentTarget(AActor* Target)
{
	TargetEnemy = Target;
}

void AShooterAIController::ClearCurrentTarget()
{
	TargetEnemy = nullptr;
}

void AShooterAIController::OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	// pass the data to the StateTree delegate hook
	OnShooterPerceptionUpdated.ExecuteIfBound(Actor, Stimulus);
}

void AShooterAIController::OnPerceptionForgotten(AActor* Actor)
{
	// pass the data to the StateTree delegate hook
	OnShooterPerceptionForgotten.ExecuteIfBound(Actor);
}
