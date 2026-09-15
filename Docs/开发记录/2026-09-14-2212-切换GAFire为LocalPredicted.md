# 切换 GA_Fire 为 LocalPredicted 并完成半自动闭环

- 日期：2026-09-14
- 计划提交说明：`GAS：实现半自动开火本地预测反馈`
- 变更类型：生产代码 / 测试

## 目的

P1-B 把 `GA_Fire` 从 `ServerOnly` 切到 `LocalPredicted`，
让拥有者在按下开火后不再等待一次网络往返就能看到第一人称表现，
同时保证服务器仍然独占弹药、弹丸、伤害与权威射击次数。

本提交同时收口 P1-A 建立的表现路径：把本地表现接到预测激活上，
并让 Multicast 与权威 Recoil 不再对拥有者重复可见。

## 本提交完成内容

### GA_Fire

- 构造函数：`NetExecutionPolicy = LocalPredicted`，
  并显式写 `bServerRespectsRemoteAbilityCancellation = false`。
  UE 5.6 的 `UGameplayAbility` 构造函数把该标志默认置为 true，
  不关闭会让客户端能用结束/取消命令终止服务器的权威 Ability。
- 分流只用一个判据对：`HasAuthority(&ActivationInfo)` 与
  `ActorInfo->IsLocallyControlledPlayer()`。
  监听主机与 Standalone 前者为真、后者也为真；Dedicated 上的远端玩家与 NPC 后者为假。
- `CanActivateAbility` 双分支：
  - 预测客户端：保留 P1-A 之前的宽松预检（修复 310faf9 的语义）加
    `IsLocallyControlledPlayer()`，不把迟到的复制 Tag / Ammo / 隐藏状态当最终裁决；
  - 服务器 / Host / Standalone：`Super` 加现有完整校验，半自动追加
    `Weapon->CanStartSemiAutoShotNow()`，全自动不追加。
- `ActivateAbility` 固定三步：
  权威防御复核 → 拥有者本地表现（全自动同时启动 `PredictedFeedbackTimer`）→
  权威端缓存武器、绑定 `OnOutOfAmmo`、`StartFiring()`。
  不在权威端伪造 `ActivationMode::Rejected`。
- `InputReleased`：先停本地表现节拍，权威端停武器；
  `bReplicateEndAbility` 只在权威端为真，不复制第二条结束命令。
- `EndAbility` 幂等清理：停本地 Timer 与标志 → 解委托 → **条件**停武器
  （`HasAuthority(&ActivationInfo)` 门控，拥有端不写权威字段）→ `Super`。
- 新增本地表现节拍：仅 `bFullAuto` 启动，间隔为 `Max(RefireRate, 0.01f)`；
  每次 Tick 前复核武器仍是当前装备且未隐藏，失效即结束 Ability。
- `PredictedShotOrdinal` 按激活复位。
- P1 统一日志标记（仅 `WITH_DEV_AUTOMATION_TESTS`）：
  `FIRE_PREDICTED_OWNER` / `FIRE_LOCAL_FEEDBACK_STOPPED`，
  字段 `PlayerId / Weapon / PredictionKey / ShotOrdinal / Count / OwnerLocalTime / NetMode`。

### WeaponActor

- 新增 `IsFullAuto()` / `GetRefireRate()` / `CanStartSemiAutoShotNow()`。
  半自动资格查询直接读权威 `RefireTimer` 是否活动，不使用
  `TimeOfLastShot == 0.0f` 作哨兵（池取用与归还都会把它复位）；
  World 不可用或全自动时返回 false。
- `MulticastPlayFiringFX`：
  Dedicated 直接返回；`HasOwnerLocalPlayerView()` 为真时只登记确认、不生成可见 FX；
  其余端枪口恒挂第三人称世界网格、音效照播。
- `ExecuteFireAtTarget`：Recoil 只在非本地玩家视图施加，
  消除 Listen Host / Standalone 的本地路径与权威路径双次施加。
- 新增 `LogFireFeedbackMarker`（仅开发构建）：
  `FIRE_AUTHORITY_COMMIT` / `FIRE_AUTHORITY_CONFIRMATION_RECEIVED` / `FIRE_REMOTE_CONFIRMED`。
  武器端不输出 `PredictionKey`，按计划 8.3 禁止持久写进池化 Weapon。

