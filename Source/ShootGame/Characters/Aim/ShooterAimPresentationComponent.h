// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "ShooterAimPresentationComponent.generated.h"

class APawn;
class AShooterCharacter;

/**
 * 玩家瞄准表现采样、复制、远端平滑与调试组件。
 *
 * 权威边界：
 * - 本地拥有者按受限频率采样无散布视线目标；
 * - Unreliable Server RPC 只更新表现快照，不是命中缓存；
 * - PresentationAimTarget 使用 COND_SkipOwner，拥有者继续使用本地即时视角；
 * - Fire 命中路径始终由 AShooterCharacter::ComputePreSpreadAimTarget 现场重算。
 */
UCLASS(ClassGroup=(Aim), meta=(BlueprintSpawnableComponent))
class SHOOTGAME_API UShooterAimPresentationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShooterAimPresentationComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 本地拥有者启动受限频率的表现目标采样；可重复调用。 */
	void StartPresentationAimSampling();

	/** 观察端按武器切换等生命周期事件直接采用最新目标。 */
	void ResetPresentationAimSmoothing();

	/** 死亡等生命周期事件显式清空表现目标，不复用旧值。 */
	void ClearPresentationAimSmoothing();

	/** 只读访问服务器表现快照（拥有者保持零向量，观察端消费复制值）。 */
	const FVector& GetPresentationAimTarget() const { return PresentationAimTarget; }

	/** 只读访问观察端平滑后的表现目标。 */
	const FVector& GetSmoothedPresentationAimTarget() const { return SmoothedPresentationAimTarget; }

	/** 观察端本地有效标记。 */
	bool IsPresentationAimTargetValid() const { return bPresentationAimTargetValid; }

	/** 观察端平滑后的表现角度（局部坐标，度）——第三人称 AnimBP 数据契约入口。 */
	UFUNCTION(BlueprintCallable, Category = "Aim")
	void GetAimPresentationAngles(float& OutAimYaw, float& OutAimPitch) const;

	/** AO 就绪形式的垂直瞄准值（sin(radians(AimPitch))）。 */
	UFUNCTION(BlueprintCallable, Category = "Aim")
	float GetAimPitchN() const;

	/** 纯判定：PresentationAimTarget 是否可建立有效状态（有限且非零）。 */
	static bool IsValidPresentationAimTargetValue(const FVector& Target);

	/** 纯判定：本地表现目标是否越过发送门槛或到达保活间隔。 */
	static bool ShouldSubmitPresentationAimTarget(
		const FVector& NewTarget,
		const FVector& PreviousTarget,
		const FVector& ViewLocation,
		float SecondsSinceLastSubmit,
		float MinChangeDistance,
		float MinChangeAngle,
		float KeepAliveInterval);

	/** 纯判定：客户端提交的表现目标是否有限且位于允许的最大视距内。 */
	static bool IsClientPresentationAimTargetWithinBounds(
		const FVector& Target,
		const FVector& ServerViewLocation,
		float MaxDistance,
		float DistanceTolerance);

	/** 16 位递增序号比较，支持回绕并拒绝重复/过期 Unreliable RPC。 */
	static bool IsNewerPresentationAimSequence(uint16 Candidate, uint16 Previous);

	/** 纯判定：该 Role / NetMode / 本地控制组合是否应运行表现目标平滑。 */
	static bool ShouldRunPresentationAimSmoothing(ENetRole LocalRole, ENetMode NetMode, bool bLocallyControlled);

	/** 纯计算：从给定表现状态输出局部 AimYaw / AimPitch。 */
	static void ComputeAimPresentationAnglesForState(
		bool bUseSmoothedTarget,
		const FVector& ViewLocation,
		const FVector& SmoothedTarget,
		const FRotator& BaseAimRotation,
		const FTransform& MeshReferenceTransform,
		float& OutAimYaw,
		float& OutAimPitch);

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 服务器表现目标：只复制给非拥有者（COND_SkipOwner）。 */
	UPROPERTY(ReplicatedUsing = OnRep_PresentationAimTarget, VisibleAnywhere, BlueprintReadOnly, Category = "Aim")
	FVector_NetQuantize PresentationAimTarget = FVector::ZeroVector;

	UFUNCTION()
	void OnRep_PresentationAimTarget();

	/** 本地拥有者表现目标采样间隔（秒）；默认 20Hz。 */
	UPROPERTY(EditAnywhere, Category = "Aim", meta = (ClampMin = 0.02, Units = "s"))
	float PresentationAimSampleInterval = 0.05f;

	/** 表现目标变化门槛：目标位移小于该值且方向夹角小于门槛角度时跳过更新。 */
	UPROPERTY(EditAnywhere, Category = "Aim", meta = (ClampMin = 0, Units = "cm"))
	float PresentationAimMinChangeDistance = 20.0f;

	/** 表现目标变化门槛：方向夹角（度）。 */
	UPROPERTY(EditAnywhere, Category = "Aim", meta = (ClampMin = 0, ClampMax = 90, Units = "Degrees"))
	float PresentationAimMinChangeAngle = 1.0f;

	/** 即使目标未越过变化门槛，也按该间隔重新提交一次，帮助 Unreliable 路径从丢包中恢复。 */
	UPROPERTY(EditAnywhere, Category = "Aim", meta = (ClampMin = 0.05, Units = "s"))
	float PresentationAimKeepAliveInterval = 0.2f;

	/** 观察端平滑指数速率（1/s）。 */
	UPROPERTY(EditAnywhere, Category = "Aim", meta = (ClampMin = 0.0))
	float PresentationAimSmoothingRate = 20.0f;

	/** 本地拥有者表现目标采样定时器。 */
	FTimerHandle PresentationAimTimer;

	/** 本地拥有者采样一次表现目标；监听主机直接提交，客户端通过 Unreliable Server RPC 提交。 */
	void SamplePresentationAimTarget();

	/** 高频、可丢失的表现输入；只更新服务器表现快照。 */
	UFUNCTION(Server, Unreliable)
	void ServerUpdatePresentationAimTarget(FVector_NetQuantize NewTarget, uint16 ClientSequence);

	FVector LastSubmittedPresentationAimTarget = FVector::ZeroVector;
	float LastPresentationAimSubmitTime = -1.0f;
	uint16 NextPresentationAimSequence = 0;

	float LastAcceptedPresentationAimServerTime = -1.0f;
	uint16 LastAcceptedPresentationAimSequence = 0;
	bool bHasAcceptedPresentationAimSequence = false;

	/** 观察端当前平滑目标（本地成员，不复制）。 */
	FVector SmoothedPresentationAimTarget = FVector::ZeroVector;

	/** 观察端本地有效标记（不复制）；死亡等生命周期事件显式清空。 */
	bool bPresentationAimTargetValid = false;

	/** 观察端上一帧视点位置（传送/重生检测）。 */
	FVector LastPresentationAimViewLocation = FVector::ZeroVector;

	/** 观察端每帧平滑更新。 */
	void UpdatePresentationAimSmoothing(float DeltaSeconds);

	/** 绘制只读瞄准调试与指标。 */
	void DrawAimDebug() const;

	/** 以下为可测试运行上下文：测试子类可覆盖，不依赖真实 Pawn。 */
	virtual ENetRole GetPresentationLocalRole() const;
	virtual ENetMode GetPresentationNetMode() const;
	virtual bool IsPresentationOwnerLocallyControlled() const;
	virtual bool IsPresentationOwnerDead() const;
	virtual FVector GetPresentationPawnViewLocation() const;
	virtual FRotator GetPresentationBaseAimRotation() const;
	virtual FTransform GetPresentationMeshReferenceTransform() const;
	virtual float GetPresentationMaxAimDistance() const;
	virtual const APawn* GetPresentationPawn() const;

	/** 观察端平滑更新是否应在当前上下文运行。 */
	bool ShouldRunPresentationAimSmoothingForContext() const;
};
