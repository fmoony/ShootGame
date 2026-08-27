// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Characters/Animation/ShooterAnimInstanceBase.h"
#include <type_traits>
#include "ShooterThirdPersonAnimInstance.generated.h"

class AShooterCharacter;
class AShooterWeapon;
class USceneComponent;
class USkeletalMesh;
class USkeletalMeshComponent;

/** IK Binding 状态机状态（第三人称 IK Binding 实施计划 4.1）。 */
UENUM()
enum class EIKBindingState : uint8
{
	Unbound,          // Character / CharacterMesh / CurrentWeapon / Owner 等依赖尚未建立
	WaitingForAttach, // Weapon 与 WeaponMesh 存在，但 Mesh 尚未 Attach 到当前 CharacterMesh
	Pending,          // 前置结构齐全，但本次 Transform 计算数学非法
	Unsupported,      // 当前武器 / 骨架稳定地不提供该 IK 所需能力
	Ready             // 所有依赖与缓存 Transform 已建立，可被本帧 Enabled 消费
};

/** IK Binding 失败原因（实施计划 4.2）；与 State 组合满足不变式。 */
UENUM()
enum class EIKBindingFailureReason : uint8
{
	None,

	MissingCharacterMeshAsset,
	MissingWeaponMeshComponent,
	MissingWeaponMeshAsset,
	MissingWeaponAttachSocket,

	MissingRightHand,
	MissingLeftHand,
	MissingMuzzle,
	MissingCharacterHandGrip,

	WeaponLeftHandGripNotConfigured,
	WeaponLeftHandGripSocketMissing,

	InvalidHandToMuzzle,
	InvalidWeaponGripInRightHandSpace,
	InvalidHandGripInLeftHandSpace
};

/**
 * 公共依赖签名：只用于观察依赖变化，不持有生命周期。
 * 所有 UObject 字段使用 TWeakObjectPtr；两个都解析为 null 的弱指针视为相等，
 * 因此“对象销毁”与“从未出现”统一为同一依赖缺失（实施计划 4.3）。
 */
struct FShooterIKBindingCommonSignature
{
	TWeakObjectPtr<AShooterCharacter> Character;
	TWeakObjectPtr<USkeletalMeshComponent> CharacterMesh;
	TWeakObjectPtr<USkeletalMesh> CharacterMeshAsset;       // CharacterMesh->GetSkeletalMeshAsset()

	TWeakObjectPtr<AShooterWeapon> Weapon;
	TWeakObjectPtr<AActor> WeaponOwner;                     // Weapon->GetOwner()
	TWeakObjectPtr<USkeletalMeshComponent> WeaponMesh;
	TWeakObjectPtr<USkeletalMesh> WeaponMeshAsset;          // WeaponMesh->GetSkeletalMeshAsset()

	TWeakObjectPtr<USceneComponent> WeaponMeshAttachParent; // WeaponMesh->GetAttachParent()
	FName WeaponMeshAttachSocketName;                       // WeaponMesh->GetAttachSocketName()

	bool operator==(const FShooterIKBindingCommonSignature& Other) const
	{
		return Character == Other.Character &&
			CharacterMesh == Other.CharacterMesh &&
			CharacterMeshAsset == Other.CharacterMeshAsset &&
			Weapon == Other.Weapon &&
			WeaponOwner == Other.WeaponOwner &&
			WeaponMesh == Other.WeaponMesh &&
			WeaponMeshAsset == Other.WeaponMeshAsset &&
			WeaponMeshAttachParent == Other.WeaponMeshAttachParent &&
			WeaponMeshAttachSocketName == Other.WeaponMeshAttachSocketName;
	}

	bool operator!=(const FShooterIKBindingCommonSignature& Other) const
	{
		return !(*this == Other);
	}
};

/** Aim IK 专属签名（实施计划 4.3）。 */
struct FAimIKBindingSignature : public FShooterIKBindingCommonSignature
{
	FName HandSocketName;
	FName WeaponMuzzleSocketName;

	bool operator==(const FAimIKBindingSignature& Other) const
	{
		return FShooterIKBindingCommonSignature::operator==(Other) &&
			HandSocketName == Other.HandSocketName &&
			WeaponMuzzleSocketName == Other.WeaponMuzzleSocketName;
	}

