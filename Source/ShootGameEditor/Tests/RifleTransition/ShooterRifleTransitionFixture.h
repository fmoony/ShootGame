// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "ShooterRifleTransitionFixture.generated.h"

class UCameraComponent;
class AShooterCharacter;
class UChildActorComponent;
class USkeletalMeshComponent;
struct FAnimInstanceProxy;
struct FAnimNode_SequencePlayer;

/** 隔离动画意图，不参与 Gameplay、输入映射或复制。 */
UENUM(BlueprintType)
enum class ERifleTransitionTestGoal : uint8
{
	ArmedAim,
	LowReady
};

/** 测试副本的数据源；生产 Native Update 后补齐没有 ShooterCharacter 的预览数据。 */
UCLASS(Blueprintable)
class SHOOTGAMEEDITOR_API UShooterRifleTransitionTestAnimInstance : public UShooterThirdPersonAnimInstance
{
	GENERATED_BODY()

public:
	/** 只驱动实验 WeaponAction。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rifle Test")
	ERifleTransitionTestGoal RifleTestGoal = ERifleTransitionTestGoal::ArmedAim;

	/** 按生产 sin(Pitch) 口径生成 AimPitchN。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rifle Test", meta = (ClampMin = "-60", ClampMax = "60"))
	float RifleTestPitch = 0.0f;

	/** 两处实验 LBB 共用的俯仰混合强度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rifle Test", meta = (ClampMin = "0", ClampMax = "1"))
	float RifleTestNaturalPitchStrength = 1.0f;

	/** 仅在实验夹具开关原有 IK，不改节点或资源。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rifle Test")
	bool bRifleTestIK = true;

	/** 正负一倍速；图中的动态 PlayRate 引脚消费该值。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rifle Test")
	float RifleTestPlayRate = 1.0f;

	/** 仅由 Sequence Player 的初始化读取；更新此输入不重置累计时间。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rifle Test")
	float RifleTestStartPosition = 0.0f;

	/** 有方向的显式过渡条件，反向出口不使用自动结束规则。 */
	UFUNCTION(BlueprintPure, Category = "Rifle Test", meta = (BlueprintThreadSafe))
	bool GetRifleTestLowerRequested() const;

	UFUNCTION(BlueprintPure, Category = "Rifle Test", meta = (BlueprintThreadSafe))
	bool GetRifleTestRaiseRequested() const;

	UFUNCTION(BlueprintPure, Category = "Rifle Test", meta = (BlueprintThreadSafe))
	bool GetRifleTestAtLowEndpoint() const;

	UFUNCTION(BlueprintPure, Category = "Rifle Test", meta = (BlueprintThreadSafe))
	bool GetRifleTestAtAimEndpoint() const;

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/** 只读诊断实际运行节点；不创建或替换 AnimNode。 */
	const FAnimNode_SequencePlayer* GetTransitionPlayer() const;
	static FAnimInstanceProxy& GetFixtureProxy(UAnimInstance* Instance);
	static bool ApplyFixtureSnapshot(UShooterThirdPersonAnimInstance* Instance, float Pitch, bool bIK);

	/** 仅诊断时在引擎求值作用域内观察真实节点，避免在 Cached Pose 生命周期外读取。 */
	TFunction<void(FAnimInstanceProxy&)> CapturePoseCallback;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
};

/** 无 Character AnimClass 修复和 Gameplay；真实 TP/FP 网格、Rifle 与 Socket 的独立预览夹具。 */
UCLASS(Blueprintable)
class SHOOTGAMEEDITOR_API AShooterRifleTransitionPreview : public AActor
{
	GENERATED_BODY()

public:
	AShooterRifleTransitionPreview();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

