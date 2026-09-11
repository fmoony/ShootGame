// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/AssetManager.h"
#include "UObject/Package.h"
#include "Weapons/Definitions/ShooterWeaponDefinition.h"

namespace ShooterWeaponDefinitionAutomationTests
{
	/** 在临时包内构造内存 Definition，模拟未保存资产与非法配置。 */
	UShooterWeaponDefinition* MakeTransientDefinition(const FName& Name)
	{
		UPackage* Package = GetTransientPackage();
		return NewObject<UShooterWeaponDefinition>(Package, Name);
	}
}

/** DefinitionId 稳定性：固定 PrimaryAssetType、名称寻址、CDO 无身份、不同资产不冲突。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponDefinitionIdStabilityTest,
	"ShootGame.WeaponDefinition.DefinitionIdStability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponDefinitionIdStabilityTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponDefinitionAutomationTests;

	UShooterWeaponDefinition* First = MakeTransientDefinition(TEXT("WD_StabilityOne"));
	UShooterWeaponDefinition* Second = MakeTransientDefinition(TEXT("WD_StabilityTwo"));
	if (!TestNotNull(TEXT("First definition created"), First) ||
		!TestNotNull(TEXT("Second definition created"), Second))
	{
		return false;
	}

	const FPrimaryAssetId FirstId = First->GetDefinitionId();
	const FPrimaryAssetId SecondId = Second->GetDefinitionId();

	TestTrue(TEXT("First definition id is valid"), FirstId.IsValid());
	TestTrue(TEXT("Second definition id is valid"), SecondId.IsValid());
	TestEqual(
		TEXT("Definition type is the fixed ShooterWeaponDefinition type"),
		FirstId.PrimaryAssetType,
		UShooterWeaponDefinition::GetWeaponDefinitionAssetType());
	TestEqual(
		TEXT("Second definition uses the same fixed type"),
		SecondId.PrimaryAssetType,
		UShooterWeaponDefinition::GetWeaponDefinitionAssetType());
	TestEqual(TEXT("Definition name addresses the asset"), FirstId.PrimaryAssetName, FName(TEXT("WD_StabilityOne")));
	TestFalse(TEXT("Distinct assets hold distinct ids"), FirstId == SecondId);

	// 同一对象重复读取必须得到相同身份。
	TestTrue(TEXT("Definition id is stable across reads"), First->GetDefinitionId() == FirstId);

	const UShooterWeaponDefinition* ClassDefaults = GetDefault<UShooterWeaponDefinition>();
	TestFalse(TEXT("Class default object has no asset identity"), ClassDefaults->GetDefinitionId().IsValid());

	return true;
}

/** 非法配置 fail closed：无效 ID、缺失 WeaponActorClass、非法弹匣容量、非法备弹声明。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponDefinitionInvalidConfigTest,
	"ShootGame.WeaponDefinition.InvalidConfigFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponDefinitionInvalidConfigTest::RunTest(const FString& Parameters)
{
	using namespace ShooterWeaponDefinitionAutomationTests;

	UClass* RifleClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C"));
	if (!TestNotNull(TEXT("Rifle weapon class loaded"), RifleClass))
	{
		return false;
	}

	UShooterWeaponDefinition* Definition = MakeTransientDefinition(TEXT("WD_InvalidConfig"));
	if (!TestNotNull(TEXT("Invalid-config definition created"), Definition))
	{
		return false;
	}

	// 缺失 WeaponActorClass：拒绝授予。
	TestFalse(TEXT("Missing WeaponActorClass fails validation"), Definition->IsValidForGrant());

	Definition->WeaponActorClass = RifleClass;
	Definition->AmmoConfig.MagazineSize = 0;
	TestFalse(TEXT("Zero magazine size fails validation"), Definition->IsValidForGrant());

	Definition->AmmoConfig.MagazineSize = -5;
	TestFalse(TEXT("Negative magazine size fails validation"), Definition->IsValidForGrant());

	Definition->AmmoConfig.MagazineSize = 30;
	Definition->AmmoConfig.InitialReserveAmmo = -2;
	TestFalse(TEXT("Reserve below -1 fails validation"), Definition->IsValidForGrant());

	Definition->AmmoConfig.InitialReserveAmmo = -1;
	TestTrue(TEXT("Valid configuration passes validation"), Definition->IsValidForGrant());
	TestEqual(
		TEXT("Automatic reserve falls back to magazine times three"),
		Definition->AmmoConfig.ResolveInitialReserveAmmo(),
		90);

	Definition->AmmoConfig.InitialReserveAmmo = 42;
	TestEqual(
		TEXT("Explicit reserve is used as-is"),
		Definition->AmmoConfig.ResolveInitialReserveAmmo(),
		42);

	return true;
}

/** 解析失败路径：无效 ID、错误类型、未注册资产都返回 nullptr。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponDefinitionResolveFailClosedTest,
	"ShootGame.WeaponDefinition.ResolveFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponDefinitionResolveFailClosedTest::RunTest(const FString& Parameters)
{
	TestNull(
		TEXT("Invalid id resolves to null"),
		UShooterWeaponDefinition::ResolveDefinitionSync(FPrimaryAssetId()));

	TestNull(
		TEXT("Foreign asset type resolves to null"),
		UShooterWeaponDefinition::ResolveDefinitionSync(
			FPrimaryAssetId(FPrimaryAssetType(TEXT("OtherType")), FName(TEXT("WD_TestAuto")))));

	TestNull(
		TEXT("Unregistered weapon definition resolves to null"),
		UShooterWeaponDefinition::ResolveDefinitionSync(
			FPrimaryAssetId(
				UShooterWeaponDefinition::GetWeaponDefinitionAssetType(),
				FName(TEXT("WD_Unregistered")))));

	return true;
}

/** 资产解析闭环：WD_TestAuto 可被 Asset Manager 发现并同步加载为 Definition。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShooterWeaponDefinitionAssetResolutionTest,
	"ShootGame.WeaponDefinition.AssetResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterWeaponDefinitionAssetResolutionTest::RunTest(const FString& Parameters)
{
	const FPrimaryAssetId ExpectedId(
		UShooterWeaponDefinition::GetWeaponDefinitionAssetType(),
		FName(TEXT("WD_TestAuto")));

	UShooterWeaponDefinition* Definition =
		UShooterWeaponDefinition::ResolveDefinitionSync(ExpectedId);
	if (!TestNotNull(
		TEXT("WD_TestAuto resolves through the Asset Manager"),
		Definition))
	{
		// 资产缺失时给出可操作的指引：先运行迁移工具测试创建资产。
		AddError(
			TEXT("WD_TestAuto asset is missing; run ShootGame.Tools.WeaponDefinition.CreateTestAsset first"));
		return false;
	}

	TestTrue(TEXT("Resolved definition passes grant validation"), Definition->IsValidForGrant());
	TestEqual(
		TEXT("Resolved definition keeps its id"),
		Definition->GetDefinitionId(),
		ExpectedId);

	// AssetManager 的对象引用必须与 DefinitionId 互认。
	const FSoftObjectPath AssetPath = UAssetManager::Get().GetPrimaryAssetPath(ExpectedId);
	TestTrue(TEXT("Asset Manager maps the id to a package path"), AssetPath.IsValid());
	TestTrue(
		TEXT("Asset Manager path points at the definition object"),
		AssetPath.TryLoad() == Definition);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
