#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "UObject/SoftObjectPtr.h"
#include "ShooterStancePreview.generated.h"

class UAnimInstance;
class USkeletalMeshComponent;
class UCameraComponent;

/** 隔离动画实验：C++ 仅提供输入、场景移动、相机和只读采集，姿态全部由 AnimBP 求值。 */
UCLASS()
class SHOOTGAMEEDITOR_API AShooterStancePreviewPawn : public APawn
{
	GENERATED_BODY()

public:
	AShooterStancePreviewPawn();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
	virtual void SetupPlayerInputComponent(UInputComponent* Input) override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> Body;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> Rifle;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> FirstPersonArms;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> FirstPersonRifle;

	/** AnimBP 事件图只读这些输入；C++ 不访问或写入动画图输出。 */
	UPROPERTY(BlueprintReadOnly, Category = "Experiment")
	int32 PreviewMode = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Experiment")
	float PreviewForward = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Experiment")
	float PreviewRight = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Experiment")
	float PreviewPitch = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Experiment")
	float PreviewYaw = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Experiment")
	bool bPreviewLowReady = false;

	UPROPERTY(BlueprintReadOnly, Category = "Experiment")
	bool bPreviewLowerOnly = false;

	/** 站蹲输入平滑参数，不参与任何骨骼求值。 */
	UPROPERTY(BlueprintReadOnly, Category = "Experiment")
	float PreviewCrouchAlpha = 1;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	void Stand();
	void Crouch();
	void Walk();
	void Slide();
	void ToggleReady();
	void ToggleView();
	void TogglePause();
	void ToggleDirectionDemo();
	void ToggleLowerOnly();
	void LookYaw(float Value);
	void LookPitch(float Value);
	void UpdatePresentation();
	void TickCaptureInputs(float DeltaSeconds);
	void CaptureFrame();
	void TickFourDirection(float DeltaSeconds);
	void CaptureFourDirectionFrame();

	int32 View = 0;
	bool bPaused = false;
	bool bDirectionDemo = false;
	bool bCapture = false;
	bool bPitchSweep = false;
	bool bTransitionCapture = false;
	bool bBaseline = false;
	bool bFourDirection = false;
	FName MuzzleSocketName;
	int32 CaptureDirections = 8;
	bool bStationary = false;
	bool bCaptureComplete = false;
	int32 CaptureCase = 0;
	int32 CaptureFrameIndex = 0;
	double CaptureClock = 0;
	double CaseClock = 0;
	float SlideClock = 0;
	FString CaptureOutput;
	FString CaptureReport;
	FString CaptureViewReport;
};

/** 仅隔离地图使用，不修改项目默认 GameMode。 */
UCLASS()
class SHOOTGAMEEDITOR_API AShooterStancePreviewGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AShooterStancePreviewGameMode();
};

/** 四方向隔离地图显式选择此 GameMode，复用原 Stance 场景与输入夹具。 */
UCLASS()
class SHOOTGAMEEDITOR_API AShooterFourDirectionPreviewGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AShooterFourDirectionPreviewGameMode();

	/** 地图明确指定初始动画类，Pawn 不再先加载其他实验的动画图。 */
	UPROPERTY(EditDefaultsOnly, Category = "Experiment")
	TSoftClassPtr<UAnimInstance> PreviewAnimClass;
};

/** 只创建原生 AnimBP 资产；不采样、加工或写入任何动画骨骼轨道。 */
UCLASS()
class SHOOTGAMEEDITOR_API UShooterStancePreviewCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UShooterStancePreviewCommandlet();
	virtual int32 Main(const FString& Params) override;
};
