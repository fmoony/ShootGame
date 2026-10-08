#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "ShooterAnimNotify_Footstep.generated.h"

/** 移动落脚与跳跃落地事件；旧动画默认继续使用 Step。 */
UENUM(BlueprintType)
enum class EShooterFootstepEvent : uint8
{
	Step,
	Landing
};

/** TP 动画的落脚和落地声音；仅本机表现，不发送 RPC 或 AI 听觉事件。 */
UCLASS(meta = (DisplayName = "Shooter Footstep"))
class SHOOTGAME_API UShooterAnimNotify_Footstep : public UAnimNotify
{
	GENERATED_BODY()

public:
	/** Step 保留水平速度过滤；Landing 允许原地落地。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footsteps")
	EShooterFootstepEvent EventType = EShooterFootstepEvent::Step;

	/** 发声位置对应的脚骨骼；左右脚事件分别填写 foot_l、foot_r。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footsteps")
	FName FootBone = TEXT("foot_l");

	/** 每次落脚的音量；音高轻微随机变化。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footsteps", meta = (ClampMin = "0"))
	float Volume = 0.65f;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

#if WITH_EDITOR
	/** 仅编辑器：配置本事件的脚骨骼、名称、权重和 Dedicated 过滤，不替换事件数组。 */
	UFUNCTION(BlueprintCallable, Category = "Footsteps")
	bool ConfigureEditorEvent(FName InFootBone);
#endif

#if WITH_DEV_AUTOMATION_TESTS
	/** 只观察生产播放请求；无设备测试不能据此宣称听到声音。 */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FPlaybackObserved, USkeletalMeshComponent*, FName);
	static FPlaybackObserved PlaybackObserved;
#endif
};
