// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShooterRifleReadyPreviewComponent.generated.h"

/** 可移除的网络姿态预览桥；只复制收枪目标，不参与 Gameplay 或射击结果。 */
UCLASS()
class SHOOTGAME_API UShooterRifleReadyPreviewComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShooterRifleReadyPreviewComponent();
	/** 临时复用 Rifle 动画的武器名单；本地入口、服务器与动画可用性共用。 */
	static bool SupportsWeapon(FName WeaponId);
	bool IsNetworkPreviewEnabled() const;
	bool GetLowReadyRequested() const { return bLowReadyRequested && !bCancelPending; }
	void ToggleNetworkPreview();
	void CancelNetworkPreview();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	/** 服务器确认的姿态目标；不复制播放器时间或预测 Shot。 */
	UPROPERTY(ReplicatedUsing = OnRep_LowReadyRequested)
	bool bLowReadyRequested = false;

	/** 本地动作中断立即释放姿态，等待一次服务器复位；不重复发送每帧 RPC。 */
	bool bCancelPending = false;

	UFUNCTION()
	void OnRep_LowReadyRequested();

	UFUNCTION(Server, Reliable)
	void ServerTogglePreview();

	UFUNCTION(Server, Reliable)
	void ServerCancelPreview();

	bool CanPreview() const;
	void SetServerGoal(bool bLowReady);
};
