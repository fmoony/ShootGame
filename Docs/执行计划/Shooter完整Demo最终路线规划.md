# Shooter 完整 Demo 最终路线规划

## 1. 文档定位

本文记录 ShootGame 从当前多人 Shooter 基线逐步升级为“可完整游玩、可联机、可自动验证”的多人射击 Demo 的最终方向。

本文只冻结：
- 最终产品目标；
- 主要玩法规则；
- 关键系统边界；
- 推荐实施顺序；
- 已达成共识的高层架构。

本文不冻结 PredictionKey、GameplayCue 具体触发方式、RepNotify、AbilityTask、Dormancy 等过早实现细节。原则是：

> 只详细设计未来 1～2 个阶段，做完、验证、复盘后再继续细化。

---

## 2. 最终 Demo 定位

目标：

> 基于 UE5.6 C++ 的完整多人射击 Vertical Slice，支持 PvP / PvE、Lobby、回合流程、武器与背包、程序化战场、GAS、服务器权威、渐进式客户端预测，以及可由 AI/Agent 自主执行的大部分自动化验证。

整体流程：

```text
Main Menu
→ Host / Find Room
→ Lobby
→ Ready / 分队 / 模式选择
→ Match
→ 程序化生成本局战场
→ Countdown
→ PvP / PvE Gameplay
→ Result
→ Return Lobby / Main Menu
```

---

## 3. 联机目标

第一阶段：
- LAN Session；
- Listen Server。

长期要求：
- Gameplay 保持 Dedicated Server Compatible；
- 后续可切换 Internet Online Backend；
- UI 不直接绑定具体 OnlineSubsystem；
- Session 层独立封装。

---

## 4. Lobby 规则

第一版支持：
- Host 创建房间；
- 其他玩家搜索并加入；
- PvP / PvE 模式选择；
- 玩家列表；
- Ready；
- PvP 自选 Team A / Team B；
- Host Start。

规则：

```text
玩家进入 Lobby → 默认 Not Ready

玩家可 Ready / Unready

PvP 换队
→ 不取消 Ready

Host 修改模式或关键比赛配置
→ 全员取消 Ready

Host Start 时服务器重新检查：
- 所有人 Ready
- PvP 双方至少各 1 人
- 每队最多 5 人
```

普通玩家不能 Start。

---

## 5. PvP 第一版规则

### 5.1 人数

支持：
```text
1v1 ～ 5v5
```

允许不平衡人数，例如：
```text
1v2
2v4
1v5
```

只要求：
- Team A 至少 1 人；
- Team B 至少 1 人；
- 单队最多 5 人。

### 5.2 单生命回合

```text
Round Start
→ 战斗
→ 玩家死亡
→ 本回合不能复活
→ 等待 Round End
```

### 5.3 回合胜负

一队全灭：
```text
→ 另一队立即获胜
```

Round Timer 到 0：
```text
比较 Team A 当前总 Health
和 Team B 当前总 Health

A > B → A Win
B > A → B Win
A == B → Draw
```

死亡玩家 Health 视为 0。

### 5.4 比分

正常胜利：
```text
胜方 +1
```

平局：
```text
双方各 +1
```

必须独立维护：
```text
ActualRoundCount
```

不能用比分总和推算实际回合数。

### 5.5 MR12 风格

```text
每半场 12 个实际回合
第 12 个实际回合结束
→ Halftime
→ Swap Spawn Side
```

Team A / Team B 身份不交换，只交换出生边。

常规阶段：
```text
单方先到 13 → Match Win
```

双方同时到 13：
```text
13:13 → Overtime
```

### 5.6 Overtime

```text
领先 2 分 → Match Win
每 3 个 OT 实际回合 → Swap Spawn Side
```

平局回合仍双方各 +1。

---

## 6. PvE 第一版规则

支持：
```text
1～5 名玩家
```

第一版为清场型 PvE：

```text
Countdown
→ 生成固定一批 NPC
→ 战斗
```

胜利：
```text
所有 NPC 死亡 → Victory
```

失败：
```text
所有玩家死亡 → Defeat
```

玩家死亡后本局不复活。

