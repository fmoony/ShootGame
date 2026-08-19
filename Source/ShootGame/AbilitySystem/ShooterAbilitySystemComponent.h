// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "ShooterAbilitySystemComponent.generated.h"

struct FGameplayTag;
struct FGameplayAbilitySpec;
class UGameplayAbility;

/**
 * ShootGame 项目通用 ASC。
 *
 * 第一版只封装输入标签到 Ability Spec 的稳定入口：
 * 按下 / 松开按 Input Tag 查找 Spec，避免 Character 直接遍历 AbilitySpecContainer。
 */
UCLASS(ClassGroup=(Shooter))
class SHOOTGAME_API UShooterAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	/** 按下输入 Tag：记录 Spec.InputPressed，并尝试激活匹配的 Ability。 */
	void AbilityInputTagPressed(const FGameplayTag& InputTag);

	/** 松开输入 Tag：非权威端通过可靠 Server RPC 转发到服务器，服务器转发给活动 Ability。 */
	void AbilityInputTagReleased(const FGameplayTag& InputTag);

	/** 返回匹配输入 Tag 的第一个 Ability Spec；没有匹配时返回 nullptr。 */
	FGameplayAbilitySpec* FindAbilitySpecFromInputTag(const FGameplayTag& InputTag);

	/** 返回指定 Ability 类的 Spec 数量；宿主用它验证授予幂等性。 */
	int32 GetAbilitySpecCountForClass(TSubclassOf<UGameplayAbility> AbilityClass) const;

	/** 返回指定 Ability 类当前处于活动状态的 Spec 数量；网络测试用它验证单激活约束。 */
	int32 GetActiveAbilityCountForClass(TSubclassOf<UGameplayAbility> AbilityClass) const;

	/** 按输入 Tag 取消所有匹配 Ability；死亡、切枪、断线等清理链共用。 */
	void CancelAbilitiesByTag(const FGameplayTag& InputTag);
};
