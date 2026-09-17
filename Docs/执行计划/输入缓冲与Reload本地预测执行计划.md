# Ability 输入缓冲与 Reload 本地预测执行计划

- 日期：2026-09-17
- 基线提交：`757545f`（射击：按本地预测语义收敛开火表现与本地开火节拍）
- 核对引擎：`E:\Unreal_Engine\UE_5.6`，版本 `5.6.1-44394996`
- 上游依赖：[网络射击 AI 自主验证契约](../架构/网络射击AI自主验证契约.md)
- 上游依赖：[Shooter 完整 Demo 最终路线规划](Shooter完整Demo最终路线规划.md)
- 前置阶段（已归档）：
  [P1 Local Predicted 基础射击反馈执行计划](../已完成计划/P1_LocalPredicted基础射击反馈执行计划.md)
- 状态：待用户批准后实施

## 1. 阶段定位

本轮不重新设计框架，只解决当前 Fire / Reload 因「一次性 TryActivate」产生的三个真实问题：

```text
1. 输入丢失：按下 → TryActivateAbility 失败 → 这次输入永久消失
2. 绕过硬门控：GA_Fire / GA_Reload 为保住唯一一次输入，
   在 Super::CanActivateAbility 之前直接 return，绕开了 GAS Tag 互斥
3. 动作边界耦合：为了不丢输入，把换弹 / 装备状态的判断散落在 Ability 自己身上
```

目标分层：

```text
ShooterAbilitySystemComponent  →  输入意图（Press / Release / Held / 短窗口 Buffer / 安全时点重试）
GAS CanActivateAbility + Tags  →  「现在能不能执行」与 State.* 动作互斥
GA_Fire / GA_Reload            →  Ability 生命周期、Owner 本地预测、Server 权威分流
Server                         →  Ammo 真值、Reload 转移、Projectile、Hit / Damage / Death
```

本阶段不是 Ammo Prediction，不是 Projectile Prediction，不引入 GameplayCue 重构。

### 1.1 明确不做（与本计划范围同级）

```text
Ammo Prediction / Ammo shadow / 预测扣弹 / 预测 GameplayEffect
Projectile Prediction / 客户端命中 / Lag Compensation
自建 PredictionKey 或每发 RPC 对账
ConfirmedFeedbackFallback（已于 757545f 删除，不恢复）
独立 Reload 状态机 / bLocalReloading / LocalReloadIntent
新 Input Manager Actor 或 Component / 第二套 bFireHeld
GA_Equip 本地预测（本轮保持 ServerOnly）
为所有 Ability 设计统一的 policy system
```

### 1.2 当前必须保留的既有语义

```text
Owner 表现绑定「本地有效 Shot Attempt」，不由 stale 复制状态二次否决
Local Fire 节拍只由「本地 Shot Attempt 被接受」推进一次
Remote 只消费服务器确认后的结果
AuthorityShot == Projectile == AmmoConsumed
Server Reject 不产生 Gameplay Result
```

## 2. 设计依据

### 2.1 外部资料

- [Using Gameplay Abilities in Unreal Engine 5.6](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-gameplay-abilities-in-unreal-engine?application_version=5.6)：
  `Local Predicted` 在拥有者客户端立即执行并把激活请求交给服务器，服务器可确认或拒绝。
- [UAbilitySystemComponent::TryActivateAbility（5.6 API）](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/GameplayAbilities/UAbilitySystemComponent/TryActivateAbility?application_version=5.6)
  与 [InternalTryActivateAbility](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/GameplayAbilities/UAbilitySystemComponent/InternalTryActivateAbility?application_version=5.6)：
  激活入口只返回成功与否，官方没有提供「失败原因」的返回值。
- [EGameplayAbilityNetExecutionPolicy::Type（5.6 API）](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/GameplayAbilities/EGameplayAbilityNetExecutionPoli-?application_version=5.6)：
  `LocalPredicted` 的官方定义。
- [UGameplayAbility::bServerRespectsRemoteAbilityCancellation（API）](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/GameplayAbilities/Abilities/UGameplayAbility/bServerRespectsR-?application_version=5.2)：
  服务器实例是否接受客户端实例发出的结束 / 取消。
