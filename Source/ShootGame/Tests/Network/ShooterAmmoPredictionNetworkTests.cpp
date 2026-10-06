// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/Network/ShooterNetworkTestCoordinator.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/Abilities/ShooterGameplayAbility_Fire.h"
#include "AbilitySystem/ShooterAbilitySystemComponent.h"
#include "AbilitySystem/ShooterGameplayTags.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerState/ShooterPlayerState.h"
#include "GameFramework/PlayerController/ShooterPlayerController.h"
#include "UI/ShooterBulletCounterUI.h"
#include "Inventory/ShooterInventoryComponent.h"
#include "Weapons/Data/ShooterWeaponTable.h"
#include "Weapons/Subsystems/ShooterWeaponRuntimeSubsystem.h"
#include "Weapons/ShooterWeapon.h"
#include "ShootGame.h"

/**
 * Ammo Prediction 夹具：只服务 -ShootGameAmmoPredictionTest。
 *
 * 本文件不修改生产语义，只在真实 Listen 会话里驱动一个远端拥有者客户端，并同时记录
 * 服务器权威增量与拥有端本地观测。目标语义是「一次 GA_Fire Activation = 恰好一发 Shot」：
 *
 * - 服务器在一个 Activation 里只调用一次 AShooterWeapon::CommitSingleShot，Ability 随该发结束；
 * - 拥有端按 PredictionKey 保留一条 Shot 记录，只有这一发的裁决才结清它：
 *   Committed 走武器的 ClientShotCommitted，Rejected 走引擎 ClientActivateAbilityFailed；
 * - Ammo 复制不再结清预算，Reject 也不依赖 CaughtUp；
 * - State.Firing 与 Activation 同生命周期，因此夹具不再采样「客户端仍在开火」，
 *   改为断言「Hold 结束后没有活动 GA_Fire、没有未结清 Shot 记录、Pending 收敛到起点」。
 *
 * 12 个用例各自是一次独立的客户端 / 服务器往返，结论只由两侧可观测事实得出：
 * 前 8 个覆盖单发 / 连发 / 接受 / 拒绝 / 松手 / 换弹 / 未预测接受 / 未预测拒绝，
 * 第 9-10 个（Rate600Rpm / Rate900Rpm）让两端使用同一 RefireRate 测量达成射速与网络流量，
 * 最后两个（SemiAutoWeaponSingleShot / SemiAutoHold）把当前武器切到动态挑出的正式半自动行，
 * 覆盖 OnInputTriggered 策略：一次按下沿一发，按住扳机绝不连发。
 *
 * 用例顺序说明：两个 Unpredicted 用例把拥有端预测预算人为抬到弹匣容量（预算 0），
 * 之后的半自动用例会在步骤起点把预算复位，因此该夹具注入不会进入半自动用例的就绪门。
 */
namespace ShooterAmmoPredictionNetworkTests
{
	constexpr float StepTimeoutSeconds = 30.0f;
	constexpr float SampleFreshSeconds = 0.6f;
	constexpr float SettleSeconds = 1.0f;
	constexpr float RejectSettleSeconds = 1.5f;

	constexpr int32 StepSetup = 0;
	constexpr int32 StepSemiAutoSingleShot = 1;
	constexpr int32 StepFullAutoHold = 2;
	constexpr int32 StepPredictedAccepted = 3;
	constexpr int32 StepPredictedRejected = 4;
	constexpr int32 StepFullAutoRelease = 5;
	constexpr int32 StepReloadLifecycle = 6;
	constexpr int32 StepRate600Rpm = 7;
	constexpr int32 StepRate900Rpm = 8;
	constexpr int32 StepUnpredictedAccepted = 9;
	constexpr int32 StepUnpredictedRejected = 10;
	constexpr int32 StepSemiAutoSetup = 11;
	constexpr int32 StepSemiAutoWeaponSingleShot = 12;
	constexpr int32 StepSemiAutoHold = 13;
	constexpr int32 StepDone = 14;

	/** DONE 行里的 Cases 值；与 ConcludeAmmoPredictionCase 的调用次数必须一致。 */
	constexpr int32 CaseCount = 12;

	/** 全自动按住窗口：至少覆盖 FullAutoMinShots 个完整节拍，且不得打空弹匣。 */
	constexpr float FullAutoHoldSeconds = 1.5f;
	constexpr float FullAutoHoldCadenceFactor = 3.5f;
	constexpr int32 FullAutoMinShots = 3;
	/** 窗口上限的保守余量：允许释放与权威观察之间多一发。 */
	constexpr int32 FullAutoPaceSlackShots = 2;

	/** Reload 生命周期窗口：Hold 中触发 Reload，且松手必须早于换弹完成。 */
	constexpr float ReloadTriggerSeconds = 0.6f;
	constexpr float ReloadHoldRatio = 0.4f;
	constexpr float ReloadHoldMinExtraSeconds = 0.15f;
	constexpr float ReloadHoldMaxExtraSeconds = 1.0f;
	/** 夹具前提：换弹时长必须长到能容纳「触发 → 松手」这段观察窗口。 */
	constexpr float ReloadDurationMinSeconds = 0.4f;
	/**
	 * Reload 用例"前提构造失败"的迟到判定余量。
	 *
	 * 前提判定必须覆盖"服务器下发指令 → 客户端 Hold → Reload 输入上行 → 服务器换弹事务启动"的完整往返，
	 * 否则高延迟下会在客户端甚至还没开始 Hold 时就误判前提失败（PktLag=100 实测复现）。
	 */
	constexpr float ReloadPremiseMarginSeconds = 2.0f;

	/**
	 * 射速测量用例：窗口内每发都必须来自一次自己被服务器接受的 Activation，
	 * 且达成射速必须等于配置射速（±15%）。
	 *
	 * 窗口固定约 3s；弹匣在步骤起点被夹具抬到"窗口所需发数 + 余量"，
	 * 保证整段窗口都不会打空弹匣——打空之后服务器会因无弹药拒绝，
	 * 那属于弹药限制而不是射速，会污染达成射速。
	 */
	constexpr float RateMeasurementSeconds = 3.0f;
	constexpr float Rate600RpmRefireRate = 0.1f;
	constexpr float Rate900RpmRefireRate = 1.0f / 15.0f;
	constexpr float Rate600RpmExpectedRps = 10.0f;
	constexpr float Rate900RpmExpectedRps = 15.0f;
	constexpr float RateToleranceRatio = 0.15f;
	constexpr int32 RateMagazineSlack = 2;

	/**
	 * 半自动用例：换枪到动态挑出的正式半自动行之后，验证 OnInputTriggered 策略。
	 *
	 * - 一次按下 + 松开必须只产生一发；
	 * - 按住扳机的时长至少覆盖 3 个完整节拍，且必须仍然只有一发。
	 * 弹药起点被夹到至少 FixtureMinMagazine 发、备弹至少 1 发，
	 * 使"弹药不足"不可能伪装成半自动结论。
	 */
	constexpr float SemiAutoHoldMinSeconds = 1.5f;
	constexpr float SemiAutoHoldCadenceFactor = 3.0f;
	constexpr int32 FixtureMinMagazine = 3;

	/** 夹具把权威备弹固定为弹匣容量的该倍数，保证换弹一定能补满。 */
	constexpr int32 FixtureReserveMultiplier = 2;

	bool IsActorInfoStartupProbe()
	{
		return FParse::Param(FCommandLine::Get(), TEXT("ShootGameActorInfoStartupTest"));
	}

	bool IsFireAbility(const UGameplayAbility* Ability)
	{
		return Ability && Ability->IsA<UShooterGameplayAbility_Fire>();
	}

	/** 拥有端预测预算被夹具固定为 0 的用例（服务器仍有弹药，请求仍必须到达服务器）。 */
	bool IsUnpredictedBudgetStep(int32 Step)
	{
		return Step == StepUnpredictedAccepted || Step == StepUnpredictedRejected;
	}

	bool IsRateMeasurementStep(int32 Step)
	{
		return Step == StepRate600Rpm || Step == StepRate900Rpm;
	}

	/**
	 * 从正式武器表动态挑一个半自动行：行名排序后取第一个 bFullAuto == false 的行。
	 * 不硬编码任何行名，也不接受"没有半自动行"这种退化：返回 NAME_None 由调用方 FailTest。
	 */
	FName PickSemiAutoWeaponRowName()
	{
		const UDataTable* WeaponTable = ShooterWeaponTable::ResolveWeaponTable();
		if (!WeaponTable)
		{
			return NAME_None;
		}

		TArray<FName> RowNames = WeaponTable->GetRowNames();
		RowNames.Sort([](const FName& Left, const FName& Right) { return Left.LexicalLess(Right); });
		for (const FName& RowName : RowNames)
		{
			const FShooterWeaponConfigRow* Row = ShooterWeaponTable::FindWeaponRow(WeaponTable, RowName);
			if (Row && !Row->bFullAuto)
			{
				return RowName;
			}
		}

		return NAME_None;
	}

	/** 射速测量用例的配置达成射速（发/秒）。 */
	float GetRateExpectedRps(int32 Step)
	{
		return Step == StepRate600Rpm ? Rate600RpmExpectedRps : Rate900RpmExpectedRps;
	}

	/** 射速测量用例在两端必须一致的 RefireRate。 */
	float GetRateRefireRate(int32 Step)
	{
		return Step == StepRate600Rpm ? Rate600RpmRefireRate : Rate900RpmRefireRate;
	}

	/**
	 * 读取一条 NetConnection 的累计网络计数（窗口差值口径）。
	 *
	 * 只使用 *Total* 系列：它们在收包 / 发包路径上无条件累加，且不在 UNetConnection::Tick
	 * 的 StatPeriod 重置列表内。刻意不使用 InBytes / OutBytes / InPackets / OutPackets：
	 * 它们在 UNetConnection::Tick 里被"除以统计周期实际时长"写入 *PerSecond 后立即清零，
	 * 是瞬时速率而不是累计值。连接为空时 bValid 保持 false，不返回伪造的 0 值。
	 */
	FShooterAmmoPredictionNetCounters ReadAmmoPredictionNetCounters(const UNetConnection* Connection, const UNetDriver* NetDriver)
	{
		FShooterAmmoPredictionNetCounters Counters;
		if (!Connection)
		{
			return Counters;
		}

		Counters.bValid = true;
		Counters.InBytes = Connection->InTotalBytes;
		Counters.OutBytes = Connection->OutTotalBytes;
		Counters.InPackets = Connection->InTotalPackets;
		Counters.OutPackets = Connection->OutTotalPackets;
		Counters.RPCsCalled = NetDriver ? NetDriver->TotalRPCsCalled : 0;
		return Counters;
	}

	/** 服务器端：解析驱动客户端（本 Coordinator 的 Owner）的 NetConnection。 */
	UNetConnection* ResolveAmmoPredictionDriverConnection(const APlayerController* Driver, const UNetDriver* NetDriver)
	{
		if (!NetDriver)
		{
			return nullptr;
		}

		if (Driver)
		{
			if (UNetConnection* PlayerConnection = Driver->NetConnection)
			{
				return PlayerConnection;
			}

			// 回退：按 OwningActor 在驱动端的连接列表里匹配。
			for (const TObjectPtr<UNetConnection>& Candidate : NetDriver->ClientConnections)
			{
				if (Candidate && Candidate->OwningActor == Driver)
				{
					return Candidate.Get();
				}
			}
		}

		return nullptr;
	}

	/** 拥有端：与服务器之间的连接（客户端只有这一条）。 */
	UNetConnection* ResolveAmmoPredictionClientConnection(const UNetDriver* NetDriver)
	{
		return NetDriver ? NetDriver->ServerConnection : nullptr;
	}

	/**
	 * 采样一次连接瞬时速率（上限 10Hz）。
	 *
	 * InBytesPerSecond / OutBytesPerSecond / InPacketsPerSecond / OutPacketsPerSecond 只在
	 * UNetConnection::Tick 的 StatPeriod（默认 1s）到达时刷新，因此必须周期采样；
	 * 单次读取不能代表整个窗口。返回是否真的采到了样本。
	 */
	bool AccumulateNetRates(const UNetConnection* Connection, float Now, FShooterAmmoPredictionNetRateWindow& Window)
	{
		if (!Connection || !Window.CanSample(Now))
		{
			return false;
		}

		Window.Accumulate(Now,
			static_cast<float>(Connection->InBytesPerSecond),
			static_cast<float>(Connection->OutBytesPerSecond),
			static_cast<float>(Connection->InPacketsPerSecond),
			static_cast<float>(Connection->OutPacketsPerSecond));
		return true;
	}

	/** 单调累计计数的窗口差值（uint32 相减对单调计数安全）。 */
	int32 DeltaNetCounter(uint32 Now, uint32 Base)
	{
		return static_cast<int32>(Now - Base);
	}

	/** 由窗口增量与窗口时长派生每秒字节数；计数不可用或时长非法时返回 0。 */
	float NetBytesPerSecond(int32 Bytes, float Seconds)
	{
		return Bytes != INDEX_NONE && Seconds > 0.0f ? static_cast<float>(Bytes) / Seconds : 0.0f;
	}
}

// ============================ 拥有端观测 ============================

void AShooterNetworkTestCoordinator::BindAmmoPredictionClientObservers(UAbilitySystemComponent* AbilitySystemComponent)
{
	if (HasAuthority() || !AbilitySystemComponent || AmmoPredictionObservedASC.Get() == AbilitySystemComponent)
	{
		return;
	}

	AmmoPredictionObservedASC = AbilitySystemComponent;
	AmmoPredictionActivatedHandle = AbilitySystemComponent->AbilityActivatedCallbacks.AddUObject(
		this, &AShooterNetworkTestCoordinator::HandleAmmoPredictionFireActivated);
	AmmoPredictionEndedHandle = AbilitySystemComponent->AbilityEndedCallbacks.AddUObject(
		this, &AShooterNetworkTestCoordinator::HandleAmmoPredictionFireEnded);
}

void AShooterNetworkTestCoordinator::BindAmmoPredictionServerObserver(UAbilitySystemComponent* AbilitySystemComponent)
{
	if (!HasAuthority() || !AbilitySystemComponent || AmmoPredictionObservedASC.Get() == AbilitySystemComponent)
	{
		return;
	}

	AmmoPredictionObservedASC = AbilitySystemComponent;
	// 权威端同时订阅激活与失败：激活用于把「被接受的 Activation」与「权威 Shot」逐一对齐，
	// 失败用于观察真实 GAS 拒绝（Tag 门控失败）。
	AmmoPredictionActivatedHandle = AbilitySystemComponent->AbilityActivatedCallbacks.AddUObject(
		this, &AShooterNetworkTestCoordinator::HandleAmmoPredictionFireActivated);
	AmmoPredictionFailedHandle = AbilitySystemComponent->AbilityFailedCallbacks.AddUObject(
		this, &AShooterNetworkTestCoordinator::HandleAmmoPredictionAuthorityFailed);
}

