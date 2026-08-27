// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Characters/Animation/AnimNodes/ShooterLeftHandIKMath.h"
#include "Characters/ShooterCharacter.h"
#include "Weapons/ShooterWeapon.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"
#include "ShooterIKBindingTestHarness.h"

/**
 * 第三人称 IK Binding 状态机自动化测试（实施计划第 8 节验证矩阵）。
 *
 * 测试壳直接构造真实对象（NewObject 的 Character / Weapon / SkeletalMeshComponent，
 * 不注册进 World），用签名驱动 UpdateAimBinding / UpdateLeftHandBinding 判定链，
 * 覆盖 Unbound / WaitingForAttach / Pending / Unsupported / Ready 与计时诊断日志。
 *
 * 本机 UE 5.6 事实（实施计划第 3 节）：
 *   - USkeletalMesh::AddSocket 要求 socket 的 BoneName 真实存在于 RefSkeleton；
 *   - USkinnedMeshComponent::DoesSocketExist 对存在的 socket 返回其 BoneName；
 *   - 未注册组件 GetBoneTransform 返回 Identity，socket 世界变换退化为 SocketLocalTransform；
 *   - 未注册组件 AttachToComponent / DetachFromComponent 直接维护 AttachParent / AttachSocketName。
 */
namespace ShooterIKBindingAutomationTests
{
	/** 测试场景：NewObject 的真实对象，全部挂在 Transient 包下，不注册进 World。 */
	struct FIKBindingTestScene
	{
		AShooterCharacter* Character = nullptr;
		AShooterIKBindingTestWeapon* Weapon = nullptr;
		USkeletalMeshComponent* CharacterMesh = nullptr;
		USkeletalMeshComponent* WeaponMesh = nullptr;
		USkeletalMesh* CharacterMeshAsset = nullptr;
		USkeletalMesh* WeaponMeshAsset = nullptr;
		UShooterIKBindingTestHarness* Harness = nullptr;
	};

