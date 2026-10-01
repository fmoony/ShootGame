// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "GameFramework/PlayerState/ShooterPlayerState.h"
#include "Tests/Animation/ShooterIKBindingTestHarness.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterReloadIdentityAuthorityTest, "ShootGame.ReloadIdentity.Authority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterReloadIdentityAuthorityTest::RunTest(const FString& Parameters)
{
	AShooterPlayerState* PlayerState = NewObject<AShooterPlayerState>();
	TestEqual(TEXT("Initial reload identity is zero"), PlayerState->GetReloadId(), uint32(0));
	PlayerState->AdvanceAcceptedReloadId();
	TestEqual(TEXT("Authority advances one accepted identity"), PlayerState->GetReloadId(), uint32(1));
	PlayerState->AdvanceAcceptedReloadId();
	TestEqual(TEXT("Next accepted identity advances once"), PlayerState->GetReloadId(), uint32(2));
	PlayerState->SetRole(ROLE_SimulatedProxy);
	PlayerState->AdvanceAcceptedReloadId();
	TestEqual(TEXT("Client cannot advance identity"), PlayerState->GetReloadId(), uint32(2));

	const FProperty* Property = FindFProperty<FProperty>(AShooterPlayerState::StaticClass(), TEXT("ReloadId"));
	if (TestNotNull(TEXT("PlayerState declares reload identity"), Property))
	{
		TestTrue(TEXT("Identity participates in replication"), Property->HasAnyPropertyFlags(CPF_Net));
		TestEqual(TEXT("Identity has diagnostic RepNotify"), Property->RepNotifyFunc, FName(TEXT("OnRep_ReloadId")));
	}
	TestNull(TEXT("Accept identity mutation is not a client RPC"),
		AShooterPlayerState::StaticClass()->FindFunctionByName(TEXT("AdvanceAcceptedReloadId")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterReloadIdentityContinuousTagTest, "ShootGame.ReloadIdentity.ContinuousTag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterReloadIdentityContinuousTagTest::RunTest(const FString& Parameters)
{
	AShooterIKBindingTestCharacter* Character = NewObject<AShooterIKBindingTestCharacter>();
	AShooterPlayerState* PlayerState = NewObject<AShooterPlayerState>();
	Character->SetPlayerState(PlayerState);
	UShooterIKBindingTestHarness* Anim = NewObject<UShooterIKBindingTestHarness>(Character->GetMesh());
	Anim->CallNativeInitializeAnimationForTest();
	Anim->bIsReloading = true;
	Anim->bReloadPresentationRecovering = true;
	PlayerState->AdvanceAcceptedReloadId();
	Anim->CallRefreshReloadIdentityForTest(Character);
	TestEqual(TEXT("First accepted identity observed"), Anim->GetObservedReloadId(), uint32(1));
	TestFalse(TEXT("Old recovery cleared on identity"), Anim->bReloadPresentationRecovering);
	TestTrue(TEXT("Real graph initialization requested"), Anim->HasReloadGraphInitializationPendingForTest());
	TestEqual(TEXT("First identity consumed once"), Anim->GetNewReloadPresentationCountForAutomationTest(), 1);
	Anim->CallRefreshReloadIdentityForTest(Character);
	TestEqual(TEXT("Repeated ID is consumed once"), Anim->GetNewReloadPresentationCountForAutomationTest(), 1);

	// 不经过 false：这是本轮结构问题的针对性覆盖，不用镜像判断函数代替生产入口。
	Anim->bReloadPresentationRecovering = true;
	PlayerState->AdvanceAcceptedReloadId();
	Anim->CallRefreshReloadIdentityForTest(Character);
	TestTrue(TEXT("Gameplay tag snapshot remains true"), Anim->bIsReloading);
	TestFalse(TEXT("Recovery does not cross the new action"), Anim->bReloadPresentationRecovering);
	TestEqual(TEXT("Second identity is distinct"), Anim->GetObservedReloadId(), uint32(2));
	TestEqual(TEXT("Second identity consumed once"), Anim->GetNewReloadPresentationCountForAutomationTest(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterReloadIdentityArrivalOrderTest, "ShootGame.ReloadIdentity.ArrivalOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterReloadIdentityArrivalOrderTest::RunTest(const FString& Parameters)
{
	AShooterIKBindingTestCharacter* Character = NewObject<AShooterIKBindingTestCharacter>();
	AShooterPlayerState* PlayerState = NewObject<AShooterPlayerState>();
	Character->SetPlayerState(PlayerState);
	UShooterIKBindingTestHarness* Anim = NewObject<UShooterIKBindingTestHarness>(Character->GetMesh());
	Anim->CallNativeInitializeAnimationForTest();
	PlayerState->AdvanceAcceptedReloadId();
	Anim->bIsReloading = false;
	Anim->CallRefreshReloadIdentityForTest(Character);
	TestEqual(TEXT("Identity is readable before the tag"), Anim->GetObservedReloadId(), uint32(1));
	TestEqual(TEXT("ID alone does not start Reload"), Anim->GetNewReloadPresentationCountForAutomationTest(), 0);
	Anim->bIsReloading = true;
	Anim->CallRefreshReloadIdentityForTest(Character);
	TestEqual(TEXT("Pending ID starts with Tag"), Anim->GetNewReloadPresentationCountForAutomationTest(), 1);
	Anim->CallRefreshReloadIdentityForTest(Character);
	TestEqual(TEXT("Pending ID consumed once"), Anim->GetNewReloadPresentationCountForAutomationTest(), 1);
	return true;
}

#endif
