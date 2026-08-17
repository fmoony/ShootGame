// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterGameplayEffectStatics.h"

#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "ShooterAttributeSet.h"

namespace ShooterGameplayEffectStaticsImpl
{
	const FName MaxHealthDataName(TEXT("SetByCaller.MaxHealth"));
	const FName HealthDataName(TEXT("SetByCaller.Health"));
	const FName DamageDataName(TEXT("SetByCaller.Damage"));

	FGameplayModifierInfo MakeModifier(
		const FGameplayAttribute& Attribute,
		EGameplayModOp::Type ModifierOp,
		const FName& SetByCallerDataName)
	{
		FGameplayModifierInfo Modifier;
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = ModifierOp;

		// 使用 DataName 路径：不依赖 GameplayTag 注册，运行时安全。
		FSetByCallerFloat SetByCallerMagnitude;
		SetByCallerMagnitude.DataName = SetByCallerDataName;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCallerMagnitude);
		return Modifier;
	}

	UGameplayEffect* CreateInitHealthEffect()
	{
		UGameplayEffect* Effect = NewObject<UGameplayEffect>(
			GetTransientPackage(),
			TEXT("ShooterInitHealthEffect"));
		Effect->DurationPolicy = EGameplayEffectDurationType::Instant;
		Effect->Modifiers.Add(MakeModifier(
			UShooterAttributeSet::GetMaxHealthAttribute(),
			EGameplayModOp::Override,
			MaxHealthDataName));
		Effect->Modifiers.Add(MakeModifier(
			UShooterAttributeSet::GetHealthAttribute(),
			EGameplayModOp::Override,
			HealthDataName));
		return Effect;
	}

	UGameplayEffect* CreateDamageEffect()
	{
		UGameplayEffect* Effect = NewObject<UGameplayEffect>(
			GetTransientPackage(),
			TEXT("ShooterDamageEffect"));
		Effect->DurationPolicy = EGameplayEffectDurationType::Instant;
		Effect->Modifiers.Add(MakeModifier(
			UShooterAttributeSet::GetHealthAttribute(),
			EGameplayModOp::Additive,
			DamageDataName));
		return Effect;
	}
}

FName UShooterGameplayEffectStatics::GetMaxHealthSetByCallerDataName()
{
	return ShooterGameplayEffectStaticsImpl::MaxHealthDataName;
}

FName UShooterGameplayEffectStatics::GetHealthSetByCallerDataName()
{
	return ShooterGameplayEffectStaticsImpl::HealthDataName;
}

FName UShooterGameplayEffectStatics::GetDamageSetByCallerDataName()
{
	return ShooterGameplayEffectStaticsImpl::DamageDataName;
}

UGameplayEffect* UShooterGameplayEffectStatics::GetOrCreateInitHealthEffect()
{
	static TStrongObjectPtr<UGameplayEffect> Effect(
		ShooterGameplayEffectStaticsImpl::CreateInitHealthEffect());
	return Effect.Get();
}

UGameplayEffect* UShooterGameplayEffectStatics::GetOrCreateDamageEffect()
{
	static TStrongObjectPtr<UGameplayEffect> Effect(
		ShooterGameplayEffectStaticsImpl::CreateDamageEffect());
	return Effect.Get();
}

void UShooterGameplayEffectStatics::ApplyInitHealthEffect(
	UAbilitySystemComponent* AbilitySystemComponent,
	float MaxHealthValue)
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	UGameplayEffect* Effect = GetOrCreateInitHealthEffect();
	FGameplayEffectSpec Spec(Effect, AbilitySystemComponent->MakeEffectContext(), 1.0f);
	Spec.SetSetByCallerMagnitude(GetMaxHealthSetByCallerDataName(), MaxHealthValue);
	Spec.SetSetByCallerMagnitude(GetHealthSetByCallerDataName(), MaxHealthValue);
	AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(Spec);
}

void UShooterGameplayEffectStatics::ApplyDamageEffect(
	UAbilitySystemComponent* AbilitySystemComponent,
	float Damage,
	AController* Instigator,
	AActor* DamageCauser)
{
	if (!AbilitySystemComponent || Damage <= 0.0f)
	{
		return;
	}

	UGameplayEffect* Effect = GetOrCreateDamageEffect();
	FGameplayEffectContextHandle EffectContext = AbilitySystemComponent->MakeEffectContext();
	EffectContext.AddInstigator(Instigator ? Instigator->GetPawn() : nullptr, DamageCauser);

	FGameplayEffectSpec Spec(Effect, EffectContext, 1.0f);
	// Additive 修饰：伤害以负数量写入 Health。
	Spec.SetSetByCallerMagnitude(GetDamageSetByCallerDataName(), -Damage);
	AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(Spec);
}