	/** 创建带最小 RefSkeleton 与 sockets 的 SkeletalMesh；socket 绑定 hand_r 骨骼。 */
	USkeletalMesh* CreateSkeletalMeshWithSockets(
		FAutomationTestBase& Test,
		const TArray<FName>& SocketNames,
		const TCHAR* Label,
		const FVector& SocketLocation = FVector(0.0f, 0.0f, 100.0f))
	{
		USkeletalMesh* Mesh = NewObject<USkeletalMesh>();
		if (!Test.TestNotNull(FString::Printf(TEXT("%s mesh created"), Label), Mesh))
		{
			return nullptr;
		}

		// AddSocket 会校验 BoneName 必须存在于 RefSkeleton（本机 UE 5.6 实际行为）。
		// FReferenceSkeletonModifier 只填充 Raw；AddSocket 校验用 Final，需从 Raw 重建。
		FReferenceSkeleton RefSkeleton;
		FReferenceSkeletonModifier Modifier(RefSkeleton, nullptr);
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("hand_r"), TEXT("hand_r"), 0), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("hand_l"), TEXT("hand_l"), 0), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("HandGrip_L"), TEXT("HandGrip_L"), 0), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("HandGrip_R"), TEXT("HandGrip_R"), 0), FTransform::Identity);
		Mesh->SetRefSkeleton(Modifier.GetReferenceSkeleton());
		Mesh->GetRefSkeleton().RebuildRefSkeleton(nullptr, true);

		// 提供 Skeleton 避免 SetSkeletalMeshAsset 的 "has no skeleton" 警告噪音。
		Mesh->SetSkeleton(NewObject<USkeleton>(Mesh));

		for (const FName SocketName : SocketNames)
		{
			USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(Mesh);
			Socket->SocketName = SocketName;
			Socket->BoneName = TEXT("hand_r");
			Socket->RelativeLocation = SocketLocation;
			Socket->RelativeRotation = FRotator::ZeroRotator;
			Socket->RelativeScale = FVector::OneVector;
			Mesh->AddSocket(Socket);
		}
		return Mesh;
	}

	/** 构建完整场景：Rifle 风格（Muzzle + Grip_L 齐全），mesh 未 attach。
	 *  角色 socket 位于 (0,0,100)、武器 socket 位于 (0,0,50)，保证相对 Transform 非 Identity。 */
	FIKBindingTestScene CreateRifleScene(FAutomationTestBase& Test)
	{
		FIKBindingTestScene Scene;
		Scene.Character = NewObject<AShooterIKBindingTestCharacter>();
		Scene.Weapon = NewObject<AShooterIKBindingTestWeapon>();

		Scene.CharacterMesh = Scene.Character ? Scene.Character->GetMesh() : nullptr;
		Scene.WeaponMesh = Scene.Weapon ? Scene.Weapon->GetThirdPersonMesh() : nullptr;

		// UAnimInstance 的 ClassWithin 是 SkeletalMeshComponent：用角色 Mesh 作 Outer 避免非法 Outer 警告。
		Scene.Harness = Scene.CharacterMesh
			? NewObject<UShooterIKBindingTestHarness>(Scene.CharacterMesh)
			: NewObject<UShooterIKBindingTestHarness>();

		Scene.CharacterMeshAsset = CreateSkeletalMeshWithSockets(
			Test,
			{TEXT("hand_r"), TEXT("hand_l"), TEXT("HandGrip_L"), TEXT("HandGrip_R")},
			TEXT("Character"),
			FVector(0.0f, 0.0f, 100.0f));
		Scene.WeaponMeshAsset = CreateSkeletalMeshWithSockets(
			Test,
			{TEXT("Muzzle"), TEXT("Grip_L")},
			TEXT("Weapon"),
			FVector(0.0f, 0.0f, 50.0f));

		if (Scene.CharacterMesh && Scene.CharacterMeshAsset)
		{
			Scene.CharacterMesh->SetSkeletalMeshAsset(Scene.CharacterMeshAsset);
		}
		if (Scene.WeaponMesh && Scene.WeaponMeshAsset)
		{
			Scene.WeaponMesh->SetSkeletalMeshAsset(Scene.WeaponMeshAsset);
		}

		if (Scene.Weapon && Scene.Character)
		{
			Scene.Weapon->SetOwner(Scene.Character);
		}
		return Scene;
	}

	/** 与生产 GatherAimSignature 相同的签名采集规则（组件实时状态），供测试直接驱动判定链。 */
	FAimIKBindingSignature MakeAimSignature(const FIKBindingTestScene& Scene)
	{
		FAimIKBindingSignature Signature;
		Signature.Character = Scene.Character;
		Signature.CharacterMesh = Scene.CharacterMesh;
		Signature.CharacterMeshAsset =
			Scene.CharacterMesh ? Scene.CharacterMesh->GetSkeletalMeshAsset() : nullptr;
		Signature.Weapon = Scene.Weapon;
		Signature.WeaponOwner = Scene.Weapon ? Scene.Weapon->GetOwner() : nullptr;
		Signature.WeaponMesh = Scene.WeaponMesh;
		Signature.WeaponMeshAsset =
			Scene.WeaponMesh ? Scene.WeaponMesh->GetSkeletalMeshAsset() : nullptr;
		Signature.WeaponMeshAttachParent =
			Scene.WeaponMesh ? Scene.WeaponMesh->GetAttachParent() : nullptr;
		Signature.WeaponMeshAttachSocketName =
			Scene.WeaponMesh ? Scene.WeaponMesh->GetAttachSocketName() : NAME_None;
		Signature.HandSocketName = TEXT("hand_r");
		Signature.WeaponMuzzleSocketName =
			Scene.Weapon ? Scene.Weapon->GetMuzzleSocketName() : NAME_None;
		return Signature;
	}

	FLeftHandIKBindingSignature MakeLeftHandSignature(const FIKBindingTestScene& Scene)
	{
		FLeftHandIKBindingSignature Signature;
		Signature.Character = Scene.Character;
		Signature.CharacterMesh = Scene.CharacterMesh;
		Signature.CharacterMeshAsset =
			Scene.CharacterMesh ? Scene.CharacterMesh->GetSkeletalMeshAsset() : nullptr;
		Signature.Weapon = Scene.Weapon;
		Signature.WeaponOwner = Scene.Weapon ? Scene.Weapon->GetOwner() : nullptr;
		Signature.WeaponMesh = Scene.WeaponMesh;
		Signature.WeaponMeshAsset =
			Scene.WeaponMesh ? Scene.WeaponMesh->GetSkeletalMeshAsset() : nullptr;
		Signature.WeaponMeshAttachParent =
			Scene.WeaponMesh ? Scene.WeaponMesh->GetAttachParent() : nullptr;
		Signature.WeaponMeshAttachSocketName =
			Scene.WeaponMesh ? Scene.WeaponMesh->GetAttachSocketName() : NAME_None;
		Signature.HandSocketName = TEXT("hand_r");
		Signature.LeftHandBoneName = TEXT("hand_l");
		Signature.HandGripSocketName = TEXT("HandGrip_L");
		Signature.WeaponLeftHandGripSocketName =
			Scene.Weapon ? Scene.Weapon->GetThirdPersonLeftHandGripSocketName() : NAME_None;
		return Signature;
	}

	/** 完成武器 Mesh 附着（未注册组件直接维护 AttachParent / AttachSocketName）。 */
	void AttachWeaponMesh(const FIKBindingTestScene& Scene, FName AttachSocketName = TEXT("HandGrip_R"))
	{
		if (Scene.WeaponMesh && Scene.CharacterMesh)
		{
			Scene.WeaponMesh->AttachToComponent(
				Scene.CharacterMesh,
				FAttachmentTransformRules::KeepRelativeTransform,
				AttachSocketName);
		}
	}

	void DetachWeaponMesh(const FIKBindingTestScene& Scene)
	{
		if (Scene.WeaponMesh)
		{
			Scene.WeaponMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		}
	}
}

using namespace ShooterIKBindingAutomationTests;

