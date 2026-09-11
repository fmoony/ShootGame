// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ShooterWeaponConfigRow.generated.h"

class AShooterProjectile;
class AShooterWeapon;
class UAnimInstance;
class UAnimMontage;
class UNiagaraSystem;
class UShooterWeaponFireBehavior;
class USkeletalMesh;
class USoundBase;
class UStaticMesh;

/**
 * 单表武器配置行：DT_WeaponData 的一行描述一类完整武器。
 *
 * 职责边界（单表武器配置纠偏小计划 4）：
 * - 只保存所有实例共享的只读配置，等价于旧 WD_* Definition 的 WeaponActorClass /
 *   AmmoConfig / FireConfig / PresentationConfig 与 FireBehavior 之和；
 * - 不保存 MagazineAmmo、ReserveAmmo、装备状态、计时器或 Actor 指针等实例可变数据；
 * - 行名（Rifle / Pistol / AWP / GrenadeLauncher）就是配置键，不再另设 WeaponId；
 * - 只为已经存在消费者的字段建列，不为表格观感预留空字段；
 * - 资源引用沿用当前同步加载边界：网格为软引用并同步加载，其余表现资产与类为硬引用。
 *
 * 数据流：Pickup/NPC 选择行 → Inventory.WeaponRowName → WeaponActor 按行应用只读配置。
 */
USTRUCT(BlueprintType)
struct SHOOTGAME_API FShooterWeaponConfigRow : public FTableRowBase
{
	GENERATED_BODY()

	// ---- Actor ----

	/** 该武器使用的池化世界实体类；蓝图只承载确有必要的结构或逻辑差异。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Actor")
	TSubclassOf<AShooterWeapon> WeaponActorClass;

	// ---- Ammo ----

	/** 弹匣容量；<=0 是非法配置，授予路径必须 fail closed。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ammo", meta=(ClampMin=1, ClampMax=100))
	int32 MagazineSize = 10;

	/** 初始备弹声明：-1 表示自动（MagazineSize × 3）；>=0 为显式有限值。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ammo", meta=(ClampMin=-1, ClampMax=999))
	int32 InitialReserveAmmo = -1;

	// ---- Fire ----

	/** 是否全自动开火。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire")
	bool bFullAuto = false;

	/** 两发之间的最小间隔；同时作用于全自动与半自动。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire", meta=(ClampMin=0, ClampMax=5, Units="s"))
	float RefireRate = 0.5f;

	/** 瞄准散布半角。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire", meta=(ClampMin=0, ClampMax=90, Units="Degrees"))
	float AimVariance = 0.0f;

	/** AI 感知系统可听到的射击响度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire", meta=(ClampMin=0, ClampMax=100))
	float ShotLoudness = 1.0f;

	/** 射击噪声的最大感知范围。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire", meta=(ClampMin=0, ClampMax=100000, Units="cm"))
	float ShotNoiseRange = 3000.0f;

	/** 射击噪声携带的感知标签。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire")
	FName ShotNoiseTag = FName("Shot");

	// ---- Timing ----

	/** 服务器权威换弹事务等待时长；表现 Montage 不得反向决定该值。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing", meta=(ClampMin=0, Units="s"))
	float ReloadDuration = 1.5f;

	/** 服务器权威切枪事务等待时长；表现 Montage 不得反向决定该值。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing", meta=(ClampMin=0, Units="s"))
	float EquipDuration = 0.5f;

	// ---- Attack ----

	/**
	 * 开火行为类：只选择无持久可变状态的行为实现，由 WeaponActor 在应用行配置时实例化。
	 * 空类表示该行回落到 WeaponActor 的兼容弹丸路径。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attack")
	TSubclassOf<UShooterWeaponFireBehavior> FireBehaviorClass;

	/** 该行投射物类；由行为实现从本行读取，不再保存在行为实例上。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attack")
	TSubclassOf<AShooterProjectile> ProjectileClass;

	/** 弹丸在枪口前方的生成偏移。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attack", meta=(ClampMin=0, ClampMax=1000, Units="cm"))
	float MuzzleOffset = 10.0f;

	// ---- Socket ----

	/** 第一/第三人称武器共用的枪口 Socket。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Socket")
	FName MuzzleSocketName;

	/** 第三人称左手握把 Socket 名；空名表示该武器没有左手握把配置，左手 IK 自动关闭。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Socket")
	FName LeftHandGripSocketName = NAME_None;

	// ---- Mesh / Anim ----

	/** 第一人称武器网格；WeaponActor 在应用行配置时同步加载。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mesh")
	TSoftObjectPtr<USkeletalMesh> FirstPersonMesh;

	/** 第三人称武器网格；WeaponActor 在应用行配置时同步加载。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mesh")
	TSoftObjectPtr<USkeletalMesh> ThirdPersonMesh;

	/** Pickup 预览网格；由 Pickup 在 OnConstruction 同步加载。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mesh")
	TSoftObjectPtr<UStaticMesh> PickupMesh;

	/** 激活本武器时第一人称角色网格使用的 AnimInstance 类。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	TSubclassOf<UAnimInstance> FirstPersonAnimInstanceClass;

	/** 激活本武器时第三人称角色网格使用的 AnimInstance 类。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	TSubclassOf<UAnimInstance> ThirdPersonAnimInstanceClass;

	/** 开火 Montage。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	TObjectPtr<UAnimMontage> FiringMontage;

	// ---- FX / Audio ----

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

	// ---- View ----

	/**
	 * 开火施加给拥有者的俯仰输入增量，最终经 APlayerController::AddControllerPitchInput
	 * 直接叠加到控制旋转的 Pitch。
	 *
	 * 符号语义：UE 约定 Pitch 正值抬头、负值下压（RotationInput.Pitch 直接加到 ViewRotation.Pitch，
	 * FRotator 的 Pitch 增大表示视线 Z 分量增大）。现有四把武器均为负值（-0.05 ～ -0.3），
	 * 即开火时轻微下压；这是既有生产基线数值，本次纠偏只统一语义并把约束修正为允许负值。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View", meta=(ClampMin=-100, ClampMax=100))
	float FiringRecoil = 0.0f;

	/** 第一人称净空代理使用的构图下沉量。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View")
	float FirstPersonCompositionDrop = 0.0f;

	/** 解析实际初始备弹：显式 >=0 直接采用，-1 回落 MagazineSize × 3。 */
	int32 ResolveInitialReserveAmmo() const
	{
		return InitialReserveAmmo >= 0
			? InitialReserveAmmo
			: FMath::Max(0, MagazineSize * 3);
	}
};
