# 输入控制流分层与 WhileInputActive 策略

- 日期：2026-09-19
- 计划提交说明：`重构：输入控制流按采集与解释分层并引入 WhileInputActive 策略`
- 变更类型：生产代码 / 测试 / 文档

## 目的

本提交把输入控制流的两个问题一次收敛，它们共用同一批文件、构成同一个可验证闭环：

1. **输入行为不再由外部同步的派生状态表达**。此前「按住可持续」由
   `InputBehavior.HeldRepeat` 动态 Spec 标签承载，由 Equipment 在换枪时同步到 Fire Spec 上，
   并随 Spec 复制。这条链需要宿主在正确时机写标签、需要复制与脏标记、还需要独立的
   HeldRepeat helper 与重试链，是 `Spec.InputPressed` 之外的第二份意图来源。
   现在改由 Ability 自己声明激活策略（`ActivationPolicy`），GA_Fire 按当前武器动态返回。
2. **输入语义不再分散在多个函数**。此前 `AbilityInputTagPressed` 当场激活、当场登记短期缓冲、
   当场维护归因成员，`AbilityInputTagReleased` 当场执行完整释放，只有按住重试留给
   `ProcessAbilityInput`；阅读者必须同时打开三个函数才能拼出完整控制流。
   现在收敛为「采集层 + 解释层」两层，形状对齐 Lyra 的
   `ULyraAbilitySystemComponent`（三组采集 + 单一 `ProcessAbilityInput`），
   但保留本项目自己的 Semi 短期输入缓冲。

## 本提交完成内容

### A. WhileInputActive 策略取代 HeldRepeat 标签

- 新增 `EShooterAbilityActivationPolicy { OnInputTriggered, WhileInputActive }` 与
  `UShooterGameplayAbility::GetActivationPolicy(ActorInfo)`：默认单次按下沿，
  显式传 ActorInfo（未激活实例没有 `CurrentActorInfo`，策略查询不得读实例缓存），
  允许按当前 Gameplay Context 动态返回。
- `UShooterGameplayAbility_Fire` 覆写策略：当前武器 `IsFullAuto()` 时 WhileInputActive，
  否则 OnInputTriggered；同一次武器切换不需要任何标签同步。
- 删除 `InputBehavior.HeldRepeat` 标签定义与声明、
  `UShooterEquipmentComponent::SyncCurrentWeaponFireInputBehavior` 及其两处调用与 include。
- 删除此前为 HeldRepeat 服务的 Spec 脏标记 / 复制同步、HeldRepeat helper 与单点重试链、
  blocker 监听与 next-tick 重试排程；`TransientInputBlockedTags` 只保留 Semi 失败分类用途。
- `Docs/已完成计划/输入缓冲与Reload本地预测执行计划.md` 同步并归档：新增解释策略与门控的单元，
  旧的 HeldRepeat 单元标记为已取代，文首加归档说明，引用它的现役文档同步更新链接。

### B. 采集与解释分层

采集层（`ShooterAbilitySystemComponent.h/.cpp`）：

- 新增三个采集容器：`PressedInputTags`、`ReleasedInputTags`、`HeldInputTags`，
  三者均为 `TArray<FGameplayTag>`。
  项目输入身份天然是 InputTag（`FindAbilitySpecFromInputTag` 一 Tag 查一个 Spec），
  因此不引入 `{Handle, InputTag}` 结构体，也不复制 Lyra 的 SpecHandle 数组。
- `AbilityInputTagPressed`：找 Spec → `PressedInputTags.AddUnique` + `HeldInputTags.AddUnique`；
  不再 `TryActivateAbility`、不再写 `Spec.InputPressed`、不再建立缓冲。
- `AbilityInputTagReleased`：`ReleasedInputTags.AddUnique` + `HeldInputTags.RemoveSingleSwap`；
  不再当场发送 `ServerSetInputReleased`。
- 两者保留非本机拥有者视图直通分支（服务器侧 NPC 没有每帧解释入口，`ShooterNPC.cpp`
  直接调用这两个函数），直通分支不写采集容器。

解释层（`ProcessAbilityInput`）按序：短期按下沿 → 按住持续 → 本帧按下沿 → 本帧松开 → 清帧级采集。

- Buffered Press：固定 150ms 绝对窗口内**每帧最多尝试一次**（此前只尝试一次即丢弃，
  与头文件注释和执行计划不一致，本次统一为「窗口内可重试」）；
  本地失败既不删除也不续期，只有成功 / 过期 / ContextChanged 才删除；
  同一 Tag 出现新 Press edge 时旧窗口立即作废（新边失败由分类重新登记固定窗口）。
  遍历用 `BufferedInputs.GetKeys` 键快照，避免成功路径在遍历中删除条目。