/**
 * 验证矩阵：Aim / LeftHand 无 Character、无 Weapon → Unbound。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingAimUnboundTest,
	"ShootGame.Aim.Binding.AimUnbound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingAimUnboundTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("harness created"), Scene.Harness))
	{
		return false;
	}

	// 空签名（无 Character / 无 Mesh / 无 Weapon）：Unbound。
	Scene.Harness->CallUpdateAimBinding(FAimIKBindingSignature(), 0.016f);
	TestTrue(TEXT("empty signature yields Unbound"), Scene.Harness->GetAimState() == EIKBindingState::Unbound);
	TestTrue(TEXT("empty signature reason is None"), Scene.Harness->GetAimReason() == EIKBindingFailureReason::None);

	// Character 存在但无武器：Unbound（与 bHasEquippedWeapon 判定一致）。
	FAimIKBindingSignature Signature = MakeAimSignature(Scene);
	Signature.Weapon = nullptr;
	Signature.WeaponOwner = nullptr;
	Signature.WeaponMesh = nullptr;
	Signature.WeaponMeshAsset = nullptr;
	Signature.WeaponMeshAttachParent = nullptr;
	Signature.WeaponMeshAttachSocketName = NAME_None;
	Scene.Harness->CallUpdateAimBinding(Signature, 0.016f);
	TestTrue(TEXT("no weapon yields Unbound"), Scene.Harness->GetAimState() == EIKBindingState::Unbound);
	TestTrue(TEXT("no weapon reason is None"), Scene.Harness->GetAimReason() == EIKBindingFailureReason::None);

	return true;
}

/**
 * 验证矩阵：LeftHand 无 Character、无 Weapon → Unbound。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingLeftHandUnboundTest,
	"ShootGame.Aim.Binding.LeftHandUnbound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingLeftHandUnboundTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("harness created"), Scene.Harness))
	{
		return false;
	}

	Scene.Harness->CallUpdateLeftHandBinding(FLeftHandIKBindingSignature(), 0.016f);
	TestTrue(TEXT("empty signature yields Unbound"), Scene.Harness->GetLeftHandState() == EIKBindingState::Unbound);
	TestTrue(TEXT("empty signature reason is None"), Scene.Harness->GetLeftHandReason() == EIKBindingFailureReason::None);

	FLeftHandIKBindingSignature Signature = MakeLeftHandSignature(Scene);
	Signature.Weapon = nullptr;
	Signature.WeaponOwner = nullptr;
	Signature.WeaponMesh = nullptr;
	Signature.WeaponMeshAsset = nullptr;
	Signature.WeaponMeshAttachParent = nullptr;
	Signature.WeaponMeshAttachSocketName = NAME_None;
	Scene.Harness->CallUpdateLeftHandBinding(Signature, 0.016f);
	TestTrue(TEXT("no weapon yields Unbound"), Scene.Harness->GetLeftHandState() == EIKBindingState::Unbound);

	return true;
}

/**
 * 验证矩阵：Weapon 先到、Mesh 未 Attach → WaitingForAttach。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingWaitingForAttachTest,
	"ShootGame.Aim.Binding.WaitingForAttach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingWaitingForAttachTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("harness created"), Scene.Harness))
	{
		return false;
	}

	const FAimIKBindingSignature AimSignature = MakeAimSignature(Scene);
	const FLeftHandIKBindingSignature LeftHandSignature = MakeLeftHandSignature(Scene);

	Scene.Harness->CallUpdateAimBinding(AimSignature, 0.016f);
	TestTrue(TEXT("unattached weapon mesh yields Aim WaitingForAttach"),
		Scene.Harness->GetAimState() == EIKBindingState::WaitingForAttach);
	TestTrue(TEXT("WaitingForAttach reason is None"),
		Scene.Harness->GetAimReason() == EIKBindingFailureReason::None);
	TestTrue(TEXT("unattached binding clears HandToMuzzle cache"),
		Scene.Harness->GetHandToMuzzleForTest().Equals(FTransform::Identity));

	Scene.Harness->CallUpdateLeftHandBinding(LeftHandSignature, 0.016f);
	TestTrue(TEXT("unattached weapon mesh yields LeftHand WaitingForAttach"),
		Scene.Harness->GetLeftHandState() == EIKBindingState::WaitingForAttach);

	// 签名不变时不进入 Rebuild：连续多帧仍 WaitingForAttach（不每帧追逐）。
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Scene.Harness->CallUpdateAimBinding(AimSignature, 0.016f);
	}
	TestTrue(TEXT("stable signature stays WaitingForAttach"),
		Scene.Harness->GetAimState() == EIKBindingState::WaitingForAttach);

	return true;
}

/**
 * 验证矩阵：Rifle Attach 完成且计算合法 → Aim / LeftHand 均 Ready。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingRifleReadyTest,
	"ShootGame.Aim.Binding.RifleReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingRifleReadyTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("harness created"), Scene.Harness))
	{
		return false;
	}

	// 角色 socket 位于 (0,0,100)、武器 socket 位于 (0,0,50)：相对 Transform 非 Identity。
	AttachWeaponMesh(Scene);

	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	TestTrue(TEXT("rifle attach yields Aim Ready"), Scene.Harness->GetAimState() == EIKBindingState::Ready);
	TestTrue(TEXT("rifle Aim reason is None"), Scene.Harness->GetAimReason() == EIKBindingFailureReason::None);
	TestFalse(TEXT("HandToMuzzle is a real rigid transform"),
		Scene.Harness->GetHandToMuzzleForTest().Equals(FTransform::Identity));

	Scene.Harness->CallUpdateLeftHandBinding(MakeLeftHandSignature(Scene), 0.016f);
	TestTrue(TEXT("rifle attach yields LeftHand Ready"), Scene.Harness->GetLeftHandState() == EIKBindingState::Ready);
	TestTrue(TEXT("rifle LeftHand reason is None"), Scene.Harness->GetLeftHandReason() == EIKBindingFailureReason::None);
	TestFalse(TEXT("WeaponGripInRightHandSpace is a real rigid transform"),
		Scene.Harness->GetWeaponGripInRightHandSpaceForTest().Equals(FTransform::Identity));

	return true;
}

/**
 * 验证矩阵：Attach 完成但输入 / 结果数学非法 → Pending；恢复合法 → Ready 且计时清零。
 * Scale 0 使 Socket 世界变换数学非法（Scale 各分量必须大于 0）。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingPendingRecoveryTest,
	"ShootGame.Aim.Binding.PendingRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingPendingRecoveryTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("harness created"), Scene.Harness))
	{
		return false;
	}

	AttachWeaponMesh(Scene);
	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	TestTrue(TEXT("valid scene yields Aim Ready"), Scene.Harness->GetAimState() == EIKBindingState::Ready);

	// 武器 Mesh 的 socket 局部 Scale 归零：Muzzle 世界变换数学非法（Scale 必须大于 0）。
	// socket 数据不在依赖签名内：Ready 状态签名不变时零重查，状态与缓存保持 Ready。
	for (USkeletalMeshSocket* Socket : Scene.WeaponMeshAsset->GetMeshOnlySocketList())
	{
		Socket->RelativeScale = FVector::ZeroVector;
	}
	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	TestTrue(TEXT("ready signature unchanged ignores socket mutation"),
		Scene.Harness->GetAimState() == EIKBindingState::Ready);

	// 签名变化（清空 StoredSignature）触发完整 Rebuild → 输入数学非法 → Pending。
	Scene.Harness->CallClearAimSignatureForTest();
	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	TestTrue(TEXT("invalid muzzle frame yields Pending"), Scene.Harness->GetAimState() == EIKBindingState::Pending);
	TestTrue(TEXT("Pending reason is InvalidHandToMuzzle"),
		Scene.Harness->GetAimReason() == EIKBindingFailureReason::InvalidHandToMuzzle);

	// Pending 签名不变仍每帧 Rebuild；恢复合法后下一帧即 Ready。
	for (USkeletalMeshSocket* Socket : Scene.WeaponMeshAsset->GetMeshOnlySocketList())
	{
		Socket->RelativeScale = FVector::OneVector;
	}
	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	TestTrue(TEXT("recovered scene yields Ready again"), Scene.Harness->GetAimState() == EIKBindingState::Ready);
	TestTrue(TEXT("recovery resets pending elapsed time"),
		FMath::IsNearlyZero(Scene.Harness->GetAimPendingElapsed()));

	// LeftHand 同链验证：WeaponGrip 帧非法 → Pending(InvalidWeaponGripInRightHandSpace)。
	Scene.Harness->CallUpdateLeftHandBinding(MakeLeftHandSignature(Scene), 0.016f);
	TestTrue(TEXT("valid scene yields LeftHand Ready"), Scene.Harness->GetLeftHandState() == EIKBindingState::Ready);

	for (USkeletalMeshSocket* Socket : Scene.WeaponMeshAsset->GetMeshOnlySocketList())
	{
		Socket->RelativeScale = FVector::ZeroVector;
	}
	Scene.Harness->CallClearLeftHandSignatureForTest();
	Scene.Harness->CallUpdateLeftHandBinding(MakeLeftHandSignature(Scene), 0.016f);
	TestTrue(TEXT("invalid grip frame yields LeftHand Pending"),
		Scene.Harness->GetLeftHandState() == EIKBindingState::Pending);
	TestTrue(TEXT("LeftHand Pending reason is InvalidWeaponGripInRightHandSpace"),
		Scene.Harness->GetLeftHandReason() == EIKBindingFailureReason::InvalidWeaponGripInRightHandSpace);

	return true;
}

/**
 * 验证矩阵：Pending 0.75s 无 Error；1.0s Error 一次；1.25s 无第二次；仍每帧 Rebuild（计时继续累加）。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingPendingTimeoutLogTest,
	"ShootGame.Aim.Binding.PendingTimeoutLog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingPendingTimeoutLogTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("harness created"), Scene.Harness))
	{
		return false;
	}

	AttachWeaponMesh(Scene);
	for (USkeletalMeshSocket* Socket : Scene.WeaponMeshAsset->GetMeshOnlySocketList())
	{
		Socket->RelativeScale = FVector::ZeroVector;
	}
	const FAimIKBindingSignature Signature = MakeAimSignature(Scene);

	// Error 级日志会被 Automation 判为测试失败；这里显式声明为预期错误（恰好一次）。
	AddExpectedErrorPlain(
		TEXT("[IKBinding][Aim] Pending exceeded"),
		EAutomationExpectedErrorFlags::Contains,
		1);

	// 进入 Pending 的帧注入 0 秒：状态变化会重置计时，避免污染后续阈值断言。
	Scene.Harness->CallUpdateAimBinding(Signature, 0.0f);
	TestTrue(TEXT("enters Pending"), Scene.Harness->GetAimState() == EIKBindingState::Pending);

	// 0.75s：无 Error（reported 标志与 Error 日志打印是同一分支），计时继续累加。
	Scene.Harness->RunAimWaitingFrames(0.25f, 3);
	TestTrue(TEXT("0.75s no pending timeout reported"),
		!Scene.Harness->GetAimPendingReported());
	TestTrue(TEXT("0.75s still Pending"), Scene.Harness->GetAimState() == EIKBindingState::Pending);
	TestTrue(TEXT("0.75s elapsed accumulates"),
		FMath::IsNearlyEqual(Scene.Harness->GetAimPendingElapsed(), 0.75f, 1e-4f));

	// 1.0s：Error 一次（AddExpectedErrorPlain 校验恰好一次），之后继续每帧 Rebuild（计时不清零）。
	Scene.Harness->RunAimWaitingFrames(0.25f, 1);
	TestTrue(TEXT("1.0s pending timeout reported once"),
		Scene.Harness->GetAimPendingReported());
	TestTrue(TEXT("1.0s elapsed reaches threshold"),
		FMath::IsNearlyEqual(Scene.Harness->GetAimPendingElapsed(), 1.0f, 1e-4f));

	// 1.5s：无第二次 Error（bPendingTimeoutReported 保持 true；重复 Error 会违反 AddExpectedError 次数）。
	Scene.Harness->RunAimWaitingFrames(0.25f, 2);
	TestTrue(TEXT("1.5s no second pending timeout report"),
		Scene.Harness->GetAimPendingReported());
	TestTrue(TEXT("1.5s elapsed keeps accumulating"),
		FMath::IsNearlyEqual(Scene.Harness->GetAimPendingElapsed(), 1.5f, 1e-4f));

	return true;
}

/**
 * 验证矩阵：WaitingForAttach 2.75s 无 Warning；3.0s Warning 一次；3.25s 无第二次。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingWaitingForAttachWarningTest,
	"ShootGame.Aim.Binding.WaitingForAttachWarning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingWaitingForAttachWarningTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("harness created"), Scene.Harness))
	{
		return false;
	}

	Scene.Harness->WaitingForAttachWarningDelaySeconds = 3.0f;
	const FAimIKBindingSignature Signature = MakeAimSignature(Scene);
	Scene.Harness->CallUpdateAimBinding(Signature, 0.0f);
	TestTrue(TEXT("enters WaitingForAttach"), Scene.Harness->GetAimState() == EIKBindingState::WaitingForAttach);

	// Warning 报告标志与 Warning 日志打印是同一分支：2.75s 无 → 3.0s 一次 → 之后不再重复。
	Scene.Harness->RunAimWaitingFrames(0.25f, 11); // 2.75s
	TestTrue(TEXT("2.75s no waiting warning reported"),
		!Scene.Harness->GetAimWaitingReported());

	Scene.Harness->RunAimWaitingFrames(0.25f, 1); // 3.0s
	TestTrue(TEXT("3.0s waiting warning reported once"),
		Scene.Harness->GetAimWaitingReported());

	Scene.Harness->RunAimWaitingFrames(0.25f, 1); // 3.25s
	TestTrue(TEXT("3.25s no second waiting warning report"),
		Scene.Harness->GetAimWaitingReported());

	// LeftHand 独立计时：两个 Binding 同时等待时允许各出一条 Warning。
	Scene.Harness->CallUpdateLeftHandBinding(MakeLeftHandSignature(Scene), 0.0f);
	TestTrue(TEXT("LeftHand enters WaitingForAttach"),
		Scene.Harness->GetLeftHandState() == EIKBindingState::WaitingForAttach);
	for (int32 Frame = 0; Frame < 12; ++Frame)
	{
		Scene.Harness->CallUpdateLeftHandBinding(MakeLeftHandSignature(Scene), 0.25f);
	}
	TestTrue(TEXT("LeftHand waiting warning reported independently"),
		Scene.Harness->GetLeftHandWaitingReported());

	return true;
}

/**
 * 验证矩阵：Ready 时 Detach → WaitingForAttach 且 Transform 清空；重新 Attach → 完整 Rebuild。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingDetachReattachTest,
	"ShootGame.Aim.Binding.DetachReattach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingDetachReattachTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("harness created"), Scene.Harness))
	{
		return false;
	}

	AttachWeaponMesh(Scene);
	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	TestTrue(TEXT("attached yields Ready"), Scene.Harness->GetAimState() == EIKBindingState::Ready);
	const FTransform CachedHandToMuzzle = Scene.Harness->GetHandToMuzzleForTest();

	// Detach：签名中 AttachParent 变化 → Rebuild → WaitingForAttach，缓存清空。
	DetachWeaponMesh(Scene);
	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	TestTrue(TEXT("detach yields WaitingForAttach"), Scene.Harness->GetAimState() == EIKBindingState::WaitingForAttach);
	TestTrue(TEXT("detach clears HandToMuzzle cache"),
		Scene.Harness->GetHandToMuzzleForTest().Equals(FTransform::Identity));

	// 重新 Attach：签名变化 → 完整 Rebuild → Ready，缓存重新计算。
	AttachWeaponMesh(Scene);
	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	TestTrue(TEXT("reattach yields Ready again"), Scene.Harness->GetAimState() == EIKBindingState::Ready);
	TestTrue(TEXT("reattach recomputes HandToMuzzle"),
		Scene.Harness->GetHandToMuzzleForTest().Equals(CachedHandToMuzzle, 1e-4f));

	return true;
}

/**
 * 验证矩阵：同组件换 SkeletalMesh 资产 → Rebuild（签名含 MeshAsset）。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingMeshAssetChangeTest,
	"ShootGame.Aim.Binding.MeshAssetChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingMeshAssetChangeTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("harness created"), Scene.Harness))
	{
		return false;
	}

	AttachWeaponMesh(Scene);
	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	TestTrue(TEXT("original mesh yields Ready"), Scene.Harness->GetAimState() == EIKBindingState::Ready);

	// 同组件换一个没有 Muzzle socket 的资产：签名变化 → Rebuild → Unsupported(MissingMuzzle)。
	USkeletalMesh* MuzzlelessMesh = CreateSkeletalMeshWithSockets(*this, {TEXT("Grip_L")}, TEXT("Muzzleless"));
	Scene.WeaponMesh->SetSkeletalMeshAsset(MuzzlelessMesh);
	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	TestTrue(TEXT("mesh asset change rebuilds to Unsupported"),
		Scene.Harness->GetAimState() == EIKBindingState::Unsupported);
	TestTrue(TEXT("missing muzzle reason"),
		Scene.Harness->GetAimReason() == EIKBindingFailureReason::MissingMuzzle);

	// 换回原资产：签名变化 → 完整 Rebuild → Ready。
	Scene.WeaponMesh->SetSkeletalMeshAsset(Scene.WeaponMeshAsset);
	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	TestTrue(TEXT("restoring mesh asset rebuilds to Ready"),
		Scene.Harness->GetAimState() == EIKBindingState::Ready);

	return true;
}

/**
 * 验证矩阵：同 Parent 换 AttachSocketName → Rebuild（签名含 AttachSocketName）。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingAttachSocketChangeTest,
	"ShootGame.Aim.Binding.AttachSocketChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingAttachSocketChangeTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("harness created"), Scene.Harness))
	{
		return false;
	}

	AttachWeaponMesh(Scene, TEXT("HandGrip_R"));
	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	TestTrue(TEXT("initial socket yields Ready"), Scene.Harness->GetAimState() == EIKBindingState::Ready);

	// 换到角色 Mesh 上不存在的 socket：签名变化 → Rebuild → Unsupported(MissingWeaponAttachSocket)。
	AttachWeaponMesh(Scene, TEXT("NoSuchSocket"));
	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	TestTrue(TEXT("attach socket change rebuilds"),
		Scene.Harness->GetAimState() == EIKBindingState::Unsupported);
	TestTrue(TEXT("missing attach socket reason"),
		Scene.Harness->GetAimReason() == EIKBindingFailureReason::MissingWeaponAttachSocket);

	// 换回有效 socket：Ready。
	AttachWeaponMesh(Scene, TEXT("HandGrip_R"));
	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	TestTrue(TEXT("restoring attach socket yields Ready"),
		Scene.Harness->GetAimState() == EIKBindingState::Ready);

	return true;
}

/**
 * 验证矩阵：WeaponOwner 从 null 变为 Character → Unbound → 后续状态完整收敛。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingWeaponOwnerTimingTest,
	"ShootGame.Aim.Binding.WeaponOwnerTiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingWeaponOwnerTimingTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("harness created"), Scene.Harness))
	{
		return false;
	}

	// Owner 尚未到达：Unbound。
	Scene.Weapon->SetOwner(nullptr);
	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	TestTrue(TEXT("owner not yet arrived yields Unbound"),
		Scene.Harness->GetAimState() == EIKBindingState::Unbound);

	// Owner 到达（WeaponMesh 仍未附着）：签名变化 → WaitingForAttach。
	Scene.Weapon->SetOwner(Scene.Character);
	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	TestTrue(TEXT("owner arrival rebuilds to WaitingForAttach"),
		Scene.Harness->GetAimState() == EIKBindingState::WaitingForAttach);

	// 附着完成：Ready。
	AttachWeaponMesh(Scene);
	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	TestTrue(TEXT("attach after owner arrival yields Ready"),
		Scene.Harness->GetAimState() == EIKBindingState::Ready);

	return true;
}

/**
 * 验证矩阵：Pistol LeftHand = Unsupported(WeaponLeftHandGripNotConfigured)，无 Warning；
 * Aim 不受影响仍 Ready。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingPistolLeftHandTest,
	"ShootGame.Aim.Binding.PistolLeftHandUnsupported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingPistolLeftHandTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("harness created"), Scene.Harness))
	{
		return false;
	}

	Scene.Weapon->SetLeftHandGripSocketNameForTest(NAME_None);
	AttachWeaponMesh(Scene);

	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	Scene.Harness->CallUpdateLeftHandBinding(MakeLeftHandSignature(Scene), 0.016f);

	TestTrue(TEXT("pistol Aim stays Ready"), Scene.Harness->GetAimState() == EIKBindingState::Ready);
	TestTrue(TEXT("pistol LeftHand is Unsupported"),
		Scene.Harness->GetLeftHandState() == EIKBindingState::Unsupported);
	TestTrue(TEXT("pistol LeftHand reason is WeaponLeftHandGripNotConfigured"),
		Scene.Harness->GetLeftHandReason() == EIKBindingFailureReason::WeaponLeftHandGripNotConfigured);
	TestTrue(TEXT("pistol LeftHand clears grip caches"),
		Scene.Harness->GetWeaponGripInRightHandSpaceForTest().Equals(FTransform::Identity) &&
		Scene.Harness->GetHandGripInLeftHandSpaceForTest().Equals(FTransform::Identity));

	// 预期能力缺失不进入任何诊断计时分支：等待/超时报告标志保持 false（无 Warning 的同一分支）。
	TestTrue(TEXT("expected missing capability does not warn"),
		!Scene.Harness->GetLeftHandWaitingReported() && !Scene.Harness->GetLeftHandPendingReported());

	// 连续帧稳定 Unsupported：签名不变零重查。
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Scene.Harness->CallUpdateLeftHandBinding(MakeLeftHandSignature(Scene), 0.016f);
	}
	TestTrue(TEXT("pistol LeftHand stays Unsupported"),
		Scene.Harness->GetLeftHandState() == EIKBindingState::Unsupported);

	return true;
}

/**
 * 验证矩阵：缺 Muzzle → Aim = Unsupported(MissingMuzzle)，LeftHand 不受影响。
 * Unsupported 签名不变连续 N 帧：零 DoesSocketExist / GetSocketTransform 调用
 * （行为验证：补上 socket 后签名不变仍不重查，换资产后才唤醒）。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingMissingMuzzleTest,
	"ShootGame.Aim.Binding.MissingMuzzle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingMissingMuzzleTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("harness created"), Scene.Harness))
	{
		return false;
	}

	// 武器 Mesh 不含 Muzzle socket（只有握把）。
	USkeletalMesh* MuzzlelessMesh = CreateSkeletalMeshWithSockets(*this, {TEXT("Grip_L")}, TEXT("Muzzleless"));
	Scene.WeaponMesh->SetSkeletalMeshAsset(MuzzlelessMesh);
	AttachWeaponMesh(Scene);

	const FAimIKBindingSignature AimSignature = MakeAimSignature(Scene);
	Scene.Harness->CallUpdateAimBinding(AimSignature, 0.016f);
	TestTrue(TEXT("missing muzzle yields Aim Unsupported"),
		Scene.Harness->GetAimState() == EIKBindingState::Unsupported);
	TestTrue(TEXT("missing muzzle reason"), Scene.Harness->GetAimReason() == EIKBindingFailureReason::MissingMuzzle);
	TestTrue(TEXT("missing muzzle clears HandToMuzzle"),
		Scene.Harness->GetHandToMuzzleForTest().Equals(FTransform::Identity));

	Scene.Harness->CallUpdateLeftHandBinding(MakeLeftHandSignature(Scene), 0.016f);
	TestTrue(TEXT("missing muzzle does not affect LeftHand"),
		Scene.Harness->GetLeftHandState() == EIKBindingState::Ready);

	// Unsupported 签名不变：补上 Muzzle socket（asset 对象不变 → 签名不变）也不会唤醒。
	USkeletalMeshSocket* MuzzleSocket = NewObject<USkeletalMeshSocket>(MuzzlelessMesh);
	MuzzleSocket->SocketName = TEXT("Muzzle");
	MuzzleSocket->BoneName = TEXT("hand_r");
	MuzzlelessMesh->AddSocket(MuzzleSocket);

	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Scene.Harness->CallUpdateAimBinding(AimSignature, 0.016f);
	}
	TestTrue(TEXT("unchanged signature keeps Unsupported without requery"),
		Scene.Harness->GetAimState() == EIKBindingState::Unsupported);

	// 换回含 Muzzle 的资产：签名变化 → 重新检查 → Ready。
	Scene.WeaponMesh->SetSkeletalMeshAsset(Scene.WeaponMeshAsset);
	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	TestTrue(TEXT("asset change wakes Unsupported to Ready"),
		Scene.Harness->GetAimState() == EIKBindingState::Ready);

	return true;
}

/**
 * 验证矩阵：Compute 输入 / 输出恰好为 Identity：成功并允许 Ready；
 * Identity 是合法 Transform，不再被任何 IK 判定当作失败。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingIdentityFrameTest,
	"ShootGame.Aim.Binding.IdentityFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingIdentityFrameTest::RunTest(const FString& Parameters)
{
	// 纯函数层：Identity 数学合法，Compute 对 Identity 输入成功。
	TestTrue(TEXT("Identity is mathematically valid"),
		UShooterThirdPersonAnimInstance::IsMathematicallyValidBindingFrame(FTransform::Identity));
	TestTrue(TEXT("Identity Compute input yields Identity output"),
		UShooterThirdPersonAnimInstance::ComputeHandToMuzzleTransform(
			FTransform::Identity,
			FTransform::Identity).Equals(FTransform::Identity));

	const FTransform ValidMuzzleWorld(FVector(100.0f, 200.0f, 300.0f));
	TestTrue(TEXT("Identity hand world computes rigid relative transform"),
		UShooterThirdPersonAnimInstance::ComputeHandToMuzzleTransform(
			FTransform::Identity,
			ValidMuzzleWorld).Equals(ValidMuzzleWorld, 1e-4f));

	TestTrue(TEXT("IsUsableFrame accepts Identity as usable"),
		FShooterLeftHandIKMath::IsUsableFrame(FTransform::Identity));

	// Binding 层：HandWorld / MuzzleWorld 恰好都为 Identity → Ready，HandToMuzzle == Identity。
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("harness created"), Scene.Harness))
	{
		return false;
	}

	// 所有 socket 的局部位置归零：未注册组件 GetBoneTransform 返回 Identity，
	// 因此 socket 世界变换 = Identity，两个参考帧恰好都为 Identity。
	for (USkeletalMeshSocket* Socket : Scene.CharacterMeshAsset->GetMeshOnlySocketList())
	{
		Socket->RelativeLocation = FVector::ZeroVector;
	}
	for (USkeletalMeshSocket* Socket : Scene.WeaponMeshAsset->GetMeshOnlySocketList())
	{
		Socket->RelativeLocation = FVector::ZeroVector;
	}
	AttachWeaponMesh(Scene);
	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	TestTrue(TEXT("identity world frames yield Aim Ready"),
		Scene.Harness->GetAimState() == EIKBindingState::Ready);
	TestTrue(TEXT("identity result HandToMuzzle is allowed while Ready"),
		Scene.Harness->GetHandToMuzzleForTest().Equals(FTransform::Identity));

	return true;
}

/**
 * 验证矩阵：AimDirection == Zero / AimTarget 失效：Binding 状态不变，仅 bAimIKEnabled = false。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingEnabledInputSeparationTest,
	"ShootGame.Aim.Binding.EnabledInputSeparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingEnabledInputSeparationTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("harness created"), Scene.Harness))
	{
		return false;
	}

	AttachWeaponMesh(Scene);
	Scene.Harness->CallUpdateAimBinding(MakeAimSignature(Scene), 0.016f);
	Scene.Harness->CallUpdateLeftHandBinding(MakeLeftHandSignature(Scene), 0.016f);
	TestTrue(TEXT("binding reaches Ready"), Scene.Harness->GetAimState() == EIKBindingState::Ready);

	// 帧级输入：本地拥有者路径（NewObject Character 非本地控制；bAimTargetWorldValid 可注入）。
	// AimDirection == Zero：Enabled 关闭，Binding 状态不变。
	Scene.Harness->AimDirectionWorld = FVector::ZeroVector;
	Scene.Harness->bAimTargetWorldValid = true;
	Scene.Harness->CallRefreshIKEnabled(Scene.Character);
	TestFalse(TEXT("zero aim direction disables bAimIKEnabled"), Scene.Harness->bAimIKEnabled);
	TestTrue(TEXT("zero aim direction keeps Binding Ready"),
		Scene.Harness->GetAimState() == EIKBindingState::Ready);

	// AimTarget 失效：Enabled 关闭，Binding 状态不变。
	Scene.Harness->AimDirectionWorld = FVector(0.5f, 0.5f, 0.70710678f).GetSafeNormal();
	Scene.Harness->bAimTargetWorldValid = false;
	Scene.Harness->CallRefreshIKEnabled(Scene.Character);
	TestFalse(TEXT("invalid aim target disables bAimIKEnabled"), Scene.Harness->bAimIKEnabled);
	TestTrue(TEXT("invalid aim target keeps Binding Ready"),
		Scene.Harness->GetAimState() == EIKBindingState::Ready);

	// 两者都有效：Enabled 开启（非本地控制 + 有效目标）。
	Scene.Harness->bAimTargetWorldValid = true;
	Scene.Harness->CallRefreshIKEnabled(Scene.Character);
	TestTrue(TEXT("valid aim input enables bAimIKEnabled"), Scene.Harness->bAimIKEnabled);

	// LeftHand 只由 Binding 决定。
	Scene.Harness->CallRefreshIKEnabled(Scene.Character);
	TestTrue(TEXT("LeftHand Ready enables bLeftHandIKEnabled"), Scene.Harness->bLeftHandIKEnabled);

	// 无 Character 复位：全部输出归零。
	Scene.Harness->CallResetBindingsAndOutputs();
	TestTrue(TEXT("reset clears Aim Binding"), Scene.Harness->GetAimState() == EIKBindingState::Unbound);
	TestTrue(TEXT("reset clears LeftHand Binding"), Scene.Harness->GetLeftHandState() == EIKBindingState::Unbound);
	TestFalse(TEXT("reset clears bAimIKEnabled"), Scene.Harness->bAimIKEnabled);
	TestFalse(TEXT("reset clears bLeftHandIKEnabled"), Scene.Harness->bLeftHandIKEnabled);
	TestTrue(TEXT("reset clears HandToMuzzle"),
		Scene.Harness->GetHandToMuzzleForTest().Equals(FTransform::Identity));

	return true;
}

/**
 * 验证矩阵：Pistol（无握把）与 Rifle（齐全）的 LeftHand 差异已由
 * PistolLeftHandUnsupported / RifleReady 覆盖；此处锁定“Rifle LeftHand Ready 允许 Identity 帧”
 * （角色 hand_l 与 HandGrip_L 在同一组件时结果可为 Identity，仍算 Ready）。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingLeftHandIdentityGripTest,
	"ShootGame.Aim.Binding.LeftHandIdentityGrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingLeftHandIdentityGripTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("harness created"), Scene.Harness))
	{
		return false;
	}

	// hand_l 与 HandGrip_L 属于同一组件且 socket 局部位置相同 → 参考帧相同 → Identity 结果合法；
	// 武器 socket 位于 (0,0,50)、角色 socket 位于 (0,0,100) → 握把相对 hand_r 为非 Identity 刚性关系。
	AttachWeaponMesh(Scene);
	Scene.Harness->CallUpdateLeftHandBinding(MakeLeftHandSignature(Scene), 0.016f);
	TestTrue(TEXT("left hand reaches Ready"), Scene.Harness->GetLeftHandState() == EIKBindingState::Ready);

	// hand_l 与 HandGrip_L 属于同一组件且无骨骼绑定 → 参考帧相同 → Identity 结果合法。
	TestTrue(TEXT("identity HandGripInLeftHandSpace is allowed while Ready"),
		Scene.Harness->GetHandGripInLeftHandSpaceForTest().Equals(FTransform::Identity));
	TestTrue(TEXT("grip in right hand space is a real rigid transform"),
		!Scene.Harness->GetWeaponGripInRightHandSpaceForTest().Equals(FTransform::Identity));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