void AShooterNetworkTestCoordinator::HandleAmmoPredictionFireActivated(UGameplayAbility* Ability)
{
	if (!bAmmoPredictionMode || !ShooterAmmoPredictionNetworkTests::IsFireAbility(Ability))
	{
		return;
	}

	const int32 Key = Ability->GetCurrentActivationInfo().GetActivationPredictionKey().Current;
	if (HasAuthority())
	{
		// 权威端记一次「被接受的 Activation」：它必须与一次 CommitSingleShot 严格一一对应。
		++AmmoPredictionAuthorityActivations;
		const float ActivationTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		if (AmmoPredictionFirstAuthorityActivationTime < 0.0f)
		{
			// 射速窗口的第一发时间：达成射速只在第一发到最后一发之间测量，不含启动延迟。
			AmmoPredictionFirstAuthorityActivationTime = ActivationTime;
		}
		if (AmmoPredictionRateLastActivationTime >= 0.0f)
		{
			// 逐间隔统计：节拍被帧率量化时最小值 / 最大值会明显张开，便于定位达成射速偏差。
			const float Interval = ActivationTime - AmmoPredictionRateLastActivationTime;
			AmmoPredictionRateMinInterval = AmmoPredictionRateMinInterval < 0.0f
				? Interval
				: FMath::Min(AmmoPredictionRateMinInterval, Interval);
			AmmoPredictionRateMaxInterval = FMath::Max(AmmoPredictionRateMaxInterval, Interval);
			++AmmoPredictionRateIntervalSamples;
		}
		AmmoPredictionRateLastActivationTime = ActivationTime;
		if (Key > 0)
		{
			AmmoPredictionAuthorityActivationKeys.Add(Key);
		}
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("AMMO_PREDICTION_AUTHORITY_FIRE_ACTIVATED Step=%d Key=%d Count=%d DistinctKeys=%d"),
			AmmoPredictionServerStep,
			Key,
			AmmoPredictionAuthorityActivations,
			AmmoPredictionAuthorityActivationKeys.Num());
		return;
	}

	++AmmoPredictionOwnerFireActivations;
	AmmoPredictionLastPredictionKey = Key;
	if (Key > 0)
	{
		// 每一次拥有端激活都必须拿到自己的 PredictionKey：Shot 记录按它索引，
		// 两发共用一个 key 会让它们共用一条记录，也会让一发裁决结清两发预算。
		AmmoPredictionStepPredictionKeys.Add(Key);
		if (AmmoPredictionFirstPredictionKey == 0)
		{
			AmmoPredictionFirstPredictionKey = Key;
		}
	}
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_OWNER_FIRE_ACTIVATED Step=%d Key=%d Count=%d DistinctKeys=%d"),
		AmmoPredictionClientStep, Key, AmmoPredictionOwnerFireActivations, AmmoPredictionStepPredictionKeys.Num());
}

void AShooterNetworkTestCoordinator::HandleAmmoPredictionFireEnded(UGameplayAbility* Ability)
{
	if (HasAuthority() || !bAmmoPredictionMode || !ShooterAmmoPredictionNetworkTests::IsFireAbility(Ability))
	{
		return;
	}

	// 正常路径下本地实例随那一发结束；只有 Activated==Rejected 的结束才是引擎拒绝。
	const FGameplayAbilityActivationInfo Info = Ability->GetCurrentActivationInfo();
	if (Info.ActivationMode == EGameplayAbilityActivationMode::Rejected)
	{
		++AmmoPredictionOwnerFireRejects;
		UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_OWNER_FIRE_REJECT Step=%d Key=%d Count=%d"),
			AmmoPredictionClientStep, Info.GetActivationPredictionKey().Current, AmmoPredictionOwnerFireRejects);
	}
}

void AShooterNetworkTestCoordinator::HandleAmmoPredictionAuthorityFailed(const UGameplayAbility* Ability,
	const FGameplayTagContainer& FailureTags)
{
	if (!HasAuthority() || !bAmmoPredictionMode || !ShooterAmmoPredictionNetworkTests::IsFireAbility(Ability))
	{
		return;
	}

	++AmmoPredictionAuthorityRejects;
	// 夹具阻塞 Tag 只用于制造一次真实拒绝，观察到失败回调后立即撤掉。
	ClearAmmoPredictionServerTag();
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_SERVER_REJECT Step=%d Count=%d Tags=%s"),
		AmmoPredictionServerStep, AmmoPredictionAuthorityRejects, *FailureTags.ToStringSimple());
}

void AShooterNetworkTestCoordinator::ClearAmmoPredictionServerTag()
{
	if (!bAmmoPredictionServerTagApplied)
	{
		return;
	}

	if (UAbilitySystemComponent* AbilitySystemComponent = AmmoPredictionObservedASC.Get())
	{
		AbilitySystemComponent->RemoveLooseGameplayTag(ShooterGameplayTags::State_Reloading);
	}
	bAmmoPredictionServerTagApplied = false;
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_SERVER_TAG_CLEARED Step=%d"), AmmoPredictionServerStep);
}

void AShooterNetworkTestCoordinator::SampleAmmoPredictionLocalState()
{
	using namespace ShooterAmmoPredictionNetworkTests;

	if (!bAmmoPredictionMode || HasAuthority())
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetOwner());
	if (!PlayerController || !PlayerController->IsLocalController() || AmmoPredictionClientStep == INDEX_NONE)
	{
		return;
	}

	if (!AmmoPredictionSubject.IsValid())
	{
		AShooterCharacter* LocalCharacter = GetShooterCharacter();
		if (LocalCharacter && LocalCharacter->IsLocallyControlled())
		{
			AmmoPredictionSubject = LocalCharacter;
		}
	}

	AShooterCharacter* Subject = AmmoPredictionSubject.Get();
	if (!Subject)
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();

	// Hold 夹具：按住持续与「Hold 中触发 Reload」都由拥有端本地时钟驱动，
	// 不直接调用 Weapon.Fire，也不走服务器实现。
	if (bAmmoPredictionHoldingFire)
	{
		const bool bReloadWindowOpen = AmmoPredictionHoldReloadTime >= 0.0f && !bAmmoPredictionReloadSubmitted;
		if (bReloadWindowOpen && Now >= AmmoPredictionHoldReloadTime)
		{
			bAmmoPredictionReloadSubmitted = true;
			Subject->DoReload();
			UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_CLIENT_HOLD_RELOAD Step=%d"), AmmoPredictionClientStep);
		}
		if (Now >= AmmoPredictionHoldEndTime)
		{
			bAmmoPredictionHoldingFire = false;
			bAmmoPredictionReleaseObserved = true;
			AmmoPredictionActivationsAtRelease = AmmoPredictionOwnerFireActivations;
			// 松手后必须再跨过一个完整本地节拍，「松手不再产生新激活」才有观察窗口。
			const AShooterWeapon* HeldWeapon = AmmoPredictionWeapon.Get();
			const float Window = HeldWeapon && HeldWeapon->GetRefireRate() > 0.0f
				? HeldWeapon->GetRefireRate()
				: 0.5f;
			AmmoPredictionReleaseSettleTime = Now + FMath::Max(Window, 0.5f);
			Subject->DoStopFiring();
			UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_CLIENT_HOLD_RELEASE Step=%d Activations=%d"),
				AmmoPredictionClientStep, AmmoPredictionActivationsAtRelease);
		}
	}

	// 拥有端跟随生产 Equipment 的当前武器：换枪用例之后必须重新钉住新武器，
	// 否则 HUD / 预算 / 表现观测都会停在旧武器上。
	AShooterWeapon* CurrentWeapon = Subject->GetCurrentWeaponActor();
	if (CurrentWeapon && CurrentWeapon->GetOwner() == Subject && CurrentWeapon != AmmoPredictionWeapon.Get())
	{
		AmmoPredictionWeapon = CurrentWeapon;
	}
	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	UShooterAbilitySystemComponent* ShooterAbilitySystemComponent = Cast<UShooterAbilitySystemComponent>(
		Subject->GetAbilitySystemComponent());
	BindAmmoPredictionClientObservers(ShooterAbilitySystemComponent);

	AShooterPlayerState* PlayerState = Subject->GetPlayerState<AShooterPlayerState>();
	FShooterAmmoPredictionObservation Sample;
	Sample.Step = AmmoPredictionClientStep;
	Sample.Subject = Subject;
	Sample.Weapon = Weapon;
	const APlayerController* PawnController = Cast<APlayerController>(Subject->GetController());
	const bool bPawnOwnedLocally = PawnController && PawnController->IsLocalController();
	const bool bCachedContextReady = ShooterAbilitySystemComponent &&
		ShooterAbilitySystemComponent->AbilityActorInfo.IsValid() &&
		ShooterAbilitySystemComponent->AbilityActorInfo->IsLocallyControlledPlayer() &&
		ShooterAbilitySystemComponent->GetAvatarActor() == Subject;
	// 启动探针不等 ASC 缓存自愈；只等真实本地关联及 Spec，首枪入口另行断言缓存一致。
	Sample.bValid = IsValid(Weapon) && PlayerState && ShooterAbilitySystemComponent && bPawnOwnedLocally &&
		Subject->GetCurrentWeaponActor() == Weapon && Weapon->GetOwner() == Subject && !Weapon->IsHidden() &&
		GetFireAbilityInstanceForTest(Subject) &&
		(IsActorInfoStartupProbe() || bCachedContextReady);
	if (Sample.bValid)
	{
		Sample.MagazineAmmo = Weapon->GetBulletCount();
		Sample.ReserveAmmo = Weapon->GetReserveAmmo();
		Sample.PendingShots = Weapon->GetPendingPredictedShots();
		Sample.PredictedMagazineAmmo = Weapon->GetPredictedMagazineAmmo();
		Sample.bCurrentWeaponIsFullAuto = Weapon->IsFullAuto();
		Sample.CurrentWeaponId = Weapon->GetWeaponId();
		AShooterPlayerController* Controller = Cast<AShooterPlayerController>(Subject->GetController());
		const UShooterBulletCounterUI* Widget = Controller ? Controller->GetBulletCounterUIForAutomationTest() : nullptr;
		if (!Widget || Widget->GetOwningPlayerPawn() != Subject)
		{
			Sample.bValid = false;
		}
		else
		{
			Sample.HudMagazine = Widget->GetDisplayedMagazineForTest();
			Sample.HudReserve = Widget->GetDisplayedReserveForTest();
			Sample.HudPredictedUpdates = Widget->GetPredictedUpdateCountForTest();
			Sample.HudUnsettledCount = Weapon->GetUnsettledAmmoDisplayCount();
		}
		Sample.PredictedOwnerFeedbackCount = Weapon->GetPredictedOwnerFeedbackCountForAutomationTest();
		Sample.ConfirmedBackfillCount = Weapon->GetConfirmedBackfillCountForAutomationTest();
		Sample.ShotVerdictCount = Weapon->GetShotVerdictReceivedCountForTest();
		Sample.OwnerFireActivationCount = AmmoPredictionOwnerFireActivations;
		Sample.OwnerFireRejectCount = AmmoPredictionOwnerFireRejects;
		Sample.DistinctPredictionKeyCount = AmmoPredictionStepPredictionKeys.Num();
		Sample.FirstPredictionKey = AmmoPredictionFirstPredictionKey;
		Sample.LastPredictionKey = AmmoPredictionLastPredictionKey;
		Sample.bFireActive = HasActiveFireAbility(Subject);
		Sample.bReloadActive = HasActiveReloadAbility(Subject);
		Sample.bReloading = ShooterAbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Reloading);
		Sample.bLocalFireCooldownReady = Weapon->IsLocalFireCooldownReady();
		Sample.MontageCount = Subject->GetOwnerLocalMontageCountForAutomationTest();
		Sample.MuzzleCount = Weapon->GetOwnerMuzzleFeedbackCountForAutomationTest();
		Sample.SoundCount = Weapon->GetOwnerSoundFeedbackCountForAutomationTest();
		Sample.RecoilCount = Subject->GetOwnerLocalRecoilCountForAutomationTest();
		Sample.bReleaseSettled = bAmmoPredictionReleaseObserved && Now >= AmmoPredictionReleaseSettleTime;
		Sample.ActivationsAtRelease = bAmmoPredictionReleaseObserved
			? AmmoPredictionActivationsAtRelease
			: INDEX_NONE;
		Sample.RefireRate = Weapon->GetRefireRate();

		// 网络证据窗口：本步骤起点 → 本次采样。
		// 累计口径（*TotalBytes / *TotalPackets）用差值；速率口径用周期采样后的平均 / 峰值，
		// 因为 *PerSecond 每个 StatPeriod 才刷新一次，单次读取不能代表窗口。
		const UNetDriver* NetDriver = GetWorld() ? GetWorld()->GetNetDriver() : nullptr;
		const UNetConnection* Connection = ResolveAmmoPredictionClientConnection(NetDriver);
		if (AmmoPredictionNetWindowStartTime >= 0.0f)
		{
			Sample.bNetConnectionResolved = Connection != nullptr;
			AccumulateNetRates(Connection, Now, AmmoPredictionNetRateWindow);
			Sample.NetRateSamples = AmmoPredictionNetRateWindow.Samples;
			Sample.NetInBytesPerSecondAvg = AmmoPredictionNetRateWindow.GetAverageInBytesPerSecond();
			Sample.NetInBytesPerSecondPeak = AmmoPredictionNetRateWindow.PeakInBytesPerSecond;
			Sample.NetOutBytesPerSecondAvg = AmmoPredictionNetRateWindow.GetAverageOutBytesPerSecond();
			Sample.NetOutBytesPerSecondPeak = AmmoPredictionNetRateWindow.PeakOutBytesPerSecond;
			Sample.NetInPacketsPerSecondAvg = AmmoPredictionNetRateWindow.GetAverageInPacketsPerSecond();
			Sample.NetInPacketsPerSecondPeak = AmmoPredictionNetRateWindow.PeakInPacketsPerSecond;
			Sample.NetOutPacketsPerSecondAvg = AmmoPredictionNetRateWindow.GetAverageOutPacketsPerSecond();
			Sample.NetOutPacketsPerSecondPeak = AmmoPredictionNetRateWindow.PeakOutPacketsPerSecond;
			const FShooterAmmoPredictionNetCounters NetNow = ReadAmmoPredictionNetCounters(Connection, NetDriver);
			if (AmmoPredictionNetBase.bValid && NetNow.bValid)
			{
				Sample.NetInBytes = DeltaNetCounter(NetNow.InBytes, AmmoPredictionNetBase.InBytes);
				Sample.NetOutBytes = DeltaNetCounter(NetNow.OutBytes, AmmoPredictionNetBase.OutBytes);
				Sample.NetInPackets = DeltaNetCounter(NetNow.InPackets, AmmoPredictionNetBase.InPackets);
				Sample.NetOutPackets = DeltaNetCounter(NetNow.OutPackets, AmmoPredictionNetBase.OutPackets);
				Sample.NetRPCsCalled = DeltaNetCounter(NetNow.RPCsCalled, AmmoPredictionNetBase.RPCsCalled);
			}
			Sample.NetWindowSeconds = FMath::Max(0.0f, Now - AmmoPredictionNetWindowStartTime);
		}

		if (const UShooterGameplayAbility_Fire* FireAbility = GetFireAbilityInstanceForTest(Subject))
		{
			const int32 Unresolved = FireAbility->GetUnresolvedShotRecordCountForTest();
			Sample.UnresolvedShotRecordCount = Unresolved;
			AmmoPredictionPeakUnresolvedShotRecords = FMath::Max(AmmoPredictionPeakUnresolvedShotRecords, Unresolved);
			Sample.PeakUnresolvedShotRecords = AmmoPredictionPeakUnresolvedShotRecords;
			Sample.ConfirmedBackfillRequestCount = FireAbility->GetConfirmedBackfillRequestCountForTest();
			const bool bLastCommitted = FireAbility->WasLastResolvedShotCommittedForTest();
			const bool bLastEngineRejected = FireAbility->WasLastResolvedShotRejectedByEngineForTest();
			Sample.LastResolvedShotKey = FireAbility->GetLastResolvedShotKeyForTest();
			Sample.bLastResolvedShotCommitted = bLastCommitted;
			Sample.bLastResolvedShotRejectedByEngine = !bLastCommitted && bLastEngineRejected;
		}
	}

	AmmoPredictionLatest = Sample;
	if (Now >= AmmoPredictionNextReportTime)
	{
		AmmoPredictionNextReportTime = Now + 0.05f;
		// 本条上报自身也是一次 RPC：先计数，再写进即将发送的样本，
		// 使 NetRPCsCalled - NetSampleReports 恰好等于玩家动作相关的 RPC 数。
		++AmmoPredictionClientSampleReports;
		Sample.NetSampleReports = AmmoPredictionClientSampleReports - AmmoPredictionNetBaseSampleReports;
		ServerReportAmmoPredictionSample(Sample);
	}
}

