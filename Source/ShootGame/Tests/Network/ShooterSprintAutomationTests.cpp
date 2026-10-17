// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/Movement/ShooterCharacterMovementComponent.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterSprintSavedMoveTest, "ShootGame.Movement.Sprint.SavedMoves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterSprintSavedMoveTest::RunTest(const FString& Parameters)
{
	FShooterSavedMove Move;
	auto* Movement = NewObject<UShooterCharacterMovementComponent>();
	for (int32 Bits = 0; Bits < 8; ++Bits)
	{
		Move.bSprintHeld = (Bits & 1) != 0;
		Move.bFireHeld = (Bits & 2) != 0;
		Move.bFireReadyPending = (Bits & 4) != 0;
		const uint8 Flags = Move.GetCompressedFlags();
		TestEqual(TEXT("Sprint bit survives packing"), (Flags & Move.FLAG_Custom_0) != 0, Move.bSprintHeld);
		TestEqual(TEXT("Fire bit survives packing"), (Flags & Move.FLAG_Custom_1) != 0, Move.bFireHeld);
		TestEqual(TEXT("Pending first shot survives packing"), (Flags & Move.FLAG_Custom_2) != 0, Move.bFireReadyPending);
		Movement->UpdateFromCompressedFlags(Flags);
		TestEqual(TEXT("Server decodes Sprint intent"), Movement->IsSprintHeld(), Move.bSprintHeld);
		TestEqual(TEXT("Server decodes Fire intent"), Movement->IsFireHeld(), Move.bFireHeld);
		TestEqual(TEXT("Server decodes pending first shot"), Movement->IsFireReadyPending(), Move.bFireReadyPending);
		FSavedMovePtr Other(new FShooterSavedMove());
		auto* OtherMove = static_cast<FShooterSavedMove*>(Other.Get());
		OtherMove->bSprintHeld = !Move.bSprintHeld;
		OtherMove->bFireHeld = Move.bFireHeld;
		TestFalse(TEXT("Shift edge cannot combine"), Move.CanCombineWith(Other, nullptr, 0.125f));
		OtherMove->bSprintHeld = Move.bSprintHeld;
		OtherMove->bFireHeld = !Move.bFireHeld;
		TestFalse(TEXT("Fire edge cannot combine"), Move.CanCombineWith(Other, nullptr, 0.125f));
		OtherMove->bFireHeld = Move.bFireHeld;
		OtherMove->bFireReadyPending = !Move.bFireReadyPending;
		TestFalse(TEXT("First shot boundary cannot combine"), Move.CanCombineWith(Other, nullptr, 0.125f));
	}
	Move.Clear();
	TestEqual(TEXT("Pool reuse clears input bits"), Move.GetCompressedFlags(), uint8(0));
	TestTrue(TEXT("Rifle supports LowReady"), UShooterCharacterMovementComponent::SupportsLowReady(TEXT("Rifle")));
	TestFalse(TEXT("Pistol does not reuse Rifle animation"), UShooterCharacterMovementComponent::SupportsLowReady(TEXT("Pistol")));
	return true;
}

#endif
