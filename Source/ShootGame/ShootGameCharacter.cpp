// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShootGameCharacter.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "ShootGameGameState.h"
#include "ShootGame.h"

AShootGameCharacter::AShootGameCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);
	
	// Create the first person mesh that will be viewed only by this character's owner
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));

	FirstPersonMesh->SetupAttachment(GetMesh());
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));

	// Create the Camera Component	
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	FirstPersonCameraComponent->SetupAttachment(FirstPersonMesh, FName("head"));
	FirstPersonCameraComponent->SetRelativeLocationAndRotation(FVector(-2.8f, 5.89f, 0.0f), FRotator(0.0f, 90.0f, -90.0f));
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
	FirstPersonCameraComponent->bEnableFirstPersonFieldOfView = true;
	FirstPersonCameraComponent->bEnableFirstPersonScale = true;
	FirstPersonCameraComponent->FirstPersonFieldOfView = 70.0f;
	FirstPersonCameraComponent->FirstPersonScale = 0.6f;

	// configure the character comps
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;

	GetCapsuleComponent()->SetCapsuleSize(34.0f, 96.0f);

	// Configure character movement
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;
	GetCharacterMovement()->AirControl = 0.5f;

	bReplicates = true;
}

void AShootGameCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{	
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AShootGameCharacter::DoJumpStart);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &AShootGameCharacter::DoJumpEnd);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AShootGameCharacter::MoveInput);

		// Looking/Aiming
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AShootGameCharacter::LookInput);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AShootGameCharacter::LookInput);
	
		EnhancedInputComponent->BindAction(NetCounterAction, ETriggerEvent::Started, this, &AShootGameCharacter::RequestIncreaseCounter);

		EnhancedInputComponent->BindAction(NetCounterTestLocalAction, ETriggerEvent::Started, this, &AShootGameCharacter::IncreaseCounterLocallyForTest);
	
		EnhancedInputComponent->BindAction(OwnershipTestAction, ETriggerEvent::Started, this, &AShootGameCharacter::TestServerRpcOnOtherPawn);
	}
	else
	{
		UE_LOG(LogShootGame, Error, TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AShootGameCharacter::OnRep_NetCounter()
{
	HandleNetCounterChanged(true);
}

void AShootGameCharacter::IncreaseCounterAuthority()
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("Only authority may modify NetCounter"));
		return;
	}

	++NetCounter;

	// OnRep主要响应客户端收到复制。
	// 服务器需要表现更新时，主动调用公共处理函数。
	HandleNetCounterChanged();
}

void AShootGameCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShootGameCharacter, NetCounter);
}

void AShootGameCharacter::ServerStartReliabilityTest_Implementation()
{
	ReliabilityTestSequence = 0;

	GetWorldTimerManager().SetTimer(
		ReliabilityTestTimer,
		this,
		&AShootGameCharacter::SendNextReliabilitySample,
		0.05f,
		true
	);
}

void AShootGameCharacter::ClientReliableSample_Implementation(
	int32 Sequence
)
{
	UE_LOG(LogTemp, Warning, TEXT("[Reliable] Sequence=%d"), Sequence);
}

void AShootGameCharacter::ClientUnreliableSample_Implementation(
	int32 Sequence
)
{
	UE_LOG(LogTemp, Warning, TEXT("[Unreliable] Sequence=%d"), Sequence);
}

void AShootGameCharacter::SendNextReliabilitySample()
{
	if (!HasAuthority())
	{
		return;
	}

	++ReliabilityTestSequence;

	ClientReliableSample(ReliabilityTestSequence);
	ClientUnreliableSample(ReliabilityTestSequence);

	if (ReliabilityTestSequence >= 50)
	{
		GetWorldTimerManager().ClearTimer(
			ReliabilityTestTimer
		);
	}
}

void AShootGameCharacter::RequestIncreaseCounter()
{
	//ServerStartReliabilityTest();

	//ServerRunRpcMatrixTest();
	//const APlayerState* PS = GetPlayerState();

	//MulticastRpcTest(PS->GetPlayerId());

	if (HasAuthority())
	{
		ServerBurstIncreaseCounter();
		//IncreaseCounterAuthority();
		return;
	}

	ServerBurstIncreaseCounter();
	//ServerIncreaseCounter();
}

void AShootGameCharacter::ServerIncreaseCounter_Implementation()
{
	IncreaseCounterAuthority();
}

void AShootGameCharacter::ServerBurstIncreaseCounter_Implementation()
{
	if (!HasAuthority())
	{
		return;
	}
	const APlayerState* PS = GetPlayerState();

	for (int32 Index = 0; Index < 10; ++Index)
	{
		++NetCounter;

		//UE_LOG(
		//	LogTemp,
		//	Warning,
		//	TEXT("[ServerWrite] Actor=%s PlayerId=%d This=%p Counter=%d"),
		//	*GetName(),
		//	PS ? PS->GetPlayerId() : INDEX_NONE,
		//	this,
		//	NetCounter
		//);
	}

	AShootGameGameState* GS = GetWorld()->GetGameState<AShootGameGameState>();
	if(IsValid(GS))
	{
		GS->AddMatchCounter(10);
	}
	else
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ServerWrite] Cannot modify MatchCounter, GameState is invalid")
		);
	}
}