### Character

- `MulticastPlayFiringMontage` 移除第一人称分支：
  拥有者第一人称 Montage 只由 `PlayOwnerLocalFiringFeedback` 播放，
  第三人称仍对所有客户端（含拥有者）播放。
- `EndPlay` 改为先取消、后 `ClearActorInfo`：
  权威端取消 Fire / Reload / Equip；拥有者本地端再取消预测 Fire。

### 测试

- `ShooterAbilityFireAutomationTests.cpp`：策略断言改为 `LocalPredicted`。
- `ShooterAbilityFireBehaviorAutomationTests.cpp`：
  helper 重命名 `TestServerOnlyContract` → `TestPredictionPolicyContract`，
  断言改为 `LocalPredicted` 并追加
  `ServerRespectsRemoteAbilityCancellation() == false`；
  `ShootGame.Ability.Fire.ServerOnly` 拆为
  `ShootGame.Ability.Fire.Prediction.Policy` 与
  `ShootGame.Ability.Fire.AuthorityBoundary`（后者保留
  `TestWeaponExecutionBoundary` 与 `TestAmmoAuthorityContract` 原文）。
- 新增 `ShootGame.Ability.Fire.Prediction.FirstShotReadyAfterAcquire`：
  取用后 `TimeOfLastShot == 0`、`RefireTimer` 未激活、允许立即开火；
  `RefireTimer` 活动时半自动资格查询为 false。
- 新增 `ShootGame.Ability.Fire.Prediction.RejectRefireCooldown`：
  冷却期拒绝且不扣弹、不计权威射击；全自动不参与该查询。
- `ShooterWeaponPresentationTestTypes.h` 为生命周期测试武器补
  `SetFullAutoForTest`。

### 网络测试协调器

GA_Fire 改为 LocalPredicted 后，服务器对本机非控制的玩家调用公开
`TryActivateAbility` 会走 `bAllowRemoteActivation` 分支把请求转回拥有者客户端
并返回 true，拿不到服务器校验结论，导致三处 Reject 断言失效。
新增 `CanServerActivateFireAbility()`：直接取服务器实例的
`CanActivateAbility`，语义与旧断言一致；三处调用点（4C Reject.NoAmmo /
Reject.Dead / Reject.NoWeapon）改为使用它，其余不变量断言原文保留。

## 验证结果

编译：

```text
Scripts/Tests/BuildEditor.ps1
Result: Succeeded
```

自动化：

```text
Scripts/Tests/RunAutomation.ps1 -TestFilter "ShootGame.Ability.Fire"
Passed=17  Warnings=4  Failed=0  NotRun=0   （共 19 项）
报告：Saved/Automation/Reports/20260914_221140_ShootGame.Ability.Fire/index.json
```

新增与改写的关键项：

```text
ShootGame.Ability.Fire.Prediction.Policy                 Success
ShootGame.Ability.Fire.AuthorityBoundary                 Success
ShootGame.Ability.Fire.Prediction.FirstShotReadyAfterAcquire  Success
ShootGame.Ability.Fire.Prediction.RejectRefireCooldown   Success
ShootGame.Ability.Fire.Prediction.LocalFeedbackCosmeticOnly   Success
ShootGame.Ability.Fire.Prediction.LocalFeedbackStateless      Success
```

定向网络场景（Dedicated，端口 17792，带成功标记门）：

```text
Saved/Automation/Network/20260914_220602
Server: AUTOMATION_TEST_CLIENT_SUCCESS=2  AUTOMATION_TEST_FAILURE=0
        FIRE_AUTHORITY_COMMIT=12（2 NPC + 5×2 玩家）
Client1/Client2: FIRE_PREDICTED_OWNER=5  FIRE_AUTHORITY_CONFIRMATION_RECEIVED=5  FIRE_REMOTE_CONFIRMED=5
```

同一客户端时钟上的 Owner 先后关系（Client1）：

```text
PREDICTED_OWNER 9.218  <  CONFIRMATION_RECEIVED 9.236
PREDICTED_OWNER 9.418  <  CONFIRMATION_RECEIVED 9.435
PREDICTED_OWNER 9.619  <  CONFIRMATION_RECEIVED 9.634
PREDICTED_OWNER 10.718 <  CONFIRMATION_RECEIVED 10.741
PREDICTED_OWNER 15.318 <  CONFIRMATION_RECEIVED 15.350
```

