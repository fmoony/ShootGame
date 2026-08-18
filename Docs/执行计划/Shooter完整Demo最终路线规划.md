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
- Inventory；
- WeaponInstance；
- WeaponDefinition；
- WeaponActor；
- Object Pool；
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

### 10.1 WeaponDefinition

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

### 10.2 WeaponInstance

第一版：
```text
USTRUCT + FastArray Item
```

至少包含：
```text
InstanceId
DefinitionId
MagazineAmmo
ReserveAmmo
SlotIndex
```

MagazineAmmo / ReserveAmmo 都属于具体 WeaponInstance。

### 10.3 Inventory

FastArray 是 Owner 的权威武器数据源，负责：
- 拥有关系；
- WeaponInstance；
- Slot；
- ActiveWeaponInstanceId。

完整 Inventory OwnerOnly。

### 10.4 WeaponActor

每把已拥有武器都有一个 WeaponActor，不在每次切枪时 Spawn/Destroy。

状态：
```text
InPool
→ Holstered
→ Equipping
→ Equipped
→ Holstered
→ InPool
```

逻辑身份：
```text
InstanceId
```

世界表现：
```text
CurrentWeaponActor
```

Character 对观察者复制 CurrentWeaponActor。

WeaponActor 可保存 BoundInstanceId，但 Actor 指针不作为永久武器身份。

### 10.5 Pickup

暂时保留现有模板 Blueprint Pickup / 生成点，不为架构重写。

后续只接入：
- Inventory；
- WeaponInstance；
- Actor Pool。

### 10.6 死亡

```text
玩家死亡
→ 停止武器行为
→ 所有 WeaponActor 回池
→ 清空 Inventory
→ ActiveWeaponInstanceId Invalid
→ CurrentWeaponActor = nullptr
```

每个新回合 / 新 PvE Match 重新获取武器。

---

## 11. 通用 Actor Pool

目标不是 WeaponPool，而是通用 Actor Pool：

```text
Actor Pool
├─ Weapon
├─ Projectile
└─ 后续其他高频 Gameplay Actor
```

高层方向：
- 模板化底层池容器；
- World 级管理者；
- Poolable 生命周期协议；
- 按实际 Actor Class 分池；
- 支持 Acquire / Release / Prewarm / Capacity；
- 必要时支持 Register Existing Actor。

第一版先保证正确，不提前做复杂 Dormancy 优化。

---

## 12. 武器行为与 GAS 边界

只冻结职责：

```text
GA_Fire
= 射击事务、Ability 生命周期、条件、未来预测

FireBehavior
= 这一枪如何产生攻击结果

WeaponActor
= 世界实体、Muzzle、Mesh、Attach、表现入口

Inventory
= 所有权、WeaponInstance、Ammo、Slot
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

当前正式下一阶段：

> GAS 基础生命闭环。

详细执行见：

`GAS第一阶段_基础生命闭环执行计划.md`

该阶段完成后必须暂停复盘，再确定第二阶段细节。
