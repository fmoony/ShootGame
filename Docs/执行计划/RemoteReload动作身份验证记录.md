# Remote Reload 动作身份：范围与验证记录

- 日期：2026-10-01。
- 本轮只处理动作身份、复制边界刷新、诊断和连续换弹验证。
- 不实施动画时间补偿、Ammo Prediction、HUD 或 Magazine 时间跳转恢复。

## 不变量覆盖确认

- Owner Immediate Feedback：保留本地预测入口；新 ID 不重播 Owner 动画。
  新增证据为拥有者每轮本地 Reload 状态与实际动画进入。
- Local Prediction Obeys Weapon Rules：不改资格判定；验证 Reject 后预测窗口收敛。
  不扩展 Fire 节拍与弹药预算，因本轮不修改这些行为。
- Server Is Final Authority：只在正式接受后增加 ID；Finish / Cancel / Reject 不增加。
  验证服务器接受计数、弹药转移和取消后未转移。
- Remote Confirmed Only：观察端只消费服务器 ID 与 ASC Tag。
  验证每个实际接受的 ID 与新动作识别、动画初始化、Recovery 清理对应。
- One Shot One Result：不改 Fire；针对 Reload 验证一次接受对应一个身份。
  Case 2 使用正式输入打一枪，额外检查没有重复权威消费。

## 当前基线（重新读取源码与运行编辑器 MCP）

- `GA_Reload` 为 LocalPredicted；服务器二次校验目标后建立 WaitDelay。
- `State.Reloading` 为 ActivationOwnedTag；ASC 位于 ShooterPlayerState，采用 Mixed 复制。
- 正式 PlayerState 未覆盖 NetUpdateFrequency；本机 UE5.6 默认值为 1Hz。
  本轮保留该值，只在动作边界请求及时更新，不把低频率认定为历史现场唯一原因。
- AnimInstance 基类逐帧读取 ASC Tag 得到 `bIsReloading`。
- TP Rifle 的 ReloadRecovery Notify 调用 `BeginReloadPresentationRecovery()`。
- Recovery 由上述函数置 true，由非 Reload 更新或武器绑定清理置 false。
- Reload 进入：`bIsReloading && !bReloadPresentationRecovering`。
- Reload 退出：`!bIsReloading || bReloadPresentationRecovering`。
- MCP 当前读取的 `bAlwaysResetOnEntry=False`；不是沿用旧审计结论。
- 保留用户已有 `ABP_FP_Pistol.uasset` 修改。

## 外部依据与 UE5.6 版本核对

