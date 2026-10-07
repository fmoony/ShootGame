// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "ShooterPlayerState.generated.h"

class UShooterAbilitySystemComponent;
class UShooterAttributeSet;
class UShooterGameplayAbility_Fire;
class UShooterGameplayAbility_Equip;
class UShooterGameplayAbility_Reload;
class AShooterWeapon;
class UGameplayAbility;
struct FGameplayAbilitySpec;
struct FOnAttributeChangeData;
struct FGameplayTag;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FPlayerCombatStatsChangedDelegate, int32, Kills, int32, Deaths, float, PersonalScore);

/**
 * 射击模式中随玩家复制的身份与战斗统计。
 * 这些字段只允许服务器修改，客户端只负责显示。
 * 同时是玩家能力系统组件（ASC）的唯一宿主：Owner = PlayerState，Avatar = 当前角色。
 */
UCLASS()
class SHOOTGAME_API AShooterPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	/** 构造函数：创建玩家 ASC 并启用 Mixed 复制模式。 */
	AShooterPlayerState();

	//~Begin IAbilitySystemInterface
	/** 返回由本 PlayerState 持有的玩家 ASC。 */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~End IAbilitySystemInterface

	/** PlayerController Owner 晚到时，刷新已建立的 ASC 上下文。 */
	virtual void OnRep_Owner() override;

	/** 以指定 Actor 为 Avatar 建立 AbilityActorInfo；Avatar 不变时幂等跳过。 */
	void InitializeAbilityActorInfo(AActor* AvatarActor);

	/** 返回由本 PlayerState 持有的玩家属性集（Health / MaxHealth）。 */
	UShooterAttributeSet* GetAttributeSet() const { return AttributeSet; }

	/** 返回服务器配置的开火 Ability 类。 */
	TSubclassOf<UShooterGameplayAbility_Fire> GetFireAbilityClass() const { return FireAbilityClass; }

	/**
	 * 返回指定武器对应的 Fire Ability Spec；该武器没有对应授予时返回 nullptr。
	 *
	 * 同一个 FireAbilityClass 现在合法存在多份 Spec（每把玩家持有的武器一份），
	 * 因此"武器身份"只能由 SourceObject 判定，不能再用 FindAbilitySpecFromClass。
	 */
	const FGameplayAbilitySpec* FindFireAbilitySpecForWeapon(const AShooterWeapon* Weapon) const;

	/** 返回当前 PlayerState ASC 中 Fire Ability Spec 总数（= 玩家当前持有的可开火武器数）。 */
	int32 GetFireAbilitySpecCount() const;

	/**
	 * 服务器幂等授予"某把武器"的 Fire Ability。
	 *
	 * 幂等键是 AbilityClass + SourceObject：武器进入玩家持有（Inventory.AddWeapon）时调用，
	 * 同一把武器重复调用不新增 Spec；不同武器各自拥有一份 Spec 与 SourceObject。
	 */
	void GrantFireAbilityForWeapon(AShooterWeapon* Weapon);

	/** 服务器撤销某把武器的 Fire Ability；武器真正离开玩家持有时调用。 */
	void RemoveFireAbilityForWeapon(AShooterWeapon* Weapon);

	/** 返回服务器配置的换弹 Ability 类。 */
	TSubclassOf<UShooterGameplayAbility_Reload> GetReloadAbilityClass() const { return ReloadAbilityClass; }

	/** 返回当前 PlayerState ASC 中 Reload Ability Spec 的数量（含尚未完成复制的本地视图）。 */
	int32 GetReloadAbilitySpecCount() const;

	/** 服务器幂等授予 Reload Ability；同一 PlayerState 只允许存在一个 Spec，重生只更新 Avatar。 */
	void GrantReloadAbility();

	/** 只读的服务器换弹动作身份；不承载弹药或换弹提交结果。 */
	uint32 GetReloadId() const { return ReloadId; }

	/** 仅供 GA_Reload 正式接受新事务后调用；客户端调用无效。 */
	void AdvanceAcceptedReloadId();

	/** 返回服务器配置的装备 Ability 类。 */
	TSubclassOf<UShooterGameplayAbility_Equip> GetEquipAbilityClass() const { return EquipAbilityClass; }

	/** 返回当前 PlayerState ASC 中 Equip Ability Spec 的数量；正常为 Next / Previous 两个。 */
	int32 GetEquipAbilitySpecCount() const;

	/** 服务器幂等授予 Next / Previous 两个 Equip Ability Spec，重生只更新 Avatar。 */
	void GrantEquipAbility();

	/** 幂等绑定 Health 属性变化回调（绑定在 PlayerState 上，跨角色重生保持有效）。 */
	void BindHealthAttributeDelegate();

	UPROPERTY(BlueprintAssignable, Category="Shooter|Stats")
	FPlayerCombatStatsChangedDelegate OnCombatStatsChanged;

	void SetTeamId(uint8 NewTeamId);
	void AddKill();
	void AddDeath();

	UFUNCTION(BlueprintPure, Category="Shooter|Stats")
	uint8 GetTeamId() const { return TeamId; }

	UFUNCTION(BlueprintPure, Category="Shooter|Stats")
	int32 GetKills() const { return Kills; }

	UFUNCTION(BlueprintPure, Category="Shooter|Stats")
	int32 GetDeaths() const { return Deaths; }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;

	/** Health 属性变化：转发给当前 Avatar 角色（服务器死亡桥接 / 客户端 HUD 事件链）。 */
	void HandleHealthAttributeChanged(const FOnAttributeChangeData& ChangeData);

	/** 是否已绑定 Health 属性变化回调（幂等保护）。 */
	bool bHealthAttributeDelegateBound = false;

	/** 随 PlayerState 复制到客户端的玩家能力系统组件。 */
	UPROPERTY(VisibleAnywhere, Category="Abilities")
	TObjectPtr<UShooterAbilitySystemComponent> AbilitySystemComponent;

	/** 玩家属性集子对象；数值由 ASC 复制到拥有者客户端。 */
	UPROPERTY(VisibleAnywhere, Category="Abilities")
	TObjectPtr<UShooterAttributeSet> AttributeSet;

	/** 服务器授予给玩家的开火 Ability 类；BP_ShooterPlayerState 不存在时使用原生默认类。 */
	UPROPERTY(EditAnywhere, Category="Abilities")
	TSubclassOf<UShooterGameplayAbility_Fire> FireAbilityClass;
	/** 服务器授予给玩家的换弹 Ability 类；BP_ShooterPlayerState 不存在时使用原生默认类。 */
	UPROPERTY(EditAnywhere, Category="Abilities")
	TSubclassOf<UShooterGameplayAbility_Reload> ReloadAbilityClass;

	/** 服务器授予给玩家的装备 Ability 类；BP_ShooterPlayerState 不存在时使用原生默认类。 */
	UPROPERTY(EditAnywhere, Category="Abilities")
	TSubclassOf<UShooterGameplayAbility_Equip> EquipAbilityClass;

	/** Ability 授予幂等公共实现：指定类已存在 Spec 时直接跳过。 */
	void GrantAbilityIfMissing(TSubclassOf<UGameplayAbility> AbilityClass);

	/** 按动态输入标签幂等授予 Ability Spec；同一 Ability 类可以承接多个方向输入。 */
	void GrantAbilityForInputTagIfMissing(TSubclassOf<UGameplayAbility> AbilityClass, const FGameplayTag& InputTag);

	UPROPERTY(ReplicatedUsing=OnRep_TeamId, VisibleAnywhere, Category="Shooter|Stats")
	uint8 TeamId = 0;

	/** 与 ASC 共用 PlayerState 复制通道；所有观察者可读，只有服务器接受时递增。 */
	UPROPERTY(ReplicatedUsing=OnRep_ReloadId)
	uint32 ReloadId = 0;

	/** 临时低噪声诊断；表现消费在 AnimInstance 的普通更新点进行。 */
	UFUNCTION()
	void OnRep_ReloadId();

	UPROPERTY(ReplicatedUsing=OnRep_CombatStats, VisibleAnywhere, Category="Shooter|Stats")
	int32 Kills = 0;

	UPROPERTY(ReplicatedUsing=OnRep_CombatStats, VisibleAnywhere, Category="Shooter|Stats")
	int32 Deaths = 0;

	UFUNCTION()
	void OnRep_TeamId();

	UFUNCTION()
	void OnRep_CombatStats();
};