PredictionKey 与 ShotOrdinal 关系符合 8.3 语义：

```text
PredictionKey=1（半自动单发）      ShotOrdinal=1
PredictionKey=2（全自动一个 Burst）ShotOrdinal=1,2
PredictionKey=3（切枪取消中的开火）ShotOrdinal=1
PredictionKey=4（手枪）            ShotOrdinal=1
```

弱网场景（Dedicated + PktLag=100 + PktLoss=2，端口 17793）：

```text
Saved/Automation/Network/20260914_220725
三份日志均出现 PktLag=100 / PktLoss=2
AUTOMATION_TEST_FAILURE=0
Server: FIRE_AUTHORITY_COMMIT=16
Client: FIRE_PREDICTED_OWNER=8  CONFIRMATION_RECEIVED=7  REMOTE_CONFIRMED=7
```

Owner 预测 8 次、确认 7 次，差额来自不可靠 Multicast 的丢包，
属于计划 6.3 明确接受的边界（Remote 增量不超过 Authority，且多发窗口内至少收到一次）。

门禁：

```text
CheckSourceIncludePaths.ps1            Files=127 Headers=51 passed
CheckTextLayout.ps1 -Scope Staged      Files=11 Warnings=2 passed
git diff --cached --check              待提交前复核
```

## 遇到的问题

1. 首次 Dedicated 会话直接失败：`RunNetworkSession.ps1` 的默认地图是
   `/Game/FirstPerson/Lvl_FirstPerson`，该资产已不存在。
2. 首次带成功标记门的 Dedicated 会话报
   `AUTOMATION_TEST_FAILURE: No-ammo activation was not rejected without projectile`。
3. `PredictedShotOrdinal` 没有按激活复位：
   第一轮日志里手枪的 ShotOrdinal 是 5（跨 Burst 累积）而不是 1。
4. 用 `powershell -File` 传 `-ServerExtraArgs @(...)` 时，
   以 `-` 开头的元素被当成脚本参数名，`PktLag` / `PktLoss` 传不进去。
5. 版式门禁报 11 处冗余换行与参数列表布局问题。

## 处理方式

1. 所有网络会话显式传
   `-MapPath "/Game/Shooter/Maps/Lvl_Shooter"`。
   该默认值属脚本既有问题，本提交不改脚本。
2. 定位为 **LocalPredicted 带来的语义变化**：服务器上对
   “非本机控制玩家”的 ASC 调用公开 `TryActivateAbility(Handle)`
   时，`bAllowRemoteActivation` 默认为 true，
   `!bIsLocal && policy == LocalPredicted` 分支会
   `ClientTryActivateAbility` 并返回 true，因此旧断言拿到的
   `bActivated` 不再代表服务器校验结果。
   新增 `CanServerActivateFireAbility()` 直接查询服务器实例的
   `CanActivateAbility`，三处 Reject 检查改为使用它；
   弹丸数、活动 Ability 数、弹药数等结果不变量断言全部原样保留。
   该变化只影响测试协调器：生产路径上玩家与 NPC 的激活都发生在
   本机控制端（客户端本地 / 服务器上的 AIController），不受影响。
3. 在 `ActivateAbility` 中按激活复位 `PredictedShotOrdinal = 0`；
   随后重跑 Dedicated 与 Emulated，ShotOrdinal 均从 1 开始。
4. 在 `Saved/P1_0_Evidence/RunEmulated.ps1` 写一个包装脚本，
   用 `&` 调用并传数组参数；`Saved/` 已被 git 忽略，不进仓库。
5. 按门禁提示合并冗余换行、把超长 `UE_LOG` 改为两行打包或每参数一行，
   随后重新编译并重跑 Fire 套件，结果一致。

## 补充修订：收紧客户端预测表现门控

复核发现原实现缺少“客户端已知阻塞态”的表现门控：`CanActivateAbility` 的客户端分支
提前返回，因此预测客户端从不评估 `ActivationBlockedTags`；
`ActivateAbility` 的拥有者分支与 `HandlePredictedFeedbackTick` 也都不检查
`State.Reloading` / `State.Equipping` / `State.Dead`，首次预测射击还不检查
武器隐藏与“是否仍是当前装备”。结果是换弹期间按开火会播放本地开火表现，
全自动还会起一段本地循环，直到服务器 Reject 才停。