- 官方复制文档说明更新频率、Actor 优先级与复制执行顺序的边界。
  ForceNetUpdate 是请求及时更新，不是可靠事件队列，也不保证每个中间值都送达。
  [Actor Replication Flow](https://dev.epicgames.com/documentation/unreal-engine/detailed-actor-replication-flow-in-unreal-engine)
  [Replication Execution Order](https://dev.epicgames.com/documentation/zh-cn/unreal-engine/replicated-object-execution-order-in-unreal-engine)
- 官方 API 提供状态机的局部初始化接口。
  当前网页显示 5.8，不能直接沿用其 SetState 重入签名。
  [FAnimNode_StateMachine](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FAnimNode_StateMachine)
- 本地 UE5.6 `SetState()` 仅在状态索引改变时初始化，不能同状态直接重入。
- 本地 `Initialize_AnyThread()` 会清除该机器的活动过渡并回到入口。
  后续机器更新继续走现有进入条件；不重置整个 AnimInstance 或 Locomotion。
- 本地 Proxy 在图更新前调用 `NativeThreadSafeUpdateAnimation()`。
  本轮在这一安全更新点处理待初始化标记，不在复制回调里直接改动画节点。
- 本地 ASC `ForceReplication()` 实际刷新 OwningActor，即玩家 PlayerState。
  结束刷新必须放在 Super::EndAbility 移除 ActivationOwnedTags 之后。

## 本轮实现

- `AShooterPlayerState::ReloadId` 为 uint32，初值 0，普通 DOREPLIFETIME 对所有观察端复制。
  RepNotify 只记录到达，不在复制回调直接修改动画节点。
- `GA_Reload::ActivateAbility()` 通过第二次服务器目标校验后递增一次。
  递增位于缓存目标之后、创建 WaitDelay 之前；客户端预测不递增。
- 开始请求 PlayerState 更新；Finish / Cancel 在 Super 移除 Tag 后调用 ASC ForceReplication。
  玩家实际刷新 PlayerState，NPC 刷新自己的 ASC 宿主；不调整全局更新频率。
- TP AnimInstance 忽略 Local Owner 的服务器身份回流，避免重新启动本地预测动画。
  观察端保存新 ID，清理旧 Recovery；已有 Reload Tag 为 true 时消费一次新身份。
  如果 ID 先到、Tag 未到，保留待消费身份，不单凭 ID 启动 Reload。
- 在 NativeThreadSafeUpdateAnimation 中只初始化既有 WeaponAction 状态机。
  清除旧过渡与时间后，仍由原来的 bool 进入规则决定 Reload。
  没有新增第二套状态机，也没有改 Locomotion、FP CopyPose 或 AnimBP 资产。
- NativePostEvaluateAnimation 读取实际状态机，记录进入、重启进入和退出。
  游戏线程 TEMP Verbose 日志记录身份、Tag、Recovery 与服务器接受/结束边界。
- 单元测试调用生产身份消费函数，而非复制一份判断逻辑。
  网络夹具复用现有测试 Coordinator，新增 RPC 仅用于该开发测试会话。
  正式 Character、Weapon、PlayerState、Ability 未新增 RPC。

## 验证结果

### 编译与单元回归

- ShootGameEditor Win64 Development：最终构建成功。
- 已执行一次 RefreshVisualStudioFiles；没有自动操作 VS 的重新加载。
- 定向 Automation 共 24 项，全部通过，0 警告、0 失败。
  覆盖 ReloadIdentity、既有 Reload、Equip.CancelReload 与 Aim.Binding。
- ReloadIdentity 的 3 项分别验证服务器递增 / 客户端不能递增、
  bool 始终为 true 时新 ID 清 Recovery 且只消费一次、ID 先于 Tag 到达。
  ContinuousTag 调用生产函数并检查真实图初始化请求，不声称它独自验证了动画求值。
- Magazine 5 项均为 Success，其中 2 项带警告，共 9 条临时 World 清理警告。
  日志为 UWorld::DestroyActor: World has no context；没有断言失败。
  不修改现有 Magazine 测试或 Runtime 以消除本轮范围外警告。

报告：

- `Saved/Automation/Reports/` 下以 `20261001_185045_` 开头的定向报告。
- `Saved/Automation/Reports/20261001_185142_ShootGame.Weapon.Magazine/index.json`。

### Listen Server + 2 Clients

最终正常会话：`Saved/Automation/Network/20261001_185246/`。
最终延迟会话：`Saved/Automation/Network/20261001_185058/`。
两端启动参数均包含 PktLag=100，日志确认此设置生效；它不是实测 RTT=100ms 的声明。
两会话各含 Server.log、Client1.log 和 Client2.log，均无失败 / 致命错误标记。

角色为 Client1 发起者、Listen Host 观察者和 Client2 观察者。
NullRHI 下强制目标 Mesh 求值真实 TP 动画图，结束时恢复 Tick 设置。
夹具不调用 Weapon.Fire 或伪造 Reload Tag；输入走 DoReload 与正式 ASC 解释入口。
取消走服务器 ASC.CancelAbilities；资格夹具只在服务器设置起点弹药。

1. Case 1，Steps 1 / 2：连续完成两轮换弹，对应 ID 1 / 2。
   Rifle 当前容量 40；第一轮 35+2 备弹得到 37，仍缺 3 发。
   下一轮只补服务器备弹后立即再次请求，得到 40 / 17，不绕过满弹拒绝规则。
2. Case 2，Steps 3 / 4：完成后正式 Press+Release 打一枪，再立即 Reload。
   权威 Shots=1、Projectiles=1，40→39；下一轮 ID=3，39→40，备弹 17→16。
   Owner 对这一枪各有一次预测反馈与权威确认。
3. Case 3，Steps 5 / 6：ID=4 已被两名观察者识别后取消。
   取消不再递增，弹药保持 35 / 20；重新请求得到 ID=5，最终 40 / 15。
4. Case 4，Steps 7 / 8 / 9：连续请求覆盖满弹、无备弹两次真实 Server Reject。
   Owner 建立本地预测激活，收到同 PredictionKey 的拒绝后结束；ID 保持 5。
   下一次有效请求只新增 ID=6，最终 40 / 15。

每个最终会话的计数对齐：

```text
Server AcceptedBegin = 6
Server ReloadId       = 1, 2, 3, 4, 5, 6
Server Reject         = 2（不新增 ID）
Host New / Restart    = 6 / 6
Remote New / Restart  = 6 / 6
Owner New / Restart   = 0 / 0（服务器 ID 不重播预测动画）
```

Owner 的正式接受轮次各实际进入一次 Reload；两次被拒绝请求可先出现本地预测动作。
这两次预测不传播给观察端，不应计入 Server Accepted 或 Remote New。
Finish / Cancel 也没有增加 ID；取消轮次没有 Ammo Commit。

每次观察端读取真实 WeaponAction 当前状态为 Reload，并保存首次进入的播放时间。
夹具检查新增进入数与新 ID 一一对应，起点不超过 0.25s，不能只靠 New 计数通过。
本次网络会话观察到了轮次间 false；不能宣称网络现场已复现“漏掉 false”。
bool 始终为 true 的身份消费 / Recovery 清理另由 ContinuousTag 定向测试覆盖。

### 相位与未验证边界

- 正常最终会话中，Remote 实际开始相对 Server AcceptedBegin 晚 7～23ms。
- PktLag=100 最终会话中，相同差值为 108～144ms。
  取同机日志 UTC 时间戳，仅度量开始边界；不是完整动画进度或人工视觉测量。
- 动作身份与重入已验证；仍有网络相位延迟，未加入服务器时间信息。
- Agent 未做人工渲染 PIE 视觉验收；用户在提交前确认验收通过。
  未执行丢包矩阵，也不保证任意断线下的历史动作回放。
- 没有定位用户此前某一次第二轮无反应现场的唯一原因。
  新日志可区分服务器拒绝与已接受但观察端未识别，不把修复结构缺口当作历史现场归因。

### 验证中遇到的问题

- 初次准备 RPC 的跨 Actor 参数在客户端出现 nullptr，但正式 Equipment 当前武器已有效。
  会话在 Step 0 超时，尚未发出 Reload 输入，不能据此判断正式动作身份逻辑失败。
- 夹具改为传稳定 PlayerId，从实际复制角色和正式 Equipment 固定目标。
  后续继续逐端校验同一角色、武器、ASC、Avatar 和真实状态机，不把 Invalid 当作通过。
- 准备失败日志保留于 `Saved/Automation/Network/20261001_182525/`、
  `Saved/Automation/Network/20261001_183022/` 与
  `Saved/Automation/Network/20261001_183625/`。
- `20261001_184214/` 已真实接受并重播首轮，但夹具等待结束后旧 true 快照而超时。
  主动刷新已经送达 false；删除该额外等待条件，保留独立的连续 bool 定向测试。
- `20261001_184718/` 的四场景通过后，关闭客户端产生夹具发起者丢失误报。
  最终完成时对全部服务器测试参与者置 Finished，再用上述两套最终会话复验。

## 修改文件

- `Source/ShootGame/GameFramework/PlayerState/ShooterPlayerState.h` 与同名 .cpp。
- `Source/ShootGame/AbilitySystem/Abilities/ShooterGameplayAbility_Reload.h` 与同名 .cpp。
- `Source/ShootGame/Characters/Animation/ShooterThirdPersonAnimInstance.h` 与同名 .cpp。
- `Source/ShootGame/Tests/Animation/ShooterIKBindingTestHarness.h`。
- `Source/ShootGame/Tests/Animation/ShooterReloadIdentityAutomationTests.cpp`。
- `Source/ShootGame/Tests/Network/ShooterNetworkTestCoordinator.h` 与同名 .cpp。
- `Source/ShootGame/Tests/Network/ShooterReloadIdentityNetworkTests.cpp`。
- 本记录，以及冻结问题记录的已解决清单 / 备注；不回写冻结正文。

没有修改蓝图、武器资产、配置、Ammo Prediction、HUD、Fire 或 Magazine Runtime。
用户已有 FP Pistol 资产修改原样保留，不纳入本次提交。

## 用户验收与提交

- 2026-10-01：用户确认“验收通过可以提交”。
- 关联提交说明：`修复：增加Reload动作身份并验证连续换弹`。
- 提交记录：`Docs/开发记录/2026-10-01-2108-RemoteReload动作身份与连续换弹.md`。
- 本次提交只补记录和验收状态，不增加实现，也不重写前述验证证据。

## 边界与遗留

- 单个复制 ID 是最新身份，不是历史事件队列。
  同一网络更新前全部开始又取消的动作可能合并；不能宣称任意丢包下回放全部历史动作。
- 首轮完整换弹若已满弹，再次请求应被拒绝。
  Case 1 需要显式不足备弹的初始条件与下一轮补给，不能修改正式资格规则。
- 仍不包含服务器时间相位信息；视觉相位落后必须另行记录，不能自动进入 Phase B。