- Held：`HeldInputTags` 中且本帧未被 Pressed 命中的 Tag，才按 WhileInputActive 策略重试。
  首帧按下统一按 Press edge 处理，从下一帧起才走 Held，因此同一 Tag 每帧最多一次尝试，
  且 Press / Held 两个来源互斥（不依赖去重）。
- Pressed：统一走引擎标准入口 `AbilitySpecInputPressed`（写 `Spec.InputPressed`，
  并在已激活时转发按下事件）；已激活则只完成事件转发，未激活才执行本次 Press edge 激活。
- Released：统一调用 `ReleaseInputTag`，真实松开仍然走可靠 `ServerSetInputReleased` +
  `AbilitySpecInputReleased`。
- 三个分支按 InputTag 互斥，唯一的顺序契约是「松开在激活之后处理」，保证同帧点击恰好一枪。

归因与生命周期：

- 新增唯一激活点 `TryActivateInputTag(InputTag, Spec, bPressEdge)`：只负责设置 / 清除
  `CurrentPressAttemptInputTag`、调用 `TryActivateAbility`、返回 bool；
  窗口消费、补释放等后续语义留在各自分支。
- 删除 `PendingPressInputTag`、`PendingPressAbilityClass`、`bPressInFlight`
  （3 个易漂移成员）→ 收敛为 1 个 `FGameplayTag CurrentPressAttemptInputTag`。
  只有 Press edge 尝试会设置它，因此 Held 与 Buffered 分支的失败结构上不可能创建 / 续期窗口。
- `HandleAbilityFailed` 保持为唯一失败分类器：游标无效即返回；用游标解析 Spec 并比对失败回调的
  Ability 类（引擎对 InstancedPerActor 传主实例、策略早退路径传 CDO）；标签分类逻辑不变。
- `InvalidateInputIntents` 统一清理三个采集容器、`BufferedInputs` 与 `Spec.InputPressed`，
  仍然不派发 `InputReleased`、不发送 Release RPC；调用边界不变
  （Death / Respawn / `InitAbilityActorInfo` / `ClearActorInfo` / `EndPlay`）。
- 新增 marker `INPUT_BUFFER_RETRY`（窗口内本地失败但保留）；`INPUT_BUFFER_CONSUMED`
  只在成功时输出（此前失败也输出 `Reason=Rejected`），其余 marker 名称与语义不变。

驱动点：

- 删除 `AShooterCharacter::Tick`（当时它只有「每帧提交输入处理时点」这一个职责）。
- 新增 `AShooterPlayerController::PostProcessInput` override：引擎在
  `UPlayerInput::ProcessInputStack` 中先执行 `EvaluateInputDelegates`（本帧全部输入回调），
  再调用 `PostProcessInput`，因此「采集完整 + 同帧解释」成为引擎输入管线的性质，
  不再依赖 PC 与 Pawn 的 tick 先后。

测试：

- `ShooterAbilityInputBufferAutomationTests.cpp`：采集与解释分离后，
  「按下之后立刻断言」的位置补一次 `ProcessAbilityInputForTest()`；
  新增 `SameFramePressReleaseFiresOnce`（同帧点击恰好一枪）与
  `NewPressEdgeSupersedesBufferedPress`（新按下沿作废旧窗口，不会补出第二枪）；
  `WhileInputActiveReleasedPressDoesNotRetry` 保持阻塞到解释时点，语义不变。
- 新增测试观察接口 `GetHeldInputTagCountForTest`，用于断言采集与生命周期清理。
- `ShooterNetworkTestCoordinator.cpp`：Reload-fire Case=2 的「本机是否已知换弹阻塞」探测
  从按下当帧改为解释之后（结算块内首次观测），因为按下当帧不再改变 Gameplay 状态。

## 验证结果

以下命令全部在 A + B 都落地之后执行，验证对象是本提交的最终工作区状态。

- `Scripts/Tests/BuildEditor.ps1`：ShootGameEditor Win64 Development 编译成功
  （首轮 105.77s；协调器修正后增量 15.33s），链接基线 DLL，无 `-0001` 变体。
- `Scripts/Tests/RunAutomation.ps1 -TestFilter ShootGame`：
  `Saved/Automation/Reports/20260919_143623_ShootGame`，共 132 项，`Failed=0 NotRun=0`，
  其中 `ShootGame.Ability.InputBuffer.*` 13 项全部 Success（含两条新增用例）。
  两条新增用例只存在于新二进制，运行新鲜度由内容自证。
- 网络三段（`Saved/RunInputLayeringNetwork.ps1`，端口 17802 / 17803 / 17804）：
  - Dedicated（2 客户端，45s）：`Saved/Automation/Network/20260919_143914` 通过；
  - Listen（1 客户端，45s）：`Saved/Automation/Network/20260919_143959` 通过；
  - Emulated（2 客户端，90s，`-PktLag=100 -PktLoss=2`）：
    `Saved/Automation/Network/20260919_144034` 通过。