第一版不做：
- 波次；
- Boss；
- 生存计时；
- 队友救援；
- 复活。

---

## 7. Match Framework

高层建议：

```text
Lobby
├─ ShooterLobbyGameMode
└─ ShooterLobbyGameState

Match
├─ ShooterMatchGameModeBase
│  ├─ ShooterPvPGameMode
│  └─ ShooterPvEGameMode
└─ ShooterMatchGameStateBase
   ├─ ShooterPvPGameState
   └─ ShooterPvEGameState
```

公共 Match 基类只管理真正公共生命周期：

```text
Preparing
→ MapLoading
→ Countdown
→ Playing
→ Ending
→ Finished
```

以及 Match Timer、玩家加入/离开、Alive/Dead 基础统计、公共 UI 状态。

PvP / PvE 规则各自放入对应模式，不构建巨型万能 GameMode。

---

## 8. 程序化地图

第一版使用：
> 基于 Grid 的程序化灰盒战场。

目标：
- 省美术资源；
- 快速可玩；
- Seed 可复现；
- 易自动验证；
- 后续可换模块化美术。

第一版不用 World Partition。

服务器权威生成：

```text
Server
→ Generate Map Manifest
├─ Server 根据 Manifest 构建权威战场
└─ Replicate Manifest
    ↓
  Clients
    ↓
根据同一 Manifest 构建本地战场
```

Manifest 至少表达：
- Seed；
- Grid / Cell 布局；
- 墙体 / 地板 / 掩体；
- Team A / Team B Spawn；
- PvE Player Spawn；
- NPC Spawn；
- Layout Hash。

整场 Match 使用同一布局，只有下一场 Match 才重新生成。

自动化方向：
- 连通性；
- Spawn 合法；
- NPC 可达；
- 同 Seed 可复现；
- Server / Client Layout Hash 一致。

---

## 9. GAS 高层定位

GAS 主要承担：
- Ability；
- Health / MaxHealth；
- GameplayEffect；
- GameplayTag；
- 行为互斥；
- Cooldown；
- 后续客户端预测；
- 部分网络化表现事件。

普通 Gameplay 系统继续承担：
- WeaponRuntimeSubsystem（启动配置快照 + WeaponId 池）；
- Inventory（WeaponActor + SlotIndex）；
- WeaponConfigRow（`DT_WeaponData`，仅启动导入）；
- WeaponActor；
- Pickup；
- Lobby；
- Match Rules；
- Procedural Map。

玩家：

```text
ShooterPlayerState
├─ AbilitySystemComponent
└─ ShooterAttributeSet

ShooterCharacter = Avatar Actor
```

即：
```text
Owner = ShooterPlayerState
Avatar = ShooterCharacter
```

NPC：

```text
ShooterNPC
├─ AbilitySystemComponent
└─ ShooterAttributeSet

Owner = Avatar = ShooterNPC
```

---

## 10. 武器系统高层方向

这些是当前架构假设，后续实现中允许根据真实问题调整。

### 10.1 武器模板（已由单表纠偏取代 Definition）

2026-09-11 的 [单表武器配置纠偏小计划](../已完成计划/单表武器配置纠偏小计划.md) 撤销了
`UShooterWeaponDefinition` 配置层；武器模板改为 `DT_WeaponData` 一行：

```text
FShooterWeaponConfigRow（DT_WeaponData 行）
├─ WeaponActorClass
├─ Ammo / Fire / Timing / Attack / Socket
├─ Mesh / Anim / FX
└─ View（FiringRecoil / FirstPersonCompositionDrop）
```

配置只保留一处来源，经 `ShooterWeaponTable` 集中解析；详细边界见
[Inventory 与武器数据架构](../架构/Inventory与武器数据架构.md)。

### 10.1.1 原 WeaponDefinition 假设（历史记录）

```text
ShooterWeaponDefinition
├─ Ammo Config
├─ Fire Config
├─ Presentation Config
└─ FireBehavior
```

第一版只实现：
```text
Projectile FireBehavior
```

Rifle / Pistol / GrenadeLauncher 共用这一行为，以数据配置区别。

