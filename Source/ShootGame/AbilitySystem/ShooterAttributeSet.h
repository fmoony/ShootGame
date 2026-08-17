// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "ShooterAttributeSet.generated.h"

/**
 * Shooter 基础属性集：Health 与 MaxHealth。
 * 玩家与 NPC 共享该类型（各自持有独立实例）。
 * 数值只由服务器通过 GameplayEffect 修改，客户端由 ASC（Mixed）复制收敛。
 */
UCLASS()
class SHOOTGAME_API UShooterAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UShooterAttributeSet();

	ATTRIBUTE_ACCESSORS_BASIC(UShooterAttributeSet, Health);
	ATTRIBUTE_ACCESSORS_BASIC(UShooterAttributeSet, MaxHealth);

protected:
	/** 效果执行后收敛数值：Health 夹在 [0, MaxHealth]。 */
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 拥有者客户端收到复制后的 Health。 */
	UFUNCTION()
	virtual void OnRep_Health(const FGameplayAttributeData& OldHealth);

	/** 拥有者客户端收到复制后的 MaxHealth。 */
	UFUNCTION()
	virtual void OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth);

	/** 当前生命。服务器权威修改，拥有者客户端通过属性集复制收敛。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Health, Category="Shooter|Health")
	FGameplayAttributeData Health;

	/** 最大生命。由初始化 GameplayEffect 写入。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxHealth, Category="Shooter|Health")
	FGameplayAttributeData MaxHealth;
};