void AShooterNetworkTestCoordinator::ClientPrepareAmmoPredictionStep_Implementation(int32 SubjectPlayerId, int32 Step)
{
	using namespace ShooterAmmoPredictionNetworkTests;

	if (!bAmmoPredictionMode)
	{
		return;
	}

	AmmoPredictionClientStep = Step;
	AmmoPredictionSubjectPlayerId = SubjectPlayerId;
	AmmoPredictionSubmittedStep = INDEX_NONE;
	AmmoPredictionStepPredictionKeys.Reset();
	AmmoPredictionFirstPredictionKey = 0;
	AmmoPredictionLastPredictionKey = 0;
	AmmoPredictionPeakUnresolvedShotRecords = 0;
	AmmoPredictionReleaseSettleTime = 0.0f;
	bAmmoPredictionReleaseObserved = false;

	if (AShooterWeapon* Weapon = AmmoPredictionWeapon.Get())
	{
		// 每个步骤的拥有端起点由夹具固定：本地开火节拍就绪、预测预算归零。
		// Unpredicted 用例再把预算抬到弹匣容量：GetPredictedMagazineAmmo() 因此恒为 0，
		// 拥有端不会提前表现也不会增加 Pending，但请求仍照原样发往服务器。
		const int32 FixturePending = IsUnpredictedBudgetStep(Step) ? Weapon->GetMagazineSize() : 0;
		Weapon->SetPendingPredictedShotsForAutomationTest(FixturePending);
		Weapon->SetLocalFireCooldownRemainingForAutomationTest(0.0f);
	}

	// 拥有端网络证据窗口起点：本步骤准备 → 结算。累计口径与速率口径都用同一条连接。
	const UNetDriver* NetDriver = GetWorld() ? GetWorld()->GetNetDriver() : nullptr;
	const UNetConnection* Connection = ResolveAmmoPredictionClientConnection(NetDriver);
	AmmoPredictionNetBase = ReadAmmoPredictionNetCounters(Connection, NetDriver);
	AmmoPredictionNetRateWindow.Reset();
	AmmoPredictionNetWindowStartTime = GetWorld()->GetTimeSeconds();
	AmmoPredictionNetBaseSampleReports = AmmoPredictionClientSampleReports;

	SampleAmmoPredictionLocalState();
	const AShooterCharacter* LocalCharacter = GetShooterCharacter();
	const APlayerState* LocalPlayerState = LocalCharacter ? LocalCharacter->GetPlayerState() : nullptr;
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_CLIENT_STEP Step=%d SubjectPlayerId=%d LocalPlayerId=%d"),
		Step, SubjectPlayerId, LocalPlayerState ? LocalPlayerState->GetPlayerId() : INDEX_NONE);
}

void AShooterNetworkTestCoordinator::ClientSetAmmoPredictionRefireRate_Implementation(int32 Step, float RefireRate)
{
	if (!bAmmoPredictionMode || Step != AmmoPredictionClientStep)
	{
		return;
	}

	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	if (!Weapon)
	{
		return;
	}

	// 夹具专用：拥有端的请求节拍与服务器权威门控必须使用同一个 RefireRate，
	// 否则测得的"达成射速"反映的是两端节拍之差，而不是配置射速。
	Weapon->SetRefireRateForAutomationTest(RefireRate);
	Weapon->SetLocalFireCooldownRemainingForAutomationTest(0.0f);
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_CLIENT_REFIRE Step=%d Rate=%.4f"), Step,
		Weapon->GetRefireRate());
}

void AShooterNetworkTestCoordinator::ClientSubmitAmmoPredictionFire_Implementation(int32 Step)
{
	using namespace ShooterAmmoPredictionNetworkTests;

	if (!bAmmoPredictionMode || Step != AmmoPredictionClientStep || Step == AmmoPredictionSubmittedStep)
	{
		return;
	}

	AShooterCharacter* Subject = AmmoPredictionSubject.Get();
	if (!Subject || !Subject->IsLocallyControlled() || Subject != GetShooterCharacter())
	{
		return;
	}

	// 启动探针：第一次真实输入必须落在已经刷新过的本地 AbilityActorInfo 上。
	if (IsActorInfoStartupProbe() && !bAmmoPredictionStartupProbeChecked)
	{
		bAmmoPredictionStartupProbeChecked = true;
		const UAbilitySystemComponent* AbilitySystemComponent = Subject->GetAbilitySystemComponent();
		const FGameplayAbilityActorInfo* Info = AbilitySystemComponent
			? AbilitySystemComponent->AbilityActorInfo.Get()
			: nullptr;
		const bool bReady = Info && Info->AvatarActor.Get() == Subject && Info->IsLocallyControlledPlayer();
		const bool bControllerMatched = Info && Info->PlayerController.Get() == Subject->GetController();
		if (!bReady || !bControllerMatched)
		{
			FailTest(TEXT("ActorInfo startup first input found stale local controller context"));
			return;
		}
		UE_LOG(LogShootGame, Display, TEXT("ACTOR_INFO_STARTUP_FIRST_INPUT Ready=%d ControllerMatched=%d"),
			bReady ? 1 : 0, bControllerMatched ? 1 : 0);
	}

	AmmoPredictionSubmittedStep = Step;
	// Press 与 Release 落在同一帧：这是真实的「一次按下沿」，只允许形成一次本地动作边界。
	Subject->DoStartFiring();
	Subject->DoStopFiring();
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_CLIENT_FIRE Step=%d Subject=%s"), Step, *GetNameSafe(Subject));
}

void AShooterNetworkTestCoordinator::ClientSubmitAmmoPredictionHoldFire_Implementation(int32 Step, float HoldSeconds,
	float LocalCooldownSeconds, float ReloadAtSeconds)
{
	if (!bAmmoPredictionMode || Step != AmmoPredictionClientStep || Step == AmmoPredictionSubmittedStep)
	{
		return;
	}

	AShooterCharacter* Subject = AmmoPredictionSubject.Get();
	if (!Subject || !Subject->IsLocallyControlled() || Subject != GetShooterCharacter())
	{
		return;
	}

	if (AShooterWeapon* Weapon = AmmoPredictionWeapon.Get())
	{
		// 夹具专用：设置本地开火节拍起点；0 表示立即可开火。
		Weapon->SetLocalFireCooldownRemainingForAutomationTest(FMath::Max(0.0f, LocalCooldownSeconds));
	}

	AmmoPredictionSubmittedStep = Step;
	const float Now = GetWorld()->GetTimeSeconds();
	AmmoPredictionHoldEndTime = Now + FMath::Max(0.1f, HoldSeconds);
	AmmoPredictionHoldReloadTime = ReloadAtSeconds >= 0.0f ? Now + ReloadAtSeconds : -1.0f;
	bAmmoPredictionReloadSubmitted = false;
	bAmmoPredictionReleaseObserved = false;
	AmmoPredictionReleaseSettleTime = 0.0f;
	bAmmoPredictionHoldingFire = true;
	Subject->DoStartFiring();
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_CLIENT_HOLD_START Step=%d Hold=%.2f ReloadAt=%.2f"),
		Step, HoldSeconds, ReloadAtSeconds);
}

void AShooterNetworkTestCoordinator::ServerReportAmmoPredictionSample_Implementation(const FShooterAmmoPredictionObservation& Observation)
{
	if (!bAmmoPredictionMode || Observation.Step != AmmoPredictionClientStep)
	{
		return;
	}

	AmmoPredictionLatest = Observation;
	AmmoPredictionLatestArrivalTime = GetWorld()->GetTimeSeconds();
	UE_LOG(LogShootGame, Display,
		TEXT("AMMO_PREDICTION_SAMPLE Step=%d Valid=%d Mag=%d Reserve=%d Pending=%d Predicted=%d ")
		TEXT("Feedback=%d Backfill=%d BackfillReq=%d Verdicts=%d Act=%d Rejects=%d DistinctKeys=%d ")
		TEXT("Records=%d Peak=%d LastKey=%d Committed=%d ByEngine=%d Released=%d AtRelease=%d ")
		TEXT("FireActive=%d ReloadActive=%d Reloading=%d FullAuto=%d CooldownReady=%d"),
		Observation.Step,
		Observation.bValid ? 1 : 0,
		Observation.MagazineAmmo,
		Observation.ReserveAmmo,
		Observation.PendingShots,
		Observation.PredictedMagazineAmmo,
		Observation.PredictedOwnerFeedbackCount,
		Observation.ConfirmedBackfillCount,
		Observation.ConfirmedBackfillRequestCount,
		Observation.ShotVerdictCount,
		Observation.OwnerFireActivationCount,
		Observation.OwnerFireRejectCount,
		Observation.DistinctPredictionKeyCount,
		Observation.UnresolvedShotRecordCount,
		Observation.PeakUnresolvedShotRecords,
		Observation.LastResolvedShotKey,
		Observation.bLastResolvedShotCommitted ? 1 : 0,
		Observation.bLastResolvedShotRejectedByEngine ? 1 : 0,
		Observation.bReleaseSettled ? 1 : 0,
		Observation.ActivationsAtRelease,
		Observation.bFireActive ? 1 : 0,
		Observation.bReloadActive ? 1 : 0,
		Observation.bReloading ? 1 : 0,
		Observation.bCurrentWeaponIsFullAuto ? 1 : 0,
		Observation.bLocalFireCooldownReady ? 1 : 0);
}

// ============================ 服务器阶段 ============================

bool AShooterNetworkTestCoordinator::IsAmmoPredictionClientSampleFresh(int32 Step) const
{
	const UWorld* World = GetWorld();
	return World && AmmoPredictionLatest.Step == Step &&
		World->GetTimeSeconds() - AmmoPredictionLatestArrivalTime <= ShooterAmmoPredictionNetworkTests::SampleFreshSeconds;
}

bool AShooterNetworkTestCoordinator::IsAmmoPredictionFixtureReady(int32 Step, int32 ExpectedPending) const
{
	const AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;
	// 就绪门是每个用例的前置条件：拥有端镜像弹药已收敛到本步骤起点、预算处于夹具固定值、
	// 没有活动动作、也没有上一发遗留的 Shot 记录。生产遗留会让这里超时而不是被静默掩盖。
	const bool bClientReady = IsAmmoPredictionClientSampleFresh(Step) && Sample.bValid &&
		Sample.MagazineAmmo == AmmoPredictionMagazineBefore && Sample.PendingShots == ExpectedPending &&
		Sample.UnresolvedShotRecordCount == 0 && !Sample.bFireActive && !Sample.bReloadActive &&
		!Sample.bReloading && Sample.bLocalFireCooldownReady;
	// 拥有端必须已经用本步骤同一个 RefireRate 就位：射速用例据此排除"两端节拍不一致"的假收敛。
	const bool bRateMatched = FMath::IsNearlyEqual(Sample.RefireRate, AmmoPredictionStepRefireRate, 0.0001f);
	// 两端必须钉在同一把武器上：换枪步骤之后这条判据防止"服务器已换枪、拥有端还停在旧枪"。
	const bool bWeaponMatched = Weapon && Sample.CurrentWeaponId == Weapon->GetWeaponId();
	// 权威射速时钟也必须已就绪：接受 / 拒绝用例都要让被观察的那一次裁决只有一个原因。
	return bClientReady && bRateMatched && bWeaponMatched && Weapon && Weapon->CanCommitAuthorityShot();
}

int32 AShooterNetworkTestCoordinator::GetAmmoPredictionAuthorityRejectProbe() const
{
	AShooterCharacter* Subject = AmmoPredictionSubject.Get();
	const UShooterGameplayAbility_Fire* FireAbility = GetFireAbilityInstanceForTest(Subject);
	return FireAbility ? FireAbility->GetAuthorityRejectCountForTest() : INDEX_NONE;
}

void AShooterNetworkTestCoordinator::ConcludeAmmoPredictionCase(const TCHAR* CaseName, bool bConverged, const FString& Detail)
{
	const AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;
	// 拥有端 HUD 显示层必须最终等于服务器权威值，并且没有未结清的显示激活。
	const bool bHudMatches = Weapon && Sample.bValid && Sample.HudUnsettledCount == 0 &&
		Sample.HudMagazine == Weapon->GetBulletCount() && Sample.HudReserve == Weapon->GetReserveAmmo();
	bConverged = bConverged && bHudMatches;
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_HUD Case=%s Match=%d Mag=%d Reserve=%d Unsettled=%d"),
		CaseName, bHudMatches ? 1 : 0, Sample.HudMagazine, Sample.HudReserve, Sample.HudUnsettledCount);
	if (bConverged)
	{
		++AmmoPredictionConvergedCount;
		UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_CONVERGED Case=%s %s"), CaseName, *Detail);
		return;
	}

	++AmmoPredictionMismatchCount;
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_MISMATCH Case=%s %s"), CaseName, *Detail);
}

void AShooterNetworkTestCoordinator::StartAmmoPredictionStep(int32 Step)
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	if (!Weapon)
	{
		return;
	}

	AmmoPredictionServerStep = Step;
	AmmoPredictionClientStep = Step;
	AmmoPredictionStepStartTime = GetWorld()->GetTimeSeconds();
	AmmoPredictionSettleStartTime = AmmoPredictionStepStartTime;
	AmmoPredictionBefore = AmmoPredictionLatest;
	bAmmoPredictionSettleStarted = false;
	bAmmoPredictionStepCommandSent = false;
	bAmmoPredictionReloadSubmitted = false;
	bAmmoPredictionHoldingFire = false;
	bAmmoPredictionReleaseObserved = false;
	AmmoPredictionReleaseSettleTime = 0.0f;
	AmmoPredictionActivationsAtRelease = 0;
	AmmoPredictionShotsAtReloadCommit = INDEX_NONE;
	bAmmoPredictionShotDuringReload = false;
	AmmoPredictionAuthorityActivationKeys.Reset();
	AmmoPredictionFirstAuthorityActivationTime = -1.0f;
	AmmoPredictionHoldStartTime = -1.0f;
	AmmoPredictionRateLastActivationTime = -1.0f;
	AmmoPredictionRateMinInterval = -1.0f;
	AmmoPredictionRateMaxInterval = -1.0f;
	AmmoPredictionRateIntervalSamples = 0;

	// 本步骤两端必须一致的射速：射速用例使用配置射速，其余步骤恢复武器行配置的原始射速。
	// 只有拥有端请求节拍与服务器权威门控使用同一个 RefireRate，"达成射速"才有意义。
	AmmoPredictionStepRefireRate = IsRateMeasurementStep(Step)
		? GetRateRefireRate(Step)
		: AmmoPredictionOriginalRefireRate;
	if (!FMath::IsNearlyEqual(Weapon->GetRefireRate(), AmmoPredictionStepRefireRate, 0.0001f))
	{
		Weapon->SetRefireRateForAutomationTest(AmmoPredictionStepRefireRate);
	}

	// 每个步骤的权威弹药起点由夹具显式固定：满弹匣 + 足量备弹，
	// 这样"服务器真的开火"与"换弹能补满"都能在同一份起点上被断言。
	// 射速用例把弹匣抬到窗口所需发数，避免窗口末尾因打空弹匣而失真。
	const int32 RateRounds = IsRateMeasurementStep(Step)
		? FMath::CeilToInt(GetRateExpectedRps(Step) * RateMeasurementSeconds) + RateMagazineSlack
		: 0;
	// 弹药下限：弹匣至少 FixtureMinMagazine 发，弹药不足不得伪装成任何用例的结论。
	const int32 MagazineStart = FMath::Max(FMath::Max(Weapon->GetMagazineSize(), RateRounds), FixtureMinMagazine);
	const int32 ReserveStart = FMath::Max(MagazineStart, 1) * FixtureReserveMultiplier;
	if (!SetReloadTestAmmo(Weapon, MagazineStart, ReserveStart))
	{
		FailTest(FString::Printf(TEXT("Ammo prediction could not set authority ammo: Step=%d"), Step));
		bAmmoPredictionFinished = true;
		return;
	}

	AmmoPredictionMagazineBefore = Weapon->GetBulletCount();
	AmmoPredictionReserveBefore = Weapon->GetReserveAmmo();
	AmmoPredictionAuthorityShotsBefore = Weapon->GetAuthorityShotCountForAutomationTest();
	AmmoPredictionAuthorityActivationsBefore = AmmoPredictionAuthorityActivations;
	AmmoPredictionAuthorityRejectsBefore = AmmoPredictionAuthorityRejects;
	AmmoPredictionAuthorityRejectProbeBefore = GetAmmoPredictionAuthorityRejectProbe();
	AmmoPredictionProjectilesBefore = ProjectileSpawnCount;

	// 服务器侧网络证据窗口起点：本步骤起点 → 结算，连接取驱动客户端的那一条。
	// GetNetDriver() 或连接为空时计数保持无效，由报告侧显式声明证据缺口而不是填 0。
	const UNetDriver* NetDriver = GetWorld() ? GetWorld()->GetNetDriver() : nullptr;
	const APlayerController* DriverController = Cast<APlayerController>(GetOwner());
	UNetConnection* Connection = ResolveAmmoPredictionDriverConnection(DriverController, NetDriver);
	AmmoPredictionNetBase = ReadAmmoPredictionNetCounters(Connection, NetDriver);
	AmmoPredictionNetRateWindow.Reset();
	AmmoPredictionNetWindowStartTime = GetWorld()->GetTimeSeconds();

	ClientPrepareAmmoPredictionStep(AmmoPredictionSubjectPlayerId, Step);
	// 射速必须在步骤准备之后下发：准备 RPC 先建立本步骤身份，射速 RPC 再据此生效。
	ClientSetAmmoPredictionRefireRate(Step, AmmoPredictionStepRefireRate);
	UE_LOG(
		LogShootGame,
		Display,
		TEXT("AMMO_PREDICTION_STEP_START Step=%d Mag=%d Reserve=%d AuthorityShots=%d AuthorityActivations=%d"),
		Step,
		AmmoPredictionMagazineBefore,
		AmmoPredictionReserveBefore,
		AmmoPredictionAuthorityShotsBefore,
		AmmoPredictionAuthorityActivationsBefore);
}

