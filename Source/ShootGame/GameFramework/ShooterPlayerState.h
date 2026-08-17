// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "ShooterPlayerState.generated.h"

class UAbilitySystemComponent;
class UShooterAttributeSet;
struct FOnAttributeChangeData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FPlayerCombatStatsChangedDelegate,
	int32,
	Kills,
	int32,
	Deaths,
	float,
	PersonalScore);

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

	/** 以指定 Actor 为 Avatar 建立 AbilityActorInfo；Avatar 不变时幂等跳过。 */
	void InitializeAbilityActorInfo(AActor* AvatarActor);

	/** 返回由本 PlayerState 持有的玩家属性集（Health / MaxHealth）。 */
	UShooterAttributeSet* GetAttributeSet() const { return AttributeSet; }

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
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	/** 玩家属性集子对象；数值由 ASC 复制到拥有者客户端。 */
	UPROPERTY(VisibleAnywhere, Category="Abilities")
	TObjectPtr<UShooterAttributeSet> AttributeSet;

	UPROPERTY(ReplicatedUsing=OnRep_TeamId, VisibleAnywhere, Category="Shooter|Stats")
	uint8 TeamId = 0;

	UPROPERTY(ReplicatedUsing=OnRep_CombatStats, VisibleAnywhere, Category="Shooter|Stats")
	int32 Kills = 0;

	UPROPERTY(ReplicatedUsing=OnRep_CombatStats, VisibleAnywhere, Category="Shooter|Stats")
	int32 Deaths = 0;

	UFUNCTION()
	void OnRep_TeamId();

	UFUNCTION()
	void OnRep_CombatStats();
};
