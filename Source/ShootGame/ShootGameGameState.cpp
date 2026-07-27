// Fill out your copyright notice in the Description page of Project Settings.


#include "ShootGameGameState.h"
#include "Net/UnrealNetwork.h"

void AShootGameGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShootGameGameState, MatchCounter);
}

void AShootGameGameState::AddMatchCounter(int32 Delta)
{
	if(!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[GameState] Client cannot modify MatchCounter"));
		return;
	}

	MatchCounter += Delta;

	LogMatchCounter(TEXT("ServerWrite"));
}

void AShootGameGameState::OnRep_MatchCounter()
{
    LogMatchCounter(TEXT("OnRep"));
}

void AShootGameGameState::LogMatchCounter(const TCHAR* Source) const
{
    UE_LOG(
        LogTemp,
        Warning,
        TEXT(
            "[GameState:%s] World=%s Actor=%s This=%p "
            "Counter=%d Authority=%s Role=%s"
        ),
        Source,
        *GetWorld()->GetPackage()->GetName(),
        *GetName(),
        this,
        MatchCounter,
        HasAuthority() ? TEXT("true") : TEXT("false"),
        *UEnum::GetValueAsString(GetLocalRole())
    );
}


