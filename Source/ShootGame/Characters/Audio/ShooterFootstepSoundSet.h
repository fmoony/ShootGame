#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ShooterFootstepSoundSet.generated.h"

class USoundBase;
class USoundAttenuation;

/** 所有脚步 Notify 共用的地面音源配置；不保存角色或播放状态。 */
UCLASS(BlueprintType)
class SHOOTGAME_API UShooterFootstepSoundSet : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 混凝土音源；也是 Default、未知表面和缺失专属音源时的回退。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Footsteps|Concrete")
	TArray<TObjectPtr<USoundBase>> ConcreteSounds;

	/** 金属音源；没有有效引用时回退 Concrete。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Footsteps|Metal")
	TArray<TObjectPtr<USoundBase>> MetalSounds;

	/** 泥土音源；没有有效引用时回退 Concrete。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Footsteps|Dirt")
	TArray<TObjectPtr<USoundBase>> DirtSounds;

	/** 默认及混凝土地面的衰减；缺少专属衰减时也使用此配置。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Footsteps|Concrete")
	TObjectPtr<USoundAttenuation> ConcreteAttenuation;

	/** 金属地面的独立衰减，不随音源回退切换。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Footsteps|Metal")
	TObjectPtr<USoundAttenuation> MetalAttenuation;

	/** 泥土地面的独立衰减，不随音源回退切换。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Footsteps|Dirt")
	TObjectPtr<USoundAttenuation> DirtAttenuation;
};