按已冻结的设计决策做的修订：

- `CanActivateAbility` 保持客户端宽松分支不变，`Super::CanActivateAbility`
  仍在客户端分支之后（不前置），确保请求照常发往服务器；
- 新增 `IsOwnerPredictedFeedbackAllowed()`：在“武器有效、未隐藏、仍是当前装备”
  基础上再排除本机已知的 `State.Reloading` / `State.Equipping` / `State.Dead`；
- `StartOwnerPredictedFeedback` 与 `HandlePredictedFeedbackTick` 都改用该判定：
  阻塞时**只跳过本地表现**，仍然启动/保留 `PredictedFeedbackTimer`，
  阻塞解除后可自行恢复，Ability 生命周期始终由服务器 Reject / Cancel 决定；
- 全自动本地循环保持“不等待服务器 Confirm”。

协调器新增一条 P1 用例（`Reload-fire` 阶段，客户端发起）：
客户端同一帧先换弹再按开火（此时必然还没收到 `State.Reloading`），
静默 0.6 秒后回报本地观测；服务器断言
`0 Authority Commit`、`0 Projectile`、`Ammo` 未被拒射击扣减、
`no State.Firing`、`0 活动 GA_Fire`、客户端已收敛。

Dedicated 实测（`Saved/Automation/Network/20260915_132629`）：

```text
Server: Reload-fire server report: Case=2 PredictedDelta=0 AmmoBefore=4 AmmoNow=10
        Shots=1->1 Projectiles=5->5 ClientConverged=true Valid=true
Client: Reload-fire client report: Case=2 ReloadingTagSeen=false PredictedDelta=0
        ActiveFire=0 FiringTag=false LocalTimer=false Converged=true
整轮：AUTOMATION_TEST_FAILURE=0，两个客户端均到达 AUTOMATION_TEST_CLIENT_SUCCESS
```

## 遗留项

- **8A 用例（客户端已知 `State.Reloading` 后再开火）未落地**。
  该用例需要紧跟 8B 在同一装备窗口内再跑一轮，但现有阶段序列在 8B 之后
  很快就清空背包并归还武器，`GetCurrentWeapon` 变为空，无法稳定提供前置条件；
  同时 `State.Reloading` 也在下一轮开始前已结束。要稳定覆盖它需要重排阶段顺序
  或为它单开一个场景，超出本次最小修订范围。当前只覆盖 8B（Tag 尚未到达）。
  `IsOwnerPredictedFeedbackAllowed()` 的 8A 分支因此仍缺运行期证据。
- 三处 Reject 检查现在验证“服务器 `CanActivateAbility` 不允许 + 结果不变量”，
  不再经过公开 `TryActivateAbility` 的返回值。
  计划 8.3 要求的 `FIRE_PREDICTION_REJECTED`
  应由协调器按 Reject 场景最终结果输出；
  P1-D 需要补一条**由客户端发起**的 Reject 阶段
  （客户端预测 → 服务器拒绝 → 客户端预测实例结束），
  以端到端覆盖预测拒绝路径。
- 计划 6.3 列出的 `OwnerImmediateSemiAuto`、`ListenNoDuplicate`、
  `RemoteConfirmedOnly`、`DedicatedNoLocalFeedback`、`ServerRejectCleanup`、
  `SingleAuthorityProjectile` 本质是多端场景，本提交用 Dedicated 与
  Emulated 会话的 `FIRE_*` 标记证据覆盖，未写成单进程 Editor 自动化测试。
- 本提交只跑了 Dedicated 与 Emulated 两个定向会话，
  未跑 ListenNetwork，也未跑七阶段全量回归（计划要求留到 P1-D）。
- 武器端标记不输出 `PredictionKey`（计划 8.3 禁止持久写进池化 Weapon），
  因此 Owner 确认标记只有 ShotOrdinal / Count / LocalTime。
- 观察到 `Saved/Automation/Logs/*.log` 在本轮出现 4 项警告，
  内容与 P1-A 相同的“最小世界无骨骼网格 / 世界无上下文”噪声。
- 本阶段按用户要求**不提交**，改动（含 Source 与后续开发记录）保持在工作区等待验收。