void AShootGameCharacter::HandleNetCounterChanged(bool bOnRep /*= false*/)
{
	const FString LocalRoleName =
		UEnum::GetValueAsString(GetLocalRole());

	const FString RemoteRoleName =
		UEnum::GetValueAsString(GetRemoteRole());

	if (!bOnRep)
	{
		const APlayerState* PS = GetPlayerState();

		UE_LOG(
			LogTemp,
			Warning,
			TEXT(
				"World=%s Actor=%s This=%p "
				"PlayerId=%d Counter=%d Role=%s"
			),
			*GetWorld()->GetPackage()->GetName(),
			*GetName(),
			this,
			PS ? PS->GetPlayerId() : INDEX_NONE,
			NetCounter,
			*UEnum::GetValueAsString(GetLocalRole())
		);
	}
	else
	{
		const APlayerState* PS = GetPlayerState();

		UE_LOG(
			LogTemp,
			Warning,
			TEXT(
				"[OnRep] World=%s Actor=%s This=%p "
				"PlayerId=%d Counter=%d Role=%s"
			),
			*GetWorld()->GetPackage()->GetName(),
			*GetName(),
			this,
			PS ? PS->GetPlayerId() : INDEX_NONE,
			NetCounter,
			*UEnum::GetValueAsString(GetLocalRole())
		);
	}

	const AShootGameGameState* GS =
		GetWorld()->GetGameState<AShootGameGameState>();

	if (IsValid(GS))
	{
		const int32 CurrentCounter =
			GS->GetMatchCounter();

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Client reads MatchCounter=%d"),
			CurrentCounter
		);
	}
	else
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Client cannot read MatchCounter, GameState is invalid")
		);
	}

}

void AShootGameCharacter::IncreaseCounterLocallyForTest()
{
	if (HasAuthority())
	{
		return;
	}

	NetCounter += 100;

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[LocalOnly] %s Counter=%d Authority=%s LocalRole=%s"),
		*GetName(),
		NetCounter,
		HasAuthority() ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(GetLocalRole())
	);

	HandleNetCounterChanged();
}

void AShootGameCharacter::TestServerRpcOnOtherPawn()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	for (TActorIterator<AShootGameCharacter> It(GetWorld()); It; ++It)
	{
		AShootGameCharacter* TargetCharacter = *It;

		if (!IsValid(TargetCharacter))
		{
			continue;
		}

		if (TargetCharacter == this)
		{
			continue;
		}

		// 在当前客户端中，另一个玩家的 Pawn 应该是 SimulatedProxy。
		if (TargetCharacter->GetLocalRole() != ROLE_SimulatedProxy)
		{
			continue;
		}

		const APlayerState* TargetPlayerState =
			TargetCharacter->GetPlayerState();

		const int32 TargetPlayerId =
			TargetPlayerState
			? TargetPlayerState->GetPlayerId()
			: INDEX_NONE;

		UE_LOG(
			LogTemp,
			Warning,
			TEXT(
				"[OwnershipTest:Attempt] Caller=%s "
				"Target=%s TargetPlayerId=%d "
				"TargetRole=%s TargetLocallyControlled=%s"
			),
			*GetName(),
			*TargetCharacter->GetName(),
			TargetPlayerId,
			*UEnum::GetValueAsString(
				TargetCharacter->GetLocalRole()
			),
			TargetCharacter->IsLocallyControlled()
			? TEXT("true")
			: TEXT("false")
		);

		// 必须调用 RPC 声明函数，而不是 _Implementation。
		TargetCharacter->ServerIncreaseCounter();

		return;
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[OwnershipTest] No SimulatedProxy target found")
	);
}

void AShootGameCharacter::ServerRunRpcMatrixTest_Implementation()
{
	const APlayerState* PS = GetPlayerState();
	const int32 PlayerId =
		PS ? PS->GetPlayerId() : INDEX_NONE;

	UE_LOG(
		LogTemp,
		Warning,
		TEXT(
			"[ServerRPC] World=%s Actor=%s PlayerId=%d "
			"Role=%s This=%p"
		),
		*GetWorld()->GetPackage()->GetName(),
		*GetName(),
		PlayerId,
		*UEnum::GetValueAsString(GetLocalRole()),
		this
	);

	ClientRpcTest(PlayerId);
	MulticastRpcTest(PlayerId);
}

void AShootGameCharacter::ClientRpcTest_Implementation(
	int32 SourcePlayerId
)
{
	UE_LOG(
		LogTemp,
		Warning,
		TEXT(
			"[ClientRPC] World=%s Actor=%s PlayerId=%d "
			"Role=%s This=%p"
		),
		*GetWorld()->GetPackage()->GetName(),
		*GetName(),
		SourcePlayerId,
		*UEnum::GetValueAsString(GetLocalRole()),
		this
	);
}

void AShootGameCharacter::MulticastRpcTest_Implementation(
	int32 SourcePlayerId
)
{
	UE_LOG(
		LogTemp,
		Warning,
		TEXT(
			"[MulticastRPC] World=%s Actor=%s PlayerId=%d "
			"Role=%s This=%p"
		),
		*GetWorld()->GetPackage()->GetName(),
		*GetName(),
		SourcePlayerId,
		*UEnum::GetValueAsString(GetLocalRole()),
		this
	);
}

void AShootGameCharacter::MoveInput(const FInputActionValue& Value)
{
	// get the Vector2D move axis
	FVector2D MovementVector = Value.Get<FVector2D>();

	// pass the axis values to the move input
	DoMove(MovementVector.X, MovementVector.Y);

}

void AShootGameCharacter::LookInput(const FInputActionValue& Value)
{
	// get the Vector2D look axis
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// pass the axis values to the aim input
	DoAim(LookAxisVector.X, LookAxisVector.Y);

}

void AShootGameCharacter::DoAim(float Yaw, float Pitch)
{
	if (GetController())
	{
		// pass the rotation inputs
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AShootGameCharacter::DoMove(float Right, float Forward)
{
	if (GetController())
	{
		// pass the move inputs
		AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void AShootGameCharacter::DoJumpStart()
{
	// pass Jump to the character
	Jump();
}

void AShootGameCharacter::DoJumpEnd()
{
	// pass StopJumping to the character
	StopJumping();
}