- 社区实践表明「输入缓冲」不是 GAS 自带能力，需要在项目侧实现：
  [Gameplay Ability Toolkit - Input Buffering](https://morningheartgames.gitbook.io/docs/gameplay-ability-toolkit/subsystems/input-buffering)、
  [Lyra 输入标签驱动 Ability 的讨论](https://forums.unrealengine.com/t/inputpressedspechandles-array-is-reset-and-abilities-dont-trigger-lyra-input-abilitysystemcomponent/1284343)、
  [How to determine why a GameplayAbility failed to activate](https://stackoverflow.com/questions/53108639/how-to-determine-why-a-gameplayability-failed-to-activate)。

与本项目相关的结论：官方没有「激活被阻塞 → 稍后重试」的一等机制，重试与缓冲必须由项目代码实现；
失败原因需要通过引擎内部的失败标签通道获取，而不是靠 TryActivateAbility 的返回值。

### 2.2 本地 UE5.6 源码核对

以下均为本机安装版本的实际代码。路径省略前缀
`Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/`。

- 失败容器是 ASC 成员 `InternalTryActivateAbilityFailureTags`，每次尝试前 Reset。
  位置：`Public/AbilitySystemComponent.h:1220-1223`、
  `Private/AbilitySystemComponent_Abilities.cpp:1687`
- `CanActivateAbility` 返回 false 时，容器为空则补一个通用失败标签，然后 `NotifyAbilityFailed`。
  位置：`Private/AbilitySystemComponent_Abilities.cpp:1795-1806`
- `NotifyAbilityFailed` 只做一件事：`AbilityFailedCallbacks.Broadcast(Ability, FailureReason)`；
  无角色过滤、无标签数量过滤。
  位置：`Private/AbilitySystemComponent_Abilities.cpp:2531-2534`；
  委托声明：`Public/AbilitySystemComponent.h:549`
- Tag 门控失败时会把**具体阻塞标签**追加进失败容器（`AppendMatchingTags`）。
  位置：`Private/Abilities/GameplayAbility.cpp:320-343`；调用点：`GameplayAbility.cpp:375`
- `Spec->InputPressed` 在 `IsActive()` 判断之前赋值，取消 / 结束**不会**清除它。
  位置：`Private/AbilitySystemComponent_Abilities.cpp:2788`、`:2824`、`:2887`、`:2910`
- `AbilitySpecInputReleased` 不会发 `ServerSetInputReleased`；该 RPC 只在
  `bReplicateInputDirectly` 且非权威时由引擎发送。
  位置：`Private/AbilitySystemComponent_Abilities.cpp:2827-2832`、`:2908-2929`
- `ServerSetInputReleased` 是可靠 Server RPC。
  位置：`Public/AbilitySystemComponent.h:1645-1650`
- LocalPredicted 预测端与权威端都会走 `CallActivateAbility` → `PreActivate`，
  同帧本地挂 `ActivationOwnedTags`。
  位置：`Private/AbilitySystemComponent_Abilities.cpp:1904-1958`、`GameplayAbility.cpp:963`
- LocalPredicted 的 `ActivationOwnedTags` 对非拥有者用 `MinimalReplicationTags`，
  复制条件 `COND_SkipOwner`。
  位置：`Private/Abilities/GameplayAbility.cpp:965-976`、`Private/AbilitySystemComponent.cpp:1655`
- 预测被拒：`ClientActivateAbilityFailed` → `SetActivationRejected` + `K2_EndAbility`
  （End，不是 Cancel），本地 Tag 会被移除。
  位置：`Private/AbilitySystemComponent_Abilities.cpp:2251-2305`、`GameplayAbility.cpp:837`
- `bServerRespectsRemoteAbilityCancellation` 决定**本地已运行实例**是否接受远端结束 / 取消，
  默认 true。
  位置：`Private/AbilitySystemComponent_Abilities.cpp:1886`、`:2167`、`GameplayAbility.cpp:87`
- `AbilityTask_WaitDelay` 无权威 / 预测限制，使用本地世界时间，Ability 结束即被拆除。
  位置：`Private/Abilities/Tasks/AbilityTask_WaitDelay.cpp:26-41`、`GameplayAbility.cpp:818-827`
- `GetOwnedGameplayTags()` 包含非复制的 loose 标签。
  位置：`Public/AbilitySystemComponent.h:591-600`
- 5.6 没有内建输入缓冲 / 阻塞后重试；`PendingServerActivatedAbilities` 是复制延迟重试且会丢弃条目。
  位置：全插件检索无 `InputBuffer` / `QueuedAbility`；
  `Private/AbilitySystemComponent_Abilities.cpp:1471-1513`
- ASC 提供 `RegisterGameplayTagEvent` 与 `OnAbilityEnded` / `AbilityEndedCallbacks`。
  位置：`Public/AbilitySystemComponent.h:744-747`、`:540-543`

### 2.3 与项目基线的差异

本项目 `Config/*.ini` 中没有任何
`[/Script/GameplayAbilities.GameplayAbilitiesDeveloperSettings]` 配置，
因此引擎的通用失败标签（`ActivateFailTagsBlockedTag` /
`ActivateFailCanActivateAbilityTag` 等）在本项目里很可能是**无效标签**，
`AddTag` 会静默丢弃。

结论：失败分类只能依赖引擎追加的**具体阻塞标签**，不得依赖通用失败标签。
本计划的判定因此改为「失败容器与本机 OwnedTags 的交集」，不读取任何通用失败标签。

## 3. 当前代码与目标的实际偏差

只列与本计划直接相关的真实差异（含位置），不重复已经冻结的正确语义：

1. **ASC 完全没有缓冲**：`AbilityInputTagPressed` 记录 `Spec.InputPressed` 后立即
   `TryActivateAbility`，失败即返回，输入永久消失
   （`Source/ShootGame/AbilitySystem/ShooterAbilitySystemComponent.cpp:10-28`）。
   引擎核对确认 5.6 无内建缓冲，必须由项目实现。
2. **GA_Fire 客户端分支绕过 Super**：为保住输入，客户端分支先于 `Super` 返回，
   且明确不读 `ActivationBlockedTags`
   （`Source/ShootGame/AbilitySystem/Abilities/ShooterGameplayAbility_Fire.cpp:90-126`）。
   文件内注释仍写着「本机已知的阻塞态只用于禁止本地预测表现，见 ActivateAbility 第 2 步」，
   该描述在 `757545f` 之后已过期。
3. **GA_Fire 表现门控不读状态标签**：`IsOwnerPredictedFeedbackAllowedForContext`
   只判「武器有效 / 未隐藏 / 仍是当前装备」（`..._Fire.cpp:380-395`）。
   这是冻结语义，本轮**保持**，不是缺陷。
4. **GA_Reload 是 ServerOnly 且客户端同样绕过 Super**（`..._Reload.cpp:50`、`76-84`），
   客户端只确认「请求来自当前 Avatar 的 ASC」，无法形成本地换弹窗口。
5. **GA_Reload 的 ActivateAbility 直接拒绝非权威端**（`..._Reload.cpp:136-141`），
   `HandleReloadWaitFinished` 也没有 `HasAuthority` 分支
   （`:202-234`）。改成 LocalPredicted 后必须拆成「服务器提交 / 客户端只结束本地实例」。
6. **GA_Reload 的客户端资格不得包含弹药真值**：`ResolveReloadTarget`
   会读 `GetBulletCount()` / `GetReserveAmmo()`（`..._Reload.cpp:113-124`），
   改成 LocalPredicted 后客户端分支不得调用它，
   否则会出现「客户端以为满弹 → 服务器本会接受的换弹请求发不出去」。
7. **`bServerRespectsRemoteAbilityCancellation` 对 Reload 尚未设置**，默认 true
   （引擎 `GameplayAbility.cpp:87`）。LocalPredicted 后必须显式设为 false，
   否则客户端本地时长到期会反向结束服务器权威事务。
8. **`Spec->InputPressed` 可被复用作 Held 状态**：引擎在取消 / 结束时不清除它
   （见 2.2 核对），本机 Release 时才清除。因此不需要第二份 `bFireHeld`。
9. **`State.Reloading` 的拥有权没有复制竞态**：LocalPredicted 预测端同帧本地持有，
   服务器的 `MinimalReplicationTags` 对 Owner `COND_SkipOwner`。
   远程客户端看到的仍是服务器最小复制（与今天 ServerOnly 完全一致，动画链路不变）。
10. **`ReloadFromReserve` 已有权威护栏**（`Source/ShootGame/Weapons/ShooterWeapon.cpp:409`），
    客户端预测换弹不需要新增弹药护栏。
11. **客户端绕过 Super 的原始动机仍然成立**：`310faf9` 记录的「弱网下迟到的
    `State.Reloading` / `State.Equipping` 复制吞掉唯一一次输入」
    在 GA_Equip 仍为 ServerOnly 时依旧存在。
    这正是必须先有 Input Buffer，才能恢复诚实 Tag 门控的原因。
12. **注释与文档漂移**：`..._Fire.cpp:90-93`、`ShooterCharacter.cpp:542`、`:568`、`:618`
    仍写着「GA_Fire / GA_Reload 为 ServerOnly」。
    契约文档已在本轮修订（不变量以标题含义为准，Invariant 2 不再有「确认回退只补播一次」），
    实现前按第 20 节做含义覆盖确认即可。

以上没有发现新的 Gameplay 语义冲突；按第 5～8 节的方案实施即可。

## 4. 分层职责（本轮冻结）

### 4.1 `UShooterAbilitySystemComponent`

```text
Press / Release / Held（复用 FGameplayAbilitySpec::InputPressed）
短窗口 Press Edge Buffer（绝对过期时间，不续期）
失败分类（只把「短暂动作标签阻塞」视为可缓存）
安全时点的延迟重试（下一 Tick，不在 Tag 回调栈内重入）
生命周期清理（Avatar / 死亡 / ClearActorInfo / Spec 移除）
```

它不读 Weapon、Ammo、Inventory、Projectile，也不判断「现在能不能开火」——那是 Ability 的职责。

### 4.2 `UShooterGameplayAbility` / GA_Fire / GA_Reload

```text
Ability 自己声明：输入语义（按下沿 / 按住持续）、输入上下文、ActivationBlockedTags / OwnedTags
Ability 自己决定：本地预测资格、权威校验、Owner 表现、服务器事务
```

基类只提供两个最小声明钩子，不做策略系统：

```cpp
/** 输入语义：true = 按住持续（松开才停），false = 单次按下沿。 */
virtual bool IsSustainedInputAbility() const { return false; }

/** 输入 Buffer 的上下文对象；默认无上下文。上下文变化后不得再消费该输入。 */
virtual const UObject* GetInputBufferContext() const { return nullptr; }
```

GA_Fire 分别返回「当前武器是否全自动」与「按下时的当前武器」；
ASC 只消费这两个声明，不读 Weapon、不判断能不能开火。

### 4.3 Server

```text
始终独占 Ammo 真值、ReloadFromReserve 事务、Projectile、Hit / Damage / Death
客户端预测只影响本地表现与本地动作边界，不产生任何 Gameplay 结果
```

## 5. Input Buffer 第一版语义

### 5.1 配置（集中在 ASC，不散落 Magic Number）

```cpp
// ShooterAbilitySystemComponent.h
/** 单次 Press Edge 的保留窗口；绝对世界时间过期，重试不得续期。 */
UPROPERTY(EditDefaultsOnly, Category = "Shooter|Input")
float InputBufferWindowSeconds = 0.15f;

/** 只有这些标签造成的阻塞才被视为「动作马上结束」，可进入 Buffer。 */
UPROPERTY(EditDefaultsOnly, Category = "Shooter|Input")
FGameplayTagContainer TransientInputBlockedTags;

/** 按住型输入在一个 Press Edge 内允许的有限再尝试次数与间隔。 */
UPROPERTY(EditDefaultsOnly, Category = "Shooter|Input")
int32 MaxHeldRetryPerPress = 2;

UPROPERTY(EditDefaultsOnly, Category = "Shooter|Input")
float HeldRetryIntervalSeconds = 0.2f;
```

构造函数默认值：`TransientInputBlockedTags = { State.Reloading, State.Equipping }`。
`State.Dead` 是硬失败，永远不可缓存。

### 5.2 条目

```cpp
/** 一条待消费的按下沿。不保存「是否已松开」，该状态读 FGameplayAbilitySpec::InputPressed。 */
struct FShooterBufferedInput
{
    FGameplayTag InputTag;
    /** 按下时的上下文对象（GA_Fire 使用当前武器）；上下文变化后不得消费。 */
    TWeakObjectPtr<const UObject> Context;
    /** 按下时是否约定了上下文；未约定时不做上下文校验。 */
    bool bHasContext = false;
    /** 绝对过期时间；同一条目不得因重试而续期。 */
    float ExpireTime = 0.0f;
};
```

上下文由 Ability 的 `GetInputBufferContext()` 提供：按下时记录，消费时重新求值并要求一致。

每个 InputTag 最多一条；新的按下沿覆盖旧条目（新输入是新意图，允许刷新窗口）。

### 5.3 失败分类：什么可以 Buffer

按下时先按现有语义调用 `AbilitySpecInputPressed` + `TryActivateAbility`，同时订阅
`AbilityFailedCallbacks`（引擎 `AbilitySystemComponent_Abilities.cpp:2533`）。只有本次按下对应的
失败才会被分类，判定顺序：

```text
1. 失败容器与本机 OwnedTags 求交 → BlockingTags
   （引擎在 GameplayAbility.cpp:339 追加过具体阻塞标签；不依赖通用失败标签）
2. BlockingTags 为空 → 丢弃
   覆盖：半自动本地节拍未 Ready、武器无效 / 隐藏、自定义客户端否决、Ammo / 权威校验失败
3. BlockingTags 与该 Ability 自己的 ActivationOwnedTags 相交 → 丢弃
   覆盖：换弹中再按换弹；阻塞来自该动作自身时重复按下没有意义
4. BlockingTags 不能全部落在 TransientInputBlockedTags 内 → 丢弃
   覆盖：State.Dead、以及任何未知阻塞标签
5. 其余情况 → 登记 / 覆盖条目
```

因此：

```text
Semi-auto 本地 RefireRate 未 Ready  → 硬失败，绝不 Buffer，绝不稍后补枪
Weapon 无效                          → 硬失败，绝不 Buffer
生命周期失效（Dead / 失去控制）      → 硬失败，绝不 Buffer
Server Ammo / Reserve / Inventory    → 不由 Buffer 猜测，由 Server 最终 Reject
```

### 5.4 消费规则

在安全时点执行 `ProcessBufferedInputs()`：

```text
对每条未过期条目：
  Spec = FindAbilitySpecFromInputTag(InputTag)
  Spec 不存在                → 移除条目
  Context 失效或与当前上下文不一致 → 移除条目（不允许跨武器补枪）
  Spec->InputPressed 仍为 true → 先 AbilitySpecInputPressed（保持引擎状态一致）
  bActivated = TryActivateAbility(Spec->Handle, true)
  if (bActivated && !Spec->InputPressed) → 立即走标准释放路径（本地释放 + ServerSetInputReleased）
  移除条目：失败不重建、不延期、不自动再次 Buffer
```

要点：

- **已松开的按下沿**（半自动 1.42 按下 / 1.45 松开 / 1.50 消费）在消费时
  `Spec->InputPressed == false`，因此不伪造按下；激活后立即补一次标准释放，
  避免残留 `State.Firing` 与不结束的本地预测实例。
- **补释放不新增 RPC**：复用现有 `ServerSetInputReleased` 通道，
  与 `AbilityInputTagReleased` 共用同一个内部释放函数。
- **已松开的全自动按下沿**由 GA_Fire 自己的门控拒绝（见 7.1），不会在换弹结束后补开枪。

### 5.5 Held 再武装（按住语义）

```text
对每个 AbilitySpec：
  Ability->IsSustainedInputAbility() && Spec->InputPressed && !Spec->IsActive()
  → TryActivateAbility
```

- Held 状态来自 `Spec->InputPressed`（引擎状态），不新增第二份布尔。
- 覆盖用户场景：Fire 已激活 → Reload 取消 Fire → 玩家一直按住 → Reload End 后重新启动全自动。
- 不建条目、不续期；每次再武装都要求输入仍处于按住状态。

### 5.6 安全时点与重入约束

```text
RegisterGameplayTagEvent(TransientInputBlockedTags, NewOrRemoved)
→ 计数归零时只登记 bBufferProcessScheduled
→ GetWorld()->GetTimerManager().SetTimerForNextTick(...)
→ 下一 Tick 执行一次 ProcessBufferedInputs()
```

- 不在 Tag 回调栈内调用 `TryActivateAbility`，避免 `EndAbility` / `RemoveTag` 栈内重入。
- 同帧多次 Tag 变化只合并成一次处理。
- 多标签同时消失只处理一次。

### 5.7 有限重试（只针对按住型输入）

服务器拒绝全自动启动时（典型场景：客户端本地换弹先结束，服务器还在换弹）：

```text
安全时点尝试一次 → 若之后 Spec 仍未激活且输入仍按住
→ 最多再安排 MaxHeldRetryPerPress 次、间隔 HeldRetryIntervalSeconds 的检查
→ 次数用尽后停止，直到出现新的按下沿或新的安全时点
```

禁止：

```text
每次 Server Reject 后无条件立即重启
无上限的 Activation / Reject 循环
半自动的自动重试（半自动一次点击只对应一次 Shot Attempt）
```

## 6. Press / Release / Held 语义

### 6.1 半自动

```text
按下 → 本机记录按下沿 → TryActivate
  ├─ 立刻可激活：一次 Shot Attempt → 一次 Owner 表现 → 服务器裁决
  └─ 被短暂动作标签阻塞：登记条目（窗口 150ms）
松开 → Spec.InputPressed = false（条目仍保留到过期）
短暂阻塞结束 → 消费条目
  ├─ 窗口内：恰好激活一次 → 恰好一次 Owner 表现 → 立即补标准释放 → 正常 End
  └─ 已过期：不激活、不表现、不请求
```

### 6.2 全自动

```text
按下（按住）→ 被阻塞则登记条目 → 阻塞结束（安全时点）
  ├─ 仍按住：激活一次 → 本地表现节拍 PredictedFeedbackTimer + 服务器 Authority RefireTimer
  └─ 已松开：不激活（GA_Fire 的按住门控拒绝），条目丢弃
按住期间 Ability 被取消（Reload / Equip）→ Held 再武装在安全时点重新启动
松开 → 现有 InputReleased / EndAbility 链路停本地节拍并通知服务器
```

两侧 Timer 允许相位差，不做逐发对齐；Reject / Release 只停止未来行为，不回滚已播 cosmetic。

## 7. GA_Fire 收敛

### 7.1 `CanActivateAbility` 顺序（恢复诚实 Tag 门控）

```text
1. Avatar / ASC 基础有效性
2. 本机拥有者视图（预测端）
3. 当前 Weapon 有效且未隐藏
4. 本地确定性条件
   半自动：IsLocalFireCooldownReady()（硬失败，不 Buffer）
   全自动：Spec->InputPressed 必须为 true（按住语义，硬失败，不 Buffer）
5. Super::CanActivateAbility()
   → State.Reloading / State.Equipping / State.Dead 由 GAS Tag 门控
6. Authority：现有完整校验
   Ammo / ownership / lifecycle / 隐藏 / 权威射速 CanStartSemiAutoShotNow()
```

约束：

- 第 4 步必须早于第 5 步，保证节拍失败不会产生「被 Tag 拦住」的失败容器，
  从而永远不会被 Buffer。
- 客户端不执行服务器的 Ammo / Inventory / lifecycle 真值校验。
- 表现门控（`IsOwnerPredictedFeedbackAllowed`）保持 757545f 的语义，不再读 Ammo / Tag。
- 全自动按住门控只作用于预测端与主机本地激活；服务器对远端请求读到的
  `Spec->InputPressed` 由引擎强制为 true（`AbilitySystemComponent_Abilities.cpp:2078`），
  因此不在权威分支依赖它。

### 7.2 保留的预测语义

```text
半自动：Weapon Valid + Local RefireRate Ready + GAS Tags Allow
       → 推进本地节拍一次 → 立即 Owner 表现 → LocalPredicted 请求 → Server Accept / Reject
全自动：一次 Start 请求 → Owner 本地节拍 Timer → Server 权威 Timer → 允许相位差
```

不恢复 ConfirmedFeedbackFallback、冷却旁路、每发 RPC、每发 PredictionKey 对账。

## 8. GA_Reload 改为 LocalPredicted

### 8.1 类配置

```cpp
InstancingPolicy = InstancedPerActor;                        // 保持
NetExecutionPolicy = LocalPredicted;                          // ServerOnly → LocalPredicted
bServerRespectsRemoteAbilityCancellation = false;             // 客户端本地到期不得结束服务器事务
ActivationOwnedTags   = { State.Reloading };                  // 保持
ActivationBlockedTags = { State.Dead, State.Reloading, State.Equipping };  // 保持
```

不新增 `bLocalReloading`、`LocalReloadIntent` 或第二个换弹状态机。

### 8.2 `CanActivateAbility` 双端差异

```text
预测端（非权威）：
  1. Avatar / ASC 与 Avatar 一致
  2. 本机拥有者视图
  3. 当前 Weapon 有效且未隐藏（不读 Magazine / Reserve / Inventory / 复制真值）
  4. Super::CanActivateAbility（Tag 互斥）

权威端：
  Super::CanActivateAbility + ResolveReloadTarget() 完整真值校验
  Inventory / Equipment 当前武器 / ownership / lifecycle / Magazine / Reserve / Dead
```

客户端不得用复制的满弹匣或空备弹决定「这次换弹永久失败」：
客户端只负责本地预测资格，最终裁决仍在服务器。

### 8.3 生命周期分离

```text
Owner Client：
  Reload Input → CanActivate → LocalPredicted Activate
  → 当帧获得本地 State.Reloading → 取消本地 GA_Fire
  → AnimInstance 继续通过现有 State.Reloading 驱动换弹表现（不新增表现路径）
  → WaitDelay(Weapon.ReloadDuration) 本地时钟
  → 到期只结束本地预测实例：EndAbility(..., bReplicateEndAbility = false, bWasCancelled = false)
  → 本地 State.Reloading 解除 → ASC 在安全时点处理 Buffer / Held

Server：
  收到预测激活 → Super + ResolveReloadTarget 完整校验
  → 取消权威 GA_Fire → WaitDelay(Weapon.ReloadDuration) 服务器时钟
  → IsReloadTargetStillCurrent()
  → ReloadFromReserve() 恰好一次（bReloadCommitted 护栏保留）
  → EndAbility(..., true, false)
```

客户端不执行：`ReloadFromReserve`、Magazine / Reserve 修改、任何 Inventory 事务、服务器 End。

### 8.4 Reject / Cancel 收敛

```text
Server Reject：
  GAS 原生 prediction reject → 本地预测实例 K2_EndAbility
  → 本地 ActivationOwnedTags 被移除（引擎 GameplayAbility.cpp:837）
  → 无 Ammo 变化、无残留 State.Reloading

Server Cancel（Equip / Death / 切枪）：
  ReplicateEndOrCancelAbility → ClientCancelAbility → 客户端预测实例收敛
  → 不允许客户端自己的时长到期反向取消服务器事务
```

## 9. Buffer 与 Reload End 的关系

```text
Input Buffer 不知道 ReloadDuration，也不知道换弹规则：
  GA_Reload 自己管理本地换弹窗口
  State.Reloading 表示当前是否阻塞其他 Ability
  ASC 只记录「玩家刚才的输入是否仍然有意义」
```

示例（ReloadDuration = 1.5s，窗口 150ms）：

```text
1.42  Press Fire → 本地节拍 Ready → Super 被 State.Reloading 拦 → Buffer 到 1.57
1.45  Release Fire → Spec.InputPressed = false，条目保留
1.50  Owner 本地 Reload End → State.Reloading 移除 → 下一 Tick ProcessBufferedInputs
1.50  条目未过期 → 恰好激活一次 → 一次 Owner 表现 → 立即补标准释放
```

```text
0.30  Press Fire → Super 被 State.Reloading 拦 → Buffer 到 0.45
0.45  条目过期
1.50  Owner 本地 Reload End → 过期条目被移除，不自动开枪
```

## 10. Server Reject 后的语义

Buffer 不是「保证产生真实 Shot」。

```text
半自动：
  这次点击结束；cosmetic 不回滚；不自动再次 Buffer；不无限重试

全自动：
  已松开 → 结束
  仍按住 → 允许在安全时点做有限次数的再尝试（MaxHeldRetryPerPress）
  禁止 Reject → 立即重启 → Reject 的死循环
```

明确记录一个既有相位差：Owner 本地换弹窗口先于服务器提交结束约一个单程延迟，
因此「本地换弹刚结束就开枪」的请求可能被服务器 Reject。
半自动接受一次 cosmetic phantom；全自动靠有限再尝试收敛。
这是不做 Ammo Prediction 的直接代价，本轮接受。

## 11. 生命周期清理

Buffer / Held 意图不得跨生命周期泄漏。以下时点必须清理或失效：

```text
InitAbilityActorInfo（Avatar 更换 / Respawn）
ClearActorInfo（ASC 失去 Avatar）
State.Dead 计数 > 0
PlayerState EndPlay（解绑 Tag 事件并清空）
Ability Spec 被移除（消费时按 InputTag 找不到 Spec 即丢弃）
Context 变化（武器切换 / 归还池）
失去本地控制（处理入口与处理时点都校验本机拥有者视图）
```

明确禁止：

```text
死亡前按下 Fire → Respawn 后旧 Buffer 自动开枪
Weapon A 的 buffered Fire → 切 Weapon B → 在 B 上消费
```

## 12. 实施单元与提交切分

### 单元 1：Input Buffer 与 GA_Fire 门控恢复

```text
ASC：条目 / 失败分类 / 安全时点 / 消费 / Held 再武装 / 有限重试 / 生命周期清理
GA 基类：IsSustainedInputAbility()（默认 false）
GA_Fire：CanActivateAbility 顺序收敛（节拍与按住前置、Super 恢复）
清理：_Fire.cpp 过期注释、ShooterCharacter 中 "GA_Fire 为 ServerOnly" 注释
测试：Buffer 自动化用例 + 既有 Fire 用例回归
```

建议提交说明：`GAS：新增输入缓冲并恢复开火 Tag 门控`

### 单元 2：GA_Reload LocalPredicted

```text
GA_Reload：NetExecutionPolicy / bServerRespectsRemoteAbilityCancellation
         CanActivateAbility 双端差异、ActivateAbility 双端生命周期、客户端不提交事务
测试：换弹预测用例 + 既有 Reload / Equip / Inventory 回归
```

建议提交说明：`GAS：改为本地预测换弹并隔离服务器事务`

### 单元 3：网络证据、文档同步与收尾

```text
NetworkTestCoordinator：半自动 Buffer 窗口 / 换弹窗口 / 全自动 Held 场景
契约对照：按第 20 节的含义覆盖给出五条结论（契约本身不再需要修改）
路线规划：当前阶段状态更新
运行七阶段完整回归并记录 Summary
```

建议提交说明：`测试：完成输入缓冲与换弹预测网络回归`

如新增测试源码文件，结构变化完成后只运行一次
`Scripts/Development/RefreshVisualStudioFiles.ps1`。

## 13. 验证矩阵

1. **Semi Buffered Press**
   换弹将结束前 Press + Release。
   期望：Reload End 后恰好激活一次、恰好一次 Owner 反馈、正常 Release / End、
   无残留 `State.Firing`。
   证据：自动化 + Dedicated。
2. **Expired Semi Buffer**
   换弹早期 Press + Release → 过期。
   期望：Reload End 后不自动开枪。
   证据：自动化。
3. **Semi Local Cadence**
   节拍未到时按 Fire。
   期望：不 Buffer，稍后不自动补枪。
   证据：自动化（扩展 `Prediction.LocalFireCadence`）。
4. **Full-auto Held**
   换弹期间一直按住 Fire。
   期望：Reload End 后自动开始全自动。
   证据：自动化 + Dedicated / Emulated。
5. **Full-auto Released**
   换弹期间按住后提前松开。
   期望：Reload End 后不自动开始。
   证据：自动化。
6. **Reload Local Prediction**
   按 R 后 Owner 当帧获得 `State.Reloading`，Fire 立刻被 Tag 阻塞。
   期望：不需要 LocalReloadIntent，表现仍由现有 `State.Reloading` 驱动。
   证据：自动化 + Dedicated。
7. **Reload Server Reject**
   Owner 先预测 Reload，Server Reject。
   期望：Owner 换弹正确结束、无 Ammo 转移、无残留 `State.Reloading`。
   证据：自动化 + Dedicated。
8. **Reload 本地到期**
   Owner 本地窗口到期。
   期望：只结束本地实例，不取消服务器 WaitDelay，服务器仍恰好一次 `ReloadFromReserve`。
   证据：Dedicated 日志。
9. **Server Ammo Authority**
   期望：Magazine / Reserve 改变只发生在服务器，提交恰好一次。
   证据：Dedicated / Listen / Emulated。
10. **Listen / Standalone**
    期望：不因 OwnerLocal + Authority 双角色产生重复表现或重复事务。
    证据：Listen 会话 + 自动化。
11. **Dedicated / Emulated**
    期望：允许 Owner 预测与服务器权威存在相位差，但服务器结果唯一。
    证据：Dedicated / Emulated。
12. **生命周期**
    期望：Death / Respawn / 切枪 / 归还池后无旧 Buffer 或 Held 泄漏。
    证据：自动化。

测试不再强制 `Owner cosmetic == Authority shot`，允许有限 prediction phantom。
必须保持的是：`AuthorityShot == Projectile == AmmoConsumed`、Reload 事务最多提交一次、
Server Reject 不产生 Gameplay Result、Remote 只消费服务器确认表现。

## 14. 测试组织与可观测性

- 新增 `Source/ShootGame/Tests/Ability/ShooterAbilityInputBufferAutomationTests.cpp`，
  最小世界复用现有预测测试世界的构造方式（ShooterPlayerState + 本地玩家 Character + 池化 Weapon）。
- 同步测试世界没有 NetDriver，`TryActivateAbility` 走本机路径，因此可以同步断言：
  条目登记 / 不登记、过期、消费次数、释放后的 `State.Firing`、上下文变化丢弃。
- 开发测试钩子沿用 `...ForTest` 命名（只读计数 + 立即处理 + 强制过期）：
  `GetBufferedInputCountForTest`、`HasBufferedInputForTest`、`ProcessBufferedInputsForTest`、
  `ExpireBufferedInputsForTest`。
- 网络侧继续扩展既有 `ShooterNetworkTestCoordinator`，不新建第二套框架：
  半自动连点窗口新增「换弹结束前的 buffered press」；
  `ReloadFire` 已知阻塞分支允许一次 cosmetic phantom 的语义按新模型重述；
  `FireAfterReload` 保持严格断言（服务器提交后再按下）。
- 日志标记沿用既有 `FIRE_*` 系列，新增 Buffer 相关标记时必须包含
  `PlayerId / InputTag / NetMode / OwnerLocalTime`。
- 不允许用恒真断言、假 0 或未执行分支作为通过证据。

## 15. 修改文件（预期）

```text
Source/ShootGame/AbilitySystem/ShooterAbilitySystemComponent.h / .cpp        主要改动
Source/ShootGame/AbilitySystem/ShooterGameplayAbility.h / .cpp               输入语义声明
Source/ShootGame/AbilitySystem/Abilities/ShooterGameplayAbility_Fire.h / .cpp
Source/ShootGame/AbilitySystem/Abilities/ShooterGameplayAbility_Reload.h / .cpp
Source/ShootGame/Characters/ShooterCharacter.cpp                            注释漂移
Source/ShootGame/Tests/Ability/ShooterAbilityInputBufferAutomationTests.cpp  新增
Source/ShootGame/Tests/Ability/ShooterAbilityFirePredictionAutomationTests.cpp
Source/ShootGame/Tests/Ability/ShooterAbilityReloadBehaviorAutomationTests.cpp
Source/ShootGame/Tests/Ability/ShooterAbilityReloadEquipAutomationTests.cpp
Source/ShootGame/Tests/Network/ShooterNetworkTestCoordinator.h / .cpp
Docs/架构/网络射击AI自主验证契约.md                                          Invariant 2 同步
Docs/执行计划/Shooter完整Demo最终路线规划.md                                  当前阶段更新
Docs/开发记录/<每个提交一份>.md
```

`ShooterWeapon`、`ShooterEquipmentComponent`、`ShooterInventoryComponent` 预期不改。
若实现必须修改它们，先停止并说明原因。

## 16. 验证流程

```text
Preflight / CodeGraph
→ 按契约标题含义确认五条不变量的覆盖（见第 20 节）
→ Test First 或先建立可观测失败证据
→ 最小实现
→ BuildEditor
→ ShootGame.Ability.* / ShootGame.Inventory.* / ShootGame.Equipment.* 定向回归
→ Dedicated / Listen / Emulated 定向网络场景
→ 读取日志与计数
→ 开发记录
→ 独立提交
```

每个单元结束运行：

```powershell
Scripts/Tests/BuildEditor.ps1
Scripts/Tests/RunAutomation.ps1 -TestFilter "ShootGame"
Scripts/Development/CheckTextLayout.ps1 -Scope Staged
Scripts/Development/CheckSourceIncludePaths.ps1
git diff --check
```

单元 3 结束运行一次七阶段完整回归并逐项读取：

```text
Saved/Automation/Runs/<timestamp>/Summary.json
Automation index.json
Dedicated / Listen / Emulated 服务器与客户端日志
DisconnectCleanup 两个子会话日志
```

Emulated 必须确认服务器与两个客户端都实际启用 `PktLag=100` / `PktLoss=2`。

## 17. 汇报格式

报告分两层，缺一不可。

第一层：按契约五条不变量的**标题含义**确认覆盖（结构见第 20 节与契约第 7 节），
给出每条含义的触及维度、未改变维度、新增证据与范围外理由。

第二层：按用户可理解的不变量给出证据明细，不使用内部 Phase / Case 编号：

```text
1. Input Buffer
2. Fire Prediction
3. Reload Prediction
4. Server Ammo Authority
5. Reject / Cancel Convergence
6. Full-auto Held
7. Lifecycle Cleanup
```

每条给出：结论、证据来源、被推翻的可能性。
最后附：修改文件、删除的旧 workaround、Build / Automation / Dedicated / Listen / Emulated 结果、
仍存在但当前明确接受的网络边界。

第二层的七条是第一层五条在本次改动上的分解，必须能映射回契约不变量；
两层结论不一致时视为未收敛，不得只报其中一层。

## 18. 失败与回退规则

出现以下任一情况立即回退当前单元：

```text
客户端能直接扣 Ammo、生成 Projectile、Apply Damage 或计分
一次输入在服务器产生两个 Projectile 或两次权威事务
Reload 事务提交两次，或服务器换弹被客户端本地到期提前结束
Reject 后出现持续重试、无限 Activation / Reject 循环
预测状态残留：Buffer 条目、Held 意图、State.Firing、State.Reloading
Listen / Standalone 出现重复换弹事务或双份第一人称表现
为让测试通过而降低既有 Gameplay 断言
```

同一失败项最多自主修复 3 轮；仍不收敛时停止扩展，保留工作区并报告
失败用例、日志路径、相关计数与已尝试的修复。

## 19. 开放项与已接受的取舍

1. **`State.Equipping` 与跨武器消费的取舍（已确认）**：本计划要求「不得在 Weapon B 上消费
   Weapon A 的 buffered Fire」，因此条目带上下文校验。副作用是换枪提交后这次按下会被丢弃
   （与今天的行为一致，不构成回归）。2026-09-17 已与用户确认保持该取舍；
   若日后改为换枪后仍补一次开火，需要取消上下文校验并同步第 5.2 与 11 节。
2. **GA_Equip 仍为 ServerOnly**：`State.Equipping` 仍是复制状态，仍可能迟到；
   迟到时的按下现在会被 Buffer 而不是丢弃（行为改进，但装备预测不在本轮范围）。
3. **换弹结束瞬间的开火请求可能被服务器 Reject**（单程延迟相位差），见第 10 节。
4. **契约文档已同步**：本轮已修订
   [网络射击 AI 自主验证契约](../架构/网络射击AI自主验证契约.md)——
   不变量改为以标题含义为准、同步当前预测与换弹语义（不再有「确认回退只补播一次」），
   并新增「按标题含义确认改动与计划」的要求；单元 3 只需按新契约报告，不再需要改契约。
5. **全自动 5ms 节拍容差**保持不动，不在本轮调整。
6. **既有不稳定阶段**（`Remote invariant invalid`、`Timed out waiting for network state`）
   不在本轮修复范围，仍可能让完整网络回归出现红点。
7. **复制动作标签的滞留长于缓冲窗口（2026-09-17 实测，已确认处理方式）**：
   GA_Reload 目前仍是 ServerOnly，其 `State.Reloading` 由引擎以
   `AddReplicatedLooseGameplayTags` 挂在拥有者 ASC 上，随 PlayerState 的复制节拍到达。
   Dedicated 会话实测：服务器 GA_Reload 已结束 361ms，拥有者按 Fire 时该标签仍为 true，
   于是这次按下被 Tag 门控拦住并进入缓冲；150ms 窗口内标签未清除，该次点击被丢弃。
   即「短暂阻塞会在窗口内解除」对**复制标签**不成立。
   已确认处理方式：保持短窗口与诚实门控不变，**单元 2 直接接上**——
   GA_Reload 改为 LocalPredicted 后，拥有者的 `State.Reloading` 是自己的本地窗口
   （服务器最小复制对 Owner `COND_SkipOwner`），该滞留从拥有者路径彻底消失。
   因此单元 1 提交只声明 Build / Automation 与不依赖换弹时序的网络证据，
   依赖换弹时序的 `FireAfterReload` 阶段在单元 2 之后验证。

## 20. 按契约标题含义的不变量覆盖确认

按 [网络射击 AI 自主验证契约](../架构/网络射击AI自主验证契约.md) 第 7 节，
实施前先逐条确认五条不变量的含义覆盖（触及维度 / 未改变维度 / 新增证据 / 范围外理由）。

### Invariant 1 Owner Immediate Feedback

- 触及维度：输入必须在本地形成动作边界（本地 Shot Attempt、本地换弹窗口），不等一次 RTT；
  该边界对应的第一人称表现仍由本地预测启动。
- 不应改变维度：Projectile 仍只由服务器生成；Montage / Muzzle / Sound / Recoil 入口不变。
- 需要的新增证据：换弹窗口在 Owner 当帧成立（自动化）；
  Owner 表现不晚于同次服务器确认（Dedicated 同一客户端时钟域）。
- 范围外与理由：主观手感与画面质量不由无头自动化判定，保持人工验收边界。

### Invariant 2 Local Prediction Obeys Weapon Rules

- 触及维度：服从（恢复 GAS Tag 门控、半自动节拍硬门、全自动按住语义）；
  不越权（不用 Ammo 或可能过期的复制状态否决已成立的本地动作）；
  收敛（节拍未 Ready 不补枪、Reject 不补播第二次）。
- 不应改变维度：本地节拍只由本地有效 Shot Attempt 推进；表现入口仍只负责 cosmetic。
- 需要的新增证据：节拍失败不进入缓冲且稍后不补枪；被阻塞的输入在窗口内被消费且只消费一次；
  复制状态与本地有效动作不一致时表现仍然成立。
- 范围外与理由：Ammo Prediction 与 Projectile Prediction 仍不做，因此
  「客户端以为有弹、服务器无弹」允许一次纯 Cosmetic，由 Invariant 3 收敛。

### Invariant 3 Server Is Final Authority

- 触及维度：换弹弹药转移明确进入权威事务清单；客户端预测换弹不提交任何事务；
  Reject 时本地预测状态收敛。
- 不应改变维度：Ammo / Projectile / Hit / Damage / Death / Score 的产生位置与路径不变。
- 需要的新增证据：客户端预测换弹不改 Ammo；Reject 后无 Ammo 转移、无残留 `State.Reloading`；
  服务器换弹事务恰好提交一次。
- 范围外与理由：换弹以外的事务（Equip）本轮不改，继续由既有 ServerOnly 路径负责。

### Invariant 4 Remote Is Confirmed Only

- 触及维度：Owner 的本地换弹窗口不得影响远端信道；远端换弹状态仍来自服务器最小复制。
- 不应改变维度：Remote 表现来源、第三人称通道与不承诺逐发必达的边界不变。
- 需要的新增证据：Remote 表现仍只在服务器确认后出现；Owner 预测不产生远端额外表现或结果。
- 范围外与理由：不新增远端可靠表现承诺，也不把远端表现改为预测。

### Invariant 5 Exactly One Authority Result

- 触及维度：一次输入只对应一次本地动作边界与一次权威请求；换弹事务最多提交一次；
  Listen 双角色不得重复。
- 不应改变维度：一次认可射击仍只产生一份 Ammo / Projectile / Hit 结果。
- 需要的新增证据：Buffer 消费与 Held 再武装不产生第二次权威请求；
  Listen 与 Standalone 无重复事务、无双份第一人称表现。
- 范围外与理由：Coordinator 内部场景重排属测试实现细节，不改变含义覆盖。

### 计划内部七条不变量到五条契约不变量的映射

```text
1. Input Buffer                → Invariant 2（服从 / 不越权 / 收敛）、Invariant 5（不重复）
2. Fire Prediction             → Invariant 1、Invariant 2
3. Reload Prediction           → Invariant 1、Invariant 3
4. Server Ammo Authority       → Invariant 3、Invariant 5
5. Reject / Cancel Convergence → Invariant 2（收敛）、Invariant 3（拒绝无结果且收敛）
6. Full-auto Held              → Invariant 1（本地节拍）、Invariant 2（服从）、Invariant 5
7. Lifecycle Cleanup           → Invariant 3（收敛）、Invariant 5（不跨生命周期泄漏）
```

两层结论必须一致；若实施中发现含义覆盖发生变化，先更新本节，再继续实施。
