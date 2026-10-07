// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/Network/ShooterNetworkTestCoordinator.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/Network/ShooterNetworkObservationTypes.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/Abilities/ShooterGameplayAbility_Fire.h"
#include "AbilitySystem/ShooterAbilitySystemComponent.h"
#include "AbilitySystem/ShooterGameplayTags.h"
#include "Characters/Equipment/ShooterEquipmentComponent.h"
#include "Characters/ShooterCharacter.h"
#include "Engine/NetDriver.h"
#include "Engine/Engine.h"
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
	 * 帧率 × 射速矩阵（-ShootGameFireCadenceMatrix）：把"射速是否达成"拆成
	 * "本地请求节拍被帧边界推迟了多少"与"服务器是否额外拒绝了请求"两件事。
	 *
	 * 每个组合的窗口不得短于 10 秒：短窗口下每秒一次的启动延迟与每发数十毫秒的
	 * 节拍量化误差会混在一起，无法区分相位漂移与噪声。
	 * 该模式下夹具只断言结构性不变量（接受数 == 权威发数、Pending 归零、记录归零），
	 * 达成射速只作为数据上报：本轮目标是定位根因，不是把数字调漂亮。
	 */
	constexpr float CadenceMatrixMeasurementSeconds = 10.0f;
	constexpr int32 CadenceMatrixRowCount = 10;
	constexpr int32 StepCadenceMatrixFirst = 100;
	constexpr int32 StepCadenceMatrixLast = StepCadenceMatrixFirst + CadenceMatrixRowCount - 1;

	/** 矩阵一行：客户端帧率上限与武器理论射速。 */
	struct FShooterCadenceMatrixRow
	{
		float MaxFPS = 0.0f;
		float RefireRate = 0.0f;
		float ExpectedRps = 0.0f;
	};

	/** 矩阵第 Index 行；顺序固定为「每个帧率下先 600 RPM 后 900 RPM」。 */
	FShooterCadenceMatrixRow GetCadenceMatrixRow(int32 Index)
	{
		const float FpsValues[5] = { 30.0f, 45.0f, 60.0f, 90.0f, 120.0f };
		const bool bHighRpm = (Index % 2) != 0;
		FShooterCadenceMatrixRow Row;
		Row.MaxFPS = FpsValues[(Index / 2) % 5];
		Row.ExpectedRps = bHighRpm ? Rate900RpmExpectedRps : Rate600RpmExpectedRps;
		Row.RefireRate = 1.0f / Row.ExpectedRps;
		return Row;
	}

	bool IsCadenceMatrixStep(int32 Step)
	{
		return Step >= StepCadenceMatrixFirst && Step <= StepCadenceMatrixLast;
	}

	int32 GetCadenceMatrixIndex(int32 Step)
	{
		return Step - StepCadenceMatrixFirst;
	}

	/** 秒值转毫秒；负值表示"该项无数据"，保持 -1 不被放大。 */
	float SecondsToMillisecondsOrUnknown(float Seconds)
	{
		return Seconds < 0.0f ? -1.0f : Seconds * 1000.0f;
	}

	/**
	 * 事件射速的统一口径：N 次事件之间只有 N-1 个间隔，因此射速 = (N - 1) / (末 - 首)。
	 *
	 * 计数与时间跨度必须来自同一个域：把拥有端计数配服务器时间跨度，
	 * 或直接用 N / 跨度，都会同时引入 N 与 N-1 的偏差和跨域误差。
	 * 样本不足两次或时间跨度非正时返回 -1，并要求调用方显式声明"样本不足"，
	 * 不构造任何看起来合理的假射速。
	 */
	float ComputeEventRps(int32 SampleCount, float FirstTime, float LastTime)
	{
		if (SampleCount < 2 || FirstTime < 0.0f || LastTime <= FirstTime)
		{
			return -1.0f;
		}
		return static_cast<float>(SampleCount - 1) / (LastTime - FirstTime);
	}

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
		return Step == StepRate600Rpm || Step == StepRate900Rpm || IsCadenceMatrixStep(Step);
	}

	/** 射速测量窗口：矩阵模式必须长到能形成稳定统计。 */
	float GetRateMeasurementSeconds(int32 Step)
	{
		return IsCadenceMatrixStep(Step) ? CadenceMatrixMeasurementSeconds : RateMeasurementSeconds;
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
		if (Step == StepRate600Rpm)
		{
			return Rate600RpmExpectedRps;
		}
		if (Step == StepRate900Rpm)
		{
			return Rate900RpmExpectedRps;
		}
		return IsCadenceMatrixStep(Step) ? GetCadenceMatrixRow(GetCadenceMatrixIndex(Step)).ExpectedRps : 0.0f;
	}

	/** 射速测量用例在两端必须一致的 RefireRate。 */
	float GetRateRefireRate(int32 Step)
	{
		if (Step == StepRate600Rpm)
		{
			return Rate600RpmRefireRate;
		}
		if (Step == StepRate900Rpm)
		{
			return Rate900RpmRefireRate;
		}
		return IsCadenceMatrixStep(Step) ? GetCadenceMatrixRow(GetCadenceMatrixIndex(Step)).RefireRate : 0.0f;
	}

	/** 矩阵行的客户端帧率上限；非矩阵步骤返回 0（不限制帧率）。 */
	float GetCadenceMatrixMaxFPS(int32 Step)
	{
		return IsCadenceMatrixStep(Step) ? GetCadenceMatrixRow(GetCadenceMatrixIndex(Step)).MaxFPS : 0.0f;
	}

	/** 矩阵用例名：帧率与射速都写进名字，报告行不依赖步骤号即可自解释。 */
	FString GetCadenceMatrixCaseName(int32 Step)
	{
		const FShooterCadenceMatrixRow Row = GetCadenceMatrixRow(GetCadenceMatrixIndex(Step));
		return FString::Printf(TEXT("Cadence%.0fFps_%.0fRpm"), Row.MaxFPS, Row.ExpectedRps * 60.0f);
	}

	/**
	 * Weapon Context 定向模式（-ShootGameWeaponContextTest）。
	 *
	 * 复现路径（全部走生产代码，不新增任何生产 RPC）：
	 * 1. 拥有端用武器 A 按住开火，服务器权威武器同样是 A；
	 * 2. 拥有端按生产输入路径请求切到下一把武器 B，GA_Equip 是 ServerOnly：
	 *    服务器在 EquipDuration 之后把 CurrentWeaponActor 改成 B；
	 * 3. 拥有端的 CurrentWeaponActor 只由复制改写，因此从"服务器已换 B"到
	 *    "拥有端收到复制"之间存在一个完整往返窗口，窗口内拥有端发出的每一次请求都绑定 A。
	 *
	 * 目标不变量：一次 Fire Action 请求必须对应一个明确的 Weapon Context。
	 * 服务器只能「仍然由 A 提交」或「拒绝」，绝不能把 A 的请求解释成 B 的 Shot。
	 *
	 * 判定分成两半，缺一不可：
	 * - 权威端：每个被接受的 PredictionKey 用的武器，必须等于拥有端该键所用的武器；
	 * - 拥有端：没有跨武器裁决，且上下文武器的预算 / 记录 / HUD 在窗口结束后收敛。
	 */
	constexpr int32 StepWeaponContextSwitch = 200;
	constexpr int32 StepSpecRemovalInFlight = 201;
	constexpr int32 StepPoolReuseRebind = 202;
	constexpr int32 StepWeaponContextCount = 3;

	/**
	 * Spec Removal 用例（StepSpecRemovalInFlight）：在途请求 + Spec 撤销的确定性窗口。
	 *
	 * 顺序：拥有端发一发真实请求（本地已有 Pending / HUD 预测）→ 服务器在请求到达之前
	 * 走真实 Inventory Remove 路径移除这把武器（Spec 撤销 + 武器回池）→ 请求最终落在
	 * 已失效 Handle / 旧生命周期上。此时旧实例可能已经被 GAS 销毁，因此正确性只能来自
	 * "Spec 生命周期结束时主动清算自己的预测债务"。
	 *
	 * 移除时机必须早于请求到达服务器：单程延迟 d = PktLag，客户端发请求前还要先收到指令，
	 * 因此 T0 + RemovalDelay 远早于请求到达（T0 + 2d 量级）。
	 */
	constexpr float SpecRemovalInFlightDelaySeconds = 0.15f;
	/** 移除后的结算窗口：覆盖复制、迟到 Reject 与拥有端样本回流。 */
	constexpr float SpecRemovalSettleSeconds = 2.0f;

	/**
	 * 本用例的请求节拍：远快于武器行配置，用来把"几十到两百毫秒的错位窗口"放大成
	 * 至少一次真实请求。窗口本身由网络往返决定，夹具只能提高落进窗口的概率。
	 */
	constexpr float WeaponContextRefireRate = 0.05f;
	constexpr float WeaponContextExpectedRps = 20.0f;

	/** 按住窗口：切枪请求在 hold 开始后的 Lead 秒下发，提交后还要留 Tail 秒继续按住。 */
	constexpr float WeaponContextHoldLeadSeconds = 0.5f;
	constexpr float WeaponContextHoldTailSeconds = 1.2f;
	constexpr float WeaponContextHoldMinSeconds = 2.0f;
	/** 本用例允许的切枪事务时长预算；超出说明武器行配置变了，夹具必须显式失败而不是静默缩短窗口。 */
	constexpr float WeaponContextEquipDurationBudgetSeconds = 1.0f;
	/** 拥有端松手后的结算时间：最后一发的裁决必须先回到拥有端。 */
	constexpr float WeaponContextSettleSeconds = 2.0f;
	/** 夹具前提：按住窗口至少要产生这么多次拥有端请求，否则窗口本身没有观测价值。 */
	constexpr int32 WeaponContextMinRequests = 4;

	bool IsWeaponContextStep(int32 Step)
	{
		return Step == StepWeaponContextSwitch || Step == StepSpecRemovalInFlight || Step == StepPoolReuseRebind;
	}

	/**
	 * 本用例的切枪目标行：优先用正式半自动行（它与被钉住的全自动上下文武器必然不同行），
	 * 该行缺失或与上下文武器同行时回落到"武器表里第一个不同的行"；
	 * 两者都拿不到时返回 NAME_None，由调用方显式失败，不静默退化。
	 */
	FName PickWeaponContextSwitchRowName(const AShooterWeapon* ContextWeapon)
	{
		const FName ContextRow = ContextWeapon ? ContextWeapon->GetWeaponId() : NAME_None;
		const FName SemiAutoRow = PickSemiAutoWeaponRowName();
		if (!SemiAutoRow.IsNone() && SemiAutoRow != ContextRow)
		{
			return SemiAutoRow;
		}

		const UDataTable* WeaponTable = ShooterWeaponTable::ResolveWeaponTable();
		if (!WeaponTable)
		{
			return NAME_None;
		}

		TArray<FName> RowNames = WeaponTable->GetRowNames();
		RowNames.Sort([](const FName& Left, const FName& Right) { return Left.LexicalLess(Right); });
		for (const FName& RowName : RowNames)
		{
			if (RowName != ContextRow)
			{
				return RowName;
			}
		}

		return NAME_None;
	}

	/** 本用例的按住窗口：必须完整覆盖「切枪事务 + 双向往返」，否则窗口里根本不会有请求。 */
	float ResolveWeaponContextHoldSeconds()
	{
		return FMath::Max(WeaponContextHoldMinSeconds,
			WeaponContextHoldLeadSeconds + WeaponContextEquipDurationBudgetSeconds + WeaponContextHoldTailSeconds);
	}

	/** 步骤射速覆盖值；<= 0 表示"使用武器行配置的原始射速"。 */
	float ResolveStepRefireRateOverride(int32 Step)
	{
		if (IsWeaponContextStep(Step))
		{
			return WeaponContextRefireRate;
		}
		return IsRateMeasurementStep(Step) ? GetRateRefireRate(Step) : -1.0f;
	}

	/**
	 * 步骤起点需要的弹匣发数：只在"窗口内必须持续开火"的步骤抬高。
	 * 它保证窗口末尾不会因为打空弹匣而让服务器因为弹药拒绝，把结论污染成弹药限制。
	 */
	int32 ResolveStepMagazineRounds(int32 Step)
	{
		if (IsWeaponContextStep(Step))
		{
			return FMath::CeilToInt(ResolveWeaponContextHoldSeconds() / WeaponContextRefireRate) + RateMagazineSlack;
		}
		return IsRateMeasurementStep(Step)
			? FMath::CeilToInt(GetRateExpectedRps(Step) * GetRateMeasurementSeconds(Step)) + RateMagazineSlack
			: 0;
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
		// 权威提交采样：计数与首末提交时间同域，射速只由它们计算。
		++AmmoPredictionAuthorityCommitSamples;
		const float ActivationTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		// 服务器真正写下权威射速时钟的时刻就是本帧：本回调由 UGameplayAbility::PreActivate
		// 里的 NotifyAbilityActivated 广播（Engine/.../GameplayAbility.cpp），因此它**早于**
		// ActivateAbility 里的 CommitSingleShot。此刻读 TimeOfLastShot 拿到的是上一发的时刻，
		// 用它会凭空造出一个跨步骤的超长间隔；提交时刻与 ActivateAbility 同帧，直接用本帧时间。
		const float CommitTime = ActivationTime;
		if (AmmoPredictionFirstAuthorityActivationTime < 0.0f)
		{
			// 射速窗口的第一发时间：达成射速只在第一发到最后一发之间测量，不含启动延迟。
			AmmoPredictionFirstAuthorityActivationTime = ActivationTime;
			AmmoPredictionFirstAuthorityCommitTime = CommitTime;
		}
		if (AmmoPredictionRateLastActivationTime >= 0.0f)
		{
			// 逐间隔统计：节拍被帧率量化时最小值 / 最大值会明显张开，便于定位达成射速偏差。
			const float Interval = CommitTime - AmmoPredictionRateLastActivationTime;
			AmmoPredictionRateMinInterval = AmmoPredictionRateMinInterval < 0.0f
				? Interval
				: FMath::Min(AmmoPredictionRateMinInterval, Interval);
			AmmoPredictionRateMaxInterval = FMath::Max(AmmoPredictionRateMaxInterval, Interval);
			AmmoPredictionRateIntervalSum += Interval;
			++AmmoPredictionRateIntervalSamples;
		}
		AmmoPredictionRateLastActivationTime = CommitTime;
		AmmoPredictionLastAuthorityCommitTime = CommitTime;
		if (Key > 0)
		{
			AmmoPredictionAuthorityActivationKeys.Add(Key);
		}
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("AMMO_PREDICTION_AUTHORITY_FIRE_ACTIVATED Step=%d Key=%d Count=%d DistinctKeys=%d ")
			TEXT("ServerTime=%.6f CommitTime=%.6f"),
			AmmoPredictionServerStep,
			Key,
			AmmoPredictionAuthorityActivations,
			AmmoPredictionAuthorityActivationKeys.Num(),
			ActivationTime,
			CommitTime);
		return;
	}

	++AmmoPredictionOwnerFireActivations;
	// 拥有端自己的激活采样：计数与首末时间在同一次回调里更新，
	// 请求射速因此只依赖客户端自己的时钟，不与服务器时间跨度混用。
	const float OwnerActivationTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	++AmmoPredictionOwnerWindowActivations;
	if (AmmoPredictionFirstOwnerActivationTime < 0.0f)
	{
		AmmoPredictionFirstOwnerActivationTime = OwnerActivationTime;
	}
	AmmoPredictionLastOwnerActivationTime = OwnerActivationTime;
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
	// 换枪提交之后仍然到达的请求：只可能来自"拥有端还以为自己在用旧武器"的那段时间。
	if (AmmoPredictionSwitchCommitTime >= 0.0f && GetWorld() && GetWorld()->GetTimeSeconds() >= AmmoPredictionSwitchCommitTime)
	{
		++AmmoPredictionRejectsAfterSwitchCommit;
	}
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
		Sample.OwnerConfirmedReplayCount = Weapon->GetOwnerConfirmedReplayCountForAutomationTest();
		Sample.ShotVerdictCount = Weapon->GetShotVerdictReceivedCountForTest();
		Sample.OwnerFireActivationCount = AmmoPredictionOwnerFireActivations;
		Sample.OwnerWindowActivationCount = AmmoPredictionOwnerWindowActivations;
		Sample.FirstOwnerActivationTime = AmmoPredictionFirstOwnerActivationTime;
		Sample.LastOwnerActivationTime = AmmoPredictionLastOwnerActivationTime;
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
		Sample.ClientMaxFPS = GEngine ? GEngine->GetMaxFPS() : 0.0f;

		// 本地开火节拍取证：本步骤窗口内的逐发样本统计。无数据项保持 -1，不填 0。
		const AShooterWeapon::FShooterLocalFireCadenceStats Cadence =
			Weapon->GetLocalFireCadenceStatsForAutomationTest();
		Sample.CadenceSamples = Cadence.Samples;
		Sample.CadenceIntervalSamples = Cadence.IntervalSamples;
		Sample.CadenceRefireRate = Cadence.Samples > 0 ? Cadence.RefireRate : -1.0f;
		Sample.CadenceMeanIntervalMs = SecondsToMillisecondsOrUnknown(Cadence.MeanIntervalSeconds);
		Sample.CadenceMinIntervalMs = SecondsToMillisecondsOrUnknown(Cadence.MinIntervalSeconds);
		Sample.CadenceMaxIntervalMs = SecondsToMillisecondsOrUnknown(Cadence.MaxIntervalSeconds);
		Sample.CadenceMeanLagMs = SecondsToMillisecondsOrUnknown(Cadence.MeanLagSeconds);
		Sample.CadenceMaxLagMs = SecondsToMillisecondsOrUnknown(Cadence.MaxLagSeconds);
		Sample.CadenceMeanFrameDeltaMs = SecondsToMillisecondsOrUnknown(Cadence.MeanFrameDeltaSeconds);
		Sample.CadenceMaxFrameDeltaMs = SecondsToMillisecondsOrUnknown(Cadence.MaxFrameDeltaSeconds);
		Sample.CadenceMeanEndToActivationMs = SecondsToMillisecondsOrUnknown(Cadence.MeanEndToActivationSeconds);
		Sample.CadenceSpanMs = SecondsToMillisecondsOrUnknown(Cadence.SpanSeconds);
		// 相位误差是有符号的（网格被保持时它围绕 0 在正负一个帧间隔内往返），
		// 因此不能沿用"负数即无数据"的约定，这里用 -99999 显式表示无数据。
		Sample.CadencePhaseErrorMs = Cadence.IntervalSamples > 0
			? Cadence.CumulativePhaseErrorSeconds * 1000.0f
			: -99999.0f;

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
	}

	// ---- DEV 观测：不依赖"当前是否持有武器" ----
	// Spec 撤销 / 武器移除之后拥有端可能一把武器都不持有（bValid=false），
	// 但"旧 Spec 的预测债务有没有清干净"恰恰必须在这种状态下才看得到。
	// 每把持有的武器各有一份 Fire Spec / 实例：DEV 观测必须跨实例聚合，
	// 否则"当前武器那一份"看不到别的武器上残留的记录（这正是旧实现漏掉的那类状态）。
	FShooterFireContextAggregateForTest FireAggregate;
	const bool bFireAggregated = AggregateFireContextForTest(Subject, FireAggregate);
	if (FireAggregate.UnresolvedSourceSpecCount > 0)
	{
		// SourceObject 未解析 / 孤儿 Spec：它既不能参与输入也不能被撤销，必须在测试里显式暴露。
		UE_LOG(
			LogShootGame,
			Warning,
			TEXT("AMMO_PREDICTION_FIRE_SPEC_UNRESOLVED Specs=%d UnresolvedSourceSpecs=%d OwnerWeapon=%s"),
			FireAggregate.SpecCount,
			FireAggregate.UnresolvedSourceSpecCount,
			*GetNameSafe(Weapon));
	}

	// 请求武器上下文：本步骤窗口内拥有端每一次激活用的武器（逐键）。
	// 它与服务器同窗口的权威样本按 PredictionKey 对齐，是"请求是否被重新解释"的另一半证据。
	for (const FShooterFireActivationContextForTest& Context : FireAggregate.ActivationContexts)
	{
		const bool bStepKey = AmmoPredictionStepPredictionKeys.Contains(Context.PredictionKey);
		if (Context.bAuthority || Context.PredictionKey <= 0 || !bStepKey)
		{
			continue;
		}
		Sample.ActivationContextKeys.Add(Context.PredictionKey);
		Sample.ActivationContextWeaponIds.Add(Context.WeaponId);
	}

	// 上下文武器（A）自己的账目：换枪 / 移除之后当前武器已经换人，只有单独看 A 才能发现残留。
	if (const AShooterWeapon* ContextWeapon = AmmoPredictionContextWeaponClient.Get())
	{
		const UShooterGameplayAbility_Fire* ContextAbility = GetFireAbilityInstanceForWeaponForTest(
			Subject->GetPlayerState<AShooterPlayerState>(), ContextWeapon);
		Sample.ContextWeaponId = ContextWeapon->GetWeaponId();
		Sample.ContextWeaponPendingShots = ContextWeapon->GetPendingPredictedShots();
		Sample.ContextWeaponMagazineAmmo = ContextWeapon->GetBulletCount();
		Sample.ContextWeaponUnsettledDisplayCount = ContextWeapon->GetUnsettledAmmoDisplayCount();
		Sample.ContextWeaponSpecRemovalUnbindCount = ContextWeapon->GetSpecRemovalUnbindCountForTest();
		Sample.ContextWeaponSpecRemovalCleanupCount = ContextWeapon->GetSpecRemovalCleanupCountForTest();
		Sample.ContextWeaponUnresolvedRecords = ContextAbility
			? ContextAbility->GetUnresolvedShotRecordCountForTest()
			: INDEX_NONE;
	}

	Sample.WeaponContextMismatchVerdictCount = bFireAggregated
		? FireAggregate.WeaponContextMismatchVerdictCount
		: INDEX_NONE;
	const UShooterGameplayAbility_Fire* CurrentFireAbility = GetFireAbilityInstanceForTest(Subject);
	const AShooterWeapon* VerdictWeapon = CurrentFireAbility
		? CurrentFireAbility->GetLastVerdictSourceWeaponForTest()
		: nullptr;
	Sample.LastVerdictSourceWeaponId = VerdictWeapon ? VerdictWeapon->GetWeaponId() : NAME_None;

	// 未结清记录与补播请求都是跨武器求和：单发不变量只能在"全部武器都干净"时成立。
	Sample.FireSpecCount = FireAggregate.SpecCount;
	Sample.FireInstanceCount = FireAggregate.InstanceCount;
	Sample.UnresolvedSourceSpecCount = FireAggregate.UnresolvedSourceSpecCount;
	if (bFireAggregated)
	{
		Sample.UnresolvedShotRecordCount = FireAggregate.UnresolvedRecordCount;
		AmmoPredictionPeakUnresolvedShotRecords = FMath::Max(AmmoPredictionPeakUnresolvedShotRecords, FireAggregate.UnresolvedRecordCount);
		Sample.PeakUnresolvedShotRecords = AmmoPredictionPeakUnresolvedShotRecords;
		Sample.OwnerConfirmedReplayRequestCount = FireAggregate.OwnerConfirmedReplayRequestCount;
	}

	if (CurrentFireAbility)
	{
		const bool bLastCommitted = CurrentFireAbility->WasLastResolvedShotCommittedForTest();
		const bool bLastEngineRejected = CurrentFireAbility->WasLastResolvedShotRejectedByEngineForTest();
		Sample.LastResolvedShotKey = CurrentFireAbility->GetLastResolvedShotKeyForTest();
		Sample.bLastResolvedShotCommitted = bLastCommitted;
		Sample.bLastResolvedShotRejectedByEngine = !bLastCommitted && bLastEngineRejected;
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
	// 拥有端请求射速只覆盖本步骤窗口：计数与首末时间一起复位。
	AmmoPredictionOwnerWindowActivations = 0;
	AmmoPredictionFirstOwnerActivationTime = -1.0f;
	AmmoPredictionLastOwnerActivationTime = -1.0f;
	AmmoPredictionPeakUnresolvedShotRecords = 0;
	AmmoPredictionReleaseSettleTime = 0.0f;
	bAmmoPredictionReleaseObserved = false;

	// 请求上下文武器：本步骤起点拥有端"以为是当前武器"的那一把。
	// 它**不跟随**后续复制，正是要观察"请求绑定 A、服务器已经换成 B"的错位。
	AmmoPredictionContextWeaponClient = GetShooterCharacter()
		? GetShooterCharacter()->GetCurrentWeaponActor()
		: nullptr;
	bAmmoPredictionSwitchSubmitted = false;

	if (AShooterWeapon* Weapon = AmmoPredictionWeapon.Get())
	{
		// 每个步骤的拥有端起点由夹具固定：本地开火节拍就绪、预测预算归零。
		// Unpredicted 用例再把预算抬到弹匣容量：GetPredictedMagazineAmmo() 因此恒为 0，
		// 拥有端不会提前表现也不会增加 Pending，但请求仍照原样发往服务器。
		const int32 FixturePending = IsUnpredictedBudgetStep(Step) ? Weapon->GetMagazineSize() : 0;
		Weapon->SetPendingPredictedShotsForAutomationTest(FixturePending);
		Weapon->SetLocalFireCooldownRemainingForAutomationTest(0.0f);
		// 节拍取证样本按步骤窗口采集：矩阵与射速用例的统计必须只覆盖本步骤。
		Weapon->ResetLocalFireCadenceTraceForAutomationTest();
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

void AShooterNetworkTestCoordinator::ClientSetAmmoPredictionMaxFPS_Implementation(int32 Step, float MaxFPS)
{
	if (!bAmmoPredictionMode || Step != AmmoPredictionClientStep)
	{
		return;
	}

	// 夹具专用：把被试客户端的帧率钉在矩阵指定值上（0 = 解除限制）。
	// 它只影响本机帧循环，不改动任何 Gameplay 结果；真正生效与否由上报的
	// ClientMaxFPS 与逐发 FrameDeltaMs 共同证明，不做假设。
	if (GEngine)
	{
		GEngine->SetMaxFPS(MaxFPS);
	}

	const float EffectiveMaxFPS = GEngine ? GEngine->GetMaxFPS() : 0.0f;
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_CLIENT_MAXFPS Step=%d Requested=%.1f Effective=%.1f"),
		Step, MaxFPS, EffectiveMaxFPS);
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

void AShooterNetworkTestCoordinator::ClientRequestAmmoPredictionWeaponSwitch_Implementation(int32 Step)
{
	// 切枪指令与按住指令属于同一个步骤，因此不能复用 AmmoPredictionSubmittedStep 判重。
	if (!bAmmoPredictionMode || Step != AmmoPredictionClientStep || bAmmoPredictionSwitchSubmitted)
	{
		return;
	}

	AShooterCharacter* Subject = AmmoPredictionSubject.Get();
	if (!Subject || !Subject->IsLocallyControlled() || Subject != GetShooterCharacter())
	{
		return;
	}

	// 生产输入路径：Character 输入入口 → ASC 输入采集 → GA_Equip（ServerOnly）请求上行。
	// 拥有端不会因此改写自己的 CurrentWeaponActor，它只等 CurrentWeaponActor 的复制。
	bAmmoPredictionSwitchSubmitted = true;
	Subject->DoSwitchWeaponInDirection(1);
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_CLIENT_SWITCH_REQUEST Step=%d Subject=%s Weapon=%s"),
		Step, *GetNameSafe(Subject), *GetNameSafe(Subject->GetCurrentWeaponActor()));
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
		TEXT("Feedback=%d ConfirmedReplay=%d ReplayReq=%d Verdicts=%d Act=%d Rejects=%d DistinctKeys=%d ")
		TEXT("Records=%d Peak=%d LastKey=%d Committed=%d ByEngine=%d Released=%d AtRelease=%d ")
		TEXT("FireActive=%d ReloadActive=%d Reloading=%d FullAuto=%d CooldownReady=%d"),
		Observation.Step,
		Observation.bValid ? 1 : 0,
		Observation.MagazineAmmo,
		Observation.ReserveAmmo,
		Observation.PendingShots,
		Observation.PredictedMagazineAmmo,
		Observation.PredictedOwnerFeedbackCount,
		Observation.OwnerConfirmedReplayCount,
		Observation.OwnerConfirmedReplayRequestCount,
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
	// 权威拒绝按"全部 Fire 实例"求和：每把武器各有一份实例，单一实例看不到别的武器的拒绝。
	FShooterFireContextAggregateForTest FireAggregate;
	AggregateFireContextForTest(AmmoPredictionSubject.Get(), FireAggregate);
	return FireAggregate.InstanceCount > 0 ? FireAggregate.AuthorityRejectCount : INDEX_NONE;
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
	AmmoPredictionRateIntervalSum = 0.0f;
	AmmoPredictionRateIntervalSamples = 0;
	AmmoPredictionAuthorityCommitSamples = 0;
	AmmoPredictionFirstAuthorityCommitTime = -1.0f;
	AmmoPredictionLastAuthorityCommitTime = -1.0f;

	// 本步骤两端必须一致的射速：射速用例使用配置射速，Weapon Context 用例用小节拍放大错位窗口，
	// 其余步骤恢复武器行配置的原始射速。只有拥有端请求节拍与服务器权威门控使用同一个 RefireRate，
	// "达成射速"与"窗口内请求数"才有意义。
	const float StepRefireOverride = ResolveStepRefireRateOverride(Step);
	AmmoPredictionStepRefireRate = StepRefireOverride > 0.0f ? StepRefireOverride : AmmoPredictionOriginalRefireRate;
	if (!FMath::IsNearlyEqual(Weapon->GetRefireRate(), AmmoPredictionStepRefireRate, 0.0001f))
	{
		Weapon->SetRefireRateForAutomationTest(AmmoPredictionStepRefireRate);
	}

	// 请求上下文武器：本步骤起点服务器"以为是当前武器"的那一把。
	// 后续换枪只更新 AmmoPredictionWeapon（跟随当前武器），这里保持不跟随。
	AmmoPredictionContextWeaponServer = Weapon;
	bAmmoPredictionSwitchRequested = false;
	bAmmoPredictionSwitchObserved = false;
	AmmoPredictionSwitchRequestTime = -1.0f;
	AmmoPredictionSwitchCommitTime = -1.0f;
	AmmoPredictionEquipCommitTime = -1.0f;
	AmmoPredictionClientLearnedSwitchTime = -1.0f;
	bAmmoPredictionSwitchLearnedObserved = false;
	AmmoPredictionWindowAcceptedCount = 0;
	AmmoPredictionRejectsAfterSwitchCommit = 0;
	AmmoPredictionSwitchTargetWeapon = nullptr;
	AmmoPredictionContextMismatchKeyCount = 0;
	AmmoPredictionContextMatchedKeyCount = 0;
	AmmoPredictionContextClientOnlyKeyCount = 0;
	AmmoPredictionContextAuthorityOnlyKeyCount = 0;

	// 每个步骤的权威弹药起点由夹具显式固定：满弹匣 + 足量备弹，
	// 这样"服务器真的开火"与"换弹能补满"都能在同一份起点上被断言。
	// 需要持续开火的步骤把弹匣抬到窗口所需发数，避免窗口末尾因打空弹匣而失真。
	const int32 RateRounds = ResolveStepMagazineRounds(Step);
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
	// 帧率同样在准备之后下发：矩阵行要求客户端在测量窗口内保持指定帧率；
	// 非矩阵步骤下发 0，等于显式恢复"不限制帧率"的默认状态。
	ClientSetAmmoPredictionMaxFPS(Step, GetCadenceMatrixMaxFPS(Step));
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
	const int32 ConfirmedReplayDelta = Sample.OwnerConfirmedReplayCount - AmmoPredictionBefore.OwnerConfirmedReplayCount;
	const int32 PendingDelta = Sample.PendingShots - AmmoPredictionBefore.PendingShots;
	const int32 VerdictDelta = Sample.ShotVerdictCount - AmmoPredictionBefore.ShotVerdictCount;
	const int32 MagazineDelta = Weapon->GetBulletCount() - AmmoPredictionMagazineBefore;

	// 一次按下 + 松开只允许形成一次本地动作边界；这一次请求必须被服务器独立接受并恰好提交一发。
	const bool bConverged = ShotDelta == 1 && AcceptedDelta == 1 && ClientActivationDelta == 1 &&
		Sample.DistinctPredictionKeyCount == 1 && VerdictDelta == 1 &&
		PredictedDelta == 1 && ConfirmedReplayDelta == 0 && PendingDelta == 0 && MagazineDelta == -1 &&
		Sample.UnresolvedShotRecordCount == 0 && !Sample.bFireActive && !Sample.bReloadActive &&
		Sample.LastResolvedShotKey == Sample.FirstPredictionKey && Sample.bLastResolvedShotCommitted;
	const FString Detail = FString::Printf(
		TEXT("FullAuto=%d Shots=%d Accepted=%d ClientAct=%d Keys=%d Verdicts=%d Predicted=%d ConfirmedReplay=%d ")
		TEXT("Pending=%d MagDelta=%d Records=%d LastKey=%d Committed=%d RefireRate=%.3f"),
		Sample.bCurrentWeaponIsFullAuto ? 1 : 0,
		ShotDelta,
		AcceptedDelta,
		ClientActivationDelta,
		Sample.DistinctPredictionKeyCount,
		VerdictDelta,
		PredictedDelta,
		ConfirmedReplayDelta,
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
	const int32 ConfirmedReplayDelta = Sample.OwnerConfirmedReplayCount - AmmoPredictionBefore.OwnerConfirmedReplayCount;
	const int32 ReplayRequestBefore = AmmoPredictionBefore.OwnerConfirmedReplayRequestCount;
	const int32 ReplayRequestDelta = Sample.OwnerConfirmedReplayRequestCount - ReplayRequestBefore;
	const int32 PendingDelta = Sample.PendingShots - AmmoPredictionBefore.PendingShots;
	const int32 VerdictDelta = Sample.ShotVerdictCount - AmmoPredictionBefore.ShotVerdictCount;
	const int32 HudPredictedDelta = Sample.HudPredictedUpdates - AmmoPredictionBefore.HudPredictedUpdates;

	// 提前表现的那一发被接受：Pending 只由这一发的 Committed 裁决结清，绝不补播第二次表现。
	const bool bConverged = ShotDelta == 1 && AcceptedDelta == 1 && PredictedDelta == 1 && ConfirmedReplayDelta == 0 &&
		ReplayRequestDelta == 0 && PendingDelta == 0 && VerdictDelta == 1 && HudPredictedDelta >= 1 &&
		Sample.UnresolvedShotRecordCount == 0 && Sample.LastResolvedShotKey == Sample.FirstPredictionKey &&
		Sample.bLastResolvedShotCommitted &&
		Weapon->GetBulletCount() == AmmoPredictionMagazineBefore - 1;
	const FString Detail = FString::Printf(
		TEXT("Shots=%d Accepted=%d Predicted=%d ConfirmedReplay=%d ReplayReq=%d Pending=%d Verdicts=%d ")
		TEXT("HudPredicted=%d Records=%d LastKey=%d Committed=%d"),
		ShotDelta,
		AcceptedDelta,
		PredictedDelta,
		ConfirmedReplayDelta,
		ReplayRequestDelta,
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
	const int32 ConfirmedReplayDelta = Sample.OwnerConfirmedReplayCount - AmmoPredictionBefore.OwnerConfirmedReplayCount;
	const int32 ReplayRequestBefore = AmmoPredictionBefore.OwnerConfirmedReplayRequestCount;
	const int32 ReplayRequestDelta = Sample.OwnerConfirmedReplayRequestCount - ReplayRequestBefore;
	const int32 PendingDelta = Sample.PendingShots - AmmoPredictionBefore.PendingShots;

	// 被拒绝的是「已经提前表现、已经占用预算」的那一发：
	// 引擎 Reject 通道按同一个 PredictionKey 退还恰好一发（净 Pending 变化为 0），不补播、不生成弹丸。
	const bool bConverged = ShotDelta == 0 && AcceptedDelta == 0 && RejectedDelta >= 1 && ProjectileDelta == 0 &&
		ClientActivationDelta == 1 && PredictedDelta == 1 && ConfirmedReplayDelta == 0 && ReplayRequestDelta == 0 &&
		PendingDelta == 0 && Sample.UnresolvedShotRecordCount == 0 &&
		Sample.MagazineAmmo == AmmoPredictionMagazineBefore &&
		Sample.LastResolvedShotKey == Sample.FirstPredictionKey && !Sample.bLastResolvedShotCommitted &&
		Sample.bLastResolvedShotRejectedByEngine;
	const FString Detail = FString::Printf(
		TEXT("Shots=%d Accepted=%d ServerRejects=%d Projectiles=%d ClientAct=%d Predicted=%d ConfirmedReplay=%d ")
		TEXT("Pending=%d Records=%d OwnerMag=%d LastKey=%d Committed=%d ByEngine=%d RejectProbeDelta=%d"),
		ShotDelta,
		AcceptedDelta,
		RejectedDelta,
		ProjectileDelta,
		ClientActivationDelta,
		PredictedDelta,
		ConfirmedReplayDelta,
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
	const int32 ConfirmedReplayDelta = Sample.OwnerConfirmedReplayCount - Before.OwnerConfirmedReplayCount;
	const int32 ReplayRequestDelta = Sample.OwnerConfirmedReplayRequestCount - Before.OwnerConfirmedReplayRequestCount;

	// 三条射速指标各用自己的域，与矩阵用例同一口径，不互相替代：
	// 1. ClientRequestRps：拥有端自己的激活采样——(数量 - 1) / (末 - 首)，只用客户端时钟；
	// 2. AuthorityCommitRps：服务器自己的提交采样——(数量 - 1) / (末 - 首)，只用服务器时钟；
	// 3. AuthorityRejectCount：服务器拒绝了多少请求，是计数而不是速率。
	// 样本不足两次时统一为 -1，并由报告行显式声明，不用另一个域的跨度凑数字。
	const float ClientRequestRps = ComputeEventRps(Sample.OwnerWindowActivationCount,
		Sample.FirstOwnerActivationTime, Sample.LastOwnerActivationTime);
	const float MeasuredSeconds = AmmoPredictionLastAuthorityCommitTime - AmmoPredictionFirstAuthorityCommitTime;
	const float AuthorityCommitRps = ComputeEventRps(AmmoPredictionAuthorityCommitSamples,
		AmmoPredictionFirstAuthorityCommitTime, AmmoPredictionLastAuthorityCommitTime);
	const bool bAuthorityRateMeasurable = AuthorityCommitRps >= 0.0f;
	const float AuthorityRateErrorRatio = bAuthorityRateMeasurable
		? FMath::Abs(AuthorityCommitRps - ExpectedRps) / ExpectedRps
		: -1.0f;
	const float ClientRateErrorRatio = ClientRequestRps >= 0.0f
		? FMath::Abs(ClientRequestRps - ExpectedRps) / ExpectedRps
		: -1.0f;
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

	// 参与 Pass / Fail 的判据（阈值统一为 RateToleranceRatio = ±15%）：
	//
	// 1. ClientRequestRate：客户端请求射速相对配置射速的偏差。
	//    这是本仓库唯一能控制的量，也是会漏掉"低帧率下按实际时间重锚点"这类回归的判据：
	//    请求节拍会慢到 87ms / 111ms，而权威达成射速因为"请求变慢、拒绝变少"看起来还行。
	// 2. AuthorityCommitRate：权威提交射速相对配置射速的偏差。
	//    它由权威门控决定，本轮不修改门控，因此该判据会随权威拒绝数波动；
	//    保留它而不是放宽它，是为了让权威侧的达成率变化必须显式出现在结果里
	//    （实测拒绝机制是服务器帧时间量化导致，见开发记录）。
	// 3. Accepted == Shots：一次被接受的 Activation 恰好一发。
	// 4. Accepted + Rejects == ClientActivations：没有"被接受了却没有发"或被静默吞掉的请求。
	// 5. Pending / Shot 记录在窗口结束时归零。
	//
	// 只是观测、不参与判定的量：逐间隔最小值 / 最大值、拒绝探针、网络字节与速率。
	const bool bClientRequestRateMeasurable = ClientRequestRps >= 0.0f;
	const bool bClientRequestRateOk = bClientRequestRateMeasurable && ClientRateErrorRatio <= RateToleranceRatio;
	const bool bAuthorityCommitRateOk = bAuthorityRateMeasurable && AuthorityRateErrorRatio <= RateToleranceRatio;
	const bool bNoSilentLoss = RejectDelta + AcceptedDelta == ClientActivationDelta;
	const bool bConverged = ShotDelta == AcceptedDelta && bClientRequestRateOk && bAuthorityCommitRateOk &&
		bNoSilentLoss && Sample.PendingShots == 0 && Sample.UnresolvedShotRecordCount == 0;

	// 判定口径显式输出：哪几项参与 Pass / Fail、各自取值与阈值，避免与"仅上报"混淆。
	const FString RateAssertLine = FString::Printf(
		TEXT("AMMO_PREDICTION_RATE_ASSERT Case=%s Gated(ClientRequestRate=%d ClientRequestErrPct=%.2f ")
		TEXT("AuthorityCommitRate=%d AuthorityCommitErrPct=%.2f AcceptedEqualsShots=%d NoSilentLoss=%d ")
		TEXT("PendingZero=%d RecordsZero=%d) ThresholdPct=%.1f ObserveOnly(MinInterval=%.4f MaxInterval=%.4f ")
		TEXT("AuthorityRejectCount=%d)"),
		CaseName,
		bClientRequestRateOk ? 1 : 0,
		ClientRateErrorRatio * 100.0f,
		bAuthorityCommitRateOk ? 1 : 0,
		AuthorityRateErrorRatio * 100.0f,
		ShotDelta == AcceptedDelta ? 1 : 0,
		bNoSilentLoss ? 1 : 0,
		Sample.PendingShots == 0 ? 1 : 0,
		Sample.UnresolvedShotRecordCount == 0 ? 1 : 0,
		RateToleranceRatio * 100.0f,
		AmmoPredictionRateMinInterval,
		AmmoPredictionRateMaxInterval,
		RejectDelta);
	UE_LOG(LogShootGame, Display, TEXT("%s"), *RateAssertLine);

	const FString RateResult = FString::Printf(
		TEXT("AMMO_PREDICTION_RATE_RESULT Case=%s ConfiguredRps=%.3f ConfiguredHoldSeconds=%.2f ")
		TEXT("ClientRequestRps=%.3f ClientRequestSamples=%d ClientRequestErrPct=%.2f ")
		TEXT("AuthorityCommitRps=%.3f AuthorityCommitSamples=%d AuthorityCommitErrPct=%.2f ")
		TEXT("AuthorityRejectCount=%d Shots=%d MeasuredSeconds=%.3f ")
		TEXT("ElapsedSinceHold=%.3f IntervalSamples=%d MinInterval=%.4f MaxInterval=%.4f ")
		TEXT("ClientActivations=%d AcceptedActivations=%d AuthorityRejectProbe=%d ")
		TEXT("RefireRate=%.4f PredictedFeedback=%d ConfirmedReplayFeedback=%d ReplayRequests=%d ")
		TEXT("Pending=%d PendingStart=%d UnresolvedRecords=%d"),
		CaseName,
		ExpectedRps,
		RateMeasurementSeconds,
		ClientRequestRps,
		Sample.OwnerWindowActivationCount,
		ClientRateErrorRatio * 100.0f,
		AuthorityCommitRps,
		AmmoPredictionAuthorityCommitSamples,
		AuthorityRateErrorRatio * 100.0f,
		RejectDelta,
		ShotDelta,
		MeasuredSeconds,
		HoldElapsed,
		AmmoPredictionRateIntervalSamples,
		AmmoPredictionRateMinInterval,
		AmmoPredictionRateMaxInterval,
		ClientActivationDelta,
		AcceptedDelta,
		GetAmmoPredictionAuthorityRejectProbe() - AmmoPredictionAuthorityRejectProbeBefore,
		Sample.RefireRate,
		PredictedDelta,
		ConfirmedReplayDelta,
		ReplayRequestDelta,
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
		TEXT("ClientRequestRps=%.3f/%.3f ClientRequestErrPct=%.2f ")
		TEXT("AuthorityCommitRps=%.3f/%.3f AuthorityCommitErrPct=%.2f AuthorityRejectCount=%d ")
		TEXT("Shots=%d Accepted=%d ClientAct=%d Predicted=%d ")
		TEXT("ConfirmedReplay=%d ReplayRequests=%d Pending=%d PendingStart=%d Records=%d NetOutBps(S=%.0f/C=%.0f) ")
		TEXT("RateSamples(S=%d/C=%d)"),
		ClientRequestRps,
		ExpectedRps,
		ClientRateErrorRatio * 100.0f,
		AuthorityCommitRps,
		ExpectedRps,
		AuthorityRateErrorRatio * 100.0f,
		RejectDelta,
		ShotDelta,
		AcceptedDelta,
		ClientActivationDelta,
		PredictedDelta,
		ConfirmedReplayDelta,
		ReplayRequestDelta,
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

void AShooterNetworkTestCoordinator::RunAmmoPredictionCadenceMatrixStep(int32 Step)
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterWeapon* Weapon = AmmoPredictionWeapon.Get();
	if (!Weapon)
	{
		return;
	}

	const FString CaseName = GetCadenceMatrixCaseName(Step);
	const float ExpectedRps = GetRateExpectedRps(Step);
	const float RequestedMaxFPS = GetCadenceMatrixMaxFPS(Step);
	const float WindowSeconds = GetRateMeasurementSeconds(Step);
	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;

	if (!bAmmoPredictionStepCommandSent)
	{
		if (!IsAmmoPredictionFixtureReady(Step, 0))
		{
			return;
		}
		bAmmoPredictionStepCommandSent = true;
		AmmoPredictionHoldStartTime = GetWorld()->GetTimeSeconds();
		ClientSubmitAmmoPredictionHoldFire(Step, WindowSeconds, 0.0f, -1.0f);
		return;
	}

	const int32 ShotDelta = Weapon->GetAuthorityShotCountForAutomationTest() - AmmoPredictionAuthorityShotsBefore;
	if (!bAmmoPredictionSettleStarted)
	{
		// 窗口结束的标志是拥有端松手并跨过一个完整本地节拍；之后仍要等一段结算，
		// 让最后一发的裁决回到拥有端，Pending 才能收敛到 0。
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

	const FShooterAmmoPredictionObservation& Before = AmmoPredictionBefore;
	const int32 AcceptedDelta = AmmoPredictionAuthorityActivations - AmmoPredictionAuthorityActivationsBefore;
	const int32 RejectDelta = AmmoPredictionAuthorityRejects - AmmoPredictionAuthorityRejectsBefore;
	const int32 ClientActivationDelta = Sample.OwnerFireActivationCount - Before.OwnerFireActivationCount;
	const int32 PredictedDelta = Sample.PredictedOwnerFeedbackCount - Before.PredictedOwnerFeedbackCount;
	const int32 ConfirmedReplayDelta = Sample.OwnerConfirmedReplayCount - Before.OwnerConfirmedReplayCount;
	const int32 ReplayRequestDelta = Sample.OwnerConfirmedReplayRequestCount - Before.OwnerConfirmedReplayRequestCount;

	// 三条射速指标各用自己的域，不互相替代：
	// 1. ClientRequestRps：拥有端自己的激活采样——(数量 - 1) / (末 - 首)，只用客户端时钟；
	// 2. AuthorityCommitRps：服务器自己的提交采样——(数量 - 1) / (末 - 首)，只用服务器时钟；
	// 3. AuthorityRejectCount：服务器拒绝了多少请求，是计数而不是速率。
	// 样本不足时一律返回 -1 并由报告行显式声明，不用另一个域的跨度去凑一个数字。
	const float ClientRequestRps = ComputeEventRps(Sample.OwnerWindowActivationCount,
		Sample.FirstOwnerActivationTime, Sample.LastOwnerActivationTime);
	const float MeasuredSeconds = AmmoPredictionLastAuthorityCommitTime - AmmoPredictionFirstAuthorityCommitTime;
	const float AuthorityCommitRps = ComputeEventRps(AmmoPredictionAuthorityCommitSamples,
		AmmoPredictionFirstAuthorityCommitTime, AmmoPredictionLastAuthorityCommitTime);
	const bool bRateMeasurable = AuthorityCommitRps >= 0.0f;
	const float ClientRateErrorPct = ClientRequestRps >= 0.0f
		? (ClientRequestRps - ExpectedRps) / ExpectedRps * 100.0f
		: -1.0f;
	const float AuthorityRateErrorPct = bRateMeasurable
		? (AuthorityCommitRps - ExpectedRps) / ExpectedRps * 100.0f
		: -1.0f;

	// 权威侧节拍统计：平均 / 最小 / 最大间隔与累计相位误差（同一口径，只是换成提交时刻）。
	const float AuthMeanIntervalMs = AmmoPredictionRateIntervalSamples > 0
		? AmmoPredictionRateIntervalSum / AmmoPredictionRateIntervalSamples * 1000.0f
		: -1.0f;
	const float AuthPhaseErrorMs = bRateMeasurable
		? (MeasuredSeconds - static_cast<float>(AmmoPredictionAuthorityCommitSamples - 1) * GetRateRefireRate(Step))
			* 1000.0f
		: -1.0f;

	const FString MatrixLine = FString::Printf(
		TEXT("AMMO_PREDICTION_CADENCE_MATRIX Case=%s Step=%d RequestedFPS=%.0f ClientFPS=%.1f ")
		TEXT("ClientFrameMeanMs=%.3f ClientFrameMaxMs=%.3f ConfiguredRps=%.3f ")
		TEXT("ClientRequestRps=%.3f ClientRequestSamples=%d ClientRequestErrPct=%.2f ")
		TEXT("AuthorityCommitRps=%.3f AuthorityCommitSamples=%d AuthorityCommitErrPct=%.2f ")
		TEXT("AuthorityRejectCount=%d Shots=%d ClientAct=%d Accepted=%d ")
		TEXT("LocalMeanMs=%.3f LocalMinMs=%.3f LocalMaxMs=%.3f LocalLagMeanMs=%.3f LocalLagMaxMs=%.3f ")
		TEXT("LocalPhaseErrMs=%.3f LocalEndToActMeanMs=%.3f LocalSpanMs=%.1f ")
		TEXT("AuthMeanMs=%.3f AuthMinMs=%.3f AuthMaxMs=%.3f AuthPhaseErrMs=%.3f ")
		TEXT("Pending=%d PendingStart=%d Records=%d Predicted=%d ConfirmedReplay=%d ReplayReq=%d"),
		*CaseName,
		Step,
		RequestedMaxFPS,
		Sample.ClientMaxFPS,
		Sample.CadenceMeanFrameDeltaMs,
		Sample.CadenceMaxFrameDeltaMs,
		ExpectedRps,
		ClientRequestRps,
		Sample.OwnerWindowActivationCount,
		ClientRateErrorPct,
		AuthorityCommitRps,
		AmmoPredictionAuthorityCommitSamples,
		AuthorityRateErrorPct,
		RejectDelta,
		ShotDelta,
		ClientActivationDelta,
		AcceptedDelta,
		Sample.CadenceMeanIntervalMs,
		Sample.CadenceMinIntervalMs,
		Sample.CadenceMaxIntervalMs,
		Sample.CadenceMeanLagMs,
		Sample.CadenceMaxLagMs,
		Sample.CadencePhaseErrorMs,
		Sample.CadenceMeanEndToActivationMs,
		Sample.CadenceSpanMs,
		AuthMeanIntervalMs,
		AmmoPredictionRateMinInterval * 1000.0f,
		AmmoPredictionRateMaxInterval * 1000.0f,
		AuthPhaseErrorMs,
		Sample.PendingShots,
		Before.PendingShots,
		Sample.UnresolvedShotRecordCount,
		PredictedDelta,
		ConfirmedReplayDelta,
		ReplayRequestDelta);
	UE_LOG(LogShootGame, Display, TEXT("%s"), *MatrixLine);

	// 矩阵只断言结构性不变量，不断言射速：射速误差是本轮的观测对象，由报告行如实上报。
	// 一次被接受的 Activation 必须恰好一发；窗口结束时预算与记录都必须收敛。
	const bool bConverged = ShotDelta == AcceptedDelta && ShotDelta >= 2 && Sample.PendingShots == 0 &&
		Sample.UnresolvedShotRecordCount == 0;
	const FString Detail = FString::Printf(
		TEXT("FPS=%.0f/%.1f ClientRequestRps=%.3f/%.3f ClientRequestErrPct=%.2f ")
		TEXT("AuthorityCommitRps=%.3f AuthorityCommitErrPct=%.2f AuthorityRejectCount=%d ")
		TEXT("Shots=%d ClientAct=%d Accepted=%d PhaseErrMs=%.3f Pending=%d Records=%d"),
		RequestedMaxFPS,
		Sample.ClientMaxFPS,
		ClientRequestRps,
		ExpectedRps,
		ClientRateErrorPct,
		AuthorityCommitRps,
		AuthorityRateErrorPct,
		RejectDelta,
		ShotDelta,
		ClientActivationDelta,
		AcceptedDelta,
		Sample.CadencePhaseErrorMs,
		Sample.PendingShots,
		Sample.UnresolvedShotRecordCount);
	ConcludeAmmoPredictionCase(*CaseName, bConverged, Detail);

	// 最后一个矩阵行之后进入收口步骤：它负责解除客户端帧率限制并打出本模式的 DONE 标记。
	StartAmmoPredictionStep(Step < StepCadenceMatrixLast ? Step + 1 : StepDone);
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionWeaponContextStep(int32 Step)
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterCharacter* Subject = AmmoPredictionSubject.Get();
	// 请求上下文武器 A：本步骤起点两端共同认可的当前武器。
	// 步骤体刻意不让它跟随换枪，否则"请求绑定 A、服务器已经换成 B"就无从观测。
	AShooterWeapon* ContextWeapon = AmmoPredictionContextWeaponServer.Get();
	if (!Subject || !ContextWeapon)
	{
		FailTest(TEXT("Weapon context case lost its pinned context weapon"));
		bAmmoPredictionFinished = true;
		return;
	}

	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;
	const float Now = GetWorld()->GetTimeSeconds();

	if (!bAmmoPredictionStepCommandSent)
	{
		if (!IsAmmoPredictionFixtureReady(Step, 0))
		{
			return;
		}

		// 前提 1：必须存在第二把武器，否则本用例无法构造"服务器已换枪"。
		// 与 Rifle / 半自动夹具同一条生产路径：先在背包里找，找不到再租用并入背包。
		UShooterInventoryComponent* Inventory = Subject->GetInventoryComponent();
		UShooterWeaponRuntimeSubsystem* Runtime = GetWorld()->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
		AShooterWeapon* SwitchTarget = Inventory ? Inventory->FindAdjacentWeapon(ContextWeapon, 1) : nullptr;
		if (!SwitchTarget && Inventory && Runtime)
		{
			const FName TargetRowName = PickWeaponContextSwitchRowName(ContextWeapon);
			AShooterWeapon* Acquired = TargetRowName.IsNone()
				? nullptr
				: Inventory->FindWeaponByWeaponId(TargetRowName);
			if (!Acquired && !TargetRowName.IsNone())
			{
				Acquired = Runtime->AcquireWeapon(TargetRowName, Subject, Subject);
				if (Acquired && Inventory->AddWeapon(Acquired) != EShooterInventoryAddResult::Added)
				{
					Runtime->ReleaseWeapon(Acquired);
					Acquired = nullptr;
				}
			}
			SwitchTarget = Inventory->FindAdjacentWeapon(ContextWeapon, 1);
			UE_LOG(LogShootGame, Display, TEXT("WEAPON_CONTEXT_ACQUIRE Row=%s Weapon=%s Count=%d"),
				*TargetRowName.ToString(), *GetNameSafe(Acquired), Inventory->GetWeaponCount());
		}

		if (!SwitchTarget || SwitchTarget == ContextWeapon)
		{
			FailTest(FString::Printf(
				TEXT("Weapon context case requires a second inventory weapon: Context=%s Count=%d"),
				*GetNameSafe(ContextWeapon),
				Inventory ? Inventory->GetWeaponCount() : INDEX_NONE));
			bAmmoPredictionFinished = true;
			return;
		}

		// 前提 2：切枪事务时长必须在夹具预算内，否则按住窗口覆盖不了"提交之后"的那一段。
		if (SwitchTarget->GetEquipDuration() > WeaponContextEquipDurationBudgetSeconds)
		{
			FailTest(FString::Printf(
				TEXT("Weapon context equip budget exceeded: Target=%s EquipDuration=%.3f Budget=%.3f"),
				*GetNameSafe(SwitchTarget),
				SwitchTarget->GetEquipDuration(),
				WeaponContextEquipDurationBudgetSeconds));
			bAmmoPredictionFinished = true;
			return;
		}

		const float HoldSeconds = ResolveWeaponContextHoldSeconds();
		bAmmoPredictionStepCommandSent = true;
		AmmoPredictionHoldStartTime = Now;
		AmmoPredictionSwitchRequestTime = Now + WeaponContextHoldLeadSeconds;
		// 精确的换枪提交时刻来自装备权威自己的事件，不依赖 poll 轮询粒度。
		if (UShooterEquipmentComponent* Equipment = Subject->GetEquipmentComponent())
		{
			Equipment->OnEquippedWeaponChanged.AddDynamic(
				this, &AShooterNetworkTestCoordinator::HandleAmmoPredictionEquippedWeaponChanged);
			bAmmoPredictionEquipDelegateBound = true;
		}
		UE_LOG(LogShootGame, Display,
			TEXT("WEAPON_CONTEXT_SETUP Step=%d Context=%s Target=%s EquipDuration=%.3f ")
			TEXT("Hold=%.2f Lead=%.2f RefireRate=%.4f ContextMag=%d"),
			Step,
			*GetNameSafe(ContextWeapon),
			*GetNameSafe(SwitchTarget),
			SwitchTarget->GetEquipDuration(),
			HoldSeconds,
			WeaponContextHoldLeadSeconds,
			AmmoPredictionStepRefireRate,
			ContextWeapon->GetBulletCount());
		ClientSubmitAmmoPredictionHoldFire(Step, HoldSeconds, 0.0f, -1.0f);
		return;
	}

	// 相位 1：按住开始 Lead 秒之后，按生产输入路径请求切枪（只请求一次）。
	if (!bAmmoPredictionSwitchRequested && AmmoPredictionSwitchRequestTime >= 0.0f && Now >= AmmoPredictionSwitchRequestTime)
	{
		bAmmoPredictionSwitchRequested = true;
		ClientRequestAmmoPredictionWeaponSwitch(Step);
	}

	// 相位 2：只观察服务器当前武器是否真的换人。引脚在这里跟随当前武器，
	// 上下文武器保持 A —— 两者分离本身就是本用例要观测的错位。
	if (!bAmmoPredictionSwitchObserved)
	{
		AShooterWeapon* CurrentWeapon = Subject->GetCurrentWeaponActor();
		if (CurrentWeapon && CurrentWeapon != ContextWeapon)
		{
			bAmmoPredictionSwitchObserved = true;
			AmmoPredictionSwitchCommitTime = Now;
			AmmoPredictionSwitchTargetWeapon = CurrentWeapon;
			AmmoPredictionWeapon = CurrentWeapon;
			AmmoPredictionOriginalRefireRate = CurrentWeapon->GetRefireRate();
			UE_LOG(LogShootGame, Display,
				TEXT("WEAPON_CONTEXT_SERVER_SWITCH Step=%d Context=%s Target=%s ServerTime=%.6f ")
				TEXT("SinceHoldStartMs=%.3f ContextShotsSinceStep=%d"),
				Step,
				*GetNameSafe(ContextWeapon),
				*GetNameSafe(CurrentWeapon),
				Now,
				(Now - AmmoPredictionHoldStartTime) * 1000.0f,
				ContextWeapon->GetAuthorityShotCountForAutomationTest() - AmmoPredictionAuthorityShotsBefore);
		}
	}

	// 相位 2b：拥有端学会新武器 = 拥有端样本第一次报告目标 WeaponId。
	// 它与换枪提交时刻一起界定"错位窗口"：窗口内拥有端每一次请求都仍然绑定旧武器。
	if (bAmmoPredictionSwitchObserved && !bAmmoPredictionSwitchLearnedObserved)
	{
		const AShooterWeapon* TargetWeapon = AmmoPredictionSwitchTargetWeapon.Get();
		const FName TargetWeaponId = TargetWeapon ? TargetWeapon->GetWeaponId() : NAME_None;
		if (Sample.bValid && !TargetWeaponId.IsNone() && Sample.CurrentWeaponId == TargetWeaponId)
		{
			bAmmoPredictionSwitchLearnedObserved = true;
			AmmoPredictionClientLearnedSwitchTime = Now;
			UE_LOG(LogShootGame, Display, TEXT("WEAPON_CONTEXT_CLIENT_LEARNED Step=%d Target=%s WindowMs=%.3f"),
				Step, *TargetWeaponId.ToString(), (Now - AmmoPredictionSwitchCommitTime) * 1000.0f);
		}
	}

	// 相位 3：等拥有端松手并跨过一个完整本地节拍，再留出裁决回流的结算时间。
	if (!bAmmoPredictionSettleStarted)
	{
		if (!IsAmmoPredictionClientSampleFresh(Step) || !Sample.bReleaseSettled)
		{
			return;
		}
		AmmoPredictionSettleStartTime = Now;
		bAmmoPredictionSettleStarted = true;
		return;
	}

	if (!IsAmmoPredictionClientSampleFresh(Step) || Now - AmmoPredictionSettleStartTime < WeaponContextSettleSeconds)
	{
		return;
	}

	// ---- 判定：逐键对齐"拥有端请求用的武器"与"服务器提交用的武器" ----
	// 每把武器各有一份 Fire 实例，因此权威样本也必须跨实例聚合后再按 PredictionKey 对齐。
	FShooterFireContextAggregateForTest AuthorityAggregate;
	AggregateFireContextForTest(Subject, AuthorityAggregate);
	TMap<int32, FName> AuthorityContext;
	for (const FShooterFireActivationContextForTest& Context : AuthorityAggregate.ActivationContexts)
	{
		if (Context.bAuthority && AmmoPredictionAuthorityActivationKeys.Contains(Context.PredictionKey))
		{
			AuthorityContext.Add(Context.PredictionKey, Context.WeaponId);
		}
	}

	TMap<int32, FName> ClientContext;
	const int32 ContextCount = FMath::Min(Sample.ActivationContextKeys.Num(), Sample.ActivationContextWeaponIds.Num());
	for (int32 Index = 0; Index < ContextCount; ++Index)
	{
		ClientContext.Add(Sample.ActivationContextKeys[Index], Sample.ActivationContextWeaponIds[Index]);
	}

	FString MismatchDetail;
	for (const TPair<int32, FName>& Authority : AuthorityContext)
	{
		const FName* ClientWeaponId = ClientContext.Find(Authority.Key);
		if (!ClientWeaponId)
		{
			++AmmoPredictionContextAuthorityOnlyKeyCount;
			continue;
		}

		++AmmoPredictionContextMatchedKeyCount;
		if (*ClientWeaponId != Authority.Value)
		{
			++AmmoPredictionContextMismatchKeyCount;
			if (MismatchDetail.Len() < 360)
			{
				MismatchDetail += FString::Printf(TEXT("[Key=%d RequestWeapon=%s CommitWeapon=%s]"),
					Authority.Key, *ClientWeaponId->ToString(), *Authority.Value.ToString());
			}
		}
	}
	AmmoPredictionContextClientOnlyKeyCount = ClientContext.Num() - AmmoPredictionContextMatchedKeyCount;

	// ---- 窗口是否真的被走过：错位窗口内被服务器接受的 Activation 数 ----
	// 窗口定义：装备事务真实提交 → 拥有端样本第一次报告新武器。窗口内拥有端不可能已经知道换枪，
	// 因此窗口内任何被接受的 Activation 都必须仍然由旧武器提交才算满足不变量。
	// 起点优先用装备权威事件时刻：poll 轮询最多晚 0.1s，会让窗口内的接受被少算。
	const float WindowStartTime = AmmoPredictionEquipCommitTime >= 0.0f
		? AmmoPredictionEquipCommitTime
		: AmmoPredictionSwitchCommitTime;
	AmmoPredictionWindowAcceptedCount = 0;
	if (WindowStartTime >= 0.0f && AmmoPredictionClientLearnedSwitchTime >= 0.0f)
	{
		for (const FShooterFireActivationContextForTest& Context : AuthorityAggregate.ActivationContexts)
		{
			const bool bInWindow = Context.bAuthority && Context.LocalTime >= WindowStartTime &&
				Context.LocalTime <= AmmoPredictionClientLearnedSwitchTime;
			AmmoPredictionWindowAcceptedCount += bInWindow ? 1 : 0;
		}
	}

	const int32 ClientActivationDelta = Sample.OwnerWindowActivationCount;
	const int32 AcceptedDelta = AmmoPredictionAuthorityActivations - AmmoPredictionAuthorityActivationsBefore;
	const int32 RejectDelta = AmmoPredictionAuthorityRejects - AmmoPredictionAuthorityRejectsBefore;
	const int32 ContextAuthorityShots = ContextWeapon->GetAuthorityShotCountForAutomationTest();
	const int32 ContextShots = ContextAuthorityShots - AmmoPredictionAuthorityShotsBefore;
	AShooterWeapon* TargetWeapon = AmmoPredictionSwitchTargetWeapon.Get();
	const int32 TargetShots = TargetWeapon ? TargetWeapon->GetAuthorityShotCountForAutomationTest() : INDEX_NONE;

	// 判定三态，不允许"没证据"被读成"通过"：
	// VIOLATION：出现过跨武器提交 / 跨武器裁决 / 账目未收敛——目标不变量被破坏；
	// VERIFIED：错位窗口内确实有请求到达服务器（被接受或被拒绝），且它们全部由正确武器处理、账目全部收敛；
	// NOT_EXERCISED：窗口内一次到达都没有——这次运行没有验证到目标不变量，必须显式失败。
	const bool bNoContextMismatch = AmmoPredictionContextMismatchKeyCount == 0;
	const bool bNoCrossWeaponVerdict = Sample.WeaponContextMismatchVerdictCount == 0;
	const bool bOwnerConverged = Sample.UnresolvedShotRecordCount == 0 &&
		Sample.ContextWeaponUnresolvedRecords == 0 && Sample.ContextWeaponPendingShots == 0 &&
		Sample.ContextWeaponUnsettledDisplayCount == 0 && Sample.PendingShots == 0;
	const bool bViolation = !bNoContextMismatch || !bNoCrossWeaponVerdict || !bOwnerConverged;
	// 夹具前提：两端至少有一个键能对齐，否则"逐键比对"本身没有发生过。
	const bool bFixtureAligned = AmmoPredictionContextMatchedKeyCount > 0 &&
		ClientActivationDelta >= WeaponContextMinRequests && bAmmoPredictionSwitchObserved;
	const bool bWindowReached = AmmoPredictionWindowAcceptedCount > 0 || AmmoPredictionRejectsAfterSwitchCommit > 0;
	const bool bVerified = !bViolation && bFixtureAligned && bWindowReached;
	const bool bConverged = bVerified;
	const TCHAR* WindowStatus = bViolation
		? TEXT("VIOLATION")
		: (bVerified ? TEXT("VERIFIED") : TEXT("NOT_EXERCISED"));

	UE_LOG(LogShootGame, Display,
		TEXT("WEAPON_CONTEXT_WINDOW Status=%s CommitTime=%.6f ClientLearnedTime=%.6f WindowMs=%.3f ")
		TEXT("WindowAccepted=%d WindowRejects=%d ClientActivations=%d FixtureAligned=%d"),
		WindowStatus,
		AmmoPredictionSwitchCommitTime,
		AmmoPredictionClientLearnedSwitchTime,
		(AmmoPredictionClientLearnedSwitchTime - WindowStartTime) * 1000.0f,
		AmmoPredictionWindowAcceptedCount,
		AmmoPredictionRejectsAfterSwitchCommit,
		ClientActivationDelta,
		bFixtureAligned ? 1 : 0);

	UE_LOG(LogShootGame, Display,
		TEXT("WEAPON_CONTEXT_RESULT Case=SwitchInFlight WindowStatus=%s Context=%s Target=%s ")
		TEXT("MatchedKeys=%d MismatchKeys=%d ClientOnlyKeys=%d AuthorityOnlyKeys=%d ")
		TEXT("CrossWeaponVerdicts=%d LastVerdictWeapon=%s ")
		TEXT("ClientActivations=%d Accepted=%d Rejects=%d ContextShots=%d TargetShots=%d ")
		TEXT("Records=%d ContextRecords=%d ContextPending=%d ContextUnsettledDisplay=%d CurrentPending=%d ")
		TEXT("WindowAccepted=%d WindowRejects=%d ContextHudMag=%d ContextAuthorityMag=%d"),
		WindowStatus,
		*GetNameSafe(ContextWeapon),
		*GetNameSafe(TargetWeapon),
		AmmoPredictionContextMatchedKeyCount,
		AmmoPredictionContextMismatchKeyCount,
		AmmoPredictionContextClientOnlyKeyCount,
		AmmoPredictionContextAuthorityOnlyKeyCount,
		Sample.WeaponContextMismatchVerdictCount,
		*Sample.LastVerdictSourceWeaponId.ToString(),
		ClientActivationDelta,
		AcceptedDelta,
		RejectDelta,
		ContextShots,
		TargetShots,
		Sample.UnresolvedShotRecordCount,
		Sample.ContextWeaponUnresolvedRecords,
		Sample.ContextWeaponPendingShots,
		Sample.ContextWeaponUnsettledDisplayCount,
		Sample.PendingShots,
		AmmoPredictionWindowAcceptedCount,
		AmmoPredictionRejectsAfterSwitchCommit,
		Sample.ContextWeaponMagazineAmmo,
		ContextWeapon->GetBulletCount());

	if (!bConverged)
	{
		const TCHAR* MismatchText = MismatchDetail.IsEmpty() ? TEXT("-") : *MismatchDetail;
		UE_LOG(LogShootGame, Display, TEXT("WEAPON_CONTEXT_MISMATCH_DETAIL %s"), MismatchText);
	}

	const FString Detail = FString::Printf(
		TEXT("WindowStatus=%s WindowAccepted=%d WindowMs=%.3f Context=%s Target=%s ")
		TEXT("Matched=%d Mismatch=%d ClientOnly=%d AuthOnly=%d ")
		TEXT("CrossVerdicts=%d ClientAct=%d Accepted=%d Rejects=%d ContextShots=%d TargetShots=%d ")
		TEXT("Records=%d ContextRecords=%d ContextPending=%d ContextUnsettled=%d CurrentPending=%d"),
		WindowStatus,
		AmmoPredictionWindowAcceptedCount,
		(AmmoPredictionClientLearnedSwitchTime - WindowStartTime) * 1000.0f,
		*GetNameSafe(ContextWeapon),
		*GetNameSafe(TargetWeapon),
		AmmoPredictionContextMatchedKeyCount,
		AmmoPredictionContextMismatchKeyCount,
		AmmoPredictionContextClientOnlyKeyCount,
		AmmoPredictionContextAuthorityOnlyKeyCount,
		Sample.WeaponContextMismatchVerdictCount,
		ClientActivationDelta,
		AcceptedDelta,
		RejectDelta,
		ContextShots,
		TargetShots,
		Sample.UnresolvedShotRecordCount,
		Sample.ContextWeaponUnresolvedRecords,
		Sample.ContextWeaponPendingShots,
		Sample.ContextWeaponUnsettledDisplayCount,
		Sample.PendingShots);
	ConcludeAmmoPredictionCase(TEXT("WeaponContextSwitchInFlight"), bConverged, Detail);
	StartAmmoPredictionStep(StepSpecRemovalInFlight);
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionSpecRemovalStep()
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterCharacter* Subject = AmmoPredictionSubject.Get();
	AShooterWeapon* RemovedWeapon = AmmoPredictionWeapon.Get();
	UShooterInventoryComponent* Inventory = Subject ? Subject->GetInventoryComponent() : nullptr;
	AShooterPlayerState* SubjectPlayerState = Subject ? Subject->GetPlayerState<AShooterPlayerState>() : nullptr;
	if (!Subject || !RemovedWeapon || !Inventory || !SubjectPlayerState)
	{
		FailTest(TEXT("Spec removal case lost the subject, weapon or inventory"));
		bAmmoPredictionFinished = true;
		return;
	}

	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;
	const float Now = GetWorld()->GetTimeSeconds();

	if (!bAmmoPredictionStepCommandSent)
	{
		if (!IsAmmoPredictionFixtureReady(StepSpecRemovalInFlight, 0))
		{
			return;
		}

		// 前提：这把武器必须由真实 Inventory 持有，且它的 Fire Spec 存在（H1 / Handle）。
		const FGameplayAbilitySpec* RemovedSpec = SubjectPlayerState->FindFireAbilitySpecForWeapon(RemovedWeapon);
		AmmoPredictionRemovedWeapon = RemovedWeapon;
		AmmoPredictionRemovedSpecHandle = RemovedSpec ? RemovedSpec->Handle : FGameplayAbilitySpecHandle();
		AmmoPredictionRemovedAuthorityShotsBefore = RemovedWeapon->GetAuthorityShotCountForAutomationTest();
		AmmoPredictionRemovedAmmoBefore = RemovedWeapon->GetBulletCount();

		if (!RemovedSpec || !Inventory->ContainsWeapon(RemovedWeapon))
		{
			FailTest(FString::Printf(TEXT("Spec removal premise failed: Weapon=%s HasSpec=%s Held=%s"),
				*GetNameSafe(RemovedWeapon),
				RemovedSpec ? TEXT("true") : TEXT("false"),
				Inventory->ContainsWeapon(RemovedWeapon) ? TEXT("true") : TEXT("false")));
			bAmmoPredictionFinished = true;
			return;
		}

		bAmmoPredictionStepCommandSent = true;
		AmmoPredictionRemovalIssued = false;
		AmmoPredictionRemovalTime = Now + SpecRemovalInFlightDelaySeconds;
		AmmoPredictionMaxClientPendingDuringWindow = 0;
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("SPEC_REMOVAL_SETUP Step=%d Weapon=%s Handle=%s ShotsBefore=%d AmmoBefore=%d ClientsPending=%d"),
			StepSpecRemovalInFlight,
			*GetNameSafe(RemovedWeapon),
			*AmmoPredictionRemovedSpecHandle.ToString(),
			AmmoPredictionRemovedAuthorityShotsBefore,
			AmmoPredictionRemovedAmmoBefore,
			Sample.PendingShots);
		ClientSubmitAmmoPredictionFire(StepSpecRemovalInFlight);
		return;
	}

	// 窗口内拥有端最大的本地 Pending：它证明"移除发生时请求确实还在途、债务确实存在"。
	AmmoPredictionMaxClientPendingDuringWindow = FMath::Max(AmmoPredictionMaxClientPendingDuringWindow, Sample.PendingShots);

	// 相位 1：在请求到达服务器之前，走真实 Inventory Remove 路径移除这把武器。
	// 这条路径就是 Pickup / 死亡清理使用的生产入口：广播 → 撤销 Fire Spec → 归还池。
	if (!AmmoPredictionRemovalIssued && Now >= AmmoPredictionRemovalTime)
	{
		AmmoPredictionRemovalIssued = true;
		const bool bRemoved = Inventory->RemoveWeapon(RemovedWeapon);
		AmmoPredictionRemovalSucceeded = bRemoved && SubjectPlayerState->FindFireAbilitySpecForWeapon(RemovedWeapon) == nullptr;

		// 夹具引脚必须跟随"仍然持有的那一把"：被移除的武器已经回池，不再是合法引脚。
		for (const FShooterInventoryWeaponEntry& Entry : Inventory->GetWeaponEntries())
		{
			if (AShooterWeapon* Remaining = Entry.Weapon.Get())
			{
				AmmoPredictionWeapon = Remaining;
				break;
			}
		}
		UE_LOG(
			LogShootGame,
			Display,
			TEXT("SPEC_REMOVAL_ISSUED Step=%d Weapon=%s Removed=%d SpecRevoked=%d HeldWeapons=%d"),
			StepSpecRemovalInFlight,
			*GetNameSafe(RemovedWeapon),
			bRemoved ? 1 : 0,
			AmmoPredictionRemovalSucceeded ? 1 : 0,
			Inventory->GetWeaponCount());
	}

	// 相位 2：等拥有端观察到"已经不再持有这把武器、也不再持有它的 Fire Spec"，并留出结算窗口。
	if (!bAmmoPredictionSettleStarted)
	{
		// 拥有端的 Fire Spec 数必须收敛到"服务器仍持有的武器数"，且被移除的武器不再是它的当前武器。
		const int32 HeldWeaponCountNow = Inventory->GetWeaponCount();
		const bool bClientObservedRemoval = IsAmmoPredictionClientSampleFresh(StepSpecRemovalInFlight) &&
			Sample.FireSpecCount == HeldWeaponCountNow && Subject->GetCurrentWeaponActor() != RemovedWeapon;
		if (!bClientObservedRemoval)
		{
			return;
		}
		AmmoPredictionSettleStartTime = Now;
		bAmmoPredictionSettleStarted = true;
		return;
	}

	if (!IsAmmoPredictionClientSampleFresh(StepSpecRemovalInFlight) || Now - AmmoPredictionSettleStartTime < SpecRemovalSettleSeconds)
	{
		return;
	}

	// ---- 判定 ----
	// 不变量：旧 Spec 生命周期结束时，它自己留下的本地预测债务必须清零，且不能有任何权威结果。
	// 注意：移除之后 AmmoPredictionWeapon 引脚已经跟随"仍然持有的那一把"，
	// 因此这里所有针对被移除武器的判据都必须用 AmmoPredictionRemovedWeapon。
	AShooterWeapon* PinnedRemovedWeapon = AmmoPredictionRemovedWeapon.Get();
	if (!PinnedRemovedWeapon)
	{
		FailTest(TEXT("Spec removal case lost the removed weapon reference"));
		bAmmoPredictionFinished = true;
		return;
	}

	const int32 RemovedShotCount = PinnedRemovedWeapon->GetAuthorityShotCountForAutomationTest();
	const int32 AuthorityShotDelta = RemovedShotCount - AmmoPredictionRemovedAuthorityShotsBefore;
	const bool bNoAuthorityShot = AuthorityShotDelta == 0;
	const bool bAmmoUntouched = PinnedRemovedWeapon->GetBulletCount() == AmmoPredictionRemovedAmmoBefore;
	// 武器级证据：Pending 归零 = 退款恰好一次；HUD unsettled 归零 = 显示预测已结清。
	// 预测弹药必须等于真实弹匣：多退一次会让预测值大于真值，这个等式同时守住"只退一次"。
	const bool bWeaponDebtCleared = Sample.ContextWeaponPendingShots == 0 && Sample.ContextWeaponUnsettledDisplayCount == 0 &&
		PinnedRemovedWeapon->GetPendingPredictedShots() == 0 && PinnedRemovedWeapon->GetUnsettledAmmoDisplayCount() == 0 &&
		PinnedRemovedWeapon->GetPredictedMagazineAmmo() == PinnedRemovedWeapon->GetBulletCount();
	// 拥有端整体：没有遗留记录、Spec 数等于仍然持有的武器数、也没有跨武器裁决。
	const int32 HeldWeaponCountAfterRemoval = Inventory->GetWeaponCount();
	const bool bOwnerCleared = Sample.UnresolvedShotRecordCount == 0 &&
		Sample.FireSpecCount == HeldWeaponCountAfterRemoval && Sample.UnresolvedSourceSpecCount == 0 &&
		Sample.WeaponContextMismatchVerdictCount == 0;
	// Spec Removal 的收口确实在这把武器上执行过（无论当时是否还有记录可清）。
	const bool bCleanupObserved = Sample.ContextWeaponSpecRemovalCleanupCount >= 1;
	// 窗口前提：请求在移除发生时仍在途（拥有端 Pending 曾 > 0）。
	const bool bWindowExercised = AmmoPredictionMaxClientPendingDuringWindow > 0 && AmmoPredictionRemovalSucceeded;

	// 注意：武器被移除时会归还池，池的归还回调会恢复该武器的初始弹药（RestoreInitialAmmo），
	// 因此"弹药数值不变"不是移除场景的有效断言；真正的判据是"没有产生任何权威 Shot"。
	const bool bViolation = !AmmoPredictionRemovalSucceeded || !bNoAuthorityShot ||
		!bWeaponDebtCleared || !bOwnerCleared || (bWindowExercised && !bCleanupObserved);
	const bool bVerified = !bViolation && bWindowExercised;
	const TCHAR* Status = bViolation ? TEXT("VIOLATION") : (bVerified ? TEXT("VERIFIED") : TEXT("NOT_EXERCISED"));

	UE_LOG(LogShootGame, Display,
		TEXT("SPEC_REMOVAL_RESULT Status=%s Weapon=%s Handle=%s WindowExercised=%d MaxClientPending=%d ")
		TEXT("SpecRevoked=%d Cleanup=%d Unbinds=%d AuthorityShotDelta=%d AmmoBefore=%d AmmoAfter=%d ")
		TEXT("WeaponPending=%d WeaponUnsettled=%d Records=%d FireSpecs=%d HeldWeapons=%d UnresolvedSource=%d ")
		TEXT("CrossVerdicts=%d"),
		Status,
		*GetNameSafe(PinnedRemovedWeapon),
		*AmmoPredictionRemovedSpecHandle.ToString(),
		bWindowExercised ? 1 : 0,
		AmmoPredictionMaxClientPendingDuringWindow,
		AmmoPredictionRemovalSucceeded ? 1 : 0,
		Sample.ContextWeaponSpecRemovalCleanupCount,
		Sample.ContextWeaponSpecRemovalUnbindCount,
		AuthorityShotDelta,
		AmmoPredictionRemovedAmmoBefore,
		PinnedRemovedWeapon->GetBulletCount(),
		Sample.ContextWeaponPendingShots,
		Sample.ContextWeaponUnsettledDisplayCount,
		Sample.UnresolvedShotRecordCount,
		Sample.FireSpecCount,
		HeldWeaponCountAfterRemoval,
		Sample.UnresolvedSourceSpecCount,
		Sample.WeaponContextMismatchVerdictCount);

	const FString Detail = FString::Printf(
		TEXT("Status=%s Window=%d MaxPending=%d SpecRevoked=%d Cleanup=%d Unbinds=%d AuthShotDelta=%d ")
		TEXT("Ammo=%d->%d WeaponPending=%d WeaponUnsettled=%d Records=%d Specs=%d Held=%d UnresolvedSource=%d"),
		Status,
		bWindowExercised ? 1 : 0,
		AmmoPredictionMaxClientPendingDuringWindow,
		AmmoPredictionRemovalSucceeded ? 1 : 0,
		Sample.ContextWeaponSpecRemovalCleanupCount,
		Sample.ContextWeaponSpecRemovalUnbindCount,
		AuthorityShotDelta,
		AmmoPredictionRemovedAmmoBefore,
		PinnedRemovedWeapon->GetBulletCount(),
		Sample.ContextWeaponPendingShots,
		Sample.ContextWeaponUnsettledDisplayCount,
		Sample.UnresolvedShotRecordCount,
		Sample.FireSpecCount,
		HeldWeaponCountAfterRemoval,
		Sample.UnresolvedSourceSpecCount);
	// 本用例不能用 ConcludeAmmoPredictionCase：它要求"当前武器 HUD == 权威值"，
	// 而这个场景的终态正是"拥有端一把武器都不持有"，HUD 断言没有意义。
	// 收敛 / 不一致仍然走同一组标记与计数，DONE 行口径保持一致。
	if (bVerified)
	{
		++AmmoPredictionConvergedCount;
		UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_CONVERGED Case=SpecRemovalInFlight %s"), *Detail);
	}
	else
	{
		++AmmoPredictionMismatchCount;
		UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_MISMATCH Case=SpecRemovalInFlight %s"), *Detail);
	}
	StartAmmoPredictionStep(StepPoolReuseRebind);
}

void AShooterNetworkTestCoordinator::RunAmmoPredictionPoolReuseStep()
{
	using namespace ShooterAmmoPredictionNetworkTests;

	AShooterCharacter* Subject = AmmoPredictionSubject.Get();
	AShooterWeapon* RemovedWeapon = AmmoPredictionRemovedWeapon.Get();
	UShooterInventoryComponent* Inventory = Subject ? Subject->GetInventoryComponent() : nullptr;
	UShooterEquipmentComponent* Equipment = Subject ? Subject->GetEquipmentComponent() : nullptr;
	UShooterWeaponRuntimeSubsystem* Runtime = GetWorld()->GetSubsystem<UShooterWeaponRuntimeSubsystem>();
	AShooterPlayerState* SubjectPlayerState = Subject ? Subject->GetPlayerState<AShooterPlayerState>() : nullptr;
	if (!Subject || !RemovedWeapon || !Inventory || !Equipment || !Runtime || !SubjectPlayerState)
	{
		FailTest(TEXT("Pool reuse case lost the subject, weapon, inventory or runtime"));
		bAmmoPredictionFinished = true;
		return;
	}

	const FShooterAmmoPredictionObservation& Sample = AmmoPredictionLatest;
	const float Now = GetWorld()->GetTimeSeconds();

	if (!bAmmoPredictionStepCommandSent)
	{
		// 同一 Owner 立刻再租用同一把武器：ReleaseWeapon 把 Actor 压入 AvailableActors，
		// AcquireWeapon 从同一端 Pop，因此"刚归还的那一把"会被首先取回。
		AShooterWeapon* Reacquired = Runtime->AcquireWeapon(RemovedWeapon->GetWeaponId(), Subject, Subject);
		AShooterWeapon* Added = nullptr;
		if (Reacquired && Inventory->AddWeapon(Reacquired) == EShooterInventoryAddResult::Added)
		{
			Added = Reacquired;
			Equipment->EquipWeapon(Reacquired);
			// 夹具引脚跟随"当前武器"：复用后的这把 Actor 就是当前武器，
			// 后续的 HUD / 弹药断言因此落在它身上，而不是上一步留下的旧引脚。
			AmmoPredictionWeapon = Reacquired;
			AmmoPredictionContextWeaponClient = Reacquired;
		}
		else if (Reacquired)
		{
			// 入背包失败必须归还，否则池里留下一个没有持有者的租约。
			Runtime->ReleaseWeapon(Reacquired);
		}

		const FGameplayAbilitySpec* NewSpec = SubjectPlayerState->FindFireAbilitySpecForWeapon(Added);
		AmmoPredictionReusedWeapon = Added;
		AmmoPredictionReusedSpecHandle = NewSpec ? NewSpec->Handle : FGameplayAbilitySpecHandle();
		AmmoPredictionReusedSameActor = Added == RemovedWeapon;
		AmmoPredictionReusedAuthorityShotsBefore = Added ? Added->GetAuthorityShotCountForAutomationTest() : INDEX_NONE;
		AmmoPredictionReusedAmmoBefore = Added ? Added->GetBulletCount() : INDEX_NONE;
		bAmmoPredictionStepCommandSent = true;

		UE_LOG(LogShootGame, Display,
			TEXT("POOL_REUSE_SETUP Step=%d Actor=%s SameActor=%d OldHandle=%s NewHandle=%s OldOwner=%s NewOwner=%s ")
			TEXT("Ammo=%d"),
			StepPoolReuseRebind,
			*GetNameSafe(Added),
			AmmoPredictionReusedSameActor ? 1 : 0,
			*AmmoPredictionRemovedSpecHandle.ToString(),
			*AmmoPredictionReusedSpecHandle.ToString(),
			*GetNameSafe(RemovedWeapon->GetOwner()),
			*GetNameSafe(Added ? Added->GetOwner() : nullptr),
			AmmoPredictionReusedAmmoBefore);

		if (!Added || !NewSpec || NewSpec->Handle == AmmoPredictionRemovedSpecHandle)
		{
			FailTest(FString::Printf(
				TEXT("Pool reuse premise failed: Actor=%s NewSpec=%s OldHandle=%s NewHandle=%s"),
				*GetNameSafe(Added),
				NewSpec ? TEXT("true") : TEXT("false"),
				*AmmoPredictionRemovedSpecHandle.ToString(),
				*AmmoPredictionReusedSpecHandle.ToString()));
			bAmmoPredictionFinished = true;
			return;
		}
		return;
	}

	// 相位 1：等拥有端看到新生命周期（当前武器 = 这把 Actor，且它自己的 Fire Spec 已建立）。
	if (!AmmoPredictionReuseShotSubmitted)
	{
		const int32 HeldWeaponCountNow = Inventory->GetWeaponCount();
		const bool bOwnerReady = IsAmmoPredictionClientSampleFresh(StepPoolReuseRebind) && Sample.bValid &&
			Sample.FireSpecCount == HeldWeaponCountNow && Sample.FireInstanceCount == HeldWeaponCountNow &&
			Sample.UnresolvedSourceSpecCount == 0 && AmmoPredictionReusedWeapon->GetOwner() == Subject &&
			Sample.CurrentWeaponId == AmmoPredictionReusedWeapon->GetWeaponId() &&
			Sample.UnresolvedShotRecordCount == 0;
		if (!bOwnerReady)
		{
			if (Now - AmmoPredictionStepStartTime > 10.0f)
			{
				const TCHAR* const bValidText = Sample.bValid ? TEXT("true") : TEXT("false");
				FailTest(FString::Printf(TEXT("Pool reuse owner saw no new lifecycle: Specs=%d Instances=%d Valid=%s ")
					TEXT("Records=%d"),
					Sample.FireSpecCount, Sample.FireInstanceCount, bValidText, Sample.UnresolvedShotRecordCount));
				bAmmoPredictionFinished = true;
			}
			return;
		}

		AmmoPredictionReuseShotSubmitted = true;
		AmmoPredictionReuseSettleStartTime = Now;
		ClientSubmitAmmoPredictionFire(StepPoolReuseRebind);
		return;
	}

	// 相位 2：等新生命周期的这一发被权威接受并结清。
	const int32 AuthorityShotDelta = AmmoPredictionReusedWeapon->GetAuthorityShotCountForAutomationTest() -
		AmmoPredictionReusedAuthorityShotsBefore;
	if (AuthorityShotDelta < 1)
	{
		return;
	}

	if (!bAmmoPredictionSettleStarted)
	{
		AmmoPredictionSettleStartTime = Now;
		bAmmoPredictionSettleStarted = true;
		return;
	}

	if (!IsAmmoPredictionClientSampleFresh(StepPoolReuseRebind) || Now - AmmoPredictionSettleStartTime < SpecRemovalSettleSeconds)
	{
		return;
	}

	// ---- 判定 ----
	// 新生命周期必须完全正常：旧 Spec 的撤销 / 清理 / 迟到 Reject 都不得干扰 H2。
	// 这里用当前武器（= 复用后的同一把 Actor）自己的 HUD 结清计数，而不是"上下文武器"计数：
	// 202 步骤的上下文武器在客户端准备时可能为空（那一步正处在移除之后的空窗）。
	// Owner historical replay 已被契约禁止，因此这里只要求"这一发被接受并结清"，
	// 并额外观察禁止的 replay 统计保持为零。
	const bool bNewShotResolved = AuthorityShotDelta == 1 && Sample.PendingShots == 0 &&
		Sample.UnresolvedShotRecordCount == 0 && Sample.HudUnsettledCount == 0 &&
		Sample.WeaponContextMismatchVerdictCount == 0 && Sample.bLastResolvedShotCommitted;
	const bool bNewAmmoCorrect = AmmoPredictionReusedAmmoBefore - AmmoPredictionReusedWeapon->GetBulletCount() == 1;
	const bool bSpecIdentityOk = Sample.FireSpecCount == Inventory->GetWeaponCount() && Sample.FireInstanceCount > 0 &&
		Sample.UnresolvedSourceSpecCount == 0 && AmmoPredictionReusedSpecHandle != AmmoPredictionRemovedSpecHandle;
	const bool bViolation = !bNewShotResolved || !bNewAmmoCorrect || !bSpecIdentityOk;
	const bool bVerified = !bViolation && AmmoPredictionReusedSameActor;
	const TCHAR* Status = bViolation ? TEXT("VIOLATION") : (bVerified ? TEXT("VERIFIED") : TEXT("NOT_EXERCISED"));

	UE_LOG(LogShootGame, Display,
		TEXT("POOL_REUSE_RESULT Status=%s SameActor=%d OldHandle=%s NewHandle=%s AuthShotDelta=%d ")
		TEXT("Ammo=%d->%d Pending=%d Records=%d HudUnsettled=%d Specs=%d CrossVerdicts=%d ")
		TEXT("ShotResolved=%d AmmoOk=%d SpecOk=%d LastCommitted=%d ConfirmedReplayDelta=%d HeldWeapons=%d"),
		Status,
		AmmoPredictionReusedSameActor ? 1 : 0,
		*AmmoPredictionRemovedSpecHandle.ToString(),
		*AmmoPredictionReusedSpecHandle.ToString(),
		AuthorityShotDelta,
		AmmoPredictionReusedAmmoBefore,
		AmmoPredictionReusedWeapon->GetBulletCount(),
		Sample.PendingShots,
		Sample.UnresolvedShotRecordCount,
		Sample.HudUnsettledCount,
		Sample.FireSpecCount,
		Sample.WeaponContextMismatchVerdictCount,
		bNewShotResolved ? 1 : 0,
		bNewAmmoCorrect ? 1 : 0,
		bSpecIdentityOk ? 1 : 0,
		Sample.bLastResolvedShotCommitted ? 1 : 0,
		Sample.OwnerConfirmedReplayCount - AmmoPredictionBefore.OwnerConfirmedReplayCount,
		Inventory->GetWeaponCount());

	const FString Detail = FString::Printf(
		TEXT("Status=%s SameActor=%d AuthShotDelta=%d Pending=%d Records=%d Unsettled=%d Specs=%d ")
		TEXT("CrossVerdicts=%d CleanupUnbinds=%d"),
		Status,
		AmmoPredictionReusedSameActor ? 1 : 0,
		AuthorityShotDelta,
		Sample.PendingShots,
		Sample.UnresolvedShotRecordCount,
		Sample.ContextWeaponUnsettledDisplayCount,
		Sample.FireSpecCount,
		Sample.WeaponContextMismatchVerdictCount,
		Sample.ContextWeaponSpecRemovalUnbindCount);
	ConcludeAmmoPredictionCase(TEXT("PoolReuseSpecRebind"), bVerified, Detail);
	StartAmmoPredictionStep(StepDone);
}

void AShooterNetworkTestCoordinator::HandleAmmoPredictionEquippedWeaponChanged(AShooterWeapon* PreviousWeapon,
	AShooterWeapon* CurrentWeapon)
{
	using namespace ShooterAmmoPredictionNetworkTests;

	if (!bWeaponContextMode || !HasAuthority() || AmmoPredictionServerStep != StepWeaponContextSwitch)
	{
		return;
	}

	// 装备事务提交时刻：这是错位窗口的精确起点（服务器此刻起把请求当成新武器处理）。
	AmmoPredictionEquipCommitTime = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f;
	UE_LOG(LogShootGame, Display, TEXT("WEAPON_CONTEXT_EQUIP_COMMIT Step=%d Previous=%s Current=%s"),
		AmmoPredictionServerStep, *GetNameSafe(PreviousWeapon), *GetNameSafe(CurrentWeapon));
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
	const int32 ConfirmedReplayDelta = Sample.OwnerConfirmedReplayCount - AmmoPredictionBefore.OwnerConfirmedReplayCount;
	const int32 ReplayRequestBefore = AmmoPredictionBefore.OwnerConfirmedReplayRequestCount;
	const int32 ReplayRequestDelta = Sample.OwnerConfirmedReplayRequestCount - ReplayRequestBefore;
	// 夹具把拥有端预算固定在弹匣容量上（预算 0）：本步骤结束时它必须完全没被动过。
	const int32 InjectedPending = Weapon->GetMagazineSize();
	const int32 PendingDelta = Sample.PendingShots - InjectedPending;
	const int32 VerdictDelta = Sample.ShotVerdictCount - AmmoPredictionBefore.ShotVerdictCount;
	const int32 MontageDelta = Sample.MontageCount - AmmoPredictionBefore.MontageCount;
	const int32 MuzzleDelta = Sample.MuzzleCount - AmmoPredictionBefore.MuzzleCount;
	const int32 SoundDelta = Sample.SoundCount - AmmoPredictionBefore.SoundCount;
	const int32 RecoilDelta = Sample.RecoilCount - AmmoPredictionBefore.RecoilCount;

	// 预算为 0：拥有端不提前表现、不增加 Pending，但请求照旧到达服务器；
	// 服务器接受并真实提交这一发，Committed 只结算状态，不插入迟到的 Owner cosmetic。
	const bool bConverged = ShotDelta == 1 && AcceptedDelta == 1 && ClientActivationDelta == 1 &&
		PredictedDelta == 0 && ConfirmedReplayDelta == 0 && ReplayRequestDelta == 0 && PendingDelta == 0 &&
		VerdictDelta == 1 && Sample.PredictedMagazineAmmo == 0 &&
		MontageDelta == 0 && MuzzleDelta == 0 && SoundDelta == 0 && RecoilDelta == 0 &&
		Sample.UnresolvedShotRecordCount == 0 && Sample.bLastResolvedShotCommitted &&
		Sample.LastResolvedShotKey == Sample.FirstPredictionKey &&
		Weapon->GetBulletCount() == AmmoPredictionMagazineBefore - 1;
	const FString Detail = FString::Printf(
		TEXT("Shots=%d Accepted=%d ClientAct=%d Predicted=%d ConfirmedReplay=%d ReplayReq=%d Pending=%d ")
		TEXT("Verdicts=%d PredictedMag=%d Channels(M=%d Z=%d S=%d R=%d) Records=%d LastKey=%d"),
		ShotDelta,
		AcceptedDelta,
		ClientActivationDelta,
		PredictedDelta,
		ConfirmedReplayDelta,
		ReplayRequestDelta,
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
	const int32 ConfirmedReplayDelta = Sample.OwnerConfirmedReplayCount - AmmoPredictionBefore.OwnerConfirmedReplayCount;
	const int32 ReplayRequestBefore = AmmoPredictionBefore.OwnerConfirmedReplayRequestCount;
	const int32 ReplayRequestDelta = Sample.OwnerConfirmedReplayRequestCount - ReplayRequestBefore;
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
		ClientActivationDelta == 1 && PredictedDelta == 0 && ConfirmedReplayDelta == 0 && ReplayRequestDelta == 0 &&
		PendingDelta == 0 && MontageDelta == 0 && MuzzleDelta == 0 && SoundDelta == 0 && RecoilDelta == 0 &&
		Sample.PredictedMagazineAmmo == 0 && Sample.UnresolvedShotRecordCount == 0 &&
		Sample.MagazineAmmo == AmmoPredictionMagazineBefore && !Sample.bLastResolvedShotCommitted &&
		Sample.bLastResolvedShotRejectedByEngine && Sample.LastResolvedShotKey == Sample.FirstPredictionKey;
	const FString Detail = FString::Printf(
		TEXT("Shots=%d Accepted=%d ServerRejects=%d Projectiles=%d ClientAct=%d Predicted=%d ConfirmedReplay=%d ")
		TEXT("Pending=%d Channels(M=%d Z=%d S=%d R=%d) Records=%d LastKey=%d ByEngine=%d"),
		ShotDelta,
		AcceptedDelta,
		RejectedDelta,
		ProjectileDelta,
		ClientActivationDelta,
		PredictedDelta,
		ConfirmedReplayDelta,
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
		AmmoPredictionSemiAutoWeapon = SemiAutoWeapon;
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

	// ---- 每把持有的武器各有一份 Fire Spec：换枪后两端都必须保住"武器 → Spec"的映射 ----
	// 这一段同时覆盖 §十二 的 Weapon Add（各自一份）与切枪后身份不被复用：
	// - 服务器：Rifle 与 AWP 各有一份 Spec，SourceObject 各自对应，Handle 不同；
	// - 拥有端：它自己看到的 Spec 数必须等于它持有的武器数，且没有 SourceObject 未解析的 Spec。
	const int32 ServerHeldWeaponCount = Inventory ? Inventory->GetWeaponCount() : INDEX_NONE;
	AShooterPlayerState* SubjectPlayerState = Subject ? Subject->GetPlayerState<AShooterPlayerState>() : nullptr;
	const FGameplayAbilitySpec* ServerContextSpec = SubjectPlayerState
		? SubjectPlayerState->FindFireAbilitySpecForWeapon(AmmoPredictionContextWeaponServer.Get())
		: nullptr;
	const FGameplayAbilitySpec* ServerSemiAutoSpec = SubjectPlayerState
		? SubjectPlayerState->FindFireAbilitySpecForWeapon(AmmoPredictionSemiAutoWeapon.Get())
		: nullptr;
	const int32 ServerFireSpecCount = SubjectPlayerState ? SubjectPlayerState->GetFireAbilitySpecCount() : INDEX_NONE;
	const bool bServerPerWeaponSpecs = ServerFireSpecCount == ServerHeldWeaponCount && ServerFireSpecCount >= 2 &&
		ServerContextSpec && ServerSemiAutoSpec &&
		ServerContextSpec->SourceObject.Get() == AmmoPredictionContextWeaponServer.Get() &&
		ServerSemiAutoSpec->SourceObject.Get() == AmmoPredictionSemiAutoWeapon.Get() &&
		ServerContextSpec->Handle != ServerSemiAutoSpec->Handle;
	const bool bOwnerPerWeaponSpecs = Sample.FireSpecCount == ServerHeldWeaponCount &&
		Sample.FireInstanceCount > 0 && Sample.UnresolvedSourceSpecCount == 0;

	UE_LOG(LogShootGame, Display,
		TEXT("FIRE_SPEC_PER_WEAPON ServerSpecs=%d ServerHeldWeapons=%d ServerIdentity=%d ")
		TEXT("OwnerSpecs=%d OwnerInstances=%d OwnerUnresolvedSource=%d OwnerIdentity=%d ")
		TEXT("ContextWeapon=%s SemiAutoWeapon=%s ContextHandle=%s SemiAutoHandle=%s"),
		ServerFireSpecCount,
		ServerHeldWeaponCount,
		bServerPerWeaponSpecs ? 1 : 0,
		Sample.FireSpecCount,
		Sample.FireInstanceCount,
		Sample.UnresolvedSourceSpecCount,
		bOwnerPerWeaponSpecs ? 1 : 0,
		*GetNameSafe(AmmoPredictionContextWeaponServer.Get()),
		*GetNameSafe(AmmoPredictionSemiAutoWeapon.Get()),
		ServerContextSpec ? *ServerContextSpec->Handle.ToString() : TEXT("null"),
		ServerSemiAutoSpec ? *ServerSemiAutoSpec->Handle.ToString() : TEXT("null"));

	if (!bServerPerWeaponSpecs || !bOwnerPerWeaponSpecs)
	{
		FailTest(FString::Printf(
			TEXT("Per-weapon Fire Spec identity invalid; ServerSpecs=%d HeldWeapons=%d ServerIdentity=%s ")
			TEXT("OwnerSpecs=%d OwnerInstances=%d OwnerUnresolvedSource=%d OwnerIdentity=%s"),
			ServerFireSpecCount,
			ServerHeldWeaponCount,
			bServerPerWeaponSpecs ? TEXT("true") : TEXT("false"),
			Sample.FireSpecCount,
			Sample.FireInstanceCount,
			Sample.UnresolvedSourceSpecCount,
			bOwnerPerWeaponSpecs ? TEXT("true") : TEXT("false")));
		bAmmoPredictionFinished = true;
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
	const int32 ConfirmedReplayDelta = Sample.OwnerConfirmedReplayCount - AmmoPredictionBefore.OwnerConfirmedReplayCount;
	const int32 PendingDelta = Sample.PendingShots - AmmoPredictionBefore.PendingShots;
	const int32 VerdictDelta = Sample.ShotVerdictCount - AmmoPredictionBefore.ShotVerdictCount;
	const int32 MagazineDelta = Weapon->GetBulletCount() - AmmoPredictionMagazineBefore;

	// 拥有端自述的武器身份：证据必须显示它跑在挑出的半自动行上，不能被读成全自动结果。
	const bool bOwnerFullAuto = Sample.bCurrentWeaponIsFullAuto;
	const bool bSemiAutoObserved = !bOwnerFullAuto && Sample.CurrentWeaponId == AmmoPredictionSemiAutoRowName;
	const bool bConverged = bSemiAutoObserved && ShotDelta == 1 && AcceptedDelta == 1 && RejectDelta == 0 &&
		ClientActivationDelta == 1 && Sample.DistinctPredictionKeyCount == 1 && VerdictDelta == 1 &&
		PredictedDelta == 1 && ConfirmedReplayDelta == 0 && PendingDelta == 0 && MagazineDelta == -1 &&
		Sample.UnresolvedShotRecordCount == 0 && !Sample.bFireActive && !Sample.bReloadActive &&
		Sample.LastResolvedShotKey == Sample.FirstPredictionKey && Sample.bLastResolvedShotCommitted;
	const FString Detail = FString::Printf(
		TEXT("Row=%s OwnerFullAuto=%d Shots=%d Accepted=%d Rejects=%d ClientAct=%d Keys=%d Verdicts=%d ")
		TEXT("Predicted=%d ConfirmedReplay=%d Pending=%d MagDelta=%d Records=%d LastKey=%d Committed=%d"),
		*Sample.CurrentWeaponId.ToString(),
		bOwnerFullAuto ? 1 : 0,
		ShotDelta,
		AcceptedDelta,
		RejectDelta,
		ClientActivationDelta,
		Sample.DistinctPredictionKeyCount,
		VerdictDelta,
		PredictedDelta,
		ConfirmedReplayDelta,
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
	const int32 ConfirmedReplayDelta = Sample.OwnerConfirmedReplayCount - AmmoPredictionBefore.OwnerConfirmedReplayCount;
	const int32 PendingDelta = Sample.PendingShots - AmmoPredictionBefore.PendingShots;
	const int32 VerdictDelta = Sample.ShotVerdictCount - AmmoPredictionBefore.ShotVerdictCount;
	const int32 MagazineDelta = Weapon->GetBulletCount() - AmmoPredictionMagazineBefore;

	// 半自动专属不变量：按住扳机绝不连发——整段窗口恰好一发、零拒绝、预算回到起点。
	const bool bOwnerFullAuto = Sample.bCurrentWeaponIsFullAuto;
	const bool bSemiAutoObserved = !bOwnerFullAuto && Sample.CurrentWeaponId == AmmoPredictionSemiAutoRowName;
	const bool bConverged = bSemiAutoObserved && ShotDelta == 1 && AcceptedDelta == 1 && RejectDelta == 0 &&
		ClientActivationDelta == 1 && Sample.DistinctPredictionKeyCount == 1 && VerdictDelta == 1 &&
		PredictedDelta == 1 && ConfirmedReplayDelta == 0 && PendingDelta == 0 && MagazineDelta == -1 &&
		Sample.bReleaseSettled && Sample.UnresolvedShotRecordCount == 0 && !Sample.bFireActive &&
		Sample.LastResolvedShotKey == Sample.FirstPredictionKey && Sample.bLastResolvedShotCommitted;
	const FString Detail = FString::Printf(
		TEXT("Row=%s OwnerFullAuto=%d HoldSeconds=%.2f RefireRate=%.3f Shots=%d Accepted=%d Rejects=%d ")
		TEXT("ClientAct=%d Keys=%d Verdicts=%d Predicted=%d ConfirmedReplay=%d Pending=%d MagDelta=%d Released=%d ")
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
		ConfirmedReplayDelta,
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

	// 本模式的用例总数：矩阵模式只跑矩阵行，Weapon Context 模式只跑定向用例，
	// 且这两种结构性模式都不跑"额外客户端也在观察"的整体契约。
	const bool bStructuralOnlyMode = bFireCadenceMatrixMode || bWeaponContextMode;
	const int32 ExpectedCaseCount = bWeaponContextMode
		? StepWeaponContextCount
		: (bFireCadenceMatrixMode ? CadenceMatrixRowCount : CaseCount);
	const TCHAR* DoneMarker = bWeaponContextMode
		? TEXT("AUTOMATION_TEST_WEAPON_CONTEXT_DONE")
		: (bFireCadenceMatrixMode
			? TEXT("AUTOMATION_TEST_FIRE_CADENCE_MATRIX_DONE")
			: TEXT("AUTOMATION_TEST_AMMO_PREDICTION_DONE"));
	UE_LOG(LogShootGame, Verbose, TEXT("Ammo prediction mode ready: MatrixMode=%d WeaponContextMode=%d ")
		TEXT("ExpectedCases=%d DoneMarker=%s"),
		bFireCadenceMatrixMode ? 1 : 0, bWeaponContextMode ? 1 : 0, ExpectedCaseCount, DoneMarker);

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
		// 结构性模式（矩阵 / Weapon Context）不跑半自动用例，因此不要求存在半自动行。
		AmmoPredictionSemiAutoRowName = PickSemiAutoWeaponRowName();
		if (AmmoPredictionSemiAutoRowName.IsNone() && !bStructuralOnlyMode)
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
	// Weapon Context 用例期间服务器会真的换枪，引脚由步骤体按"跟随当前武器"更新；
	// 其余模式仍然要求引脚严格等于当前武器，避免引脚漂移被静默容忍。
	const bool bWeaponPinValid = Weapon && Weapon->GetOwner() == Subject &&
		(bWeaponContextMode || Subject->GetCurrentWeaponActor() == Weapon);
	if (!Subject || !bWeaponPinValid)
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

	// 结构性模式在完成前置检查后直接进入自己的步骤表，不经过任何单发 / 半自动 / 换枪用例。
	if (IsWeaponContextStep(AmmoPredictionServerStep))
	{
		switch (AmmoPredictionServerStep)
		{
		case StepWeaponContextSwitch:
			RunAmmoPredictionWeaponContextStep(AmmoPredictionServerStep);
			return;
		case StepSpecRemovalInFlight:
			RunAmmoPredictionSpecRemovalStep();
			return;
		case StepPoolReuseRebind:
			RunAmmoPredictionPoolReuseStep();
			return;
		default:
			return;
		}
	}

	if (IsCadenceMatrixStep(AmmoPredictionServerStep))
	{
		RunAmmoPredictionCadenceMatrixStep(AmmoPredictionServerStep);
		return;
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
				StartAmmoPredictionStep(bWeaponContextMode
					? StepWeaponContextSwitch
					: (bFireCadenceMatrixMode ? StepCadenceMatrixFirst : StepSemiAutoSingleShot));
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
			// 结构性模式不跑"额外客户端也在观察"这条整体契约，直接收口并解除帧率限制。
			if (!bStructuralOnlyMode)
			{
				for (TActorIterator<AShooterNetworkTestCoordinator> It(GetWorld()); It; ++It)
				{
					if (It->GetOwner() != Driver)
					{
						It->ClientVerifyAmmoPredictionObserver(AmmoPredictionSubjectPlayerId);
					}
				}
			}
			return;
		}
		if (!bStructuralOnlyMode)
		{
			for (TActorIterator<AShooterNetworkTestCoordinator> It(GetWorld()); It; ++It)
			{
				if (It->GetOwner() != Driver && !It->bAmmoPredictionObserverVerified)
				{
					return;
				}
			}
		}
		ClearAmmoPredictionServerTag();
		if (AmmoPredictionConvergedCount != ExpectedCaseCount || AmmoPredictionMismatchCount != 0)
		{
			FailTest(TEXT("Ammo prediction cases did not all converge; DONE is not a success marker"));
		}
		// 两种结构性模式各自打出唯一的成功标记：矩阵不是 Ammo Prediction 整体契约的成功标记，
		// Weapon Context 也不是矩阵的成功标记。
		UE_LOG(LogShootGame, Display, TEXT("%s Cases=%d Converged=%d Mismatches=%d"), DoneMarker,
			ExpectedCaseCount, AmmoPredictionConvergedCount, AmmoPredictionMismatchCount);
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

	// 装备变化委托只在本模式下绑定；收口时按绑定标志解绑，避免留下指向本协调器的悬挂委托。
	if (bAmmoPredictionEquipDelegateBound)
	{
		if (AShooterCharacter* Subject = AmmoPredictionSubject.Get())
		{
			if (UShooterEquipmentComponent* Equipment = Subject->GetEquipmentComponent())
			{
				Equipment->OnEquippedWeaponChanged.RemoveDynamic(
					this, &AShooterNetworkTestCoordinator::HandleAmmoPredictionEquippedWeaponChanged);
			}
		}
		bAmmoPredictionEquipDelegateBound = false;
	}
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
		ObservedWeapon ? ObservedWeapon->GetOwnerConfirmedReplayCountForAutomationTest() : INDEX_NONE,
		ObservedWeapon ? ObservedWeapon->GetShotVerdictReceivedCountForTest() : INDEX_NONE);
}

void AShooterNetworkTestCoordinator::ServerReportAmmoPredictionObserver_Implementation(bool bValid,
	int32 PredictedCount, int32 ConfirmedReplayCount, int32 VerdictCount)
{
	if (!bValid || PredictedCount != 0 || ConfirmedReplayCount != 0 || VerdictCount != 0)
	{
		FailTest(FString::Printf(
			TEXT("Ammo prediction observer received owner-only feedback: Valid=%d Predicted=%d ConfirmedReplay=%d ")
			TEXT("Verdicts=%d"), bValid ? 1 : 0, PredictedCount, ConfirmedReplayCount, VerdictCount));
		return;
	}
	UE_LOG(LogShootGame, Display, TEXT("AMMO_PREDICTION_OBSERVER_SUCCESS Predicted=0 ConfirmedReplay=0 Verdicts=0"));
	bAmmoPredictionObserverVerified = true;
}

#else

void AShooterNetworkTestCoordinator::ClientPrepareAmmoPredictionStep_Implementation(int32 SubjectPlayerId, int32 Step)
{
}

void AShooterNetworkTestCoordinator::ClientSetAmmoPredictionRefireRate_Implementation(int32 Step, float RefireRate)
{
}

void AShooterNetworkTestCoordinator::ClientSetAmmoPredictionMaxFPS_Implementation(int32 Step, float MaxFPS)
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

void AShooterNetworkTestCoordinator::ClientRequestAmmoPredictionWeaponSwitch_Implementation(int32 Step)
{
}

void AShooterNetworkTestCoordinator::ServerReportAmmoPredictionObserver_Implementation(bool bValid,
	int32 PredictedCount, int32 ConfirmedReplayCount, int32 VerdictCount)
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