### 10.2 WeaponActor 即运行时武器实例（2026-09-12 起）

第一版：
```text
World 开始时按 WeaponId 预创建 WeaponActor，池内 Actor 即具体武器实例
```

静态身份与配置：
```text
WeaponId（创建后不变）
RuntimeConfig 快照（启动冻结）
FireBehaviorInstance
```

可变状态：
```text
MagazineAmmo
ReserveAmmo
Owner / Instigator
LifecycleState
Timer / Delegate
```

MagazineAmmo / ReserveAmmo 都属于具体 WeaponActor；不再生成 InstanceId。

### 10.3 Inventory

OwnerOnly FastArray 是 Owner 的权威武器数据源，负责：
- 拥有关系（直接引用 WeaponActor）；
- SlotIndex；
- 判重（WeaponActor.WeaponId）；
- Remove / Clear 时把 WeaponActor Release 回池。

完整 Inventory OwnerOnly；观察者只通过 Equipment.CurrentWeaponActor 获取公共持枪表现。

### 10.4 Equipment

只保存并复制：
```text
CurrentWeaponActor
```

Equipment 负责装备事务、表现收敛与装备变化事件；不再维护 ActiveWeaponInstanceId。

### 10.5 Pickup

Pickup 只保存 `FName WeaponId`，服务器 Overlap 顺序为：
`HasWeaponId → WeaponRuntime.AcquireWeapon → Inventory.AddWeapon → Equipment.EquipWeapon → 消费`。
编辑器预览 Mesh 是唯一允许的 Editor-only 查表入口。

### 10.6 死亡

```text
玩家死亡
→ 停止武器行为
→ 清空 Inventory / Equipment
→ 所有 WeaponActor Release 回对应 WeaponId Bucket
→ CurrentWeaponActor = nullptr
```

每个新回合 / 新 PvE Match 重新获取武器。

---

## 11. WeaponRuntimeSubsystem（2026-09-12 起）

通用 Actor Pool 已被
[武器启动预配置与实体池简化重构方案](../已完成计划/武器启动预配置与实体池简化重构方案.md)
删除，
当前只有武器专用池：

```text
UShooterWeaponRuntimeSubsystem（WorldSubsystem）
└─ TMap<FName WeaponId, FShooterWeaponRuntimeBucket>
   ├─ RuntimeConfig（启动冻结快照）
   ├─ AvailableActors
   └─ LeasedActors
```

高层方向：
- 启动按 InitialPoolSize 预热；
- 可用池耗尽时按 RuntimeConfig 弹性 Spawn，不重读 DataTable；
- Acquire 直接返回 AShooterWeapon*；
- Projectile 继续按当前路径生成 / 销毁；
- 只有出现第二个真实池化消费者时，才重新评估公共池基础层。

---

## 12. 武器行为与 GAS 边界

只冻结职责：

```text
GA_Fire
= 射击事务、Ability 生命周期、条件、未来预测

FireBehavior
= 这一枪如何产生攻击结果

WeaponActor
= 世界实体、Muzzle、Mesh、Attach、表现入口、弹药与开火节拍

Inventory
= 所有权（WeaponActor + SlotIndex）

Equipment
= CurrentWeaponActor、装备事务与变化事件
```

暂不冻结：
- GameplayCue 具体位置；
- PredictionKey；
- AbilityTask；
- Ammo Prediction；
- RepNotify；
- Reload 具体时间点。

---

## 13. 自动化目标

项目最终希望实现：

> Agent 能完成大部分“实现 → 测试 → 失败分析 → 修复 → 重测”的自主闭环。

测试三层：

### Feature Test
例如：
```text
GAS.ASC.PlayerLifecycle
GAS.Health.Damage
Inventory.OwnerOnlyReplication
Ability.Fire.ServerOnly
```

### Scenario Test
例如：
```text
Scenario.PvP.1v1.Round
Scenario.PvE.1Player.Clear
Scenario.PvE.TeamWipe
```

### Full Regression
持续扩展现有：
```text
Build
Automation
Standalone
DedicatedNetwork
ListenNetwork
EmulatedNetwork
DisconnectCleanup
```

