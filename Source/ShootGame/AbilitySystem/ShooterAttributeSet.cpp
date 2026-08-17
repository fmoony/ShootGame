// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterAttributeSet.h"

#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UShooterAttributeSet::UShooterAttributeSet()
{
	// 未初始化前保持零值；出生生命由初始化 GameplayEffect 写入。
}

void UShooterAttributeSet::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 属性数值由属性集作为复制子对象同步；注册到 ASC 时按复制模式重写条件
	// （Mixed：仅拥有者客户端收到属性数值）。
	DOREPLIFETIME_CONDITION_NOTIFY(UShooterAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UShooterAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
}

void UShooterAttributeSet::OnRep_Health(const FGameplayAttributeData& OldHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UShooterAttributeSet, Health, OldHealth);
}

void UShooterAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UShooterAttributeSet, MaxHealth, OldMaxHealth);
}

void UShooterAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		// 生命值收敛到 [0, MaxHealth]，保证伤害与死亡判定确定。
		SetHealth(FMath::Clamp(GetHealth(), 0.0f, GetMaxHealth()));
	}
}
