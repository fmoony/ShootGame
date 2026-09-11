# 通用 Actor Pool 子系统与 Poolable 契约（B1）

- 日期：2026-09-11
- 计划提交说明：`池：新增 World 级通用 ActorPool 子系统与 Poolable 契约`
- 变更类型：生产代码 / 测试 / 构建

## 目的

实施《武器与 Inventory 正式架构实施计划》大阶段 B1：新增 World 级通用 Actor 池与最小 Poolable 协议。底层保持通用，第一位正式消费者是 B2 的 WeaponActor；Projectile 池化按计划延后，本阶段只用轻量测试 Actor 验证。

## 本提交完成内容

- 新增 `Pool/ShooterPoolableActor.h/.cpp`：`IShooterPoolableActor` 最小协议（`OnAcquiredFromPool` / `OnReleasedToPool`），领域绑定与清理由实现者承担，池负责通用复位。
- 新增 `Pool/ShooterActorPoolSubsystem.h/.cpp`（UWorldSubsystem，仅 Game/PIE World）：
  - `Acquire(Class, Transform, SpawnParams)`：池命中弹出并通用复位（变换 / Owner / Instigator / 可见性 / 碰撞 / Tick / 回调），未命中生成后走同一复位；非法类、非权威端、生成失败 fail closed；
  - `Release(Actor)`：回调 → Detach → 隐藏 → 碰撞/Tick 关闭 → Owner/Instigator 清空 → 入池；容量满时销毁；重复归还、非托管、错误 World、PendingKill、非权威端全部 fail closed 不崩溃；
  - `Prewarm(Class, Count)`、`SetClassCapacity/GetClassCapacity`（默认 32）、`RegisterExisting`（重复注册拒绝）、`GetPooledCount`、`IsManaged` 观测口；
  - `Deinitialize` 显式销毁池内与在用 Actor，构成 World teardown 的幂等清理边界；
  - 按 ActorClass 分池（`TMap<UClass, FPool>`），跨 Class 不串池。
- `ShootGame.Build.cs` 新增 `ShootGame/Pool` include 路径。
- 新增测试 `Tests/Pool/ShooterActorPoolAutomationTests.cpp` + `ShooterActorPoolTestTypes.h`（带根组件的轻量 A/B/普通测试 Actor）：
  - `ReuseSameActor`：取-还-取复用同一实例，回调计数、可见性、Tick、变换复位全部正确；
  - `CrossClassIsolation`：A/B 分池互不串池，未实现契约的普通类同样可用；
  - `ReleaseFailClosed`：null/外部对象/重复归还/重复注册全部拒绝且不崩溃，RegisterExisting 后可正常归还；
  - `CapacityAndPrewarm`：容量满后归还销毁不入池，Prewarm 数量正确、第 4 次获取新生成；
  - `WorldTeardown`：销毁 World 后池内 Actor 无悬空（WeakObjectPtr 验证）。

## 验证结果

- `BuildEditor.ps1`：Passed。
- `RunAutomation.ps1 -TestFilter ShootGame.Pool`：5/5 通过（Failed=0）；警告为测试 World `DestroyWorld` 的既有良性日志（`World has no context`）与故意触发的 Release 拒绝日志。
- 按计划本子阶段未接入武器；完整回归保留在大阶段 B 收口。

## 遇到的问题

- 初版 Acquire 的"生成新 Actor"路径没有统一走 `ApplyAcquiredState`，新实例缺少回调与复位；测试 Actor 无根组件导致 `SetActorTransform`/生成变换不生效，首轮测试暴露回调计数 0、变换丢失。
- 两处编译小错：误给 const 形参赋值（SpawnParams 笔误）、指针用 `.` 访问。

## 处理方式

- 生成路径与复用路径统一调用 `ApplyAcquiredState`，两条路径状态一致；测试 Actor 增加根组件后全部断言通过。
- 修正笔误；移除池测试 World 不需要的 `NotifyBeginPlay` 消除 teardown 警告。

## 遗留项

- 池当前无生产消费者；B2 接入 WeaponActor，B3 接入 Inventory 授予/移除/死亡清理。
- 客户端自主 Acquire、Dormancy、动态容量回收按计划明确非目标。
