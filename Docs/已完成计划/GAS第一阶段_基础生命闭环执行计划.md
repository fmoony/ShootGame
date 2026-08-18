# GAS 第一阶段：基础生命闭环执行计划

## 1. 阶段目标

本阶段是 ShootGame 从现有服务器权威 Shooter 基线正式进入 GAS 的第一步。

目标不是“把 Shooter GAS 化”，而是建立一个最小、真实、可验证的 GAS Gameplay 闭环：

```text
GAS 模块可用
→ 玩家 / NPC ASC 生命周期正确
→ Health / MaxHealth 进入 AttributeSet
→ 现有 Projectile Damage 转为 GameplayEffect
→ 继续触发现有死亡 / 计分 / 重生基础逻辑
```

阶段结束时：

- 现有射击方式不变；
- 现有 Weapon / Inventory 不重构；
- 不创建 GA_Fire；
- 不做 Reload；
- 不做客户端预测；
- 不做 Lobby；
- 不做程序地图；
- 不重构 Match Flow。

本阶段重点：

> 真正理解并验证 ASC、Owner/Avatar、AttributeSet、GameplayEffect 与现有服务器权威伤害闭环如何工作。

---

# 2. 强制前置条件：Content Migration Gate

这一部分是 GAS 开发的前置收尾，不计入新功能阶段。

在写第一行 GAS 代码前必须满足：

- `/Game/Shooter` 当前迁移资产可以正常加载；
- 核心 Blueprint / AnimBP / Control Rig 编译正常；
- 已知迁移断引用已修复；
- 当前运行代码、Config、Tests、Scripts 不再依赖错误的 `/Game/Variant_Shooter` 路径；
- Redirector 已按实际情况正确处理；
- `git diff --check` 通过；
- 当前七阶段自动化基线可以通过；
- 至少完成一次迁移后的 Cook 检查；
- 工作区不存在无法解释的失败。

如果 Content Migration 仍有失败：

> 停止 GAS 开发，先修迁移。

不能把迁移问题带入 GAS 阶段。

---

# 3. 阶段拆分

第一阶段拆成三个独立小迭代：

```text
1A：只接入 GAS 模块
↓
1B：建立玩家 / NPC ASC 生命周期
↓
1C：Health / Damage 迁入 GAS
```

规则：

- 前一个子阶段完全通过后才进入下一个；
- 每个子阶段独立开发记录；
- 原则上每个子阶段独立提交；
- 每个子阶段都要有 Focused Test + Full Regression；
- Agent 可以在当前阶段边界内自行修复并重跑。

---

# 4. 通用 Agent 执行规则

每个子阶段开始前：

```powershell
git status --short --branch
```

确认：

- 不覆盖用户已有资产修改；
- 不恢复无关文件；
- 不混入当前阶段之外的重构；
- 不直接编辑 `.uasset`；
- 不顺手实现后续 Ability；
- 不重构 Weapon / Inventory；
- 不提前实现 Prediction。

理解 C++ 时：

- 遵循 `AGENTS.md`；
- 优先使用项目 CodeGraph 工作流；
- 先定位现有调用链，再修改。

检查 Blueprint 时：

- 优先使用项目已有只读 Blueprint / Asset 工具；
- 需要人工 Editor 操作时明确交给用户。

---

# 5. 通用 AI 自主测试闭环

每个子阶段默认：

```text
Preflight
↓
Implementation
↓
Focused Feature Test
↓
Build
↓
Focused Runtime Validation
↓
Full Regression
↓
Read Summary.json / Logs
↓
Failed?
├─ Yes → Analyze → Fix → Re-run
└─ No
↓
Acceptance Report
```

Agent 可以自行：

- 分析编译错误；
- 分析断言失败；
- 分析日志；
- 在当前阶段边界内修复；
- 重新运行测试。

以下情况停止并报告用户：

1. 需要人工 UE Editor 资产修改；
2. 需要新的架构决策；
3. 修复会越过阶段边界；
4. 发现失败属于旧基线；
5. 多轮修复后仍无法可靠定位。

---

# 6. 现有完整回归入口

