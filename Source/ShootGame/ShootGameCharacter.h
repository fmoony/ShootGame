// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "Net/UnrealNetwork.h"
#include "ShootGameCharacter.generated.h"

class UInputComponent;
class USkeletalMeshComponent;
class UCameraComponent;
class UInputAction;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  A basic first person character
 */
UCLASS(abstract)
class AShootGameCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Pawn mesh: first person view (arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;

protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* MouseLookAction;
	
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* NetCounterAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* NetCounterTestLocalAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* OwnershipTestAction;

public:
	AShootGameCharacter();

protected:

	/** Called from Input Actions for movement input */
	void MoveInput(const FInputActionValue& Value);

	/** Called from Input Actions for looking input */
	void LookInput(const FInputActionValue& Value);

	/** Handles aim inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoAim(float Yaw, float Pitch);

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles jump start inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump end inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

protected:

	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;

	UFUNCTION(Server, Reliable)
	void ServerIncreaseCounter();

	UFUNCTION(Server, Reliable)
	void ServerBurstIncreaseCounter();

	UPROPERTY(ReplicatedUsing = OnRep_NetCounter, VisibleAnywhere, BlueprintReadOnly, Category = "NetWork")
	int32 NetCounter = 0;

	UFUNCTION()
	void OnRep_NetCounter();

	/** 只有服务器真正修改权威计数 */
	void IncreaseCounterAuthority();

	/** 服务器和客户端共用的表现更新 */
	void HandleNetCounterChanged(bool bOnRep = false);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void IncreaseCounterLocallyForTest();

	void TestServerRpcOnOtherPawn();

	UFUNCTION(Server, Reliable)
	void ServerRunRpcMatrixTest();

	UFUNCTION(Client, Reliable)
	void ClientRpcTest(int32 SourcePlayerId);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastRpcTest(int32 SourcePlayerId);

	UFUNCTION(Server, Reliable)
	void ServerStartReliabilityTest();

	UFUNCTION(Client, Reliable)
	void ClientReliableSample(int32 Sequence);

	UFUNCTION(Client, Unreliable)
	void ClientUnreliableSample(int32 Sequence);

	void SendNextReliabilitySample();

	FTimerHandle ReliabilityTestTimer;
	int32 ReliabilityTestSequence = 0;

public:

	/** Returns the first person mesh **/
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

	/** Returns first person camera component **/
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

private:
	void RequestIncreaseCounter();
};