	bool operator!=(const FAimIKBindingSignature& Other) const
	{
		return !(*this == Other);
	}
};

/** LeftHand IK 专属签名（实施计划 4.3；握把以 hand_r 为参考，变化也影响左手）。 */
struct FLeftHandIKBindingSignature : public FShooterIKBindingCommonSignature
{
	FName HandSocketName;
	FName LeftHandBoneName;
	FName HandGripSocketName;
	FName WeaponLeftHandGripSocketName;

	bool operator==(const FLeftHandIKBindingSignature& Other) const
	{
		return FShooterIKBindingCommonSignature::operator==(Other) &&
			HandSocketName == Other.HandSocketName &&
			LeftHandBoneName == Other.LeftHandBoneName &&
			HandGripSocketName == Other.HandGripSocketName &&
			WeaponLeftHandGripSocketName == Other.WeaponLeftHandGripSocketName;
	}

	bool operator!=(const FLeftHandIKBindingSignature& Other) const
	{
		return !(*this == Other);
	}
};

/** Aim IK Binding：只保存状态、失败原因、签名、缓存 Transform 与计时/诊断字段，不保存任何强引用（实施计划 4.4）。 */
struct FAimIKBinding
{
	EIKBindingState State = EIKBindingState::Unbound;
	EIKBindingFailureReason FailureReason = EIKBindingFailureReason::None;

	FAimIKBindingSignature StoredSignature;
	FTransform HandToMuzzle = FTransform::Identity;

	float WaitingForAttachElapsedSeconds = 0.0f;
	bool bWaitingForAttachWarningReported = false;

	float PendingElapsedSeconds = 0.0f;
	bool bPendingTimeoutReported = false;
};

/** LeftHand IK Binding（实施计划 4.4）。 */
struct FLeftHandIKBinding
{
	EIKBindingState State = EIKBindingState::Unbound;
	EIKBindingFailureReason FailureReason = EIKBindingFailureReason::None;

	FLeftHandIKBindingSignature StoredSignature;
	FTransform WeaponGripInRightHandSpace = FTransform::Identity;
	FTransform HandGripInLeftHandSpace = FTransform::Identity;

	float WaitingForAttachElapsedSeconds = 0.0f;
	bool bWaitingForAttachWarningReported = false;

	float PendingElapsedSeconds = 0.0f;
	bool bPendingTimeoutReported = false;
};

/**
 * 第三人称 AnimBP 数据源（计划 C2.1 / C4）。
 *
 * 向 AnimGraph 提供：
 *   AimDirectionWorld —— 即时基础视线方向；
 *   AimTargetWorld    —— 观察端平滑后的世界目标点，由 Aim IK 使用预 IK 枪口求精确方向；
 *   HandToMuzzle      —— 当前武器 Muzzle 相对角色 Mesh hand socket 的刚性 Transform（Binding Ready 后缓存）；
 *   bAimIKEnabled     —— 程序化 Aim IK 总开关（Binding 非 Ready 或 Aim 输入无效时自动关闭）。
 *   LeftHandGripInRightHandSpace —— 当前武器左手握把 Socket 相对 hand_r 的刚性 Transform（Binding Ready 后缓存）；
 *   HandGripInLeftHandSpace —— 角色 HandGrip_L 相对 hand_l 的固定 Transform；
 *   bLeftHandIKEnabled     —— Shooter Left Hand IK 总开关（仅 LeftHand Binding Ready 时开启）。
 *
 * Aim IK 与 LeftHand IK 的依赖判定由统一五状态机（EIKBindingState）承担，
 * 不再使用散落的缓存 bool 组合（第三人称 IK Binding 实施计划）。
 * FTransform::Identity 是合法 Transform，不承载“无效 / 未计算 / 未就绪”含义；
 * Transform 是否可消费只由 Binding.State == Ready 决定。
 *
 * 只承载表现数据，不修改骨骼；骨骼修改由 FAnimNode_ShooterAimIK 完成。
 */
