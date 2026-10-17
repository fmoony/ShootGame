// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ShooterCharacterMovementComponent.generated.h"

/** 走跑共用原生 CMC；只传输入意图，服务器自行校验速度和动作限制。 */
UCLASS()
class SHOOTGAME_API UShooterCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UShooterCharacterMovementComponent();

	/** 地面行走和跑步速度，单位 cm/s；不覆盖其它移动模式。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shooter|Movement", meta = (ClampMin = "0"))
	float WalkSpeed = 250.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shooter|Movement", meta = (ClampMin = "0"))
	float SprintSpeed = 600.0f;

	void SetSprintHeld(bool bHeld);
	void SetFireHeld(bool bHeld);
	void SetFireReadyPending(bool bPending);
	void ResetMovementIntents();
	void RefreshSprintState();
	bool IsSprintHeld() const { return bSprintHeld; }
	bool IsFireHeld() const { return bFireHeld; }
	bool IsFireReadyPending() const { return bFireReadyPending; }
	bool IsSprinting() const { return bIsSprinting; }
	bool CanSprint() const;
	bool IsSprintBlockedByServerAction() const { return bSprintBlockedByAction; }
	static bool SupportsLowReady(FName WeaponId);

#if WITH_DEV_AUTOMATION_TESTS
	int32 CorrectionReplayCount = 0;
	int32 CorrectionCount = 0;
#endif

	virtual float GetMaxSpeed() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual bool ClientUpdatePositionAfterServerUpdate() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;

private:
	/** 输入位随 SavedMove 保存；不能从服务器复制值反写拥有者的即时意图。 */
	bool bSprintHeld = false;
	bool bFireHeld = false;
	bool bFireReadyPending = false;

	/** 仅向模拟代理复制服务器确认的实际 Sprint；拥有者自行预测并由 CMC 纠正。 */
	UPROPERTY(Replicated)
	bool bIsSprinting = false;

	/** ServerOnly Equip 的短暂 Tag 不保证到达 Owner；复制移动门控，收到后停止越权预测。 */
	UPROPERTY(Replicated)
	bool bSprintBlockedByAction = false;

	bool HasBlockingAction() const;
};

/** 压缩位使用原生 ServerMove 协议，不另建 Toggle RPC 或自定义 NetSerialize。 */
class FShooterSavedMove : public FSavedMove_Character
{
public:
	using Super = FSavedMove_Character;
	bool bSprintHeld = false;
	bool bFireHeld = false;
	bool bFireReadyPending = false;
	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* Character, float DeltaSeconds, const FVector& NewAccel,
		FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* Character) override;
};