bool AShooterNetworkTestCoordinator::AdvanceAmmoPredictionSingleFireStep(int32 Step, bool bExpectAuthorityReject)
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	if (!Weapon)
	{
		return false;
	}

	const int32 ExpectedPending = IsUnpredictedBudgetStep(Step) ? Weapon->GetMagazineSize() : 0;
	if (!bAmmoPredictionStepCommandSent)
	{
		if (!IsAmmoPredictionFixtureReady(Step, ExpectedPending))
		{
			return false;
		}

		if (bExpectAuthorityReject)
		{
			// 服务器仅本地持有的阻塞 Tag：Loose Tag 不复制，拥有端看不到它，
			// 因此拥有端仍会建立本地预测并发出真实请求，服务器在 CanActivateAbility 的 Tag 门控处拒绝。
			if (UAbilitySystemComponent* AbilitySystemComponent = AmmoPredictionObservedASC.Get())
			{
				AbilitySystemComponent->AddLooseGameplayTag(ShooterGameplayTags::State_Reloading);
				bAmmoPredictionServerTagApplied = true;
			}
		}
		bAmmoPredictionStepCommandSent = true;
		ClientSubmitAmmoPredictionFire(Step);
		return false;
	}

	const int32 ShotDelta = Weapon->GetAuthorityShotCountForAutomationTest() - AmmoPredictionAuthorityShotsBefore;
	if (!bAmmoPredictionSettleStarted)
	{
		if (bExpectAuthorityReject)
		{
			if (AmmoPredictionAuthorityRejects - AmmoPredictionAuthorityRejectsBefore < 1)
			{
				if (ShotDelta > 0)
				{
					// 前提构造失败：夹具必须给出一次真实拒绝，而不是"被接受后没有开枪"。
					FailTest(FString::Printf(
						TEXT("Ammo prediction reject fixture was accepted by the server: Step=%d"), Step));
					ClearAmmoPredictionServerTag();
					bAmmoPredictionFinished = true;
				}
				return false;
			}
			ClearAmmoPredictionServerTag();
			AmmoPredictionSettleStartTime = GetWorld()->GetTimeSeconds();
			bAmmoPredictionSettleStarted = true;
			return false;
		}

		// 接受用例：必须观察到这一发真实落到权威 Shot 计数上。
		if (ShotDelta < 1)
		{
			return false;
		}
		AmmoPredictionSettleStartTime = GetWorld()->GetTimeSeconds();
		bAmmoPredictionSettleStarted = true;
		return false;
	}

	const float Wait = bExpectAuthorityReject ? RejectSettleSeconds : SettleSeconds;
	return IsAmmoPredictionClientSampleFresh(Step) && GetWorld()->GetTimeSeconds() - AmmoPredictionSettleStartTime >= Wait;
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionSemiAutoSingleShotStep()
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	if (!Weapon || !AdvanceAmmoPredictionSingleFireStep(StepSemiAutoSingleShot, /*bExpectAuthorityReject*/ false))
	{
		return;
	}

	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;
	const int32 ShotDelta = Weapon->GetAuthorityShotCountForAutomationTest() - AmmoPredictionAuthorityShotsBefore;
	const int32 AcceptedDelta = AmmoPredictionAuthorityActivations - AmmoPredictionAuthorityActivationsBefore;
	const int32 ClientActivationDelta = Sample.OwnerFireActivationCount - AmmoPredictionBefore.OwnerFireActivationCount;
	const int32 PredictedDelta = Sample.PredictedOwnerFeedbackCount - AmmoPredictionBefore.PredictedOwnerFeedbackCount;
	const int32 BackfillDelta = Sample.ConfirmedBackfillCount - AmmoPredictionBefore.ConfirmedBackfillCount;
	const int32 PendingDelta = Sample.PendingShots - AmmoPredictionBefore.PendingShots;
	const int32 VerdictDelta = Sample.ShotVerdictCount - AmmoPredictionBefore.ShotVerdictCount;
	const int32 MagazineDelta = Weapon->GetBulletCount() - AmmoPredictionMagazineBefore;

	// 一次按下 + 松开只允许形成一次本地动作边界；这一次请求必须被服务器独立接受并恰好提交一发。
	const bool bConverged = ShotDelta == 1 && AcceptedDelta == 1 && ClientActivationDelta == 1 &&
		Sample.DistinctPredictionKeyCount == 1 && VerdictDelta == 1 &&
		PredictedDelta == 1 && BackfillDelta == 0 && PendingDelta == 0 && MagazineDelta == -1 &&
		Sample.UnresolvedShotRecordCount == 0 && !Sample.bFireActive && !Sample.bReloadActive &&
		Sample.LastResolvedShotKey == Sample.FirstPredictionKey && Sample.bLastResolvedShotCommitted;
	const FString Detail = FString::Printf(
		TEXT("FullAuto=%d Shots=%d Accepted=%d ClientAct=%d Keys=%d Verdicts=%d Predicted=%d Backfill=%d ")
		TEXT("Pending=%d MagDelta=%d Records=%d LastKey=%d Committed=%d RefireRate=%.3f"),
		Sample.bCurrentWeaponIsFullAuto ? 1 : 0,
		ShotDelta,
		AcceptedDelta,
		ClientActivationDelta,
		Sample.DistinctPredictionKeyCount,
		VerdictDelta,
		PredictedDelta,
		BackfillDelta,
		PendingDelta,
		MagazineDelta,
		Sample.UnresolvedShotRecordCount,
		Sample.LastResolvedShotKey,
		Sample.bLastResolvedShotCommitted ? 1 : 0,
		Weapon->GetRefireRate());
	// 前提说明：本用例跑在被试角色的起始武器（正式 Rifle，全自动）上，
	// 只证明「一次按下沿 → 一次 Activation → 一发 Shot」这一形状；
	// Detail 里的 FullAuto=1 就是这一前提的显式记录。
	// 半自动 OnInputTriggered 策略证据由后面的 SemiAutoWeaponSingleShot / SemiAutoHold 承担：
	// 那两个用例会把当前武器切到动态挑出的正式半自动行，并由拥有端自述武器身份。
	ConcludeAmmoPredictionCase(TEXT("SemiAutoSingleShot"), bConverged, Detail);
	StartAmmoPredictionStep(StepFullAutoHold);
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionFullAutoHoldStep()
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	if (!Weapon)
	{
		return;
	}

	if (!bAmmoPredictionStepCommandSent)
	{
		if (!IsAmmoPredictionFixtureReady(StepFullAutoHold, 0))
		{
			return;
		}
		bAmmoPredictionStepCommandSent = true;
		ClientSubmitAmmoPredictionHoldFire(StepFullAutoHold, AmmoPredictionFullAutoHoldSeconds, 0.0f, -1.0f);
		return;
	}

	const int32 ShotDelta = Weapon->GetAuthorityShotCountForAutomationTest() - AmmoPredictionAuthorityShotsBefore;
	if (!bAmmoPredictionSettleStarted)
	{
		if (ShotDelta < FullAutoMinShots)
		{
			return;
		}
		AmmoPredictionSettleStartTime = GetWorld()->GetTimeSeconds();
		bAmmoPredictionSettleStarted = true;
		return;
	}
	if (!IsAmmoPredictionClientSampleFresh(StepFullAutoHold) ||
		GetWorld()->GetTimeSeconds() - AmmoPredictionSettleStartTime < SettleSeconds + FullAutoHoldSeconds)
	{
		return;
	}

	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;
	const int32 AcceptedDelta = AmmoPredictionAuthorityActivations - AmmoPredictionAuthorityActivationsBefore;
	const int32 ClientActivationDelta = Sample.OwnerFireActivationCount - AmmoPredictionBefore.OwnerFireActivationCount;
	const int32 PendingDelta = Sample.PendingShots - AmmoPredictionBefore.PendingShots;
	const int32 VerdictDelta = Sample.ShotVerdictCount - AmmoPredictionBefore.ShotVerdictCount;
	if (!Sample.bReleaseSettled || Sample.PendingShots != AmmoPredictionBefore.PendingShots ||
		Sample.UnresolvedShotRecordCount != 0 || Sample.OwnerFireActivationCount != Sample.ActivationsAtRelease)
	{
		return;
	}

	// 目标语义：连发由「按住 + 本地节拍就绪」下的独立 Activation 表达。
	// - 每一发都必须来自一次自己被服务器接受的 Activation（Shots == Accepted），不允许 2 发/Activation；
	// - 每一次激活都必须拿到自己的 PredictionKey（服务器与拥有端各自检查 key 的唯一性）；
	// - 拥有端请求数受本地节拍限制，且必须带上限，避免出现每帧一次的 RPC 洪水。
	const bool bConverged = ShotDelta >= FullAutoMinShots && ShotDelta == AcceptedDelta &&
		AmmoPredictionAuthorityActivationKeys.Num() == AcceptedDelta &&
		ClientActivationDelta == Sample.DistinctPredictionKeyCount &&
		ClientActivationDelta >= FullAutoMinShots && ClientActivationDelta <= AmmoPredictionMaxPacedShots &&
		VerdictDelta >= 1 && PendingDelta == 0 && !Sample.bFireActive && !Sample.bReloadActive;
	const FString Detail = FString::Printf(
		TEXT("Shots=%d Accepted=%d AuthKeys=%d ClientAct=%d ClientKeys=%d MaxPaced=%d Verdicts=%d ")
		TEXT("Pending=%d Records=%d Peak=%d Released=%d AtRelease=%d Hold=%.2f"),
		ShotDelta,
		AcceptedDelta,
		AmmoPredictionAuthorityActivationKeys.Num(),
		ClientActivationDelta,
		Sample.DistinctPredictionKeyCount,
		AmmoPredictionMaxPacedShots,
		VerdictDelta,
		PendingDelta,
		Sample.UnresolvedShotRecordCount,
		Sample.PeakUnresolvedShotRecords,
		Sample.bReleaseSettled ? 1 : 0,
		Sample.ActivationsAtRelease,
		AmmoPredictionFullAutoHoldSeconds);
	ConcludeAmmoPredictionCase(TEXT("FullAutoHold"), bConverged, Detail);
	StartAmmoPredictionStep(StepPredictedAccepted);
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionPredictedAcceptedStep()
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	if (!Weapon || !AdvanceAmmoPredictionSingleFireStep(StepPredictedAccepted, /*bExpectAuthorityReject*/ false))
	{
		return;
	}

	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;
	const int32 ShotDelta = Weapon->GetAuthorityShotCountForAutomationTest() - AmmoPredictionAuthorityShotsBefore;
	const int32 AcceptedDelta = AmmoPredictionAuthorityActivations - AmmoPredictionAuthorityActivationsBefore;
	const int32 PredictedDelta = Sample.PredictedOwnerFeedbackCount - AmmoPredictionBefore.PredictedOwnerFeedbackCount;
	const int32 BackfillDelta = Sample.ConfirmedBackfillCount - AmmoPredictionBefore.ConfirmedBackfillCount;
	const int32 BackfillReqBefore = AmmoPredictionBefore.ConfirmedBackfillRequestCount;
	const int32 BackfillReqDelta = Sample.ConfirmedBackfillRequestCount - BackfillReqBefore;
	const int32 PendingDelta = Sample.PendingShots - AmmoPredictionBefore.PendingShots;
	const int32 VerdictDelta = Sample.ShotVerdictCount - AmmoPredictionBefore.ShotVerdictCount;
	const int32 HudPredictedDelta = Sample.HudPredictedUpdates - AmmoPredictionBefore.HudPredictedUpdates;

	// 提前表现的那一发被接受：Pending 只由这一发的 Committed 裁决结清，绝不补播第二次表现。
	const bool bConverged = ShotDelta == 1 && AcceptedDelta == 1 && PredictedDelta == 1 && BackfillDelta == 0 &&
		BackfillReqDelta == 0 && PendingDelta == 0 && VerdictDelta == 1 && HudPredictedDelta >= 1 &&
		Sample.UnresolvedShotRecordCount == 0 && Sample.LastResolvedShotKey == Sample.FirstPredictionKey &&
		Sample.bLastResolvedShotCommitted &&
		Weapon->GetBulletCount() == AmmoPredictionMagazineBefore - 1;
	const FString Detail = FString::Printf(
		TEXT("Shots=%d Accepted=%d Predicted=%d Backfill=%d BackfillReq=%d Pending=%d Verdicts=%d ")
		TEXT("HudPredicted=%d Records=%d LastKey=%d Committed=%d"),
		ShotDelta,
		AcceptedDelta,
		PredictedDelta,
		BackfillDelta,
		BackfillReqDelta,
		PendingDelta,
		VerdictDelta,
		HudPredictedDelta,
		Sample.UnresolvedShotRecordCount,
		Sample.LastResolvedShotKey,
		Sample.bLastResolvedShotCommitted ? 1 : 0);
	ConcludeAmmoPredictionCase(TEXT("PredictedAccepted"), bConverged, Detail);
	StartAmmoPredictionStep(StepPredictedRejected);
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionPredictedRejectedStep()
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	if (!Weapon || !AdvanceAmmoPredictionSingleFireStep(StepPredictedRejected, /*bExpectAuthorityReject*/ true))
	{
		return;
	}

	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;
	const int32 ShotDelta = Weapon->GetAuthorityShotCountForAutomationTest() - AmmoPredictionAuthorityShotsBefore;
	const int32 AcceptedDelta = AmmoPredictionAuthorityActivations - AmmoPredictionAuthorityActivationsBefore;
	const int32 RejectedDelta = AmmoPredictionAuthorityRejects - AmmoPredictionAuthorityRejectsBefore;
	const int32 ProjectileDelta = ProjectileSpawnCount - AmmoPredictionProjectilesBefore;
	const int32 ClientActivationDelta = Sample.OwnerFireActivationCount - AmmoPredictionBefore.OwnerFireActivationCount;
	const int32 PredictedDelta = Sample.PredictedOwnerFeedbackCount - AmmoPredictionBefore.PredictedOwnerFeedbackCount;
	const int32 BackfillDelta = Sample.ConfirmedBackfillCount - AmmoPredictionBefore.ConfirmedBackfillCount;
	const int32 BackfillReqBefore = AmmoPredictionBefore.ConfirmedBackfillRequestCount;
	const int32 BackfillReqDelta = Sample.ConfirmedBackfillRequestCount - BackfillReqBefore;
	const int32 PendingDelta = Sample.PendingShots - AmmoPredictionBefore.PendingShots;

	// 被拒绝的是「已经提前表现、已经占用预算」的那一发：
	// 引擎 Reject 通道按同一个 PredictionKey 退还恰好一发（净 Pending 变化为 0），不补播、不生成弹丸。
	const bool bConverged = ShotDelta == 0 && AcceptedDelta == 0 && RejectedDelta >= 1 && ProjectileDelta == 0 &&
		ClientActivationDelta == 1 && PredictedDelta == 1 && BackfillDelta == 0 && BackfillReqDelta == 0 &&
		PendingDelta == 0 && Sample.UnresolvedShotRecordCount == 0 &&
		Sample.MagazineAmmo == AmmoPredictionMagazineBefore &&
		Sample.LastResolvedShotKey == Sample.FirstPredictionKey && !Sample.bLastResolvedShotCommitted &&
		Sample.bLastResolvedShotRejectedByEngine;
	const FString Detail = FString::Printf(
		TEXT("Shots=%d Accepted=%d ServerRejects=%d Projectiles=%d ClientAct=%d Predicted=%d Backfill=%d ")
		TEXT("Pending=%d Records=%d OwnerMag=%d LastKey=%d Committed=%d ByEngine=%d RejectProbeDelta=%d"),
		ShotDelta,
		AcceptedDelta,
		RejectedDelta,
		ProjectileDelta,
		ClientActivationDelta,
		PredictedDelta,
		BackfillDelta,
		PendingDelta,
		Sample.UnresolvedShotRecordCount,
		Sample.MagazineAmmo,
		Sample.LastResolvedShotKey,
		Sample.bLastResolvedShotCommitted ? 1 : 0,
		Sample.bLastResolvedShotRejectedByEngine ? 1 : 0,
		GetAmmoPredictionAuthorityRejectProbe() - AmmoPredictionAuthorityRejectProbeBefore);
	ConcludeAmmoPredictionCase(TEXT("PredictedRejected"), bConverged, Detail);
	if (bConverged)
	{
		// H-A2 定向验收标记：拒绝只退还被拒的那一发，且不依赖任何 CaughtUp 语义。
		UE_LOG(LogShootGame, Display, TEXT("AUTOMATION_TEST_AMMO_PREDICTION_H_A2_SUCCESS"));
	}
	StartAmmoPredictionStep(StepFullAutoRelease);
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionFullAutoReleaseStep()
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	if (!Weapon)
	{
		return;
	}

	if (!bAmmoPredictionStepCommandSent)
	{
		if (!IsAmmoPredictionFixtureReady(StepFullAutoRelease, 0))
		{
			return;
		}
		bAmmoPredictionStepCommandSent = true;
		ClientSubmitAmmoPredictionHoldFire(StepFullAutoRelease, AmmoPredictionFullAutoHoldSeconds, 0.0f, -1.0f);
		return;
	}

	const int32 ShotDelta = Weapon->GetAuthorityShotCountForAutomationTest() - AmmoPredictionAuthorityShotsBefore;
	if (!bAmmoPredictionSettleStarted)
	{
		// 至少要有一轮真实连发，然后才有"松手之后不再产生激活"的观察对象。
		if (ShotDelta < 2)
		{
			return;
		}
		AmmoPredictionSettleStartTime = GetWorld()->GetTimeSeconds();
		bAmmoPredictionSettleStarted = true;
		return;
	}
	if (!IsAmmoPredictionClientSampleFresh(StepFullAutoRelease) ||
		GetWorld()->GetTimeSeconds() - AmmoPredictionSettleStartTime < SettleSeconds + FullAutoHoldSeconds)
	{
		return;
	}

	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;
	const int32 AcceptedDelta = AmmoPredictionAuthorityActivations - AmmoPredictionAuthorityActivationsBefore;
	const int32 ClientActivationDelta = Sample.OwnerFireActivationCount - AmmoPredictionBefore.OwnerFireActivationCount;
	const int32 PendingDelta = Sample.PendingShots - AmmoPredictionBefore.PendingShots;
	// 松手时刻已经固定过一次拥有端激活计数；松手后再跨过一个完整节拍，计数不得再增长。
	const bool bNoActivationAfterRelease = Sample.bReleaseSettled &&
		Sample.ActivationsAtRelease >= AmmoPredictionBefore.OwnerFireActivationCount + 2 &&
		Sample.OwnerFireActivationCount == Sample.ActivationsAtRelease;
	const bool bPendingSettled = Sample.PendingShots == AmmoPredictionBefore.PendingShots;
	const bool bRecordsSettled = Sample.UnresolvedShotRecordCount == 0;
	if (!bNoActivationAfterRelease || !bPendingSettled || !bRecordsSettled)
	{
		return;
	}

	const bool bConverged = ShotDelta >= 2 && ShotDelta == AcceptedDelta && ClientActivationDelta >= 2 &&
		ClientActivationDelta == Sample.DistinctPredictionKeyCount && PendingDelta == 0 &&
		!Sample.bFireActive && !Sample.bReloadActive && !Sample.bReloading &&
		Sample.MagazineAmmo == Weapon->GetBulletCount();
	const FString Detail = FString::Printf(
		TEXT("Shots=%d Accepted=%d ClientAct=%d Keys=%d ReleaseAt=%d FinalAct=%d Pending=%d Records=%d ")
		TEXT("Peak=%d ServerMag=%d OwnerMag=%d FireActive=%d"),
		ShotDelta,
		AcceptedDelta,
		ClientActivationDelta,
		Sample.DistinctPredictionKeyCount,
		Sample.ActivationsAtRelease,
		Sample.OwnerFireActivationCount,
		PendingDelta,
		Sample.UnresolvedShotRecordCount,
		Sample.PeakUnresolvedShotRecords,
		Weapon->GetBulletCount(),
		Sample.MagazineAmmo,
		Sample.bFireActive ? 1 : 0);
	ConcludeAmmoPredictionCase(TEXT("FullAutoRelease"), bConverged, Detail);
	StartAmmoPredictionStep(StepReloadLifecycle);
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionReloadLifecycleStep()
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	AShooterCharacter* Subject = AmmoPredictionSubject.Get();
	UAbilitySystemComponent* AbilitySystemComponent = Subject ? Subject->GetAbilitySystemComponent() : nullptr;
	if (!Weapon || !Subject || !AbilitySystemComponent)
	{
		return;
	}

	if (!bAmmoPredictionStepCommandSent)
	{
		if (!IsAmmoPredictionFixtureReady(StepReloadLifecycle, 0))
		{
			return;
		}
		// Hold 期间触发 Reload：Reload PreActivate 会取消活动中的 Fire，
		// 夹具据此验证取消 / 阻塞之后没有残留的预测预算与 Shot 记录。
		const float HoldSeconds = AmmoPredictionReloadHoldSeconds;
		bAmmoPredictionStepCommandSent = true;
		AmmoPredictionHoldStartTime = GetWorld()->GetTimeSeconds();
		ClientSubmitAmmoPredictionHoldFire(StepReloadLifecycle, HoldSeconds, 0.0f, ReloadTriggerSeconds);
		return;
	}

	// Reload 阻塞窗口：服务器一旦观察到换弹状态，窗口内的权威 Shot 计数就不得再增长。
	const int32 ShotsNow = Weapon->GetAuthorityShotCountForAutomationTest();
	const bool bServerReloading = HasActiveReloadAbility(Subject) ||
		AbilitySystemComponent->HasMatchingGameplayTag(ShooterGameplayTags::State_Reloading);
	if (bServerReloading)
	{
		if (AmmoPredictionShotsAtReloadCommit == INDEX_NONE)
		{
			AmmoPredictionShotsAtReloadCommit = ShotsNow;
			UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_RELOAD_BLOCK_START Step=%d Shots=%d"),
				AmmoPredictionServerStep, ShotsNow);
		}
		else if (ShotsNow != AmmoPredictionShotsAtReloadCommit)
		{
			bAmmoPredictionShotDuringReload = true;
		}
	}

	const int32 MagazineSize = Weapon->GetMagazineSize();
	const bool bReloadFinished = !bServerReloading && Weapon->GetBulletCount() == MagazineSize;
	if (!bReloadFinished || !IsAmmoPredictionClientSampleFresh(StepReloadLifecycle))
	{
		return;
	}
	if (AmmoPredictionShotsAtReloadCommit == INDEX_NONE)
	{
		// 前提构造失败：客户端在 Hold 中的 Reload 必须真实落地，否则本用例没有阻塞窗口可断言。
		// 但"没观察到窗口"只有在给足完整往返时间之后才能算失败：
		// Hold 指令、Reload 输入与松手都是可靠 RPC，高延迟下服务器会先看到"弹匣满 + 无换弹"。
		const float PremiseDeadline = AmmoPredictionHoldStartTime + AmmoPredictionReloadHoldSeconds +
			Weapon->GetReloadDuration() + ReloadPremiseMarginSeconds;
		if (GetWorld()->GetTimeSeconds() < PremiseDeadline)
		{
			return;
		}

		FailTest(TEXT("Ammo prediction ReloadLifecycle never observed a server-side reload window"));
		bAmmoPredictionFinished = true;
		return;
	}
	if (!bAmmoPredictionSettleStarted)
	{
		AmmoPredictionSettleStartTime = GetWorld()->GetTimeSeconds();
		bAmmoPredictionSettleStarted = true;
		return;
	}

	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;
	if (GetWorld()->GetTimeSeconds() - AmmoPredictionSettleStartTime < SettleSeconds ||
		!Sample.bReleaseSettled || Sample.PendingShots != AmmoPredictionBefore.PendingShots ||
		Sample.UnresolvedShotRecordCount != 0)
	{
		return;
	}

	const int32 ShotDelta = Weapon->GetAuthorityShotCountForAutomationTest() - AmmoPredictionAuthorityShotsBefore;
	const int32 AcceptedDelta = AmmoPredictionAuthorityActivations - AmmoPredictionAuthorityActivationsBefore;
	const int32 ClientActivationDelta = Sample.OwnerFireActivationCount - AmmoPredictionBefore.OwnerFireActivationCount;
	const bool bConverged = !bAmmoPredictionShotDuringReload && ShotDelta >= 1 && ShotDelta == AcceptedDelta &&
		ClientActivationDelta >= 1 && ClientActivationDelta == Sample.DistinctPredictionKeyCount &&
		Weapon->GetBulletCount() == MagazineSize && Weapon->GetReserveAmmo() < AmmoPredictionReserveBefore &&
		Sample.MagazineAmmo == MagazineSize && Sample.PendingShots == AmmoPredictionBefore.PendingShots &&
		!Sample.bFireActive && !Sample.bReloadActive && !Sample.bReloading;
	const FString Detail = FString::Printf(
		TEXT("Shots=%d Accepted=%d ClientAct=%d Keys=%d ShotsAtReload=%d ShotDuringReload=%d ")
		TEXT("ServerMag=%d/%d Reserve=%d->%d OwnerMag=%d Pending=%d Records=%d Released=%d"),
		ShotDelta,
		AcceptedDelta,
		ClientActivationDelta,
		Sample.DistinctPredictionKeyCount,
		AmmoPredictionShotsAtReloadCommit,
		bAmmoPredictionShotDuringReload ? 1 : 0,
		Weapon->GetBulletCount(),
		MagazineSize,
		AmmoPredictionReserveBefore,
		Weapon->GetReserveAmmo(),
		Sample.MagazineAmmo,
		Sample.PendingShots,
		Sample.UnresolvedShotRecordCount,
		Sample.bReleaseSettled ? 1 : 0);
	ConcludeAmmoPredictionCase(TEXT("ReloadLifecycle"), bConverged, Detail);
	StartAmmoPredictionStep(StepRate600Rpm);
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionRateStep(int32 Step)
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	if (!Weapon)
	{
		return;
	}

	const TCHAR* CaseName = Step == StepRate600Rpm ? TEXT("Rate600Rpm") : TEXT("Rate900Rpm");
	const float ExpectedRps = GetRateExpectedRps(Step);
	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;
	if (!bAmmoPredictionStepCommandSent)
	{
		if (!IsAmmoPredictionFixtureReady(Step, 0))
		{
			return;
		}
		bAmmoPredictionStepCommandSent = true;
		AmmoPredictionHoldStartTime = GetWorld()->GetTimeSeconds();
		ClientSubmitAmmoPredictionHoldFire(Step, RateMeasurementSeconds, 0.0f, -1.0f);
		return;
	}

	const int32 ShotDelta = Weapon->GetAuthorityShotCountForAutomationTest() - AmmoPredictionAuthorityShotsBefore;
	if (!bAmmoPredictionSettleStarted)
	{
		// 窗口结束的标志是拥有端松手并跨过一个完整本地节拍；之后仍要等一段结算，
		// 让最后一发的裁决回到拥有端，Pending 才能收敛到 0。至少两发才存在可测的间隔。
		if (ShotDelta < 2 || !Sample.bReleaseSettled)
		{
			return;
		}
		AmmoPredictionSettleStartTime = GetWorld()->GetTimeSeconds();
		bAmmoPredictionSettleStarted = true;
		return;
	}

	const float SettleElapsed = GetWorld()->GetTimeSeconds() - AmmoPredictionSettleStartTime;
	if (!IsAmmoPredictionClientSampleFresh(Step) || SettleElapsed < SettleSeconds)
	{
		return;
	}

	// 本步骤起点样本的短别名：让每条增量断言都能落在 120 列以内。
	const FShooterAmmoPredictionObservation& Before = AmmoPredictionBefore;
	const int32 AcceptedDelta = AmmoPredictionAuthorityActivations - AmmoPredictionAuthorityActivationsBefore;
	const int32 RejectDelta = AmmoPredictionAuthorityRejects - AmmoPredictionAuthorityRejectsBefore;
	const int32 ClientActivationDelta = Sample.OwnerFireActivationCount - Before.OwnerFireActivationCount;
	const int32 PredictedDelta = Sample.PredictedOwnerFeedbackCount - Before.PredictedOwnerFeedbackCount;
	const int32 BackfillDelta = Sample.ConfirmedBackfillCount - Before.ConfirmedBackfillCount;
	const int32 BackfillReqDelta = Sample.ConfirmedBackfillRequestCount - Before.ConfirmedBackfillRequestCount;

	// 达成射速只在权威时钟上测量：第一发到最后一发之间的间隔数除以其跨度，
	// 因此不把"从下指令到第一发"的启动延迟算进射速。
	const float FirstShotTime = AmmoPredictionFirstAuthorityActivationTime;
	const float MeasuredSeconds = Weapon->GetTimeOfLastShotForAutomationTest() - FirstShotTime;
	const int32 Intervals = ShotDelta - 1;
	const float AchievedRps = MeasuredSeconds > 0.0f && Intervals >= 1
		? static_cast<float>(Intervals) / MeasuredSeconds
		: 0.0f;
	const float RateErrorRatio = FMath::Abs(AchievedRps - ExpectedRps) / ExpectedRps;
	const float HoldElapsed = AmmoPredictionHoldStartTime >= 0.0f
		? GetWorld()->GetTimeSeconds() - AmmoPredictionHoldStartTime
		: 0.0f;

	// 服务器侧网络证据：连接取驱动客户端那一条；累计口径做窗口差值，速率口径用周期采样的平均 / 峰值。
	// GetNetDriver() 或连接为空时字段保持 INDEX_NONE，并由 AMMO_PREDICTION_NET_GAP 显式声明缺口。
	const UNetDriver* NetDriver = GetWorld() ? GetWorld()->GetNetDriver() : nullptr;
	const APlayerController* DriverController = Cast<APlayerController>(GetOwner());
	const UNetConnection* ServerConnection = ResolveAmmoPredictionDriverConnection(DriverController, NetDriver);
	const FShooterAmmoPredictionNetCounters NetNow = ReadAmmoPredictionNetCounters(ServerConnection, NetDriver);
	const bool bServerTotalsAvailable = NetNow.bValid && AmmoPredictionNetBase.bValid;
	const float ServerNetSeconds = AmmoPredictionNetWindowStartTime >= 0.0f
		? GetWorld()->GetTimeSeconds() - AmmoPredictionNetWindowStartTime
		: 0.0f;
	const int32 ServerInBytes = bServerTotalsAvailable
		? DeltaNetCounter(NetNow.InBytes, AmmoPredictionNetBase.InBytes)
		: INDEX_NONE;
	const int32 ServerOutBytes = bServerTotalsAvailable
		? DeltaNetCounter(NetNow.OutBytes, AmmoPredictionNetBase.OutBytes)
		: INDEX_NONE;
	const int32 ServerInPackets = bServerTotalsAvailable
		? DeltaNetCounter(NetNow.InPackets, AmmoPredictionNetBase.InPackets)
		: INDEX_NONE;
	const int32 ServerOutPackets = bServerTotalsAvailable
		? DeltaNetCounter(NetNow.OutPackets, AmmoPredictionNetBase.OutPackets)
		: INDEX_NONE;
	const int32 ServerRPCs = bServerTotalsAvailable
		? DeltaNetCounter(NetNow.RPCsCalled, AmmoPredictionNetBase.RPCsCalled)
		: INDEX_NONE;
	const float ServerCumInBps = NetBytesPerSecond(ServerInBytes, ServerNetSeconds);
	const float ServerCumOutBps = NetBytesPerSecond(ServerOutBytes, ServerNetSeconds);
	const float ClientCumInBps = NetBytesPerSecond(Sample.NetInBytes, Sample.NetWindowSeconds);
	const float ClientCumOutBps = NetBytesPerSecond(Sample.NetOutBytes, Sample.NetWindowSeconds);
	const int32 ClientPlayerRPCs = Sample.NetRPCsCalled != INDEX_NONE && Sample.NetSampleReports != INDEX_NONE
		? Sample.NetRPCsCalled - Sample.NetSampleReports
		: INDEX_NONE;
	const bool bServerRatesAvailable = AmmoPredictionNetRateWindow.bConnectionResolved &&
		AmmoPredictionNetRateWindow.Samples > 0 && (AmmoPredictionNetRateWindow.PeakInBytesPerSecond > 0.0f ||
			AmmoPredictionNetRateWindow.PeakOutBytesPerSecond > 0.0f);
	const bool bClientRatesAvailable = Sample.bNetConnectionResolved && Sample.NetRateSamples > 0 &&
		(Sample.NetInBytesPerSecondPeak > 0.0f || Sample.NetOutBytesPerSecondPeak > 0.0f);
	if (!bServerRatesAvailable || !bClientRatesAvailable)
	{
		// 明确声明证据缺口，不替换成任何数字。
		const TCHAR* GapReason = !AmmoPredictionNetRateWindow.bConnectionResolved ||
			!Sample.bNetConnectionResolved ? TEXT("NoConnection") : TEXT("RateCountersZero");
		const FString NetGapLine = FString::Printf(
			TEXT("AMMO_PREDICTION_NET_GAP Case=%s Reason=%s ServerConnection=%d ServerSamples=%d ")
			TEXT("ClientConnection=%d ClientSamples=%d ServerTotalsAvailable=%d"),
			CaseName,
			GapReason,
			AmmoPredictionNetRateWindow.bConnectionResolved ? 1 : 0,
			AmmoPredictionNetRateWindow.Samples,
			Sample.bNetConnectionResolved ? 1 : 0,
			Sample.NetRateSamples,
			bServerTotalsAvailable ? 1 : 0);
		UE_LOG(LogShootGame, Display, TEXT("%s"), *NetGapLine);
	}

	// 三条硬断言：一次被接受的 Activation 恰好一发；达成射速在 ±15% 内；窗口结束预算收敛。
	const bool bConverged = ShotDelta == AcceptedDelta && MeasuredSeconds > 0.0f && Intervals >= 1 &&
		RateErrorRatio <= RateToleranceRatio && Sample.PendingShots == 0 && Sample.UnresolvedShotRecordCount == 0;

	const FString RateResult = FString::Printf(
		TEXT("AMMO_PREDICTION_RATE_RESULT Case=%s ConfiguredRps=%.3f ConfiguredHoldSeconds=%.2f ")
		TEXT("AchievedRps=%.3f RateErrorPct=%.2f Shots=%d Intervals=%d MeasuredSeconds=%.3f ")
		TEXT("ElapsedSinceHold=%.3f IntervalSamples=%d MinInterval=%.4f MaxInterval=%.4f ")
		TEXT("ClientActivations=%d AcceptedActivations=%d AuthorityRejects=%d AuthorityRejectProbe=%d ")
		TEXT("RefireRate=%.4f PredictedFeedback=%d BackfillFeedback=%d BackfillRequests=%d ")
		TEXT("Pending=%d PendingStart=%d UnresolvedRecords=%d"),
		CaseName,
		ExpectedRps,
		RateMeasurementSeconds,
		AchievedRps,
		RateErrorRatio * 100.0f,
		ShotDelta,
		Intervals,
		MeasuredSeconds,
		HoldElapsed,
		AmmoPredictionRateIntervalSamples,
		AmmoPredictionRateMinInterval,
		AmmoPredictionRateMaxInterval,
		ClientActivationDelta,
		AcceptedDelta,
		RejectDelta,
		GetAmmoPredictionAuthorityRejectProbe() - AmmoPredictionAuthorityRejectProbeBefore,
		Sample.RefireRate,
		PredictedDelta,
		BackfillDelta,
		BackfillReqDelta,
		Sample.PendingShots,
		AmmoPredictionBefore.PendingShots,
		Sample.UnresolvedShotRecordCount);
	UE_LOG(LogShootGame, Display, TEXT("%s"), *RateResult);

	// 两侧网络计数如实上报：夹具自身 20Hz 采样 RPC 也计入其中，
	// 因此客户端 NetRPCsCalled - NetSampleReports 才是与玩家动作相关的 RPC 数。
	const FString ServerNetLine = FString::Printf(
		TEXT("AMMO_PREDICTION_RATE_NET Case=%s Role=Server Connected=%d RateSamples=%d InBpsAvg=%.0f ")
		TEXT("InBpsPeak=%.0f OutBpsAvg=%.0f OutBpsPeak=%.0f InPpsAvg=%.1f InPpsPeak=%.1f ")
		TEXT("OutPpsAvg=%.1f OutPpsPeak=%.1f CumInBytes=%d CumOutBytes=%d CumInPackets=%d ")
		TEXT("CumOutPackets=%d RPCCalls=%d WindowSeconds=%.3f CumInBps=%.0f CumOutBps=%.0f"),
		CaseName,
		bServerRatesAvailable ? 1 : 0,
		AmmoPredictionNetRateWindow.Samples,
		AmmoPredictionNetRateWindow.GetAverageInBytesPerSecond(),
		AmmoPredictionNetRateWindow.PeakInBytesPerSecond,
		AmmoPredictionNetRateWindow.GetAverageOutBytesPerSecond(),
		AmmoPredictionNetRateWindow.PeakOutBytesPerSecond,
		AmmoPredictionNetRateWindow.GetAverageInPacketsPerSecond(),
		AmmoPredictionNetRateWindow.PeakInPacketsPerSecond,
		AmmoPredictionNetRateWindow.GetAverageOutPacketsPerSecond(),
		AmmoPredictionNetRateWindow.PeakOutPacketsPerSecond,
		ServerInBytes,
		ServerOutBytes,
		ServerInPackets,
		ServerOutPackets,
		ServerRPCs,
		ServerNetSeconds,
		ServerCumInBps,
		ServerCumOutBps);
	UE_LOG(LogShootGame, Display, TEXT("%s"), *ServerNetLine);
	const FString ClientNetLine = FString::Printf(
		TEXT("AMMO_PREDICTION_RATE_NET Case=%s Role=Client Connected=%d RateSamples=%d InBpsAvg=%.0f ")
		TEXT("InBpsPeak=%.0f OutBpsAvg=%.0f OutBpsPeak=%.0f InPpsAvg=%.1f InPpsPeak=%.1f ")
		TEXT("OutPpsAvg=%.1f OutPpsPeak=%.1f CumInBytes=%d CumOutBytes=%d CumInPackets=%d ")
		TEXT("CumOutPackets=%d RPCCalls=%d SampleReports=%d PlayerRPCs=%d WindowSeconds=%.3f ")
		TEXT("CumInBps=%.0f CumOutBps=%.0f"),
		CaseName,
		bClientRatesAvailable ? 1 : 0,
		Sample.NetRateSamples,
		Sample.NetInBytesPerSecondAvg,
		Sample.NetInBytesPerSecondPeak,
		Sample.NetOutBytesPerSecondAvg,
		Sample.NetOutBytesPerSecondPeak,
		Sample.NetInPacketsPerSecondAvg,
		Sample.NetInPacketsPerSecondPeak,
		Sample.NetOutPacketsPerSecondAvg,
		Sample.NetOutPacketsPerSecondPeak,
		Sample.NetInBytes,
		Sample.NetOutBytes,
		Sample.NetInPackets,
		Sample.NetOutPackets,
		Sample.NetRPCsCalled,
		Sample.NetSampleReports,
		ClientPlayerRPCs,
		Sample.NetWindowSeconds,
		ClientCumInBps,
		ClientCumOutBps);
	UE_LOG(LogShootGame, Display, TEXT("%s"), *ClientNetLine);

	const FString Detail = FString::Printf(
		TEXT("Rps=%.3f/%.3f ErrPct=%.2f Shots=%d Accepted=%d ClientAct=%d Rejects=%d Predicted=%d ")
		TEXT("Backfill=%d Requests=%d Pending=%d PendingStart=%d Records=%d NetOutBps(S=%.0f/C=%.0f) ")
		TEXT("RateSamples(S=%d/C=%d)"),
		AchievedRps,
		ExpectedRps,
		RateErrorRatio * 100.0f,
		ShotDelta,
		AcceptedDelta,
		ClientActivationDelta,
		RejectDelta,
		PredictedDelta,
		BackfillDelta,
		BackfillReqDelta,
		Sample.PendingShots,
		AmmoPredictionBefore.PendingShots,
		Sample.UnresolvedShotRecordCount,
		AmmoPredictionNetRateWindow.GetAverageOutBytesPerSecond(),
		Sample.NetOutBytesPerSecondAvg,
		AmmoPredictionNetRateWindow.Samples,
		Sample.NetRateSamples);
	ConcludeAmmoPredictionCase(CaseName, bConverged, Detail);
	StartAmmoPredictionStep(Step == StepRate600Rpm ? StepRate900Rpm : StepUnpredictedAccepted);
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionUnpredictedAcceptedStep()
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	if (!Weapon || !AdvanceAmmoPredictionSingleFireStep(StepUnpredictedAccepted, /*bExpectAuthorityReject*/ false))
	{
		return;
	}

	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;
	const int32 ShotDelta = Weapon->GetAuthorityShotCountForAutomationTest() - AmmoPredictionAuthorityShotsBefore;
	const int32 AcceptedDelta = AmmoPredictionAuthorityActivations - AmmoPredictionAuthorityActivationsBefore;
	const int32 ClientActivationDelta = Sample.OwnerFireActivationCount - AmmoPredictionBefore.OwnerFireActivationCount;
	const int32 PredictedDelta = Sample.PredictedOwnerFeedbackCount - AmmoPredictionBefore.PredictedOwnerFeedbackCount;
	const int32 BackfillDelta = Sample.ConfirmedBackfillCount - AmmoPredictionBefore.ConfirmedBackfillCount;
	const int32 BackfillReqBefore = AmmoPredictionBefore.ConfirmedBackfillRequestCount;
	const int32 BackfillReqDelta = Sample.ConfirmedBackfillRequestCount - BackfillReqBefore;
	// 夹具把拥有端预算固定在弹匣容量上（预算 0）：本步骤结束时它必须完全没被动过。
	const int32 InjectedPending = Weapon->GetMagazineSize();
	const int32 PendingDelta = Sample.PendingShots - InjectedPending;
	const int32 VerdictDelta = Sample.ShotVerdictCount - AmmoPredictionBefore.ShotVerdictCount;
	const int32 MontageDelta = Sample.MontageCount - AmmoPredictionBefore.MontageCount;
	const int32 MuzzleDelta = Sample.MuzzleCount - AmmoPredictionBefore.MuzzleCount;
	const int32 SoundDelta = Sample.SoundCount - AmmoPredictionBefore.SoundCount;
	const int32 RecoilDelta = Sample.RecoilCount - AmmoPredictionBefore.RecoilCount;

	// 预算为 0：拥有端不提前表现、不增加 Pending，但请求照旧到达服务器；
	// 服务器接受并真实提交这一发，拥有端因此收到恰好一次 Committed 裁决 → 恰好一次补播。
	const bool bConverged = ShotDelta == 1 && AcceptedDelta == 1 && ClientActivationDelta == 1 &&
		PredictedDelta == 0 && BackfillDelta == 1 && BackfillReqDelta == 1 && PendingDelta == 0 &&
		VerdictDelta == 1 && Sample.PredictedMagazineAmmo == 0 &&
		MontageDelta == 1 && MuzzleDelta == 1 && SoundDelta == 1 && RecoilDelta == 1 &&
		Sample.UnresolvedShotRecordCount == 0 && Sample.bLastResolvedShotCommitted &&
		Sample.LastResolvedShotKey == Sample.FirstPredictionKey &&
		Weapon->GetBulletCount() == AmmoPredictionMagazineBefore - 1;
	const FString Detail = FString::Printf(
		TEXT("Shots=%d Accepted=%d ClientAct=%d Predicted=%d Backfill=%d BackfillReq=%d Pending=%d ")
		TEXT("Verdicts=%d PredictedMag=%d Channels(M=%d Z=%d S=%d R=%d) Records=%d LastKey=%d"),
		ShotDelta,
		AcceptedDelta,
		ClientActivationDelta,
		PredictedDelta,
		BackfillDelta,
		BackfillReqDelta,
		PendingDelta,
		VerdictDelta,
		Sample.PredictedMagazineAmmo,
		MontageDelta,
		MuzzleDelta,
		SoundDelta,
		RecoilDelta,
		Sample.UnresolvedShotRecordCount,
		Sample.LastResolvedShotKey);
	ConcludeAmmoPredictionCase(TEXT("UnpredictedAccepted"), bConverged, Detail);
	StartAmmoPredictionStep(StepUnpredictedRejected);
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionUnpredictedRejectedStep()
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	if (!Weapon || !AdvanceAmmoPredictionSingleFireStep(StepUnpredictedRejected, /*bExpectAuthorityReject*/ true))
	{
		return;
	}

	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;
	const int32 ShotDelta = Weapon->GetAuthorityShotCountForAutomationTest() - AmmoPredictionAuthorityShotsBefore;
	const int32 AcceptedDelta = AmmoPredictionAuthorityActivations - AmmoPredictionAuthorityActivationsBefore;
	const int32 RejectedDelta = AmmoPredictionAuthorityRejects - AmmoPredictionAuthorityRejectsBefore;
	const int32 ProjectileDelta = ProjectileSpawnCount - AmmoPredictionProjectilesBefore;
	const int32 ClientActivationDelta = Sample.OwnerFireActivationCount - AmmoPredictionBefore.OwnerFireActivationCount;
	const int32 PredictedDelta = Sample.PredictedOwnerFeedbackCount - AmmoPredictionBefore.PredictedOwnerFeedbackCount;
	const int32 BackfillDelta = Sample.ConfirmedBackfillCount - AmmoPredictionBefore.ConfirmedBackfillCount;
	const int32 BackfillReqBefore = AmmoPredictionBefore.ConfirmedBackfillRequestCount;
	const int32 BackfillReqDelta = Sample.ConfirmedBackfillRequestCount - BackfillReqBefore;
	// 夹具把拥有端预算固定在弹匣容量上（预算 0）：被拒绝时没有任何预算可退还，它必须保持原值。
	const int32 InjectedPending = Weapon->GetMagazineSize();
	const int32 PendingDelta = Sample.PendingShots - InjectedPending;
	const int32 MontageDelta = Sample.MontageCount - AmmoPredictionBefore.MontageCount;
	const int32 MuzzleDelta = Sample.MuzzleCount - AmmoPredictionBefore.MuzzleCount;
	const int32 SoundDelta = Sample.SoundCount - AmmoPredictionBefore.SoundCount;
	const int32 RecoilDelta = Sample.RecoilCount - AmmoPredictionBefore.RecoilCount;

	// 预算为 0 且请求被服务器拒绝：拥有端既没有提前表现，也没有可退还的预算，
	// 因此预测表现与补播表现都必须为 0；Shot 记录仍必须被引擎 Reject 结清。
	const bool bConverged = ShotDelta == 0 && AcceptedDelta == 0 && RejectedDelta >= 1 && ProjectileDelta == 0 &&
		ClientActivationDelta == 1 && PredictedDelta == 0 && BackfillDelta == 0 && BackfillReqDelta == 0 &&
		PendingDelta == 0 && MontageDelta == 0 && MuzzleDelta == 0 && SoundDelta == 0 && RecoilDelta == 0 &&
		Sample.PredictedMagazineAmmo == 0 && Sample.UnresolvedShotRecordCount == 0 &&
		Sample.MagazineAmmo == AmmoPredictionMagazineBefore && !Sample.bLastResolvedShotCommitted &&
		Sample.bLastResolvedShotRejectedByEngine && Sample.LastResolvedShotKey == Sample.FirstPredictionKey;
	const FString Detail = FString::Printf(
		TEXT("Shots=%d Accepted=%d ServerRejects=%d Projectiles=%d ClientAct=%d Predicted=%d Backfill=%d ")
		TEXT("Pending=%d Channels(M=%d Z=%d S=%d R=%d) Records=%d LastKey=%d ByEngine=%d"),
		ShotDelta,
		AcceptedDelta,
		RejectedDelta,
		ProjectileDelta,
		ClientActivationDelta,
		PredictedDelta,
		BackfillDelta,
		PendingDelta,
		MontageDelta,
		MuzzleDelta,
		SoundDelta,
		RecoilDelta,
		Sample.UnresolvedShotRecordCount,
		Sample.LastResolvedShotKey,
		Sample.bLastResolvedShotRejectedByEngine ? 1 : 0);
	ConcludeAmmoPredictionCase(TEXT("UnpredictedRejected"), bConverged, Detail);
	StartAmmoPredictionStep(StepSemiAutoSetup);
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionSemiAutoSetupStep()
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterCharacter* Subject = AmmoPredictionSubject.Get();
	UShooterWeaponRuntimeSubsystem* Runtime = GetWorld()->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
	UShooterInventoryComponent* Inventory = Subject ? Subject->GetInventoryComponent() : nullptr;
	UShooterEquipmentComponent* Equipment = Subject ? Subject->GetEquipmentComponent() : nullptr;
	if (!Subject || !Runtime || !Inventory || !Equipment)
	{
		FailTest(TEXT("Ammo prediction semi-auto switch lost the subject or its equipment path"));
		bAmmoPredictionFinished = true;
		return;
	}

	if (!bAmmoPredictionStepCommandSent)
	{
		bAmmoPredictionStepCommandSent = true;

		// 与 Rifle 夹具同一条生产路径：先在背包里找，找不到再租用并入背包，最后装备。
		AShooterWeapon* SemiAutoWeapon = Inventory->FindWeaponByWeaponId(AmmoPredictionSemiAutoRowName);
		if (!SemiAutoWeapon)
		{
			SemiAutoWeapon = Runtime->AcquireWeapon(AmmoPredictionSemiAutoRowName, Subject, Subject);
			if (SemiAutoWeapon && Inventory->AddWeapon(SemiAutoWeapon) != EShooterInventoryAddResult::Added)
			{
				Runtime->ReleaseWeapon(SemiAutoWeapon);
				SemiAutoWeapon = nullptr;
			}
		}
		if (SemiAutoWeapon)
		{
			Equipment->EquipWeapon(SemiAutoWeapon);
		}

		if (!SemiAutoWeapon || Subject->GetCurrentWeaponActor() != SemiAutoWeapon ||
			SemiAutoWeapon->GetOwner() != Subject || SemiAutoWeapon->IsFullAuto() ||
			SemiAutoWeapon->GetWeaponId() != AmmoPredictionSemiAutoRowName)
		{
			FailTest(FString::Printf(
				TEXT("Ammo prediction could not equip the semi-auto weapon: Row=%s Weapon=%s FullAuto=%d Id=%s"),
				*AmmoPredictionSemiAutoRowName.ToString(),
				*GetNameSafe(SemiAutoWeapon),
				SemiAutoWeapon && SemiAutoWeapon->IsFullAuto() ? 1 : 0,
				SemiAutoWeapon ? *SemiAutoWeapon->GetWeaponId().ToString() : TEXT("None")));
			bAmmoPredictionFinished = true;
			return;
		}

		// 引脚切到新武器，并让"原始射速"跟随新武器行配置：后续步骤起点都会恢复到它。
		AmmoPredictionWeapon = SemiAutoWeapon;
		AmmoPredictionOriginalRefireRate = SemiAutoWeapon->GetRefireRate();
		AmmoPredictionStepRefireRate = AmmoPredictionOriginalRefireRate;
		AmmoPredictionSemiAutoHoldSeconds = FMath::Max(SemiAutoHoldMinSeconds,
			AmmoPredictionOriginalRefireRate * SemiAutoHoldCadenceFactor);

		// 弹药起点显式抬高：弹匣至少 FixtureMinMagazine 发、备弹至少 1 发，
		// 使"弹药不足"不可能伪装成半自动结论。
		const int32 MagazineStart = FMath::Max(SemiAutoWeapon->GetMagazineSize(), FixtureMinMagazine);
		const int32 ReserveStart = FMath::Max(MagazineStart, 1);
		if (!SetReloadTestAmmo(SemiAutoWeapon, MagazineStart, ReserveStart))
		{
			FailTest(TEXT("Ammo prediction could not seed the semi-auto weapon ammo"));
			bAmmoPredictionFinished = true;
			return;
		}

		// 换枪会复制新武器的行配置；这里再下发一次，让"两端同一 RefireRate"在换枪后仍然成立。
		ClientSetAmmoPredictionRefireRate(StepSemiAutoSetup, AmmoPredictionStepRefireRate);
		const FString SwitchLine = FString::Printf(
			TEXT("AMMO_PREDICTION_SEMI_AUTO_SWITCH Step=%d Row=%s Weapon=%s FullAuto=%d Size=%d RefireRate=%.3f ")
			TEXT("Mag=%d Reserve=%d Hold=%.2f"),
			StepSemiAutoSetup,
			*AmmoPredictionSemiAutoRowName.ToString(),
			*GetNameSafe(SemiAutoWeapon),
			SemiAutoWeapon->IsFullAuto() ? 1 : 0,
			SemiAutoWeapon->GetMagazineSize(),
			AmmoPredictionOriginalRefireRate,
			SemiAutoWeapon->GetBulletCount(),
			SemiAutoWeapon->GetReserveAmmo(),
			AmmoPredictionSemiAutoHoldSeconds);
		UE_LOG(LogShootGame, Display, TEXT("%s"), *SwitchLine);
		return;
	}

	// 等拥有端真的观察到「当前武器 == 挑出的行」并且它的配置是非全自动。
	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;
	if (!IsAmmoPredictionClientSampleFresh(StepSemiAutoSetup) || !Sample.bValid ||
		Sample.CurrentWeaponId != AmmoPredictionSemiAutoRowName || Sample.bCurrentWeaponIsFullAuto)
	{
		return;
	}

	const FString ReadyLine = FString::Printf(
		TEXT("AMMO_PREDICTION_SEMI_AUTO_READY Row=%s OwnerWeapon=%s OwnerFullAuto=%d OwnerMag=%d OwnerReserve=%d"),
		*Sample.CurrentWeaponId.ToString(),
		*GetNameSafe(Sample.Weapon),
		Sample.bCurrentWeaponIsFullAuto ? 1 : 0,
		Sample.MagazineAmmo,
		Sample.ReserveAmmo);
	UE_LOG(LogShootGame, Display, TEXT("%s"), *ReadyLine);
	StartAmmoPredictionStep(StepSemiAutoWeaponSingleShot);
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionSemiAutoWeaponSingleShotStep()
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	if (!Weapon ||
		!AdvanceAmmoPredictionSingleFireStep(StepSemiAutoWeaponSingleShot, /*bExpectAuthorityReject*/ false))
	{
		return;
	}

	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;
	const int32 ShotDelta = Weapon->GetAuthorityShotCountForAutomationTest() - AmmoPredictionAuthorityShotsBefore;
	const int32 AcceptedDelta = AmmoPredictionAuthorityActivations - AmmoPredictionAuthorityActivationsBefore;
	const int32 RejectDelta = AmmoPredictionAuthorityRejects - AmmoPredictionAuthorityRejectsBefore;
	const int32 ClientActivationDelta = Sample.OwnerFireActivationCount - AmmoPredictionBefore.OwnerFireActivationCount;
	const int32 PredictedDelta = Sample.PredictedOwnerFeedbackCount - AmmoPredictionBefore.PredictedOwnerFeedbackCount;
	const int32 BackfillDelta = Sample.ConfirmedBackfillCount - AmmoPredictionBefore.ConfirmedBackfillCount;
	const int32 PendingDelta = Sample.PendingShots - AmmoPredictionBefore.PendingShots;
	const int32 VerdictDelta = Sample.ShotVerdictCount - AmmoPredictionBefore.ShotVerdictCount;
	const int32 MagazineDelta = Weapon->GetBulletCount() - AmmoPredictionMagazineBefore;

	// 拥有端自述的武器身份：证据必须显示它跑在挑出的半自动行上，不能被读成全自动结果。
	const bool bOwnerFullAuto = Sample.bCurrentWeaponIsFullAuto;
	const bool bSemiAutoObserved = !bOwnerFullAuto && Sample.CurrentWeaponId == AmmoPredictionSemiAutoRowName;
	const bool bConverged = bSemiAutoObserved && ShotDelta == 1 && AcceptedDelta == 1 && RejectDelta == 0 &&
		ClientActivationDelta == 1 && Sample.DistinctPredictionKeyCount == 1 && VerdictDelta == 1 &&
		PredictedDelta == 1 && BackfillDelta == 0 && PendingDelta == 0 && MagazineDelta == -1 &&
		Sample.UnresolvedShotRecordCount == 0 && !Sample.bFireActive && !Sample.bReloadActive &&
		Sample.LastResolvedShotKey == Sample.FirstPredictionKey && Sample.bLastResolvedShotCommitted;
	const FString Detail = FString::Printf(
		TEXT("Row=%s OwnerFullAuto=%d Shots=%d Accepted=%d Rejects=%d ClientAct=%d Keys=%d Verdicts=%d ")
		TEXT("Predicted=%d Backfill=%d Pending=%d MagDelta=%d Records=%d LastKey=%d Committed=%d"),
		*Sample.CurrentWeaponId.ToString(),
		bOwnerFullAuto ? 1 : 0,
		ShotDelta,
		AcceptedDelta,
		RejectDelta,
		ClientActivationDelta,
		Sample.DistinctPredictionKeyCount,
		VerdictDelta,
		PredictedDelta,
		BackfillDelta,
		PendingDelta,
		MagazineDelta,
		Sample.UnresolvedShotRecordCount,
		Sample.LastResolvedShotKey,
		Sample.bLastResolvedShotCommitted ? 1 : 0);
	ConcludeAmmoPredictionCase(TEXT("SemiAutoWeaponSingleShot"), bConverged, Detail);
	StartAmmoPredictionStep(StepSemiAutoHold);
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionSemiAutoHoldStep()
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	if (!Weapon)
	{
		return;
	}

	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;
	if (!bAmmoPredictionStepCommandSent)
	{
		if (!IsAmmoPredictionFixtureReady(StepSemiAutoHold, 0))
		{
			return;
		}
		bAmmoPredictionStepCommandSent = true;
		// 半自动按住：OnInputTriggered 下"按住"不产生第二次动作边界，
		// 因此整段窗口必须恰好一发，这就是半自动专属的不变量。
		ClientSubmitAmmoPredictionHoldFire(StepSemiAutoHold, AmmoPredictionSemiAutoHoldSeconds, 0.0f, -1.0f);
		return;
	}

	const int32 ShotDelta = Weapon->GetAuthorityShotCountForAutomationTest() - AmmoPredictionAuthorityShotsBefore;
	if (!bAmmoPredictionSettleStarted)
	{
		// 必须先看到窗口真的结束（松手且跨过一个完整节拍），
		// 否则"没有连发"只说明第二次还没轮到，而不是半自动语义成立。
		if (ShotDelta < 1 || !Sample.bReleaseSettled)
		{
			return;
		}
		AmmoPredictionSettleStartTime = GetWorld()->GetTimeSeconds();
		bAmmoPredictionSettleStarted = true;
		return;
	}
	if (!IsAmmoPredictionClientSampleFresh(StepSemiAutoHold) ||
		GetWorld()->GetTimeSeconds() - AmmoPredictionSettleStartTime < SettleSeconds)
	{
		return;
	}

	const int32 AcceptedDelta = AmmoPredictionAuthorityActivations - AmmoPredictionAuthorityActivationsBefore;
	const int32 RejectDelta = AmmoPredictionAuthorityRejects - AmmoPredictionAuthorityRejectsBefore;
	const int32 ClientActivationDelta = Sample.OwnerFireActivationCount - AmmoPredictionBefore.OwnerFireActivationCount;
	const int32 PredictedDelta = Sample.PredictedOwnerFeedbackCount - AmmoPredictionBefore.PredictedOwnerFeedbackCount;
	const int32 BackfillDelta = Sample.ConfirmedBackfillCount - AmmoPredictionBefore.ConfirmedBackfillCount;
	const int32 PendingDelta = Sample.PendingShots - AmmoPredictionBefore.PendingShots;
	const int32 VerdictDelta = Sample.ShotVerdictCount - AmmoPredictionBefore.ShotVerdictCount;
	const int32 MagazineDelta = Weapon->GetBulletCount() - AmmoPredictionMagazineBefore;

	// 半自动专属不变量：按住扳机绝不连发——整段窗口恰好一发、零拒绝、预算回到起点。
	const bool bOwnerFullAuto = Sample.bCurrentWeaponIsFullAuto;
	const bool bSemiAutoObserved = !bOwnerFullAuto && Sample.CurrentWeaponId == AmmoPredictionSemiAutoRowName;
	const bool bConverged = bSemiAutoObserved && ShotDelta == 1 && AcceptedDelta == 1 && RejectDelta == 0 &&
		ClientActivationDelta == 1 && Sample.DistinctPredictionKeyCount == 1 && VerdictDelta == 1 &&
		PredictedDelta == 1 && BackfillDelta == 0 && PendingDelta == 0 && MagazineDelta == -1 &&
		Sample.bReleaseSettled && Sample.UnresolvedShotRecordCount == 0 && !Sample.bFireActive &&
		Sample.LastResolvedShotKey == Sample.FirstPredictionKey && Sample.bLastResolvedShotCommitted;
	const FString Detail = FString::Printf(
		TEXT("Row=%s OwnerFullAuto=%d HoldSeconds=%.2f RefireRate=%.3f Shots=%d Accepted=%d Rejects=%d ")
		TEXT("ClientAct=%d Keys=%d Verdicts=%d Predicted=%d Backfill=%d Pending=%d MagDelta=%d Released=%d ")
		TEXT("Records=%d LastKey=%d Committed=%d"),
		*Sample.CurrentWeaponId.ToString(),
		bOwnerFullAuto ? 1 : 0,
		AmmoPredictionSemiAutoHoldSeconds,
		Weapon->GetRefireRate(),
		ShotDelta,
		AcceptedDelta,
		RejectDelta,
		ClientActivationDelta,
		Sample.DistinctPredictionKeyCount,
		VerdictDelta,
		PredictedDelta,
		BackfillDelta,
		PendingDelta,
		MagazineDelta,
		Sample.bReleaseSettled ? 1 : 0,
		Sample.UnresolvedShotRecordCount,
		Sample.LastResolvedShotKey,
		Sample.bLastResolvedShotCommitted ? 1 : 0);
	ConcludeAmmoPredictionCase(TEXT("SemiAutoHold"), bConverged, Detail);
	StartAmmoPredictionStep(StepDone);
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionServerPhase()
{
	using namespace ShooterAmmoPredictionNetworkTests;
	if (bAmmoPredictionFinished || !HasAuthority())
	{
		return;
	}

	// 只由「归属远端被试客户端」的服务器端 Coordinator 驱动：Listen Host 自身只提供观察。
	APlayerController* Driver = nullptr;
	int32 PlayerCount = 0;
	int32 LocalCount = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		if (!PlayerController || !PlayerController->PlayerState)
		{
			continue;
		}
		++PlayerCount;
		LocalCount += PlayerController->IsLocalController() ? 1 : 0;
		if (!PlayerController->IsLocalController() &&
			(!Driver || PlayerController->PlayerState->GetPlayerId() < Driver->PlayerState->GetPlayerId()))
		{
			Driver = PlayerController;
		}
	}

	if (GetOwner() != Driver)
	{
		const APlayerController* OwnerPlayerController = Cast<APlayerController>(GetOwner());
		if (bAmmoPredictionSetup || (!Driver && OwnerPlayerController && OwnerPlayerController->IsLocalController() &&
			GetWorld()->GetTimeSeconds() - TestStartTime > StepTimeoutSeconds))
		{
			FailTest(TEXT("Ammo prediction lost or never received its selected remote driver"));
			bAmmoPredictionFinished = true;
		}
		return;
	}

	const int32 ExpectedLocal = GetNetMode() == NM_DedicatedServer ? 0 : 1;
	if (!bAmmoPredictionSetup)
	{
		if (GetWorld()->GetTimeSeconds() - TestStartTime > StepTimeoutSeconds)
		{
			FailTest(TEXT("Ammo prediction two-player Coordinator setup timed out"));
			bAmmoPredictionFinished = true;
			return;
		}
		if (PlayerCount < 2 || LocalCount != ExpectedLocal || !GetShooterCharacter())
		{
			return;
		}

		AShooterCharacter* Subject = Cast<AShooterCharacter>(Driver->GetPawn());
		if (!Subject)
		{
			return;
		}

		for (TActorIterator<AShooterNPC> It(GetWorld()); It; ++It)
		{
			It->StopShooting();
			if (AController* NpcController = It->GetController())
			{
				NpcController->Destroy();
			}
		}

		UShooterWeaponRuntimeSubsystem* Runtime = GetWorld()->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
		UShooterInventoryComponent* Inventory = Subject->GetInventoryComponent();
		UShooterEquipmentComponent* Equipment = Subject->GetEquipmentComponent();
		AShooterWeapon* Weapon = Subject->GetCurrentWeaponActor();
		if (Runtime && Inventory && Equipment && (!Weapon || Weapon->GetWeaponId() != FName(TEXT("Rifle"))))
		{
			Weapon = Inventory->FindWeaponByWeaponId(TEXT("Rifle"));
			if (!Weapon)
			{
				Weapon = Runtime->AcquireWeapon(TEXT("Rifle"), Subject, Subject);
				if (Weapon && Inventory->AddWeapon(Weapon) != EShooterInventoryAddResult::Added)
				{
					Runtime->ReleaseWeapon(Weapon);
					Weapon = nullptr;
				}
			}
			if (Weapon)
			{
				Equipment->EquipWeapon(Weapon);
			}
		}

		if (!Weapon || !Inventory || !Inventory->ContainsWeapon(Weapon) ||
			Subject->GetCurrentWeaponActor() != Weapon || !Subject->GetAbilitySystemComponent())
		{
			FailTest(TEXT("Ammo prediction could not equip the production Rifle"));
			bAmmoPredictionFinished = true;
			return;
		}

		// 夹具前提显式声明：连发窗口必须能容纳至少 3 个完整节拍，并且不能被单个窗口打空弹匣。
		const float RefireRate = Weapon->GetRefireRate();
		const float HoldSeconds = FMath::Max(FullAutoHoldSeconds, RefireRate * FullAutoHoldCadenceFactor);
		const float RefireGuard = FMath::Max(RefireRate, 0.01f);
		const int32 MaxPacedShots = FullAutoPaceSlackShots + FMath::CeilToInt(HoldSeconds / RefireGuard);
		if (!Weapon->IsFullAuto() || RefireRate <= 0.0f || Weapon->GetMagazineSize() < FullAutoMinShots + 3)
		{
			FailTest(FString::Printf(
				TEXT("Ammo prediction requires a full-auto weapon with MagazineSize >= %d: ")
				TEXT("Weapon=%s FullAuto=%d RefireRate=%.3f Size=%d"), FullAutoMinShots + 3,
				*GetNameSafe(Weapon), Weapon->IsFullAuto() ? 1 : 0, RefireRate, Weapon->GetMagazineSize()));
			bAmmoPredictionFinished = true;
			return;
		}
		if (Weapon->GetReloadDuration() < ReloadDurationMinSeconds)
		{
			FailTest(FString::Printf(TEXT("Ammo prediction requires ReloadDuration >= %.2f: Weapon=%s %.3f"),
				ReloadDurationMinSeconds, *GetNameSafe(Weapon), Weapon->GetReloadDuration()));
			bAmmoPredictionFinished = true;
			return;
		}

		// Reload 用例的 Hold 必须在换弹完成前结束：松手早于换弹结束，
		// 才能断言"换弹阻塞期间没有开火"以及"结束时弹匣已经补满"。
		const float ReloadExtra = FMath::Clamp(Weapon->GetReloadDuration() * ReloadHoldRatio,
			ReloadHoldMinExtraSeconds, ReloadHoldMaxExtraSeconds);
		const float ReloadHoldSeconds = ReloadTriggerSeconds + ReloadExtra;

		// 半自动用例的被试武器行：从正式武器表动态挑出，不硬编码行名。
		// 没有半自动行时直接失败，不允许静默跳过半自动覆盖。
		AmmoPredictionSemiAutoRowName = PickSemiAutoWeaponRowName();
		if (AmmoPredictionSemiAutoRowName.IsNone())
		{
			FailTest(TEXT("Ammo prediction requires at least one production semi-auto weapon row"));
			bAmmoPredictionFinished = true;
			return;
		}
		const FString SemiAutoRowName = AmmoPredictionSemiAutoRowName.ToString();
		UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_SEMI_AUTO_PICK Row=%s"), *SemiAutoRowName);

		// 武器行配置的原始射速：射速用例结束后每个步骤起点都会把两端恢复到它。
		AmmoPredictionOriginalRefireRate = Weapon->GetRefireRate();
		AmmoPredictionSubject = Subject;
		AmmoPredictionWeapon = Weapon;
		AmmoPredictionSubjectPlayerId = Driver->PlayerState->GetPlayerId();
		AmmoPredictionFullAutoHoldSeconds = HoldSeconds;
		AmmoPredictionMaxPacedShots = MaxPacedShots;
		AmmoPredictionReloadHoldSeconds = ReloadHoldSeconds;
		UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_RELOAD_WINDOW Hold=%.2f Trigger=%.2f"),
			ReloadHoldSeconds, ReloadTriggerSeconds);
		BindAmmoPredictionServerObserver(Subject->GetAbilitySystemComponent());
		bAmmoPredictionSetup = true;
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("AMMO_PREDICTION_SETUP Weapon=%s FullAuto=%d Size=%d RefireRate=%.3f Hold=%.2f MaxPaced=%d"),
			*GetNameSafe(Weapon),
			Weapon->IsFullAuto() ? 1 : 0,
			Weapon->GetMagazineSize(),
			RefireRate,
			HoldSeconds,
			MaxPacedShots);
		StartAmmoPredictionStep(StepSetup);
		return;
	}

	AShooterCharacter* Subject = AmmoPredictionSubject.Get();
	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	if (!Subject || !Weapon || Subject->GetCurrentWeaponActor() != Weapon || Weapon->GetOwner() != Subject)
	{
		FailTest(TEXT("Ammo prediction lost the pinned Avatar or Weapon"));
		bAmmoPredictionFinished = true;
		return;
	}

	if (GetWorld()->GetTimeSeconds() - AmmoPredictionStepStartTime > StepTimeoutSeconds)
	{
		FailTest(FString::Printf(TEXT("Ammo prediction step timed out: Step=%d Mag=%d Reserve=%d Rejects=%d "),
			AmmoPredictionServerStep, Weapon->GetBulletCount(), Weapon->GetReserveAmmo(),
			AmmoPredictionAuthorityRejects));
		ClearAmmoPredictionServerTag();
		bAmmoPredictionFinished = true;
		return;
	}

	// 每个服务器 poll（ShooterNetworkTest::PollIntervalSeconds = 0.1s）采样一次连接瞬时速率：
	// 速率字段每个 StatPeriod 才刷新，单次读取不能代表窗口，必须周期采样后取平均 / 峰值。
	if (bAmmoPredictionSetup && AmmoPredictionNetWindowStartTime >= 0.0f)
	{
		const UNetDriver* SamplingDriver = GetWorld()->GetNetDriver();
		UNetConnection* SamplingConnection = ResolveAmmoPredictionDriverConnection(
			Cast<APlayerController>(GetOwner()), SamplingDriver);
		AccumulateNetRates(SamplingConnection, GetWorld()->GetTimeSeconds(), AmmoPredictionNetRateWindow);
	}

	switch (AmmoPredictionServerStep)
	{
	case StepSetup:
		if (IsAmmoPredictionClientSampleFresh(StepSetup))
		{
			const FShooterAmmoPredictionObservation& Ready = AmmoPredictionLatest;
			if (Ready.bValid && Ready.PendingShots == 0 && Ready.UnresolvedShotRecordCount == 0 &&
				!Ready.bFireActive && !Ready.bReloadActive && !Ready.bReloading)
			{
				StartAmmoPredictionStep(StepSemiAutoSingleShot);
			}
		}
		return;
	case StepSemiAutoSingleShot:
		RunAmmoPredictionSemiAutoSingleShotStep();
		return;
	case StepFullAutoHold:
		RunAmmoPredictionFullAutoHoldStep();
		return;
	case StepPredictedAccepted:
		RunAmmoPredictionPredictedAcceptedStep();
		return;
	case StepPredictedRejected:
		RunAmmoPredictionPredictedRejectedStep();
		return;
	case StepFullAutoRelease:
		RunAmmoPredictionFullAutoReleaseStep();
		return;
	case StepReloadLifecycle:
		RunAmmoPredictionReloadLifecycleStep();
		return;
	case StepRate600Rpm:
	case StepRate900Rpm:
		RunAmmoPredictionRateStep(AmmoPredictionServerStep);
		return;
	case StepUnpredictedAccepted:
		RunAmmoPredictionUnpredictedAcceptedStep();
		return;
	case StepUnpredictedRejected:
		RunAmmoPredictionUnpredictedRejectedStep();
		return;
	case StepSemiAutoSetup:
		RunAmmoPredictionSemiAutoSetupStep();
		return;
	case StepSemiAutoWeaponSingleShot:
		RunAmmoPredictionSemiAutoWeaponSingleShotStep();
		return;
	case StepSemiAutoHold:
		RunAmmoPredictionSemiAutoHoldStep();
		return;
	case StepDone:
		if (!bAmmoPredictionStepCommandSent)
		{
			bAmmoPredictionStepCommandSent = true;
			for (TActorIterator<AShooterNetworkTestCoordinator> It(GetWorld()); It; ++It)
			{
				if (It->GetOwner() != Driver)
				{
					It->ClientVerifyAmmoPredictionObserver(AmmoPredictionSubjectPlayerId);
				}
			}
			return;
		}
		for (TActorIterator<AShooterNetworkTestCoordinator> It(GetWorld()); It; ++It)
		{
			if (It->GetOwner() != Driver && !It->bAmmoPredictionObserverVerified)
			{
				return;
			}
		}
		ClearAmmoPredictionServerTag();
		if (AmmoPredictionConvergedCount != CaseCount || AmmoPredictionMismatchCount != 0)
		{
			FailTest(TEXT("Ammo prediction cases did not all converge; DONE is not a success marker"));
		}
		UE_LOG(LogShootGame, Display, TEXT("AUTOMATION_TEST_AMMO_PREDICTION_DONE Cases=%d Converged=%d Mismatches=%d"),
			CaseCount, AmmoPredictionConvergedCount, AmmoPredictionMismatchCount);
		for (TActorIterator<AShooterNetworkTestCoordinator> It(GetWorld()); It; ++It)
		{
			It->bAmmoPredictionFinished = true;
		}
		return;
	default:
		return;
	}
}

