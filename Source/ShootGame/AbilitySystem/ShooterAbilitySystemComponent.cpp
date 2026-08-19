// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterAbilitySystemComponent.h"

#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"
#include "ShooterGameplayTags.h"
#include "ShootGame.h"

void UShooterAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
{
	FGameplayAbilitySpec* Spec = FindAbilitySpecFromInputTag(InputTag);
	if (!Spec)
	{
		UE_LOG(
			LogShootGame,
			Verbose,
			TEXT("AbilityInputTagPressed: no ability spec for InputTag=%s Owner=%s"),
			*InputTag.ToString(),
			*GetNameSafe(GetOwnerActor()));
		return;
	}

	// 先把按下状态写进 Spec，再尝试激活。
	// 对 ServerOnly Ability：客户端 TryActivateAbility 会走 ServerTryActivateAbility；
	// 服务器端则直接进入 InternalTryActivateAbility。
	AbilitySpecInputPressed(*Spec);
	if (!Spec->IsActive())
	{
		TryActivateAbility(Spec->Handle, true);
	}
}

void UShooterAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag& InputTag)
{
	FGameplayAbilitySpec* Spec = FindAbilitySpecFromInputTag(InputTag);
	if (!Spec)
	{
		UE_LOG(
			LogShootGame,
			Verbose,
			TEXT("AbilityInputTagReleased: no ability spec for InputTag=%s Owner=%s"),
			*InputTag.ToString(),
			*GetNameSafe(GetOwnerActor()));
		return;
	}

	// ServerOnly Ability 在客户端没有活动实例，可靠 RPC 把松开转发到服务器；
	// 服务器收到后由 AbilitySpecInputReleased 分发给活动 Ability 实例。
	if (!IsOwnerActorAuthoritative())
	{
		ServerSetInputReleased(Spec->Handle);
	}

	AbilitySpecInputReleased(*Spec);
}

FGameplayAbilitySpec* UShooterAbilitySystemComponent::FindAbilitySpecFromInputTag(
	const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return nullptr;
	}

	for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (!Spec.Ability)
		{
			continue;
		}

		// AbilityTags 是 CDO 上的静态定义，DynamicAbilityTags 覆盖授予时新增的动态标签。
		if (Spec.Ability->GetAssetTags().HasTag(InputTag) ||
			Spec.GetDynamicSpecSourceTags().HasTag(InputTag))
		{
			return &Spec;
		}
	}

	return nullptr;
}

int32 UShooterAbilitySystemComponent::GetAbilitySpecCountForClass(
	TSubclassOf<UGameplayAbility> AbilityClass) const
{
	if (!AbilityClass)
	{
		return 0;
	}

	int32 Count = 0;
	for (const FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->IsA(AbilityClass))
		{
			++Count;
		}
	}

	return Count;
}