继续使用：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Tests\RunAll.ps1 -Port <未占用端口>
```

完整验证至少覆盖：

```text
Build
Automation
Standalone
DedicatedNetwork
ListenNetwork
EmulatedNetwork
DisconnectCleanup
```

不能只看退出码。

必须读取：

```text
Saved/Automation/Runs/<timestamp>/Summary.json
```

并按照项目现有自动化 SOP 检查：

- 顶层状态；
- 每个 Phase；
- 测试是否真实执行；
- failure markers；
- success markers；
- Dedicated / Listen 客户端结果；
- 弱网是否真实启用；
- Unreal 进程是否清理。

---

# 7. 子阶段 1A：只接入 GAS 模块

## 7.1 目标

只让工程具备编译 GAS 代码的能力。

不创建：

- ASC；
- AttributeSet；
- GameplayAbility；
- GameplayEffect 逻辑；
- GameplayCue；
- GAS Input。

---

## 7.2 实施

检查 `.uproject` 当前插件状态。

按实际需要启用：

```text
GameplayAbilities
```

在 `ShootGame.Build.cs` 中加入：

```text
GameplayAbilities
GameplayTags
GameplayTasks
```

如果已有则不重复改。

---

## 7.3 禁止越界

本阶段禁止：

```text
ShooterPlayerState + ASC
ShooterNPC + ASC
ShooterAttributeSet
GA_Fire
GE_Damage
GameplayTag 状态设计
```

本阶段只证明：

> GAS 依赖变化不会破坏现有工程。

---

## 7.4 Focused Validation

至少验证：

```text
Editor Target Build
Game Target Build
```

如果项目已有 Server Target，一并验证。

确认：

- Module 可解析；
- Header Include 正常；
- Plugin 加载正常；
- 无 Module Not Found；
- 无 Link Error。

---

## 7.5 Full Regression

运行完整 `RunAll.ps1`。

现有旧射击闭环必须保持不变。

---

## 7.6 验收

必须：

```text
Build              Passed
Automation         Passed
Standalone         Passed
DedicatedNetwork   Passed
ListenNetwork      Passed
EmulatedNetwork    Passed
DisconnectCleanup  Passed
```

且玩法行为没有变化。

---

## 7.7 开发记录

记录：

- 启用了哪些插件；
- Build.cs 新增哪些模块；
- 编译结果；
- 完整回归结果；
- `Summary.json` 路径；
- 是否出现明显编译时间变化；
- 问题与处理。

建议提交：

```text
GAS：接入 Gameplay Ability System 模块
```

---

# 8. 子阶段 1B：建立玩家与 NPC ASC 生命周期

## 8.1 目标

建立 GAS 身份与生命周期，但暂时不承载真实 Gameplay 数值。

重点理解并验证：

```text
Owner Actor
Avatar Actor
```

覆盖：

- Standalone；
- Listen Host；
- Remote Client；
- Dedicated Server；
- Player Respawn；
- NPC。

---

# 9. 玩家 ASC 高层结构

目标：

```text
ShooterPlayerState
└─ AbilitySystemComponent
```

Character 不拥有第二个玩家 ASC。

关系：

```text
Owner Actor  = ShooterPlayerState
Avatar Actor = ShooterCharacter
```

`ShooterPlayerState` 实现：

```text
IAbilitySystemInterface
```

并对外提供 ASC。

---

# 10. 玩家初始化时机

服务器：

```text
Character PossessedBy
↓
取得 PlayerState
↓
取得 ASC
↓
InitAbilityActorInfo(PlayerState, Character)
```

拥有者客户端：

```text
OnRep_PlayerState
↓
取得 PlayerState
↓
取得 ASC
↓
InitAbilityActorInfo(PlayerState, Character)
```

重点不是“调用过函数”，而是机器证明：

```text
Owner / Avatar 实际正确
```

---

# 11. 玩家重生生命周期

必须专门验证：

```text
PlayerState_A
└─ ASC_A
```

第一个 Pawn：

```text
ASC_A.Avatar = Character_1
```

Character_1 死亡 / 销毁后：

```text
PlayerState_A 仍存在
ASC_A 仍存在
```

新 Pawn：

```text
ASC_A.Avatar = Character_2
```

必须证明：

- ASC 没有因 Pawn 重生错误重建；
- Avatar 已切换；
- Owner 仍是原 PlayerState；
- 不残留旧 Character 引用。

---

# 12. NPC ASC

目标：

```text
ShooterNPC
└─ AbilitySystemComponent
```

关系：

```text
Owner = ShooterNPC
Avatar = ShooterNPC
```

NPC 实现：

```text
IAbilitySystemInterface
```

NPC 不依赖：

- PlayerController；
- PlayerState；
- Local Player。

Dedicated Server 上必须独立初始化成功。

---

# 13. ASC Replication Mode

当前计划方向：

玩家：
```text
Mixed
```

NPC：
```text
Minimal
```

但不能只写配置。

必须实际检查：

- 玩家 PlayerState Owner 是否确实是对应 PlayerController；
- Mixed 所依赖的 Owner 关系是否成立；
- Listen / Dedicated 行为是否一致。

如果真实工程与计划假设冲突：

> 停止并报告，不为了匹配文档硬改。

---

# 14. 子阶段 1B Feature Tests

建议新增：

```text
Shooter.GAS.ASC.PlayerLifecycle
Shooter.GAS.ASC.PlayerRespawnAvatar
Shooter.GAS.ASC.NPCLifecycle
```

## 14.1 PlayerLifecycle

服务器：

```text
ASC != nullptr
OwnerActor == ShooterPlayerState
AvatarActor == ShooterCharacter
```

拥有者客户端：

```text
ASC != nullptr
OwnerActor == ShooterPlayerState
AvatarActor == Local ShooterCharacter
```

---

## 14.2 PlayerRespawnAvatar

重生前记录：

```text
ASC Identity
Old Avatar
PlayerState
```

重生后验证：

```text
ASC 仍属于同一 PlayerState 生命周期
New Avatar != Old Avatar
Avatar == New Character
Owner == same PlayerState
```

---

## 14.3 NPCLifecycle

Dedicated Server：

```text
NPC ASC != nullptr
Owner == NPC
Avatar == NPC
```

---

# 15. 子阶段 1B 日志

为学习和定位，本阶段可以加入开发日志：

```text
Actor
Role
NetMode
ASC
OwnerActor
AvatarActor
PlayerState
Character
```

阶段完成后：

- 保留自动化需要的日志；
- 删除纯调试噪声。

---

# 16. 子阶段 1B Full Regression

Focused Tests 通过后，再运行完整七阶段。

必须确认现有：

- 武器；
- Projectile；
- Damage；
- Death；
- Score；
- Respawn；
- Disconnect；

都没有因为“只是加 ASC”而变化。

---

# 17. 子阶段 1B 开发记录

记录：

- Player ASC 宿主；
- NPC ASC 宿主；
- Owner / Avatar 实际结果；
- Listen Host；
- Remote Client；
- Dedicated；
- Respawn 前后结果；
- 自动化测试名称；
- `Summary.json` 路径；
- 问题和修复。

建议提交：

```text
GAS：建立玩家与 NPC ASC 生命周期
```

---

# 18. 子阶段 1C：Health / MaxHealth 迁入 GAS

## 18.1 目标

让 GAS 第一次真正承载 Gameplay 数据。

只迁：

```text
Health
MaxHealth
Damage
```

保留现有：

- Projectile；
- Weapon；
- 当前开火链；
- Death；
- Kill；
- Team Score；
- Respawn；

作为已有闭环。

---

# 19. ShooterAttributeSet

新增：

```text
ShooterAttributeSet
├─ Health
└─ MaxHealth
```

玩家：

```text
ShooterPlayerState
├─ ASC
└─ ShooterAttributeSet
```

NPC：

```text
ShooterNPC
├─ ASC
└─ ShooterAttributeSet
```

共享的是：

> AttributeSet 类型。

不是：

> AttributeSet 实例。

---

# 20. Health 初始化

出生 Health 不再依赖旧 Pawn 内存。

目标：

```text
Initialization GameplayEffect
↓
MaxHealth = 配置值
Health = MaxHealth
```

玩家重生：

```text
PlayerState ASC 保留
↓
Avatar 切到新 Character
↓
重新初始化当前 Pawn 生命周期的 Health
```

必须验证：

- 新 Pawn 满血；
- 死亡前 Health 不错误残留；
- ASC 持续存在不代表 Health 必须跨生命保留。

---

# 21. Damage 桥接

第一版不同时重写开火。

保留：

```text
Projectile Hit
↓
Server Damage Entry
```

最终改成：

```text
Damage
↓
GameplayEffect
↓
Target ASC
↓
Health Attribute
```

现有 `TakeDamage` 可以暂时作为桥接入口。

本阶段改变：

> 伤害最终如何作用到 Health。

不改变：

> 子弹如何发射。

---

# 22. 死亡桥接

监听：

```text
Health Change
```

当：

```text
Health <= 0
```

进入现有死亡闭环。

必须保证：

- 同一次伤害只触发一次死亡；
- 不重复计分；
- 不重复触发 Death；
- NPC 仍触发现有死亡通知；
- 玩家仍按当前基线死亡 / 重生。

本阶段不强制把：

```text
bIsDead
```

立刻改为 GameplayTag。

等 GAS 生命周期真正跑通后再决定。

---

# 23. HUD

如果当前 HUD 读取旧 Health，迁移为：

```text
ASC / Attribute Health Change
↓
HUD Update
```

优先事件驱动，不新增 Tick 轮询。

验证：

```text
Server Health
Owner Client Health
HUD Health
```

最终一致。

---

# 24. 子阶段 1C Feature Tests

建议新增：

```text
Shooter.GAS.Health.Initialization
Shooter.GAS.Health.Damage
Shooter.GAS.Health.Death
Shooter.GAS.Health.Respawn
Shooter.GAS.Health.NPCDeath
```

---

## 24.1 Initialization

玩家和 NPC：

```text
Health == MaxHealth
Health > 0
```

服务器和客户端最终一致。

---

## 24.2 Damage

固定场景：

```text
Before = 100
Damage = 25
After = 75
```

证明：

- 只应用一次；
- Server 权威；
- 客户端最终收敛；
- HUD 正确。

---

## 24.3 Death

当：

```text
Health <= 0
```

验证：

- Death 只触发一次；
- Kill / Death 只增加一次；
- Team Score 不重复；
- Character 进入正确死亡状态。

---

## 24.4 Respawn

```text
Old Character Dead
↓
New Character
↓
ASC Avatar 更新
↓
Health 初始化
```

验证：

```text
New Health == MaxHealth
ASC Owner 未改变
ASC Avatar == New Character
```

---

## 24.5 NPCDeath

```text
Projectile Damage
↓
NPC ASC
↓
Health <= 0
↓
现有 NPC Death 流程
```

验证 StateTree / AIController 依赖的死亡通知无回退。

---

# 25. 子阶段 1C 网络验收

至少验证：

```text
Standalone
Listen Server
Dedicated Server
```

场景：

1. Player → Player Damage；
2. Player → NPC Damage；
3. Player Death；
4. NPC Death；
5. Respawn；
6. HUD Health；
7. Score 不重复。

---

# 26. 弱网验收

本阶段不做预测。

仍运行 EmulatedNetwork，目标是验证：

```text
高延迟 / 丢包
→ 服务器权威 Health
→ 客户端最终收敛
```

必须无：

- 永久 Health 分歧；
- 重复 Damage；
- 重复 Death；
- 重复 Score。

---

# 27. Full Regression

Focused Tests 全部通过后：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Tests\RunAll.ps1 -Port <未占用端口>
```