void AShooterNetworkTestCoordinator::CleanupAmmoPredictionTest()
{
	ClearAmmoPredictionServerTag();
	if (UAbilitySystemComponent* AbilitySystemComponent = AmmoPredictionObservedASC.Get())
	{
		AbilitySystemComponent->AbilityActivatedCallbacks.Remove(AmmoPredictionActivatedHandle);
		AbilitySystemComponent->AbilityEndedCallbacks.Remove(AmmoPredictionEndedHandle);
		AbilitySystemComponent->AbilityFailedCallbacks.Remove(AmmoPredictionFailedHandle);
	}
	AmmoPredictionActivatedHandle.Reset();
	AmmoPredictionEndedHandle.Reset();
	AmmoPredictionFailedHandle.Reset();
	AmmoPredictionObservedASC.Reset();
}

void AShooterNetworkTestCoordinator::ClientVerifyAmmoPredictionObserver_Implementation(int32 SubjectPlayerId)
{
	AShooterWeapon* ObservedWeapon = nullptr;
	for (TActorIterator<AShooterCharacter> It(GetWorld()); It; ++It)
	{
		if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId() == SubjectPlayerId)
		{
			ObservedWeapon = It->GetCurrentWeaponActor();
			break;
		}
	}
	const APawn* ObservedPawn = ObservedWeapon ? Cast<APawn>(ObservedWeapon->GetOwner()) : nullptr;
	const bool bValid = IsValid(ObservedWeapon) && ObservedPawn && !ObservedPawn->IsLocallyControlled();
	// 非拥有端不得收到任何拥有者通道：预测表现、确认补播与单发裁决通知都必须为 0。
	ServerReportAmmoPredictionObserver(bValid,
		ObservedWeapon ? ObservedWeapon->GetPredictedOwnerFeedbackCountForAutomationTest() : INDEX_NONE,
		ObservedWeapon ? ObservedWeapon->GetConfirmedBackfillCountForAutomationTest() : INDEX_NONE,
		ObservedWeapon ? ObservedWeapon->GetShotVerdictReceivedCountForTest() : INDEX_NONE);
}

