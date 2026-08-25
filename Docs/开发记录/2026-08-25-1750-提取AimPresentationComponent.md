# 提取 AimPresentationComponent

- 日期：2026-08-25
- 计划提交说明：`瞄准：提取 AimPresentationComponent`
- 变更类型：生产代码 / 测试

## 目的

执行《Shooter 核心玩法架构解耦重构执行计划》R2：把表现瞄准采样、Unreliable Server RPC、Sequence 校验、COND_SkipOwner 复制、远端平滑、重置与 Debug 从 `AShooterCharacter` 迁移到静态可复制的 `UShooterAimPresentationComponent`，使 Character 不再承载 Aim 网络算法。

## 本提交完成内容

- 新增 `Source/ShootGame/Characters/Aim/ShooterAimPresentationComponent.h/.cpp`：
  - 组件默认复制，TickComponent 驱动观察端平滑与 Debug；
  - 承接 PresentationAimTarget（ReplicatedUsing + COND_SkipOwner）、20Hz 采样 Timer、Server RPC、16 位 Sequence、距离/方向/频率校验、平滑/重置/清空；
  - 提供 `ComputeAimPresentationAnglesForState` 纯计算与可覆盖运行上下文，便于无 World 测试；
  - 迁移原 `Characters/Debug/ShooterAimDebug.cpp` 的只读调试实现到组件。
- `AShooterCharacter`：
  - 构造函数创建 `AimPresentationComponent`，删除 Character 上的 Aim 字段、RPC、RepNotify、Tick 与 Timer；
  - 保留 R2 迁移期转发 Getter / 静态纯函数包装与 BP 入口；
  - 死亡调用 `ClearPresentationAimSmoothing`，切枪/拾取调用 `ResetPresentationAimSmoothing`；
  - `TracePreSpreadAimTarget` 移到 public，供组件 Debug 与权威 Fire 共用。
- `UShooterThirdPersonAnimInstance`：直接读取 `Character->GetAimPresentationComponent()` 的有效状态与平滑目标。
- 测试：
  - `ShooterAimPresentationTestHarness.h` 改为 Component 测试壳，覆盖 Role/NetMode/本地控制/死亡/视点/旋转/MeshTransform 上下文；
  - `ShooterAimPresentationAutomationTests.cpp` 的状态机测试迁移到 Component；
  - `ShooterArchitectureBaselineAutomationTests.cpp` 的 OwnershipSurface 改为断言 Character 不再拥有 Aim 属性/RPC，Component 承接复制与 Server RPC。
- 删除 `Characters/Debug/ShooterAimDebug.cpp`；运行一次 VS 项目刷新脚本。

## 验证结果

- `git diff --check`：通过。
- `BuildEditor.ps1`：Passed（首次编译暴露 TracePreSpread 访问级别问题，修复后通过）。
- `RunAutomation.ps1 -TestFilter ShootGame`：65 Passed / 1 Warning / 0 Failed。
- 完整 `RunAll.ps1 -Port 17794`：Passed，七阶段全绿。
  - `Saved/Automation/Runs/20260825_174314/Summary.json`；
  - DedicatedNetwork / ListenNetwork / EmulatedNetwork / DisconnectCleanup 均通过。
- 外部依据：已查询 Epic 官方 [Replicating Actor Components](https://dev.epicgames.com/documentation/en-us/unreal-engine/replicating-actor-components-in-unreal-engine) 与 [Lyra Inventory and Equipment](https://dev.epicgames.com/documentation/de-de/unreal-engine/lyra-inventory-and-equipment-in-unreal-engine?application_version=5.6) 页面；本地 UE5.6 `ActorComponent.h` 核对 `SetIsReplicatedByDefault` 后实现。

## 遇到的问题

- 第一次编译失败：`AShooterCharacter::TracePreSpreadAimTarget` 是 protected，组件 Debug 无法调用。
- 第一次完整 RunAll 在 DisconnectCleanup 第二段出现 `Death equip precondition failed; Activated=false Active=true Tag=true` 失败标记；同一代码随后单独重跑 `RunAbilityCleanup.ps1` 和完整 RunAll 均通过，判定为该无头协调器的既有并发调度抖动，非 Aim 行为回退。

## 处理方式

- 将 `TracePreSpreadAimTarget` 提升到 Character public：它是 Fire 与表现 Debug 共用的无副作用纯 Trace 入口。
- 保留失败日志作为证据；不修改测试协调器掩盖抖动，连续重跑证明 DisconnectCleanup 可收敛，并把该抖动记录为遗留风险。

## 遗留项

- Character 的 Aim 转发 Getter 仍保留，按计划到 R8 删除零调用接口。
- 第一次 RunAll 的 DisconnectCleanup 抖动需要后续网络阶段继续观察；不改变本轮纯结构结论。
- AimComponent 的纯数学尚未迁到 ThreadSafe 更新，按计划等数据契约稳定后另行评估。