后续增加：
```text
Session
Lobby
PvP
PvE
GAS
Inventory
ProceduralMap
Prediction
Cook
```

Agent 开发闭环：

```text
Preflight
→ Implementation
→ Feature Tests
→ Build
→ Focused Validation
→ Full Regression
→ Read Summary / Logs
→ Failed ? Analyze → Fix → Re-test
→ Acceptance
```

---

## 14. 推荐实施路线

```text
0. Content Migration 收尾
↓
1. GAS 基础生命闭环
↓
2. 根据 Stage 1 真实经验复盘，再决定：
   - 第三人称动画稳定
   - 或 Weapon / Inventory 新架构
↓
3. Weapon / Inventory 新架构逐步落地
↓
4. GA_Fire ServerOnly
↓
5. GA_Reload / GA_Equip
↓
6. P1 Local Predicted 基础射击反馈
↓
7. Lobby + LAN Session
↓
8. PvP Round / MR12 Match Flow
↓
9. PvE Clear Match Flow
↓
10. 程序化 Grid Map
↓
11. UI 完整闭环
↓
12. 高级预测 P2～P5 按收益实验
↓
13. Internet Online Backend
↓
14. Demo Polish / 性能 / 表现
```

路线允许根据真实实现结果调整。

原则：
> 路线图不是提前实现未来功能的理由。

---

## 15. 当前下一阶段

已完成：

```text
GAS 基础生命闭环
Weapon / Inventory 第二阶段基础闭环
GA_Fire ServerOnly
GA_Reload / GA_Equip ServerOnly
武器装备表现事件收束与动画切换解耦
单表武器配置纠偏（DT_WeaponData + WeaponRowName，撤销 WeaponDefinition 层）
武器启动快照与 WeaponId 预热池（S1）
WeaponActor 成为运行时实例（S2）
Inventory 与 Equipment 去 InstanceId（S3）
Pickup 接入 WeaponId 与旧路径删除（S4）
```

当前状态：

```text
启动预配置与实体池简化重构 S1～S5 已完成并通过七阶段正式验收
→ Saved/Automation/Runs/20260912_130521/Summary.json
→ 下一阶段：P1 Local Predicted 基础射击反馈（待用户批准）
```

武器与 Inventory 当前的事实边界（生产路径）：

```text
武器模板：/Game/Shooter/Data/DT_WeaponData（仅 World 启动导入）
武器身份：FName WeaponId（创建后不变）
具体武器：AShooterWeapon*（池化 Actor 本身）
世界实体：UShooterWeaponRuntimeSubsystem Acquire / Release
Inventory：WeaponActor + SlotIndex（OwnerOnly）
Equipment：CurrentWeaponActor（所有观察者）
生命周期：InPool → Holstered → Equipping → Equipped → Holstered → InPool
```

详细职责见 [Inventory 与武器数据架构](../架构/Inventory与武器数据架构.md)。

默认后续候选：

> P1 Local Predicted 基础射击反馈（前置已满足）。

已完成的 Reload / Equip 计划与验收证据见：

[GA_Reload 与 GA_Equip ServerOnly 执行计划](../已完成计划/GA_Reload与GA_Equip_ServerOnly执行计划.md)

武器装备表现事件收束与动画切换解耦的验收证据见：

[武器装备表现事件收束与动画切换解耦执行计划](../已完成计划/武器装备表现事件收束与动画切换解耦执行计划.md)

武器与 Inventory 正式架构的当前阶段证据见：

[武器启动预配置与实体池简化重构方案](../已完成计划/武器启动预配置与实体池简化重构方案.md)、
[Inventory 与武器数据架构](../架构/Inventory与武器数据架构.md)。

历史基线（大阶段 A / B、单表纠偏）的验收证据保留在：
[武器与 Inventory 正式架构实施计划](../已完成计划/武器与Inventory正式架构实施计划.md)、
[单表武器配置纠偏小计划](../已完成计划/单表武器配置纠偏小计划.md)。

新模型正式验收已通过（2026-09-12）；P1 具备详细执行计划，待用户批准后开始实施。
