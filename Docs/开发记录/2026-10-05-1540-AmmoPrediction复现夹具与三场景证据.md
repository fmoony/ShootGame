# Ammo Prediction 复现夹具与三场景证据

- 日期：2026-10-05
- 计划提交说明：`测试：建立AmmoPrediction复现夹具并复现三项风险`
- 变更类型：测试 / 文档

## 目的

完成待解决问题记录 Phase C 的第一步：为 H-A1、H-A2、H-A3 建立可归因的复现夹具与逐端证据。
本提交不修改生产预测语义；是否引入 Activation / Shot acknowledgement、是否扩大预测边界，
留待复现结果后的用户确认。

## 本提交完成内容

- 新增网络夹具模式 `-ShootGameAmmoPredictionTest`：Listen Server + 1 个远端拥有者客户端。
  所有开火输入走生产入口 `DoStartFiring / DoStopFiring`，不直接调用 `Weapon.Fire` 或服务器实现。
- 新增 `Source/ShootGame/Tests/Network/ShooterAmmoPredictionNetworkTests.cpp`：
  `SameValueSnapshot`、`BudgetVeto`、`LateReject` 三个场景与逐端观测。
- `ShooterNetworkTestCoordinator.h/.cpp` 增加该模式的 flag、测试 RPC、观测字段与
  `HandleActorSpawned` 的同值恢复分支；非该模式路径保持原行为。
- 新增 `Docs/已完成计划/AmmoPrediction归因与收敛PhaseC执行计划.md`：范围、复现设计、
  最小充分验证链与五条不变量覆盖确认。

## 验证结果

### 编译与回归

- UBT `ShootGameEditor Win64 Development` 最终成功。
- `RunAutomation.ps1 -TestFilter ShootGame`：Passed=87、Warnings=53、Failed=0、NotRun=0。
  报告 `Saved/Automation/Reports/20261005_153843_ShootGame/`。
- `CheckTextLayout.ps1 -Scope Staged` 与 `CheckSourceIncludePaths.ps1` 通过。

### 网络复现

两轮均为 Listen Server + 1 个远端拥有者客户端；服务器权威增量与拥有端本地观测成对记录。

- 正常：`Saved/Automation/Network/20261005_153728/`，端口 17840。
- 延迟：`Saved/Automation/Network/20261005_153802/`，端口 17841；
  两端 `-PktLag=100`，日志确认 `PktLag set to 100`。
- 两会话均无 `AUTOMATION_TEST_FAILURE`、Fatal error 或 AccessViolation。
- 两会话三条结论完全一致，`Converged=0 Mismatches=3`：

```text
SameValueSnapshot
ServerShots=1 Projectiles=1 ServerMag=1 ServerReserve=5
OwnerMag=1 Pending=1 Predicted=0 -> H-A1 复现

BudgetVeto
AuthorityShots=1 ServerMag 1->0
Owner ActivationDelta=1 FeedbackDelta=0 ConfirmationDelta=1 -> H-A3 复现

LateReject
ServerShots=0 ServerMag=10 ServerReject Tags=State.Reloading
Owner ActivationDelta=1 FeedbackDelta=1 Pending=1 Predicted=9 -> H-A2 复现
```

- H-A1：服务器在同一复制帧内完成「射击 → 恢复到原值」后，拥有端收不到
  `MagazineAmmo` 变化通知，`PendingPredictedShots` 保持 1，本地预测可用弹药被永久压低。
- H-A3：承接 H-A1 的残留预算后，服务器接受并提交该发，拥有端
  `FeedbackDelta=0` 而 `ConfirmationDelta=1`，形成「权威已射击、Owner 无本地预测表现」的单向缺口。
- H-A2：服务器用仅服务器可见的阻塞状态触发真实 GAS Reject，权威射击与权威弹药均不变化；
  拥有端在 Reject 到达前已结束本地预测实例，Reject 后 `PendingPredictedShots` 仍为 1，
  退款没有发生。

### 证据边界

- 夹具用不复制的 `AddLooseGameplayTag` 构造「服务器已知、拥有者尚未收到」的阻塞状态；
  用既有测试弹药钩子恢复精确同值；两者只构造状态，不复制生产判定，也不由夹具自身给出通过结论。
- 当前夹具只输出 MISMATCH / CONVERGED 诊断，不作为验收断言；修复实施后应转为 Converged 断言。
- 未运行 Dedicated、丢包矩阵、断线、重生与切枪边界；未覆盖 FullAuto 连续发数归因。

## 遇到的问题

- 默认地图路径 `/Game/FirstPerson/Lvl_FirstPerson` 已不存在，服务器启动即退出；
  改用 `RunAll.ps1` 同款 `/Game/Shooter/Maps/Lvl_Shooter` 后正常。
- 首次同值构造调用 `ReloadFromReserve`，因 Rifle MagazineSize 大于 1 会补满到容量，
  未形成同值快照；改为测试弹药钩子恢复步骤起点值。
- 沙箱曾拒绝 UBT 写入工作区外的 AppData 日志目录；提升权限后刷新 VS 工程成功。
- 版式门禁先报冗余折行与参数折行，按规则修正后通过。

## 处理方式

- 分别修正地图参数、同值构造方式与折行格式，重编译并重跑两会话。
- 沙箱拒绝按规则提升权限重试一次，未扩大改动范围。

## 遗留项

- 修复方向待用户确认：
  - H-A2：迟到 Reject 的退款上下文如何跨 `EndAbility` 保留；
  - H-A1：是否引入权威弹药版本 / 确认身份来归因 Pending；
  - H-A3：是否允许本地预测换弹补给或调整预算门控语义。
- 夹具的 `MISMATCH` 需要转为修复后的验收断言。
- 未覆盖 Dedicated、丢包、断线、重生、切枪与 FullAuto 多段归因。
- 本提交只含测试夹具、执行计划与本记录，不含生产语义修复。
