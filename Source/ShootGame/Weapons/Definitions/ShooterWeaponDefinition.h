// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ShooterWeaponDefinition.generated.h"

class AShooterWeapon;
class UAnimInstance;
class UAnimMontage;
class UNiagaraSystem;
class USkeletalMesh;
class USoundBase;
class UStaticMesh;

/** 武器弹药配置：所有实例共享的只读弹药经济参数，权威弹药数据保存在 WeaponInstance。 */
USTRUCT(BlueprintType)
struct FShooterWeaponAmmoConfig
{
	GENERATED_BODY()

	/** 弹匣容量；<=0 是非法配置，授予路径必须 fail closed。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ammo", meta=(ClampMin=1, ClampMax=100))
	int32 MagazineSize = 10;

	/** 初始备弹声明：-1 表示自动（MagazineSize × 3，兼容既有资产基线）；>=0 为显式有限值。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ammo", meta=(ClampMin=-1, ClampMax=999))
	int32 InitialReserveAmmo = -1;

	/** 解析实际初始备弹：显式 >=0 直接采用，-1 回落 MagazineSize × 3。 */
	int32 ResolveInitialReserveAmmo() const
	{
		return InitialReserveAmmo >= 0
			? InitialReserveAmmo
			: FMath::Max(0, MagazineSize * 3);
	}
};

/** 开火节奏与射击感知噪声配置；权威计时器不保存在这里。 */
USTRUCT(BlueprintType)
struct FShooterWeaponFireConfig
{
	GENERATED_BODY()

	/** 是否全自动开火。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire")
	bool bFullAuto = false;

	/** 两发之间的最小间隔；同时作用于全自动与半自动。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire", meta=(ClampMin=0, ClampMax=5, Units="s"))
	float RefireRate = 0.5f;

	/** AI 感知系统可听到的射击响度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire", meta=(ClampMin=0, ClampMax=100))
	float ShotLoudness = 1.0f;

	/** 射击噪声的最大感知范围。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire", meta=(ClampMin=0, ClampMax=100000, Units="cm"))
	float ShotNoiseRange = 3000.0f;

	/** 射击噪声携带的感知标签。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire")
	FName ShotNoiseTag = FName("Shot");
};

/** 表现配置：动画类、Montage、特效、音效、后坐力与预览网格。
 *  第一版中 WeaponActor 组件模板仍是表现堆栈的活动数据源；
 *  本结构先完成正式数据归档，消费者迁移按计划分阶段推进。 */
USTRUCT(BlueprintType)
struct FShooterWeaponPresentationConfig
{
	GENERATED_BODY()

	/** 第一人称武器网格；WeaponActor 组件模板的正式数据归档。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mesh")
	TSoftObjectPtr<USkeletalMesh> FirstPersonMesh;

	/** 第三人称武器网格；WeaponActor 组件模板的正式数据归档。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mesh")
	TSoftObjectPtr<USkeletalMesh> ThirdPersonMesh;

	/** Pickup 预览网格。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mesh")
	TSoftObjectPtr<UStaticMesh> PickupPreviewMesh;

	/** 激活本武器时第一人称角色网格使用的 AnimInstance 类。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	TSubclassOf<UAnimInstance> FirstPersonAnimInstanceClass;

	/** 激活本武器时第三人称角色网格使用的 AnimInstance 类。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	TSubclassOf<UAnimInstance> ThirdPersonAnimInstanceClass;

	/** 开火 Montage。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	TObjectPtr<UAnimMontage> FiringMontage;

	/** 枪口 Niagara 特效。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FX")
	TObjectPtr<UNiagaraSystem> MuzzleFlash;

	/** 开火音效。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FX")
	TObjectPtr<USoundBase> FireSound;

	/** 换弹音效：弹匣退出阶段。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FX")
	TObjectPtr<USoundBase> ReloadMagazineOutSound;

	/** 换弹音效：弹匣插入阶段。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FX")
	TObjectPtr<USoundBase> ReloadMagazineInSound;

	/** 换弹音效：拉枪机上膛阶段。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FX")
	TObjectPtr<USoundBase> ReloadCockingSound;

	/** 开火施加给拥有者的后坐力。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim", meta=(ClampMin=0, ClampMax=100))
	float FiringRecoil = 0.0f;

	/** 瞄准散布半角。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim", meta=(ClampMin=0, ClampMax=90, Units="Degrees"))
	float AimVariance = 0.0f;
};

/**
 * 武器正式定义资产（共享只读配置）。
 *
 * 职责边界（实施计划 4.1）：
 * - 保存所有实例共享的只读配置：WeaponActorClass、Ammo / Fire / Presentation 配置；
 * - 不保存 MagazineAmmo、ReserveAmmo、当前装备或计时器等实例可变数据；
 * - PrimaryAssetType 固定为类名 ShooterWeaponDefinition，不依赖 WeaponActor 蓝图类名；
 * - 扫描目录固定在 /Game/Shooter/Weapons/Definitions（见 DefaultGame.ini AssetManager 配置）。
 */
UCLASS()
class SHOOTGAME_API UShooterWeaponDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 固定 PrimaryAssetType；重写 GetPrimaryAssetId 保证任何子类实例都归入该类型。 */
	static const FPrimaryAssetType& GetWeaponDefinitionAssetType();

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	/** 返回本资产的稳定 DefinitionId；CDO 与未保存对象可能无效，调用方必须校验。 */
	FPrimaryAssetId GetDefinitionId() const { return GetPrimaryAssetId(); }

	/**
	 * 生产授予前的最小合法配置校验；失败时授予路径必须 fail closed：
	 * DefinitionId 有效、WeaponActorClass 是具体 AShooterWeapon 子类、弹匣容量合法。
	 */
	bool IsValidForGrant() const;

	/**
	 * 同步解析 DefinitionId -> Definition。
	 * 类型不匹配、未注册资产或加载失败都返回 nullptr；第一版同步加载即可。
	 */
	static UShooterWeaponDefinition* ResolveDefinitionSync(const FPrimaryAssetId& DefinitionId);

	/** 池化世界实体类：WeaponActorClass 只由 Definition 决定。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	TSubclassOf<AShooterWeapon> WeaponActorClass;

	/** 弹药经济配置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	FShooterWeaponAmmoConfig AmmoConfig;

	/** 开火节奏与射击噪声配置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	FShooterWeaponFireConfig FireConfig;

	/** 表现配置归档。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	FShooterWeaponPresentationConfig PresentationConfig;
};
