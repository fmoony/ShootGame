// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Characters/ShooterCharacter.h"
#include "Weapons/ShooterWeapon.h"
#include "ShooterIKBindingTestHarness.generated.h"

/**
 * 第三人称 IK Binding 状态机测试壳（仅 Editor Automation 使用，实施计划第 8 节）。
 *
 * 测试武器具体化 abstract 的 AShooterWeapon；测试角色去掉 AShooterCharacter 的 abstract 标记；
 * 测试壳 AnimInstance 访问 protected 状态机层，绕过 GetOwningActor 依赖，
 * 用真实对象（NewObject 的 Character / Weapon / SkeletalMeshComponent）驱动
 * Unbound / WaitingForAttach / Pending / Unsupported / Ready 判定链。
 */

/** 测试角色：AShooterCharacter 是 UCLASS(abstract)，测试需要可 NewObject 的具体子类。 */
UCLASS(Transient, NotBlueprintable)
class AShooterIKBindingTestCharacter : public AShooterCharacter
{
	GENERATED_BODY()
};

/** 测试武器：具体化 abstract 的 AShooterWeapon（UCLASS 必须在文件顶层，UHT 不支持 namespace 内反射）。 */
UCLASS(Transient, NotBlueprintable)
class AShooterIKBindingTestWeapon : public AShooterWeapon
{
	GENERATED_BODY()

public:
	AShooterIKBindingTestWeapon()
	{
		MuzzleSocketName = TEXT("Muzzle");
		ThirdPersonLeftHandGripSocketName = TEXT("Grip_L");
	}

	void SetMuzzleSocketNameForTest(FName InName) { MuzzleSocketName = InName; }
	void SetLeftHandGripSocketNameForTest(FName InName) { ThirdPersonLeftHandGripSocketName = InName; }
};

/** 测试壳 AnimInstance：访问 protected 状态机层。 */
UCLASS(Transient, NotBlueprintable)
class UShooterIKBindingTestHarness : public UShooterThirdPersonAnimInstance
{
	GENERATED_BODY()

public:
	void CallUpdateAimBinding(const FAimIKBindingSignature& Signature, float DeltaSeconds)
	{
		UpdateAimBinding(Signature, DeltaSeconds);
	}

	void CallUpdateLeftHandBinding(const FLeftHandIKBindingSignature& Signature, float DeltaSeconds)
	{
		UpdateLeftHandBinding(Signature, DeltaSeconds);
	}

	void CallResetBindingsAndOutputs()
	{
		ResetBindingsAndOutputs();
	}

	/** 与生产 UpdateShooterAnimationData 相同的调用序列（Binding → 输入 → Enabled），Character 由测试注入。 */
	void CallUpdateShooterAnimationDataForCharacter(AShooterCharacter* Character, float DeltaSeconds)
	{
		const FAimIKBindingSignature AimSignature = GatherAimSignature(Character);
		const FLeftHandIKBindingSignature LeftHandSignature = GatherLeftHandSignature(Character);
		UpdateAimBinding(AimSignature, DeltaSeconds);
		UpdateLeftHandBinding(LeftHandSignature, DeltaSeconds);
		UpdateAimInputs(Character);
		RefreshIKEnabled(Character);
	}

	void CallRefreshIKEnabled(AShooterCharacter* Character)
	{
		RefreshIKEnabled(Character);
	}

	/** 清空 StoredSignature 强制下一次 Update 走完整 Rebuild（模拟依赖签名变化 / ForceRebuild 唤醒）。 */
	void CallClearAimSignatureForTest()
	{
		AimBinding.StoredSignature = FAimIKBindingSignature();
	}

	void CallClearLeftHandSignatureForTest()
	{
		LeftHandBinding.StoredSignature = FLeftHandIKBindingSignature();
	}

	void CallForceRebuildForTest(AShooterCharacter* Character)
	{
		AimBinding.StoredSignature = FAimIKBindingSignature();
		LeftHandBinding.StoredSignature = FLeftHandIKBindingSignature();
		UpdateAimBinding(GatherAimSignature(Character), 0.0f);
		UpdateLeftHandBinding(GatherLeftHandSignature(Character), 0.0f);
		RefreshIKEnabled(Character);
	}

	EIKBindingState GetAimState() const { return AimBinding.State; }
	EIKBindingFailureReason GetAimReason() const { return AimBinding.FailureReason; }
	EIKBindingState GetLeftHandState() const { return LeftHandBinding.State; }
	EIKBindingFailureReason GetLeftHandReason() const { return LeftHandBinding.FailureReason; }

	const FTransform& GetHandToMuzzleForTest() const { return AimBinding.HandToMuzzle; }
	const FTransform& GetWeaponGripInRightHandSpaceForTest() const { return LeftHandBinding.WeaponGripInRightHandSpace; }
	const FTransform& GetHandGripInLeftHandSpaceForTest() const { return LeftHandBinding.HandGripInLeftHandSpace; }

	float GetAimPendingElapsed() const { return AimBinding.PendingElapsedSeconds; }
	bool GetAimPendingReported() const { return AimBinding.bPendingTimeoutReported; }
	float GetAimWaitingElapsed() const { return AimBinding.WaitingForAttachElapsedSeconds; }
	bool GetAimWaitingReported() const { return AimBinding.bWaitingForAttachWarningReported; }
	float GetLeftHandPendingElapsed() const { return LeftHandBinding.PendingElapsedSeconds; }
	bool GetLeftHandPendingReported() const { return LeftHandBinding.bPendingTimeoutReported; }
	bool GetLeftHandWaitingReported() const { return LeftHandBinding.bWaitingForAttachWarningReported; }

	/** 等待计时：从当前状态累积注入，覆盖 Pending 超时 / WaitingForAttach 诊断阈值。 */
	void RunAimWaitingFrames(float DeltaSeconds, int32 FrameCount)
	{
		for (int32 Frame = 0; Frame < FrameCount; ++Frame)
		{
			CallUpdateAimBinding(GetAimStoredSignature(), DeltaSeconds);
		}
	}

	const FAimIKBindingSignature& GetAimStoredSignature() const { return AimBinding.StoredSignature; }
};