UCLASS(Blueprintable)
class SHOOTGAME_API UShooterThirdPersonAnimInstance : public UShooterAnimInstanceBase
{
	GENERATED_BODY()

public:
	/** 世界空间基础瞄准方向；本地拥有者直接使用，观察端用于目标安全投影与 IK 求解参考。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	FVector AimDirectionWorld = FVector::ZeroVector;

	/** 观察端平滑并经过视点安全距离处理后的世界目标点。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	FVector AimTargetWorld = FVector::ZeroVector;

	/** AimTargetWorld 是否可供 Aim IK 节点使用；本地拥有者保持 false。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	bool bAimTargetWorldValid = false;

	/**
	 * 观察端表现目标沿基础视线方向距 Pawn 视点小于该值时，
	 * 将姿势目标沿基础视线向前投影到该安全距离，同时保留目标的横向偏移。
	 * 这只稳定第三人称姿势，不改变相机 Trace、服务器命中或弹丸方向。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumRemoteAimTargetDistanceFromView = 150.0f;

	/**
	 * 观察端安全姿势目标至少位于第三人称枪口前方该距离，
	 * 避免长枪枪口接近视点安全平面时重新放大近点方向变化。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumRemoteAimTargetDistanceFromMuzzle = 50.0f;

	/** 第三人称 AimOffset 俯仰输入（旧 AnimBP 的 PitchN 数据来源）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	float AimPitchN = 0.0f;

	/** 第三人称移动方向角（旧 AnimBP 的 Direction，含 ±45° 夹取逻辑）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter|Locomotion")
	float MoveDirection = 0.0f;

	/** 第三人称移动阈值开关（旧 AnimBP 的 ShouldMove：LocomotionGroundSpeed > 0.01）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter|Locomotion")
	bool bShouldMove = false;

	/** 当前武器 Muzzle 相对角色 Mesh HandSocket 的刚性 Transform（仅 AimBinding Ready 时保留计算结果）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	FTransform HandToMuzzle = FTransform::Identity;

	/** 程序化 Aim IK 总开关；AimBinding 非 Ready 或 Aim 输入无效时为 false。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	bool bAimIKEnabled = false;

	/** 当前武器第三人称左手握把 Socket 相对角色 Mesh HandSocket 的刚性 Transform（只缓存事件重建，不逐帧追逐世界点）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Left Hand IK")
	FTransform LeftHandGripInRightHandSpace = FTransform::Identity;

	/** 角色 HandGrip_L 相对 hand_l 的固定 Transform，用于把手掌接触帧还原为手腕骨骼目标。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Left Hand IK")
	FTransform HandGripInLeftHandSpace = FTransform::Identity;

	/** 左手 Two Bone IK 总开关；仅 LeftHandBinding Ready 时为 true。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Left Hand IK")
	bool bLeftHandIKEnabled = false;

	/** Aim Binding 状态调试副本（AnimBP 调试面板只读显示；权威状态在私有 Binding 中）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	EIKBindingState AimBindingState = EIKBindingState::Unbound;

	/** Aim Binding 失败原因调试副本。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Aim")
	EIKBindingFailureReason AimBindingFailureReason = EIKBindingFailureReason::None;

	/** LeftHand Binding 状态调试副本。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Left Hand IK")
	EIKBindingState LeftHandBindingState = EIKBindingState::Unbound;

	/** LeftHand Binding 失败原因调试副本。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shooter Left Hand IK")
	EIKBindingFailureReason LeftHandBindingFailureReason = EIKBindingFailureReason::None;

	/**
	 * WaitingForAttach 超过该时长后开发期 Warning 一次（之后继续等待，不降频）。
	 * 运行时按 DeltaSeconds 累计游戏时间，不按墙钟计时。
	 */
	UPROPERTY(EditAnywhere, Category = "Shooter Aim", meta = (ClampMin = "3.0", ClampMax = "5.0", Units = "s"))
	float WaitingForAttachWarningDelaySeconds = 3.0f;

	/**
	 * 清空两个 StoredSignature 并立即 Rebuild：编辑器资产重导入、测试或 Debug 时唤醒 Unsupported。
	 * 运行时正式逻辑不依赖它。
	 */
	void ForceRebuildIKBindings();

	/** 角色 Mesh 上的手部 socket 名（与 FAnimNode_ShooterAimIK.HandBone 保持一致）。 */
	UPROPERTY(EditAnywhere, Category = "Shooter Aim", meta = (DisplayName = "Hand Socket Name"))
	FName HandSocketName = TEXT("hand_r");

	/** 左手 IK 末端骨骼名。 */
	UPROPERTY(EditAnywhere, Category = "Shooter Left Hand IK")
	FName LeftHandBoneName = TEXT("hand_l");