void AShooterNetworkTestCoordinator::ServerReportAmmoPredictionObserver_Implementation(bool bValid,
	int32 PredictedCount, int32 BackfillCount, int32 VerdictCount)
{
	if (!bValid || PredictedCount != 0 || BackfillCount != 0 || VerdictCount != 0)
	{
		FailTest(FString::Printf(
			TEXT("Ammo prediction observer received owner-only feedback: Valid=%d Predicted=%d Backfill=%d ")
			TEXT("Verdicts=%d"), bValid ? 1 : 0, PredictedCount, BackfillCount, VerdictCount));
		return;
	}
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_OBSERVER_SUCCESS Predicted=0 Backfill=0 Verdicts=0"));
	bAmmoPredictionObserverVerified = true;
}

#else

void AShooterNetworkTestCoordinator::ClientPrepareAmmoPredictionStep_Implementation(int32 SubjectPlayerId, int32 Step)
{
}

void AShooterNetworkTestCoordinator::ClientSetAmmoPredictionRefireRate_Implementation(int32 Step, float RefireRate)
{
}

void AShooterNetworkTestCoordinator::ClientSubmitAmmoPredictionFire_Implementation(int32 Step)
{
}

void AShooterNetworkTestCoordinator::ClientSubmitAmmoPredictionHoldFire_Implementation(int32 Step, float HoldSeconds,
	float LocalCooldownSeconds, float ReloadAtSeconds)
{
}

void AShooterNetworkTestCoordinator::ClientVerifyAmmoPredictionObserver_Implementation(int32 SubjectPlayerId)
{
}

void AShooterNetworkTestCoordinator::ServerReportAmmoPredictionObserver_Implementation(bool bValid,
	int32 PredictedCount, int32 BackfillCount, int32 VerdictCount)
{
}

void AShooterNetworkTestCoordinator::ServerReportAmmoPredictionSample_Implementation(const FShooterAmmoPredictionObservation& Observation)
{
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionServerPhase()
{
}

void AShooterNetworkTestCoordinator::SampleAmmoPredictionLocalState()
{
}

void AShooterNetworkTestCoordinator::CleanupAmmoPredictionTest()
{
}

#endif
