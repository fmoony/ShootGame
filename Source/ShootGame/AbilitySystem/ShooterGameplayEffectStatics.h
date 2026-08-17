// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayEffectTypes.h"
#include "ShooterGameplayEffectStatics.generated.h"

class UAbilitySystemComponent;
class UGameplayEffect;

/**
 * 以纯代码构建第一阶段所需的瞬时 GameplayEffect：
 * - 初始化生命：MaxHealth = 配置值，Health = MaxHealth；
 * - 伤害：Health 减去 SetByCaller 伤害值。
 * 不使用蓝图资产，保证服务器权威且可被自动化直接验证。
 */
UCLASS()
class SHOOTGAME_API UShooterGameplayEffectStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 服务器：初始化目标 ASC 的生命属性（Health = MaxHealth = 传入配置值）。 */
	static void ApplyInitHealthEffect(UAbilitySystemComponent* AbilitySystemComponent, float MaxHealthValue);

	/** 服务器：对目标 ASC 的 Health 施加伤害（SetByCaller）。 */
	static void ApplyDamageEffect(
		UAbilitySystemComponent* AbilitySystemComponent,
		float Damage,
		AController* Instigator,
		AActor* DamageCauser);

	/** SetByCaller 数据名：初始化时的 MaxHealth 值。 */
	static FName GetMaxHealthSetByCallerDataName();

	/** SetByCaller 数据名：初始化时的 Health 值。 */
	static FName GetHealthSetByCallerDataName();

	/** SetByCaller 数据名：伤害值。 */
	static FName GetDamageSetByCallerDataName();

private:
	/** 惰性创建并持有初始化效果（进程级单例，防止 GC）。 */
	static UGameplayEffect* GetOrCreateInitHealthEffect();

	/** 惰性创建并持有伤害效果（进程级单例，防止 GC）。 */
	static UGameplayEffect* GetOrCreateDamageEffect();
};
