// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Characters/Animation/AnimNodes/ShooterLeftHandIKMath.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Weapons/ShooterWeapon.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"
#include "UObject/UnrealType.h"
#include "ShooterIKBindingTestHarness.h"

/**
 * E4 验证：第三人称 IK 静态 Binding 改为事件驱动。
 *
 * 覆盖：
 *   - OnWeaponPresentationChanged 到达时重建 / 空事件清空；
 *   - 初始化回放读取 Equipment.CurrentWeaponActor；
 *   - 缺失 Socket 只关闭对应 IK，且不逐帧重试，下一次事件才唤醒；
 *   - 骨骼 Transform 瞬时非法时只保留一个 Pending Bool，下一次更新重试一次；
 *   - Detach 等静态结构变化在新事件前不被逐帧轮询。
 */
namespace ShooterIKBindingAutomationTests
{
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

		FReferenceSkeleton RefSkeleton;
		FReferenceSkeletonModifier Modifier(RefSkeleton, nullptr);
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("hand_r"), TEXT("hand_r"), 0), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("hand_l"), TEXT("hand_l"), 0), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("HandGrip_L"), TEXT("HandGrip_L"), 0), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("HandGrip_R"), TEXT("HandGrip_R"), 0), FTransform::Identity);
		Mesh->SetRefSkeleton(Modifier.GetReferenceSkeleton());
		Mesh->GetRefSkeleton().RebuildRefSkeleton(nullptr, true);
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

	FIKBindingTestScene CreateRifleScene(FAutomationTestBase& Test)
	{
		FIKBindingTestScene Scene;
		Scene.Character = NewObject<AShooterIKBindingTestCharacter>();
		Scene.Weapon = NewObject<AShooterIKBindingTestWeapon>();
		Scene.CharacterMesh = Scene.Character ? Scene.Character->GetMesh() : nullptr;
		Scene.WeaponMesh = Scene.Weapon ? Scene.Weapon->GetThirdPersonMesh() : nullptr;
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

		// 初始化 AnimInstance 的 Owning Character 缓存与表现事件订阅。
		if (Scene.Harness)
		{
			Scene.Harness->CallNativeInitializeAnimationForTest();
		}
		return Scene;
	}

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
 * E4 验证：空表现事件 / 无武器回放清空全部静态 Binding 与 IK 开关。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingEventClearTest,
	"ShootGame.Aim.Binding.EventClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingEventClearTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("event harness created"), Scene.Harness))
	{
		return false;
	}

	Scene.Harness->CallHandleWeaponPresentationChanged(nullptr, nullptr);
	TestFalse(TEXT("empty event disables Aim binding"), Scene.Harness->IsAimBindingValidForTest());
	TestFalse(TEXT("empty event disables LeftHand binding"), Scene.Harness->IsLeftHandBindingValidForTest());
	TestNull(TEXT("empty event clears cached weapon"), Scene.Harness->GetCachedPresentationWeaponForTest());
	TestTrue(TEXT("empty event clears HandToMuzzle"), Scene.Harness->HandToMuzzle.Equals(FTransform::Identity));
	TestTrue(TEXT("empty event clears public grips"),
		Scene.Harness->LeftHandGripInRightHandSpace.Equals(FTransform::Identity) &&
		Scene.Harness->HandGripInLeftHandSpace.Equals(FTransform::Identity));
	TestFalse(TEXT("empty event disables bAimIKEnabled"), Scene.Harness->bAimIKEnabled);
	TestFalse(TEXT("empty event disables bLeftHandIKEnabled"), Scene.Harness->bLeftHandIKEnabled);

	// 初始化回放在 Equipment 无武器时同样清空。
	Scene.Harness->CallReplayWeaponPresentationState(Scene.Character);
	TestFalse(TEXT("empty replay disables Aim binding"), Scene.Harness->IsAimBindingValidForTest());
	TestFalse(TEXT("empty replay disables LeftHand binding"), Scene.Harness->IsLeftHandBindingValidForTest());

	return true;
}

