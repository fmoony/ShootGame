// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimInstance.h"
#include "Characters/Animation/ShooterAnimInstanceBase.h"
#include "Characters/Animation/ShooterFirstPersonAnimInstance.h"
#include "Characters/Animation/ShooterThirdPersonAnimInstance.h"
#include "UObject/UnrealType.h"

/**
 * R6.1 C++ 类型层次：第三/第一人称 AnimInstance 必须共享薄公共基类，
 * 第一人称专用数据不得上提到第三人称，公共基类也不得包含视角专用状态。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterAnimInstanceHierarchyTest,
	"ShootGame.Animation.InstanceHierarchy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAnimInstanceHierarchyTest::RunTest(const FString& Parameters)
{
	TestTrue(
		TEXT("AnimInstanceBase derives from UAnimInstance"),
		UShooterAnimInstanceBase::StaticClass()->IsChildOf(UAnimInstance::StaticClass()));
	TestTrue(
		TEXT("FirstPersonAnimInstance derives from AnimInstanceBase"),
		UShooterFirstPersonAnimInstance::StaticClass()->IsChildOf(UShooterAnimInstanceBase::StaticClass()));
	TestTrue(
		TEXT("ThirdPersonAnimInstance derives from AnimInstanceBase"),
		UShooterThirdPersonAnimInstance::StaticClass()->IsChildOf(UShooterAnimInstanceBase::StaticClass()));

	// 共享基类只承载两边都消费的值快照。
	const TCHAR* SharedPropertyNames[] = {
		TEXT("GroundSpeed"),
		TEXT("bIsInAir"),
		TEXT("bHasEquippedWeapon"),
		TEXT("bIsFiring"),
		TEXT("bIsReloading"),
		TEXT("bIsEquipping"),
		TEXT("bIsDead"),
		TEXT("CurrentWeaponActor"),
	};
	for (const TCHAR* PropertyName : SharedPropertyNames)
	{
		const FProperty* Property = FindFProperty<FProperty>(
			UShooterAnimInstanceBase::StaticClass(),
			PropertyName);
		TestNotNull(
			FString::Printf(TEXT("AnimInstanceBase exposes %s"), PropertyName),
			Property);
	}

	// 第一人称专用状态只在 FirstPerson 子类。
	TestNotNull(
		TEXT("FirstPersonAnimInstance exposes bFirstPersonDataValid"),
		FindFProperty<FBoolProperty>(UShooterFirstPersonAnimInstance::StaticClass(), TEXT("bFirstPersonDataValid")));
	TestNull(
		TEXT("FirstPerson validity is not leaked into shared base"),
		FindFProperty<FProperty>(UShooterAnimInstanceBase::StaticClass(), TEXT("bFirstPersonDataValid")));

	// 第三人称专用 Aim/LeftHand 数据仍只属于 ThirdPerson。
	TestNotNull(
		TEXT("ThirdPersonAnimInstance exposes AimDirectionWorld"),
		FindFProperty<FProperty>(UShooterThirdPersonAnimInstance::StaticClass(), TEXT("AimDirectionWorld")));
	TestNull(
		TEXT("AimDirectionWorld is not leaked into shared base"),
		FindFProperty<FProperty>(UShooterAnimInstanceBase::StaticClass(), TEXT("AimDirectionWorld")));
	TestNull(
		TEXT("HandToMuzzle is not leaked into shared base"),
		FindFProperty<FProperty>(UShooterAnimInstanceBase::StaticClass(), TEXT("HandToMuzzle")));

	// 公共基类不得包含 RPC 或复制属性。
	for (TFieldIterator<UFunction> It(UShooterAnimInstanceBase::StaticClass()); It; ++It)
	{
		if (It->HasAnyFunctionFlags(FUNC_Net))
		{
			TestFalse(
				FString::Printf(TEXT("AnimInstanceBase function %s must not be a net function"), *It->GetName()),
				true);
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