读取：

```text
Saved/Automation/Runs/<timestamp>/Summary.json
```

七阶段全部通过。

---

# 28. 第一阶段明确不做

禁止顺手实现：

```text
GA_Fire
GA_Reload
GA_Equip
Local Predicted
GameplayCue 枪口表现
WeaponDefinition 重构
WeaponInstance FastArray 重构
WeaponActor Pool
Projectile Pool
Lobby
Session
PvP Round Flow
PvE Match Flow
Procedural Map
```

如果 Health 迁移时发现这些系统似乎也要改：

> 优先用桥接维持旧实现。

只有无法桥接时才停下来重新讨论。

---

# 29. 第一阶段最终验收

必须同时满足：

## GAS 模块

```text
GameplayAbilities
GameplayTags
GameplayTasks
```

可编译、可运行。

## 玩家 ASC

```text
Owner = PlayerState
Avatar = Current Character
```

Standalone / Listen / Dedicated / Respawn 正确。

## NPC ASC

```text
Owner = NPC
Avatar = NPC
```

Dedicated 正确。

## Health

玩家和 NPC 都使用：

```text
ShooterAttributeSet
Health
MaxHealth
```

## Damage

正式伤害链：

```text
Projectile
→ Server
→ GameplayEffect
→ ASC
→ Health
```

## Death

Health 到零继续进入现有：

- Death；
- Kill；
- Score；
- Respawn / NPC Destroy。

## 自动化

Focused GAS Tests 通过，且：

```text
Build              Passed
Automation         Passed
Standalone         Passed
DedicatedNetwork   Passed
ListenNetwork      Passed
EmulatedNetwork    Passed
DisconnectCleanup  Passed
```

---

# 30. 阶段结束后必须暂停复盘

第一阶段通过后，不让 Agent 自动进入下一大阶段。

先人工复盘：

1. 是否真正理解 PlayerState ASC / Character Avatar；
2. Respawn 时 ASC 实际生命周期；
3. AttributeSet 如何同步；
4. GameplayEffect 如何改变 Health；
5. Death 与 GAS 的桥接是否清楚；
6. 是否发现当前架构假设与真实实现冲突。

然后再决定第二阶段详细做：

```text
第三人称动画稳定
```

还是：

```text
Weapon / Inventory 新架构
```

后续计划根据第一阶段真实经验更新，而不是机械执行旧路线。