/**
 * E4 验证：Rifle 表现事件到达时一次性重建 Aim + LeftHand 静态 Binding。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingEventRifleRebuildTest,
	"ShootGame.Aim.Binding.EventRifleRebuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingEventRifleRebuildTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("event harness created"), Scene.Harness))
	{
		return false;
	}

	AttachWeaponMesh(Scene);
	Scene.Harness->CallHandleWeaponPresentationChanged(nullptr, Scene.Weapon);

	TestTrue(TEXT("rifle event caches current weapon"), Scene.Harness->GetCachedPresentationWeaponForTest() == Scene.Weapon);
	TestTrue(TEXT("rifle event enables Aim binding"), Scene.Harness->IsAimBindingValidForTest());
	TestTrue(TEXT("rifle event enables LeftHand binding"), Scene.Harness->IsLeftHandBindingValidForTest());
	TestFalse(TEXT("rifle event computes non-identity HandToMuzzle"),
		Scene.Harness->HandToMuzzle.Equals(FTransform::Identity));
	TestFalse(TEXT("rifle event computes non-identity grip in right hand space"),
		Scene.Harness->LeftHandGripInRightHandSpace.Equals(FTransform::Identity));
	TestTrue(TEXT("same component left hand grip frame may be identity and stays valid"),
		Scene.Harness->HandGripInLeftHandSpace.Equals(FTransform::Identity));
	TestFalse(TEXT("rifle event leaves no pending retry"), Scene.Harness->HasPendingRebuildForTest());

	Scene.Harness->CallClearWeaponStaticBindings();
	TestFalse(TEXT("clear disables Aim binding"), Scene.Harness->IsAimBindingValidForTest());
	TestNull(TEXT("clear drops cached weapon"), Scene.Harness->GetCachedPresentationWeaponForTest());
	TestTrue(TEXT("clear resets public HandToMuzzle"), Scene.Harness->HandToMuzzle.Equals(FTransform::Identity));
	TestTrue(TEXT("clear resets public grips"),
		Scene.Harness->LeftHandGripInRightHandSpace.Equals(FTransform::Identity) &&
		Scene.Harness->HandGripInLeftHandSpace.Equals(FTransform::Identity));

	return true;
}

/**
 * E4 验证：缺失 Muzzle 只关闭 Aim IK；同一资产事后补上 Socket 也不会逐帧重试，
 * 只有下一次表现事件才重新判定。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingEventMissingMuzzleTest,
	"ShootGame.Aim.Binding.EventMissingMuzzleNoPolling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingEventMissingMuzzleTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("event harness created"), Scene.Harness))
	{
		return false;
	}

	USkeletalMesh* MuzzlelessMesh = CreateSkeletalMeshWithSockets(*this, {TEXT("Grip_L")}, TEXT("Muzzleless"));
	Scene.WeaponMesh->SetSkeletalMeshAsset(MuzzlelessMesh);
	AttachWeaponMesh(Scene);

	Scene.Harness->CallHandleWeaponPresentationChanged(nullptr, Scene.Weapon);
	TestFalse(TEXT("missing muzzle disables Aim binding"), Scene.Harness->IsAimBindingValidForTest());
	TestTrue(TEXT("missing muzzle keeps LeftHand binding"), Scene.Harness->IsLeftHandBindingValidForTest());
	TestTrue(TEXT("missing muzzle clears HandToMuzzle"), Scene.Harness->HandToMuzzle.Equals(FTransform::Identity));
	TestFalse(TEXT("permanent socket missing does not create pending retry"), Scene.Harness->HasPendingRebuildForTest());

	// 同一资产事后补上 Muzzle：无新表现事件时不得逐帧重试。
	USkeletalMeshSocket* MuzzleSocket = NewObject<USkeletalMeshSocket>(MuzzlelessMesh);
	MuzzleSocket->SocketName = TEXT("Muzzle");
	MuzzleSocket->BoneName = TEXT("hand_r");
	MuzzlelessMesh->AddSocket(MuzzleSocket);
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Scene.Harness->CallProductionPendingRetryForTest(Scene.Weapon);
	}
	TestFalse(TEXT("no per-frame requery after missing socket"), Scene.Harness->IsAimBindingValidForTest());

	// 下一次表现事件到达才重建。
	Scene.Harness->CallHandleWeaponPresentationChanged(nullptr, Scene.Weapon);
	TestTrue(TEXT("next event wakes Aim binding after socket restored"), Scene.Harness->IsAimBindingValidForTest());

	return true;
}

/**
 * E4 验证：Pistol 未配置握把是预期能力缺失，只关闭 LeftHand IK，Aim IK 不受影响。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingEventPistolNoGripTest,
	"ShootGame.Aim.Binding.EventPistolNoGrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingEventPistolNoGripTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("event harness created"), Scene.Harness))
	{
		return false;
	}

	Scene.Weapon->SetLeftHandGripSocketNameForTest(NAME_None);
	AttachWeaponMesh(Scene);
	Scene.Harness->CallHandleWeaponPresentationChanged(nullptr, Scene.Weapon);

	TestTrue(TEXT("pistol keeps Aim binding"), Scene.Harness->IsAimBindingValidForTest());
	TestFalse(TEXT("pistol disables LeftHand binding"), Scene.Harness->IsLeftHandBindingValidForTest());
	TestTrue(TEXT("pistol clears grip caches"),
		Scene.Harness->LeftHandGripInRightHandSpace.Equals(FTransform::Identity) &&
		Scene.Harness->HandGripInLeftHandSpace.Equals(FTransform::Identity));
	TestFalse(TEXT("expected missing capability does not create pending retry"), Scene.Harness->HasPendingRebuildForTest());

	return true;
}

/**
 * E4 验证：事件重建时骨骼 Transform 瞬时非法只置 Pending，下一次普通更新重试一次；
 * 恢复后成功即清 Pending；再次消费不会重复重建。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingEventPendingSingleRetryTest,
	"ShootGame.Aim.Binding.EventPendingSingleRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingEventPendingSingleRetryTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("event harness created"), Scene.Harness))
	{
		return false;
	}

	AttachWeaponMesh(Scene);

	// 武器 socket 局部 Scale 归零：世界 Transform 数学非法。
	for (USkeletalMeshSocket* Socket : Scene.WeaponMeshAsset->GetMeshOnlySocketList())
	{
		Socket->RelativeScale = FVector::ZeroVector;
	}
	Scene.Harness->CallHandleWeaponPresentationChanged(nullptr, Scene.Weapon);
	TestTrue(TEXT("invalid frame enters pending retry"), Scene.Harness->HasPendingRebuildForTest());
	TestFalse(TEXT("invalid frame disables Aim binding"), Scene.Harness->IsAimBindingValidForTest());
	TestFalse(TEXT("invalid frame disables LeftHand binding"), Scene.Harness->IsLeftHandBindingValidForTest());

	// 恢复后只消费一次 Pending：成功并清空 Pending。
	for (USkeletalMeshSocket* Socket : Scene.WeaponMeshAsset->GetMeshOnlySocketList())
	{
		Socket->RelativeScale = FVector::OneVector;
	}
	Scene.Harness->CallProductionPendingRetryForTest(Scene.Weapon);
	TestTrue(TEXT("single retry rebuilds Aim binding"), Scene.Harness->IsAimBindingValidForTest());
	TestTrue(TEXT("single retry rebuilds LeftHand binding"), Scene.Harness->IsLeftHandBindingValidForTest());
	TestFalse(TEXT("success clears pending retry"), Scene.Harness->HasPendingRebuildForTest());

	// 无 Pending 时后续帧零重建。
	Scene.Harness->CallProductionPendingRetryForTest(Scene.Weapon);
	TestTrue(TEXT("no pending means no extra rebuild"), Scene.Harness->IsAimBindingValidForTest());

	return true;
}

/**
 * E4 验证：事件后 Detach 不会逐帧轮询；下一次表现事件才重建（本帧表现为已完成的旧 Binding 被清空）。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingEventDetachNoPollingTest,
	"ShootGame.Aim.Binding.EventDetachNoPolling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingEventDetachNoPollingTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("event harness created"), Scene.Harness))
	{
		return false;
	}

	AttachWeaponMesh(Scene);
	Scene.Harness->CallHandleWeaponPresentationChanged(nullptr, Scene.Weapon);
	TestTrue(TEXT("attached event enables binding"), Scene.Harness->IsAimBindingValidForTest());

	// Detach 后没有新事件：不得逐帧扫描 AttachParent。
	DetachWeaponMesh(Scene);
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Scene.Harness->CallProductionPendingRetryForTest(Scene.Weapon);
	}
	TestTrue(TEXT("no per-frame attach polling before next event"), Scene.Harness->IsAimBindingValidForTest());

	// 新表现事件到达：按当前静态结构重建并清空失效 Binding。
	Scene.Harness->CallHandleWeaponPresentationChanged(nullptr, Scene.Weapon);
	TestFalse(TEXT("next event sees detached structure and disables Aim"), Scene.Harness->IsAimBindingValidForTest());
	TestFalse(TEXT("next event sees detached structure and disables LeftHand"), Scene.Harness->IsLeftHandBindingValidForTest());
	TestTrue(TEXT("detached event clears HandToMuzzle"), Scene.Harness->HandToMuzzle.Equals(FTransform::Identity));

	return true;
}

/**
 * E4 验证：初始化回放主动读取 Equipment.CurrentWeaponActor，不依赖以后一定来一个事件。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingEventInitReplayTest,
	"ShootGame.Aim.Binding.EventInitReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingEventInitReplayTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("event harness created"), Scene.Harness))
	{
		return false;
	}

	AttachWeaponMesh(Scene);

	// 模拟 AnimClass 替换后新 AnimInstance 初始化时 Equipment 已有当前武器。
	FObjectProperty* CurrentWeaponProperty = FindFProperty<FObjectProperty>(
		UShooterEquipmentComponent::StaticClass(),
		TEXT("CurrentWeaponActor"));
	if (!TestNotNull(TEXT("Equipment exposes CurrentWeaponActor for replay injection"), CurrentWeaponProperty))
	{
		return false;
	}
	CurrentWeaponProperty->SetObjectPropertyValue_InContainer(Scene.Character->GetEquipmentComponent(), Scene.Weapon);

	Scene.Harness->CallReplayWeaponPresentationState(Scene.Character);
	TestTrue(TEXT("init replay rebuilds Aim binding from Equipment"), Scene.Harness->IsAimBindingValidForTest());
	TestTrue(TEXT("init replay rebuilds LeftHand binding from Equipment"), Scene.Harness->IsLeftHandBindingValidForTest());
	TestTrue(TEXT("init replay caches current weapon"), Scene.Harness->GetCachedPresentationWeaponForTest() == Scene.Weapon);

	CurrentWeaponProperty->SetObjectPropertyValue_InContainer(Scene.Character->GetEquipmentComponent(), nullptr);
	Scene.Harness->CallReplayWeaponPresentationState(Scene.Character);
	TestFalse(TEXT("empty replay clears Aim binding"), Scene.Harness->IsAimBindingValidForTest());
	TestFalse(TEXT("empty replay clears LeftHand binding"), Scene.Harness->IsLeftHandBindingValidForTest());

	return true;
}

/**
 * E4 验证：帧级 Aim 输入与静态 Binding 分离；Identity 仍是合法 Transform。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingEventInputSeparationTest,
	"ShootGame.Aim.Binding.EventInputSeparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingEventInputSeparationTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Identity is a valid binding frame"),
		UShooterThirdPersonAnimInstance::IsMathematicallyValidBindingFrame(FTransform::Identity));
	TestTrue(TEXT("Identity input yields identity HandToMuzzle"),
		UShooterThirdPersonAnimInstance::ComputeHandToMuzzleTransform(
			FTransform::Identity,
			FTransform::Identity).Equals(FTransform::Identity));
	TestTrue(TEXT("Identity input yields identity left hand grip"),
		UShooterThirdPersonAnimInstance::ComputeLeftHandGripInRightHandSpace(
			FTransform::Identity,
			FTransform::Identity).Equals(FTransform::Identity));

	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("event harness created"), Scene.Harness))
	{
		return false;
	}

	AttachWeaponMesh(Scene);
	Scene.Harness->CallHandleWeaponPresentationChanged(nullptr, Scene.Weapon);
	TestTrue(TEXT("event enables Aim binding"), Scene.Harness->IsAimBindingValidForTest());
	TestTrue(TEXT("event enables LeftHand binding"), Scene.Harness->IsLeftHandBindingValidForTest());

	Scene.Harness->AimDirectionWorld = FVector::ZeroVector;
	Scene.Harness->bAimTargetWorldValid = true;
	Scene.Harness->CallRefreshIKEnabled();
	TestFalse(TEXT("zero aim direction disables only bAimIKEnabled"), Scene.Harness->bAimIKEnabled);
	TestTrue(TEXT("zero aim direction keeps static Aim binding"), Scene.Harness->IsAimBindingValidForTest());
	TestTrue(TEXT("left hand static binding is independent"), Scene.Harness->bLeftHandIKEnabled);

	// Refresh 不再解释网络角色或直接读取 bAimTargetWorldValid；
	// 动态输入有效性由 ResolveAimPresentationInput 统一表达为非零有限 AimDirectionWorld。
	Scene.Harness->AimDirectionWorld = FVector(0.5f, 0.5f, 0.70710678f).GetSafeNormal();
	Scene.Harness->bAimTargetWorldValid = false;
	Scene.Harness->CallRefreshIKEnabled();
	TestTrue(TEXT("valid dynamic direction enables bAimIKEnabled"), Scene.Harness->bAimIKEnabled);

	return true;
}


/**
 * 阶段 3：Equipment 当前值已变化但尚未发布表现事件时，AnimInstance 不得主动调用
 * Character::EnsureWeaponPresentation 做反向表现修复；表现收敛只由生命周期入口负责。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterIKBindingNoReversePresentationRepairTest,
	"ShootGame.Aim.Binding.NoReversePresentationRepair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterIKBindingNoReversePresentationRepairTest::RunTest(const FString& Parameters)
{
	FIKBindingTestScene Scene = CreateRifleScene(*this);
	if (!TestNotNull(TEXT("no reverse repair harness created"), Scene.Harness))
	{
		return false;
	}

	AttachWeaponMesh(Scene);
	Scene.Harness->CallHandleWeaponPresentationChanged(nullptr, Scene.Weapon);
	TestTrue(TEXT("event establishes Aim binding"), Scene.Harness->IsAimBindingValidForTest());
	TestTrue(TEXT("event establishes LeftHand binding"), Scene.Harness->IsLeftHandBindingValidForTest());
	TestFalse(TEXT("weapon starts visible for stale/repair observation"), Scene.Weapon->IsHidden());

	// 把 Equipment 逻辑当前值改为 null，但不广播表现完成事件：
	// 这模拟“AnimInstance 每帧先看到逻辑变化，生命周期事件尚未到达”的窗口。
	FObjectProperty* CurrentWeaponProperty = FindFProperty<FObjectProperty>(
		UShooterEquipmentComponent::StaticClass(),
		TEXT("CurrentWeaponActor"));
	if (!TestNotNull(TEXT("Equipment exposes CurrentWeaponActor for mismatch injection"), CurrentWeaponProperty))
	{
		return false;
	}
	CurrentWeaponProperty->SetObjectPropertyValue_InContainer(
		Scene.Character->GetEquipmentComponent(),
		nullptr);
	TestNull(TEXT("logical current weapon becomes null"), Scene.Character->GetCurrentWeaponActor());

	Scene.Harness->NativeUpdateAnimation(1.0f / 60.0f);

	TestTrue(TEXT("tick keeps cached weapon without reverse repair"), Scene.Harness->GetCachedPresentationWeaponForTest() == Scene.Weapon);
	TestTrue(TEXT("tick keeps Aim binding until lifecycle event"), Scene.Harness->IsAimBindingValidForTest());
	TestTrue(TEXT("tick keeps LeftHand binding until lifecycle event"), Scene.Harness->IsLeftHandBindingValidForTest());
	TestFalse(TEXT("tick does not deactivate/hide weapon through Character"), Scene.Weapon->IsHidden());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