	/** 在关卡详情面板切换；不会生成 Gameplay 请求。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rifle Test")
	ERifleTransitionTestGoal RifleTestGoal = ERifleTransitionTestGoal::ArmedAim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rifle Test", meta = (ClampMin = "-60", ClampMax = "60"))
	float RifleTestPitch = 0.0f;

	/** 仅用于实验遮罩；运行中可用按钮调节，不重建动画实例。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rifle Test", meta = (ClampMin = "0", ClampMax = "1"))
	float RifleTestNaturalPitchStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rifle Test")
	bool bRifleTestIK = true;

	/** 仅选择两个隔离副本之一。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rifle Test")
	bool bAlwaysUpdateSourcePose = false;

	/** 允许对同一真实 Fire Montage 进行动画覆盖测试。 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Rifle Test")
	void PlayFireAnimationOnly();

	/** 运行中直接翻转目标，避免详情属性编辑触发 Actor 重建并中断实例。 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Rifle Test")
	void ToggleRifleTestGoal();

	/** 运行中按 0、30、60、-30、-60 循环俯仰，不重建组件。 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Rifle Test")
	void CycleRifleTestPitch();

	/** 运行中按 0、0.5、1 循环遮罩强度。 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Rifle Test")
	void CycleRifleTestNaturalPitchStrength();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rifle Test")
	TObjectPtr<USkeletalMeshComponent> ThirdPersonMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rifle Test")
	TObjectPtr<USkeletalMeshComponent> FirstPersonMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rifle Test")
	TObjectPtr<USkeletalMeshComponent> RifleMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rifle Test")
	TObjectPtr<USkeletalMeshComponent> FirstPersonRifleMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rifle Test")
	TObjectPtr<UCameraComponent> FirstPersonCamera;
};

/** 可附身的本地实验视角；组合既有夹具，不进入生产角色或网络流程。 */
UCLASS(Blueprintable)
class SHOOTGAMEEDITOR_API AShooterRifleTransitionDebugPawn : public APawn
{
	GENERATED_BODY()

public:
	AShooterRifleTransitionDebugPawn();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** 只包装已有实验 Actor，其 AnimBP、三态和 IK 隔离方式不变。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rifle Debug")
	TObjectPtr<UChildActorComponent> PreviewComponent;

	/** 第一人称默认开启；V 可切到外部观察。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rifle Debug")
	bool bThirdPersonView = false;

	/** HUD 同步显示最后一次实际按键操作。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rifle Debug")
	FString LastDebugAction = TEXT("Ready");

	UFUNCTION(BlueprintPure, Category = "Rifle Debug")
	AShooterRifleTransitionPreview* GetPreview() const;

	UFUNCTION(BlueprintPure, Category = "Rifle Debug")
	FString GetDebugText() const;

	UFUNCTION(BlueprintCallable, Category = "Rifle Debug")
	void ToggleGoal();

	UFUNCTION(BlueprintCallable, Category = "Rifle Debug")
	void CyclePitch();

	UFUNCTION(BlueprintCallable, Category = "Rifle Debug")
	void CycleStrength();

	UFUNCTION(BlueprintCallable, Category = "Rifle Debug")
	void ToggleView();

protected:
	virtual void BeginPlay() override;

private:
	void ConfigurePreview();
	void LookYaw(float Value);
	void LookPitch(float Value);
};

/** 正式角色子蓝图的本地实验控制；相机、输入与 FP 修正仍由继承的角色实现。 */
UCLASS(ClassGroup = (RifleTest), meta = (BlueprintSpawnableComponent))
class SHOOTGAMEEDITOR_API UShooterRifleCharacterDebugComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShooterRifleCharacterDebugComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 只写入实验动画实例的目标和遮罩强度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rifle Test")
	ERifleTransitionTestGoal Goal = ERifleTransitionTestGoal::ArmedAim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rifle Test", meta = (ClampMin = "0", ClampMax = "1"))
	float NaturalPitchStrength = 1.0f;

private:
	void SetDebugView(AShooterCharacter* Character);
	bool bThirdPersonView = false;
	bool bEquipAttempted = false;
	float LogElapsed = 0.0f;
	FString LastAction = TEXT("Ready");
};