	/** 角色手掌上的握持参考 Socket；它与武器握把完整对齐。 */
	UPROPERTY(EditAnywhere, Category = "Shooter Left Hand IK")
	FName HandGripSocketName = TEXT("HandGrip_L");

	/** 计算 Muzzle 相对 HandSocket 的刚性 Transform（纯几何计算，可被 Automation 验证）。
	 *  调用方负责前置能力与数学合法性判定（Binding Rebuild）；本函数不再把 Identity 当作失败。 */
	UFUNCTION(BlueprintPure, Category = "Shooter Aim")
	static FTransform ComputeHandToMuzzleTransform(
		const FTransform& InHandWorld,
		const FTransform& InMuzzleWorld);

	/** 从第三人称 Muzzle 世界位置指向表现目标的世界方向；无效输入返回零向量（C4 纯计算）。 */
	UFUNCTION(BlueprintPure, Category = "Shooter Aim")
	static FVector ComputeMuzzleToTargetDirection(
		const FVector& MuzzleWorldLocation,
		const FVector& TargetWorld);

	/**
	 * C4 角色矩阵：按本地控制 / 消费角色 / 有效性决定 AimDirectionWorld 来源（纯计算，可被 Automation 验证）。
	 * 远端目标沿基础视线离 Pawn 视点过近或位于后方时，把姿势目标投影到安全射线深度，避免近点奇异方向驱动 IK。
	 */
	static FVector ComputeAimDirectionWorldForState(
		bool bLocallyControlled,
		bool bShouldRunPresentationSmoothing,
		bool bPresentationTargetValid,
		const FVector& LocalAimDirection,
		const FVector& ViewWorldLocation,
		const FVector& MuzzleWorldLocation,
		const FVector& SmoothedPresentationTarget,
		bool bHasThirdPersonMuzzle,
		float MinimumTargetDistanceFromView = 150.0f,
		float MinimumTargetDistanceFromMuzzle = 50.0f);

	/** 读取角色 Mesh 指定 socket 的世界变换；无角色 / 无 socket 时回退 Identity。 */
	UFUNCTION(BlueprintPure, Category = "Shooter Aim")
	static FTransform GetHandWorldTransform(const AShooterCharacter* InCharacter, FName InHandSocketName);

	/** 计算左手握把相对右手 HandSocket 的刚性 Transform（纯几何计算，可被 Automation 验证）。
	 *  调用方负责前置能力与数学合法性判定（Binding Rebuild）；本函数不再把 Identity 当作失败。 */
	UFUNCTION(BlueprintPure, Category = "Shooter Left Hand IK")
	static FTransform ComputeLeftHandGripInRightHandSpace(
		const FTransform& InRightHandWorld,
		const FTransform& InLeftHandGripWorld);

	/** 计算角色 HandGrip_L 相对 hand_l 的固定 Transform。 */
	UFUNCTION(BlueprintPure, Category = "Shooter Left Hand IK")
	static FTransform ComputeHandGripInLeftHandSpace(
		const FTransform& InLeftHandWorld,
		const FTransform& InHandGripWorld);

	/**
	 * 只查真正的非法数值 / 旋转 / Scale，不比较 Identity：
	 * FTransform::IsValid() 已覆盖 NaN/Inf 与 Rotation 归一化；Scale 各分量必须大于 0。
	 */
	static bool IsMathematicallyValidBindingFrame(const FTransform& T);

protected:
	/** 基类完成公共快照后采集第三人称专用数据。 */
	virtual void UpdateShooterAnimationData(float DeltaSeconds) override;

	// ---- IK Binding 状态机实现层 ----
	// 以下成员与函数仅供测试壳（Tests/Animation/ShooterIKBindingAutomationTests.cpp）访问，
	// 不属于 AnimBP 接口；两个 Binding 不需要 UPROPERTY 参与 GC（不保存任何强引用）。

	/** 从 Character 采集 Aim 依赖签名（含 WeaponOwner / AttachParent / AttachSocketName 等时序敏感项）。 */
	FAimIKBindingSignature GatherAimSignature(AShooterCharacter* Character) const;

	/** 从 Character 采集 LeftHand 依赖签名。 */
	FLeftHandIKBindingSignature GatherLeftHandSignature(AShooterCharacter* Character) const;