- 证据内容（新架构可观察）：
  - `Owner accepted-shot evidence ... Valid=true`：Dedicated 2/2、Listen 1/1、Emulated 2/2，
    同帧按下 + 松开的点击路径在解释时点仍然恰好形成一次动作边界。
  - `Reload-fire server report: Case=2/1 ... Known=true LocalRefused=true Valid=true`：
    两段用例与改动前基线（`Saved/Automation/Network/20260919_120641`）判定路径完全一致
    （`ServerRejected=false`）。
  - `INPUT_BUFFER_REGISTERED → INPUT_BUFFER_RETRY(逐帧) → INPUT_BUFFER_DROPPED`：
    窗口内逐帧重试、绝对过期、失败不续期均按新语义生效；
    这些尝试全部是本地拒绝（不产生任何网络请求）。
  - `INPUT_HELD_RETRY`：Dedicated 客户端 351 次（347 次本地拒绝），
    与改动前基线同量级（195 次 / 185 次拒绝），持续意图仍只由 Held 集合表达。
- `Scripts/Development/CheckTextLayout.ps1 -Scope WorkingTree`：
  `Files=17 Warnings=2`，退出码 0；2 条 warning 是测试文件里
  `GetActiveAbilityCountForClass(...::StaticClass()), 0);` 这一行的
  「unable to verify last-parameter boundary」启发式误报（该模式在 HEAD 中已存在），已人工确认。
- `Scripts/Development/CheckSourceIncludePaths.ps1`：`Files=129 Headers=52` 通过；
  `git diff --check` 干净。

## 遇到的问题

- HeldRepeat 方向实施期间曾出现「同帧点击丢输入」：网络测试协调器在同一帧
  `DoStartFiring(); DoStopFiring();`，当时把 Press edge 推迟到下一帧解释会被松开清掉。
  该问题在 A 部分以「Press edge 当场尝试」解决，B 部分改为「边沿入采集容器、
  同一次解释内先激活后释放」后仍然成立。
- B 部分首次 Dedicated 段失败：`Reload-fire case 2 invalid; ... Known=false
  ServerRejected=false LocalRefused=false`。原因不是 Gameplay 行为回归，而是网络测试的探测时点：
  用例在 `DoReload()` 之后同帧读 `HasMatchingGameplayTag(State.Reloading)`，
  采集分离后该标签要到输入处理时点才由预测的 GA_Reload 挂上，当帧必然读到 false。
- 随后两次 Dedicated 段失败于既有抖动：
  `Remote invariant invalid: Confirmed=2 Montage=2 Muzzle=2 Sound=2 Auth=3 Exact=false
  Stable=true`。该签名与数字在改动前会话中已出现过（观察窗口 ±1 帧 race），非本次引入。
- 文本版式检查最初报 4 处「redundant line break」（2 处 ASC、2 处测试），已合并为单行。

## 处理方式

- 同帧点击：Press edge 在解释时点统一尝试，Released 在激活之后处理；
  新增 `SameFramePressReleaseFiresOnce` 用例固定该语义。
- Reload-fire 探测：把 `bClientReloadFireTagSeen` 的采样移到客户端结算块（解释之后首次观测），
  删除按下当帧那次现在必然为 false 的读取，并同步用例注释；
  用例判定路径与改动前一致（本地拒绝、无服务器 Reject）。
- 既有抖动：端口轮换重跑三段网络会话，全部通过；按既有做法记录为已知抖动，
  未修改不变量判定代码。
- 版式：按检查器要求合并可合并的行，保留 2 条已确认的启发式误报。

## 遗留项

- `ShouldAttemptWhileInputActive(ActorInfo)` 未实现：Ammo == 0 的保守本地门控留待后续。
- 采集与解释之间存在一帧内相位差：`Spec.InputPressed` 由解释时点写入，
  因此「按下当帧、解释之前」读它仍是上一帧的值（生命周期失效入口除外）。
  真实玩家输入无新增延迟（回调与解释在同一帧的输入管线内）；
  测试协调器这类直接调用输入入口的路径最多相差一帧。
- 若「按下当帧被阻塞、解释时点前阻塞已解除」，Press edge 会在解释时点按当前真值形成一次
  动作边界（乐观预测的直接推论）；旧的即时解释模型会把它转为短期按下沿或被 WhileInputActive
  丢弃。该窗口极窄，本次网络会话未触发。
- 逐帧重试在开发构建会为每个被阻塞的按下沿输出约「窗口 / 帧长」条 `INPUT_BUFFER_RETRY`
  标记（无渲染客户端约 1000 fps 时为 150 条级），仅存在于 `WITH_DEV_AUTOMATION_TESTS`。
- 空弹匣按住不放时仍会按 RTT 频率产生服务器 Reject（当前实现接受该代价，未加 Ammo 门控）。