	/** 单帧更新 Aim Binding：签名变化或 Pending 时 Rebuild，随后计时与诊断（实施计划 5.1）。 */
	void UpdateAimBinding(const FAimIKBindingSignature& Signature, float DeltaSeconds);

	/** 单帧更新 LeftHand Binding。 */
	void UpdateLeftHandBinding(const FLeftHandIKBindingSignature& Signature, float DeltaSeconds);

	/** Aim Binding 判定链（实施计划 5.2）。 */
	void RebuildAimBinding(const FAimIKBindingSignature& Signature);

	/** LeftHand Binding 判定链（实施计划 5.3）。 */
	void RebuildLeftHandBinding(const FLeftHandIKBindingSignature& Signature);

	/** 无 Character 时复位两个 Binding、缓存 Transform、Enabled 与 Aim 输入输出。 */
	void ResetBindingsAndOutputs();

	/** 采集 AimDirectionWorld / AimTargetWorld / bAimTargetWorldValid（本地即时 / 远端平滑投影）。 */
	void UpdateAimInputs(const AShooterCharacter* Character);

	/** 刷新公开输出：发布三个缓存 Transform 到 AnimBP 接口，并刷新调试副本与 bAimIKEnabled / bLeftHandIKEnabled（实施计划 5.5）。 */
	void RefreshIKEnabled(const AShooterCharacter* Character);

	/** Aim 与 LeftHand 的 Binding 状态与缓存（不持有任何强引用）。 */
	FAimIKBinding AimBinding;
	FLeftHandIKBinding LeftHandBinding;

private:
	/** Pending 超时诊断阈值（固定 1.0 秒，实施计划 5.4）。 */
	static constexpr float PendingTimeoutSeconds = 1.0f;

	/** 统一设置 State / FailureReason；非 Ready 状态同时清空缓存 Transform 为 Identity（实施计划 5.3）。 */
	template <typename TBinding>
	static void SetBindingState(TBinding& Binding, EIKBindingState NewState, EIKBindingFailureReason NewReason, const TCHAR* Tag);

	/** 清零计时与报告标记（实施计划 5.4）。 */
	static void ResetBindingTimers(FAimIKBinding& Binding);
	static void ResetBindingTimers(FLeftHandIKBinding& Binding);

	/** 按状态累加计时并输出一次性诊断日志（实施计划 5.4）。 */
	static void TickBindingTimers(FAimIKBinding& Binding, float DeltaSeconds, float WaitingForAttachDelay);
	static void TickBindingTimers(FLeftHandIKBinding& Binding, float DeltaSeconds, float WaitingForAttachDelay);

	/** 失败原因的可读名（日志与测试断言共用）。 */
	static const TCHAR* GetFailureReasonName(EIKBindingFailureReason Reason);

	/** 进入异常类 Unsupported 时 Warning 一次；WeaponLeftHandGripNotConfigured 属于预期能力缺失，不 Warning。 */
	static void WarnOnUnexpectedUnsupported(EIKBindingFailureReason Reason, const TCHAR* Tag, const AShooterWeapon* Weapon);
};

template <typename TBinding>
void UShooterThirdPersonAnimInstance::SetBindingState(
	TBinding& Binding,
	EIKBindingState NewState,
	EIKBindingFailureReason NewReason,
	const TCHAR* Tag)
{
	const bool bStateOrReasonChanged =
		Binding.State != NewState || Binding.FailureReason != NewReason;

	Binding.State = NewState;
	Binding.FailureReason = NewReason;

	// 每个 SetState 同时把缓存 Transform 重置为 Identity；只有 Ready 保留计算结果。
	if (NewState != EIKBindingState::Ready)
	{
		if constexpr (std::is_same_v<TBinding, FAimIKBinding>)
		{
			Binding.HandToMuzzle = FTransform::Identity;
		}
		else
		{
			Binding.WeaponGripInRightHandSpace = FTransform::Identity;
			Binding.HandGripInLeftHandSpace = FTransform::Identity;
		}
	}

	if (NewState == EIKBindingState::Unsupported && bStateOrReasonChanged)
	{
		WarnOnUnexpectedUnsupported(NewReason, Tag, Binding.StoredSignature.Weapon.Get());
	}
}
