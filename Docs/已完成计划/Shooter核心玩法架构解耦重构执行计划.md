# Shooter 核心玩法架构解耦重构执行计划

> 状态：已完成（2026-08-26）。R0～R8 已实施、逐阶段提交并通过自动化与网络回归；遗留项与人工视觉验收记录见 `Docs/开发记录/` 与各阶段开发记录。

## 1. 计划定位

当前项目的装备、射击、生命、瞄准表现和动画闭环已经可运行，但 `AShooterCharacter` 同时承担了过多协调职责。后续继续加入背包、预测、更多武器和 NPC 时，直接在 Character 上扩展会让网络权威、表现和资产逻辑越来越难区分。

本次重构的目标不是把每项功能都拆成 Component，也不是复制 Lyra 的完整框架，而是：

> 在保持现有玩法结果和服务器权威不变的前提下，让每个长期系统只有一个明确的数据权威、一个明确的写入口，并让第一、第三人称动画都通过 AnimInstance 消费稳定的值数据。

本计划属于结构重构，不新增玩法。每个阶段必须可单独编译、测试、提交和回退。

---

## 2. 已确认决策

### 2.1 本轮新增的长期类型

- `UShooterAimPresentationComponent`：管理瞄准表现采样、复制、观察端平滑和调试。
- `UShooterEquipmentComponent`：管理“当前正在使用什么武器”的装备事务和公开状态。
- `UShooterAnimInstanceBase`：第一、第三人称 AnimInstance 的薄公共基类。
- `UShooterFirstPersonAnimInstance`：第一人称动画侧适配层。
- 现有 `UShooterThirdPersonAnimInstance`：改为继承薄公共基类，继续承载第三人称专用 Aim IK、左手 IK 数据。

### 2.2 本轮明确不新增

- 不新增 `HealthComponent`。
- 不新增 `AnimationComponent`。
- 不新增独立的 `AnimationSnapshot` UObject 或复制结构。
- 不新增覆盖玩家和 NPC 全部行为的巨型 `IShooterCombatant`。
- 不把 `AShooterWeapon` 立即拆成 WeaponLogic、WeaponRuntime、WeaponView 多层对象。
- 不照搬 Lyra 的 Equipment Instance、Fragment、Ability Set 和 Quick Bar 全套实现。

### 2.3 保留的权威原则

- Health / MaxHealth：`UAbilitySystemComponent + UShooterAttributeSet` 是唯一玩法权威。
- 拥有的武器、槽位、弹匣和备弹：`UShooterInventoryComponent` 是唯一玩法权威。
- 当前装备：最终由 `UShooterEquipmentComponent` 统一描述。
- 命中、Spread、Projectile 和伤害：始终由服务器权威计算。
- `AimPresentationTarget`：只服务远端动画表现，永远不能成为命中权威。
- AnimInstance：只采集、缓存和转换表现数据，不产生权威玩法结果。

---

## 3. 当前项目基线与主要耦合

以下结论来自当前项目 CodeGraph 和源码核对。

| 当前对象 | 已承担职责 | 当前问题 |
| --- | --- | --- |
| `AShooterCharacter` | 输入、角色生命周期、GAS Avatar 初始化、死亡桥接、武器装备、武器附着、AnimClass 切换、HUD 事件、瞄准表现采样/复制/平滑 | 成为跨系统事务中心；修改装备或动画时容易连带瞄准、UI 和网络代码 |
| `UShooterInventoryComponent` | FastArray 武器实例、槽位、弹匣/备弹、`ActiveWeaponInstanceId`、WeaponActor 生成/销毁和映射 | “拥有关系”与“当前使用状态”混在一起；当前装备还在 Character 保留另一份引用 |
| `AShooterWeapon` | 第一/第三人称 Mesh、Socket、开火节奏、Projectile、弹药转发、FX/Montage/Recoil、激活/停用 | 是完整运行时武器，不能在本轮误当成纯表现 Actor |
| `UShooterThirdPersonAnimInstance` | 瞄准方向、HandToMuzzle、左手握把缓存、Aim/LeftHand IK 开关 | `NativeUpdateAnimation()` 已过长，直接读取 Character 和 Weapon 的细节较多 |
| 第一人称 AnimBP | 第一人称手臂、武器姿势、Control Rig 和贴墙表现 | 尚无对应的项目 C++ AnimInstance 适配层，公共动画语义难以统一 |
| `AShooterPlayerState` | 玩家 ASC、AttributeSet、跨重生 Health Delegate | 方向正确，应保留；不能再套一层 HealthComponent |
| `GA_Fire / GA_Reload / GA_Equip` | ServerOnly 事务和互斥状态 | 部分逻辑仍 Cast 到 Character/NPC，并依赖宽泛的 `IShooterWeaponHolder` |

### 3.1 当前重复或危险路径

1. `Inventory.ActiveWeaponInstanceId` 与 `Character.CurrentWeapon` 同时描述当前武器。
2. `AShooterCharacter::AddWeaponClass()` 仍可绕过 Inventory，直接 Spawn 并写 `CurrentWeapon`。
3. Inventory 的 `TryAddWeapon()` 也会 Spawn WeaponActor，因此当前存在两条武器创建路径。
4. `CurrentBullets` 仍存在兼容镜像；实际已绑定武器的弹药消耗会转发到 Inventory。
5. Character 中的 `CurrentHP`、`MaxHP` 和 `bIsDead` 与 GAS 状态并存；其中前两者需要继续收敛，`bIsDead` 暂可作为廉价复制表现状态。
6. 第一、第三人称 AnimBP 缺少共同但克制的数据契约。

---

## 4. 外部依据、本地 UE5.6 核对与采用范围

### 4.1 ActorComponent 复制

Epic 文档说明：ActorComponent 可作为拥有 Actor 的子对象复制属性和子对象，也可以定义 RPC；静态 Component 可在构造函数中设置默认复制。

- [Replicating Actor Components](https://dev.epicgames.com/documentation/en-us/unreal-engine/replicating-actor-components-in-unreal-engine)

本地 UE5.6 `ActorComponent.h` 已核对存在：

```text
SetIsReplicated(bool)
SetIsReplicatedByDefault(bool)
```

采用范围：Aim 和 Equipment 可以作为 Character 的静态 Component 参与复制。它们仍受 Character 的所有权和网络相关性约束，不单独发明新的网络主体。

### 4.2 Inventory 与 Equipment 边界

Epic 的 Lyra 说明把 Inventory 定义为“拥有的物品”，把 Equipment 定义为“当前拿出、穿戴或使用的物品”。

- [Lyra Inventory and Equipment](https://dev.epicgames.com/documentation/en-us/unreal-engine/lyra-inventory-and-equipment-in-unreal-engine)

采用范围：只采用职责区别，不复制 Lyra 的完整对象模型。ShootGame 当前每把已拥有武器都保留一个 WeaponActor，本轮不会强行改成“只在装备时创建 Actor”。

### 4.3 GAS 与 Health

Epic 文档明确 ASC 负责 Ability、GameplayTag、Attribute、Effect 和网络交互；AttributeSet 承载 Gameplay Attribute。Lyra 把玩家 ASC 放在 PlayerState，以跨 Pawn 重生保留 GAS 状态。

- [Understanding the Gameplay Ability System](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-the-unreal-engine-gameplay-ability-system)
- [Abilities in Lyra](https://dev.epicgames.com/documentation/unreal-engine/abilities-in-lyra-in-unreal-engine?lang=en-US)

本地 UE5.6 `AbilitySystemComponent.h` 已核对 `InitAbilityActorInfo(OwnerActor, AvatarActor)` 以及 Owner/Avatar 访问接口。当前项目 `ShooterPlayerState` 已持久绑定 Health Attribute Delegate，这与跨重生目标一致。

采用范围：保持 ASC/AttributeSet 权威，不新增 HealthComponent。Ability 的死亡判断逐步改用 ASC 的 `State.Dead`，而不是 Cast Character/NPC 再读取各自的 `IsDead()`。

### 4.4 AnimInstance 数据采集

Epic 动画优化文档建议使用 Thread Safe 更新和 Property Access，并强调线程安全函数不能任意读取非线程安全对象。

- [Animation Optimization](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-optimization-in-unreal-engine)

本地 UE5.6 `AnimInstance.h` 明确建议：

```text
NativeUpdateAnimation：采集数据
NativeThreadSafeUpdateAnimation：执行可线程安全的主要计算
```

采用范围：本轮先把 UObject、Component、Weapon 和 Socket 读取集中在普通更新路径，再把 POD 值交给 AnimGraph 和自定义 FAnimNode。只有测得收益且数据契约稳定后，才把纯数学迁到 ThreadSafe 更新；不为了形式立即引入自定义 AnimInstanceProxy。

### 4.5 版本差异说明

外部页面当前展示的文档版本可能高于 5.6；最终实现以本地 UE5.6 头文件、实际编译和运行结果为准。本文只采用已经在本地 5.6 API 中得到确认的基础能力。

---

## 5. 目标职责与唯一权威

| 数据或事务 | 唯一写入方 | 可读方 | 明确禁止 |
| --- | --- | --- | --- |
| Inventory Entries / Slot / Magazine / Reserve | `UShooterInventoryComponent`（服务器） | Equipment、Ability、UI | Character/Weapon 各自维护另一份权威弹药 |
| 当前装备 InstanceId / WeaponActor | `UShooterEquipmentComponent`（服务器提交） | Character 转发、Ability、AnimInstance、UI | Inventory 和 Character 各写一份“当前武器” |
| 武器开火运行时 | `AShooterWeapon`，由 Ability 发起/结束 | Ability、表现层 | AnimBP 决定是否真正开火或消耗弹药 |
| Health / MaxHealth | ASC + AttributeSet | Character、UI、GameMode、Ability | 新增 HealthComponent 或继续写 Character HP 镜像 |
| Death Gameplay State | ASC `State.Dead`（服务器） | Ability、Character、UI | Ability 通过具体 Character/NPC Cast 判断死亡 |
| `bIsDead` | Character/NPC 的复制表现镜像 | Animation、Character 表现、旧接口 | 把它作为 Health 或 Ability 的独立权威 |
| 表现瞄准目标 | `UShooterAimPresentationComponent` | 第三人称 AnimInstance、Debug | Fire/Projectile 直接使用表现目标判定命中 |
| 第一人称动画值 | `UShooterFirstPersonAnimInstance` 本地快照 | 第一人称 AnimGraph/Control Rig | 复制第一人称专用相机和手臂状态 |
| 第三人称动画值 | `UShooterThirdPersonAnimInstance` 本地快照 | 第三人称 AnimGraph/FAnimNode | FAnimNode 在 AnyThread 中访问 UObject/Component |

---

## 6. 目标结构

```text
AShooterPlayerState
├─ UShooterAbilitySystemComponent
└─ UShooterAttributeSet
   └─ Health / MaxHealth 权威

AShooterCharacter
├─ Movement / Capsule / Camera / Mesh / Input
├─ UShooterInventoryComponent
│  └─ 拥有关系、槽位、弹匣、备弹、WeaponActor 映射
├─ UShooterEquipmentComponent
│  └─ 当前 InstanceId、CurrentWeapon、装备事务、装备变化事件
└─ UShooterAimPresentationComponent
   └─ 表现目标采样、RPC、验证、复制、平滑、Debug

AShooterWeapon
├─ 运行时武器行为
├─ 第一/第三人称 Mesh 与 Socket
├─ Fire / Refire / Projectile
└─ 表现入口与 Inventory 弹药转发

UShooterAnimInstanceBase
├─ UShooterFirstPersonAnimInstance
└─ UShooterThirdPersonAnimInstance
```

Character 最终只保留：

- Pawn 生命周期、移动、相机、Mesh 和输入入口；
- PlayerState/ASC Avatar 初始化；
- 对旧蓝图和测试的短期转发接口；
- Component 之间少量不可避免的生命周期编排。

Character 不再直接实现瞄准复制算法或装备提交事务。

---

## 7. 薄 AnimInstance 基类设计

### 7.1 `UShooterAnimInstanceBase` 只承载共享语义

允许进入基类的内容：

- Owning Character / Pawn 的弱引用缓存与失效清理。
- 只读 Equipment / ASC 访问缓存。AimPresentationComponent 只由第三人称子类访问，不因为第三人称需要而上提到公共基类。
- 两套视角都确实使用的基础值，例如：
  - GroundSpeed；
  - bIsInAir；
  - bHasEquippedWeapon；
  - 当前 WeaponActor 的只读缓存；
  - `State.Firing / Reloading / Equipping / Dead` 的表现布尔值。
- `NativeInitializeAnimation()`、`NativeUninitializeAnimation()` 的共同生命周期骨架。
- 清空共同快照的受保护函数。

禁止进入基类的内容：

- 第一人称摄像机、贴墙收枪、第一人称 Control Rig 参数。
- 第三人称 `PresentationAimTarget`、Aim IK、HandToMuzzle、左手 IK。
- 网络 RPC、RepNotify 或服务器校验。
- HUD、伤害、弹药修改或装备提交。
- 为“以后也许会用”预留的大量虚函数和配置结构。

基类初版以“能删除第一、第三人称重复采集”为上限；如果最终只有少量字段真正共享，就保持少量字段，不追求抽象数量。

### 7.2 `UShooterFirstPersonAnimInstance`

职责：

- 仅为本地拥有者的第一人称 Mesh 采集表现数据。
- 从 Equipment 获取当前 WeaponActor 和武器表现配置。
- 从 Character/Camera 获取第一人称必要的局部姿势输入。
- 从 ASC Tag 获取开火、换弹、切枪、死亡等表现状态。
- 为第一人称 AnimGraph 和 Control Rig 提供稳定值；不复制这些变量。
- 无本地控制、无 Mesh、无武器或死亡时清空专用快照。

资产迁移原则：

- 先只读审计 `ABP_FP_Weapon`、Rifle、Pistol 等实际继承关系。
- 优先只重设共同根 AnimBP 的 Parent Class，让子 AnimBP 自然继承。
- 若 Rifle/Pistol 不是同一父链，分别建立明确迁移清单，不批量猜测。
- 重设 Parent Class 前后必须记录变量、Event Graph、AnimGraph、Linked Layer 和编译状态。
- 首轮只迁移数据来源，不同时重做第一人称姿势或 Control Rig。

### 7.3 `UShooterThirdPersonAnimInstance`

保留第三人称专用内容，但把当前过长的更新函数拆成内部步骤：

```text
UpdateAimPresentationData()
UpdateEquipmentAnimationData()
RefreshWeaponPoseCacheIfNeeded()
UpdateLeftHandIKData()
ClearThirdPersonAnimationData()
```

目标不是机械拆函数，而是让每个步骤只读取一个稳定边界：

- Aim 数据来自 AimPresentationComponent。
- 当前武器来自 EquipmentComponent。
- Socket 和 Mesh 数据来自 WeaponActor 的只读接口。
- 移动和基础状态来自薄基类。

### 7.4 AnimGraph 与自定义节点边界

- `FAnimNode_ShooterAimIK`、LeftHand IK 等仍属于 AnimGraph，因为它们依赖 Pose 顺序和 Component Space 计算。
- FAnimNode 只能通过 Pin 消费 Vector、Transform、Float、Bool 等值。
- `EvaluateSkeletalControl_AnyThread()` 中禁止读取 Character、Weapon、Component 或其他 UObject。
- AnimInstance 普通更新负责采集 UObject 数据；纯数学在确认线程安全后才迁到 ThreadSafe 更新。

---

## 8. Health 边界：不再增加一层

### 8.1 保持现有正确部分

玩家：

```text
ShooterPlayerState = ASC Owner
ShooterCharacter = ASC Avatar
ShooterAttributeSet = Health / MaxHealth 权威
```

NPC：

```text
ShooterNPC = ASC Owner + Avatar
ShooterAttributeSet = Health / MaxHealth 权威
```

PlayerState 已持久绑定 Health Attribute Delegate，并把变化转发给当前 Avatar；该绑定跨重生有效，应继续保留。

### 8.2 收敛方式

- `State.Dead` 成为 Ability 和权威事务的统一死亡判断。
- `bIsDead` 暂时保留为 Character/NPC 的廉价复制表现状态和旧接口兼容。
- `CurrentHP / MaxHP` 的所有读取方先登记，再逐步改读 AttributeSet。
- 当 Character HP 镜像不再被 Gameplay 写入且自动化覆盖完成后，再单独删除；不与 Aim/Equipment 提取混在同一提交。
- HUD 可继续订阅 PlayerState/ASC 转发事件，不引入 HealthComponent 作为中转站。

### 8.3 暂不处理的情况

如果未来出现大量非 GAS Actor 也需要相同受伤/死亡生命周期，再重新评估是否需要健康接口或 Component。当前玩家和 NPC 都已经接入 GAS，没有新增一层的实际收益。

---

## 9. 瞄准与命中必须保持两条链路

```text
ShooterTargeting / ShooterAimMath（纯规则与纯数学）
             ↑                         ↑
       Fire 权威重算              Aim 表现采样
```

### 9.1 `UShooterAimPresentationComponent`

最终负责：

- 本地拥有者固定频率采样；
- Unreliable Server RPC；
- Sequence 和旧包拒绝；
- 服务器有限值、距离和方向验证；
- `COND_SkipOwner` 等既有复制语义；
- RepNotify；
- 远端平滑和生命周期重置；
- 当前瞄准调试绘制和指标。

Character 在迁移期只保留同名转发 Getter，避免一次修改所有 AnimBP、测试和调试调用方。

### 9.2 权威 Fire

Fire 继续从服务器视角、控制旋转和世界碰撞重新建立命中路径。它可以与 Aim Component 共享纯函数，但不得直接读取复制的 PresentationTarget 作为命中终点。

---

## 10. Equipment 与 Inventory 的迁移边界

### 10.1 Inventory 最终保留

- FastArray Entries；
- InstanceId / DefinitionId；
- Slot；
- Magazine / Reserve；
- 拥有关系查询；
- WeaponActor 与 InstanceId 映射；
- 本轮继续保留 WeaponActor Spawn/Destroy 生命周期。

### 10.2 Equipment 最终接管

- ActiveWeaponInstanceId；
- CurrentWeaponActor；
- 服务器 `EquipWeapon(InstanceId)` 事务；
- 旧武器 Stop/Deactivate；
- 新武器 Activate；
- Attach；
- AnimInstance Class 切换；
- 装备变化事件；
- Aim 平滑重置通知；
- CurrentWeapon 的复制与 RepNotify。

`ActiveWeaponInstanceId` 与 `CurrentWeaponActor` 是同一装备事务的“稳定身份 + 世界实体”原子对，不是两份独立权威。必须长期保持：

```text
两者同时有效，或同时无效
CurrentWeaponActor.BoundInstanceId == ActiveWeaponInstanceId
CurrentWeaponActor.Owner == Equipment.Owner
```

复制建议保持最小公开范围：完整 Inventory 和 Active Instance 身份只发给 Owner；观察者只需要 CurrentWeaponActor。不能假设 Component 属性、WeaponActor 和 `BoundInstanceId` 的到达顺序，客户端应用函数必须幂等，并能在引用稍后可用时补做 Attach 和 AnimClass 切换。

### 10.3 迁移期规则

- 首先建立 facade：`Equipment->EquipWeapon(Id)` 暂时转发到现有 `Character->CommitActiveWeapon(Id)`。
- facade 阶段不移动字段，不改变复制结果。
- 只有 facade 的 Characterization Test 通过后，才把字段和事务逐项迁到 Equipment。
- 在最终切换提交中删除 Inventory 的 ActiveWeaponInstanceId 和 Character 的 CurrentWeapon 写权，只留下必要的兼容 Getter。
- Inventory 仍可通过事件通知 Equipment 某个 Instance 被移除；Equipment 负责决定清空或切换当前装备。
- 迁移 `AShooterWeapon::InitializeWeaponOwner()` 的自动 Attach 依赖：玩家武器最终由 Equipment 应用附件和激活状态；NPC 在尚未接入 Equipment 时保留明确的兼容路径，不能为了删除接口破坏 NPC。
- Equipment 发布 `OnEquippedWeaponChanged`；HUD 在迁移期可通过 Character 转发，最终由本地 Controller/UI 绑定稳定事件。

---

## 11. Ability 依赖收敛

目标依赖顺序：

```text
ActorInfo / ASC
├─ State.Dead、State.Firing、State.Reloading、State.Equipping
├─ AvatarActor
└─ OwnerActor

AvatarActor
├─ EquipmentComponent：当前武器和装备事务
└─ InventoryComponent：弹药、槽位和目标 Instance 查询
```

实施原则：

- `GA_Fire`、`GA_Reload`、`GA_Equip` 先改用 ASC Tag 判断死亡。
- 当前武器改从 Equipment 获取。
- Reload 弹药事务仍直接访问 Inventory。
- Equip 目标查询访问 Inventory，最终提交调用 Equipment。
- 不新增大而全的 `IShooterCombatant`。
- 现有 `IShooterWeaponHolder` 在迁移期保留，但不得继续扩展；调用方清零后再单独删除。
- 玩家和 NPC 若暂时没有同构 Equipment，不强行一次统一。先为 Player 完成闭环；Ability 可使用“Equipment 优先、现有最小武器接口作为 NPC 兼容回退”的解析函数。待基于 NPC 的真实生命周期决定是否复用 Equipment 后，再判断能否删除该接口。

---

## 12. 分阶段执行与阶段门

### R0：冻结行为与所有权表

Agent：

- 使用 CodeGraph 生成当前装备、开火、换弹、死亡、瞄准和动画调用链。
- 使用只读 MCP 审计第一/第三人称 AnimBP 父类、继承关系、关键变量和图表。
- 为当前行为补 Characterization Test，不改生产行为。
- 记录所有 Character 旧接口调用方及蓝图引用。

最少测试门：

- 拾取后立即装备；
- Rifle/Pistol 切换；
- 弹匣/备弹不因切枪丢失；
- 开火、换弹和切枪互斥；
- 死亡、重生后状态清理；
- Listen 与 Dedicated 的 CurrentWeapon 和 Aim 表现一致；
- 第一、第三人称 AnimBP 当前父类与编译状态有快照。

完成条件：已有行为可以通过自动化和资产审计证明，而不是仅凭人工记忆。

### R1：封死绕过 Inventory 的武器创建路径

只修改 `AShooterCharacter::AddWeaponClass()` 及其针对性测试：

```text
AddWeaponClass
→ Inventory.TryAddWeapon
→ Added 时调用现有 HandleWeaponAddedToInventory(InstanceId)
```

注意：只调用 `TryAddWeapon()` 不足以保持“拾取后立即装备”的旧行为，必须进入现有装备处理入口。

本阶段不新增 Equipment，不移动字段。

完成条件：玩家只有 Inventory 一条 WeaponActor 创建路径；NPC 现有独立生成路径不在本提交改造。

### R2：提取 AimPresentationComponent

迁移顺序：

1. 建立静态、可复制 Component。
2. 搬移采样、RPC、Sequence、验证、复制、平滑、重置和 Debug。
3. Character 保留转发 Getter 和最小生命周期调用。
4. 第三人称 AnimInstance 先通过转发接口保持行为，再切换到 Component 只读接口。
5. 清理 Character 中已无调用的 Aim 字段和函数。

完成条件：网络数值和当前视觉基线不变；Character 不再拥有 Aim RPC、RepNotify 和 Tick/Timer 算法。

### R3：建立 Equipment facade

- Character 构造函数创建 `UShooterEquipmentComponent`。
- 暂不移动 `CurrentWeapon`、`ActiveWeaponInstanceId` 或复制字段。
- `Equipment->EquipWeapon(Id)` 转发现有 `CommitActiveWeapon(Id)`。
- Pickup、GA_Equip 和测试逐步只调用 Equipment facade。
- 建立 Equipment 变化事件，但先由 Character 旧事务触发。

完成条件：外部系统不再直接调用 Character 的装备提交函数；运行结果仍由旧事务产生。

### R4：移动 Equipment 权威

建议在一个受控提交内完成最终切换：

1. Equipment 接管 CurrentWeaponActor 和 ActiveWeaponInstanceId。
2. Equipment 执行 Deactivate/Activate/Attach/AnimClass/Event/AimReset 完整事务。
3. Inventory 删除 ActiveWeaponInstanceId 写权，只保留拥有关系和 Actor 映射。
4. Character 删除 CurrentWeapon 写权，旧 Getter 改为转发 Equipment。
5. OnRep 和 Listen Server 本地应用路径统一由 Equipment 处理。
6. Inventory Remove/Clear 通过事件要求 Equipment 清空当前装备。
7. WeaponActor、Equipment 属性和 BoundInstanceId 到达顺序不固定时，客户端重复应用仍得到相同结果。

阶段门：服务器、Owning Client、Simulated Proxy 必须最终收敛到同一 CurrentWeaponActor；同一时刻只能有一个装备写入口。

### R5：收敛 Ability 依赖

- Fire/Reload/Equip 使用 ASC `State.Dead`。
- 当前武器统一从 Equipment 获取。
- 弹药与槽位从 Inventory 获取。
- 清理对 Character/NPC 具体类型的非必要 Cast。
- 保持 ServerOnly 行为、Cooldown/Cost/Tag 顺序和现有失败语义不变。

完成条件：Ability 不再依赖 Character 的装备内部实现；GA 自动化、Dedicated 和 Listen 回归全部通过。

### R6：建立第一、第三人称 AnimInstance 体系

#### R6.1 C++ 类型

- 新建薄 `UShooterAnimInstanceBase`。
- 新建 `UShooterFirstPersonAnimInstance`。
- `UShooterThirdPersonAnimInstance` 改继承公共基类。
- 将第三人称长更新函数按职责拆分，但不改变计算公式和视觉参数。
- 为纯判断、缓存失效和无效状态清理补测试。

新增源码文件后按项目规则运行一次：

```powershell
Scripts/Development/RefreshVisualStudioFiles.ps1
```

这只是同一 Runtime 模块内新增文件，不要求因 VS 提示而暂停外部 UBT；但需提示用户 VS 可能需要“全部重新加载”。

#### R6.2 资产父类迁移

Agent 先通过只读 MCP 给出实际 AnimBP 继承清单和精确迁移对象。

人工或已明确放行的资产工具：

- 将第一人称共同根 AnimBP 重设为 `UShooterFirstPersonAnimInstance`。
- 将第三人称共同根 AnimBP 保持/重设为 `UShooterThirdPersonAnimInstance`。
- 编译、保存并确认所有 Rifle/Pistol 子类仍继承正确。
- 不在本阶段修改 Control Rig、AimOffset、Montage、Slot 或姿势资源。

#### R6.3 数据迁移

- 把两边真正共享的基础变量改为基类提供。
- 第一人称专用变量迁到 FirstPerson 子类。
- 第三人称 Aim/LeftHand 数据保留在 ThirdPerson 子类。
- 删除已被 C++ 快照替代的重复 Event Graph 节点前，必须逐项对比值来源。
- 自定义 AnimNode 继续只吃 Pin 值。

完成条件：第一、第三人称所有目标 AnimBP 编译通过；站立、移动、跳跃、瞄准、贴墙、开火、换弹、切枪和死亡视觉不低于重构前基线。

### R7：Health 读取收敛

- 建立 Character/NPC `CurrentHP / MaxHP / IsDead` 调用方清单。
- Ability 全部依赖 ASC Tag 后，再迁移 UI/GameMode/测试的 Health 读取。
- `bIsDead` 保留为表现镜像。
- `CurrentHP / MaxHP` 是否删除单独决策、单独提交。

本阶段禁止新增 HealthComponent。

完成条件：不存在第二套可写 Health；伤害、死亡、击杀、重生、HUD 与 NPC 行为回归通过。

### R8：Character 瘦身与兼容接口退出

- 删除零调用的 Character Aim/Equipment 兼容函数。
- 删除已失去用途的 `IShooterWeaponHolder` 或缩到真实最小边界。
- 将剩余 Character 函数按 Input、Lifecycle、GAS Avatar、Mesh Attachment 等职责整理。
- 更新 `Shooter完整Demo最终路线规划.md` 中 Inventory/Equipment 的最终所有权描述。
- 更新 AGENTS 导航，仅保留本计划作为当前阶段入口；完成后移入已完成计划。

完成条件：Character 不再拥有 Aim 网络算法、装备权威事务或 Health 权威值；公开接口均能对应明确职责。

### R9：可选后续，不属于本计划完成门

以下内容只有出现真实需求后才另立计划：

- WeaponActor Spawn/Destroy 从 Inventory 迁到 Equipment 或 Pool。
- `ShooterWeaponDefinition` 数据资产化。
- NPC 完整接入 Inventory/Equipment。
- WeaponActor 池化。
- Local Predicted Fire、Ammo Prediction 和更激进的瞄准方向流。

---

## 13. Agent 与人工职责边界

### 13.1 Agent 自主完成

- CodeGraph、官方文档、本地 UE5.6 源码对照。
- C++、自动化测试、脚本和普通 Markdown 修改。
- 编译、Focused Automation、Dedicated/Listen 网络回归。
- 只读 MCP 审计 AnimBP、资产路径、父类、节点和编译状态。
- 每个提交前编写中文开发记录，并保证提交范围只覆盖本阶段。
- 对新增源码文件运行一次 VS 项目刷新脚本。

### 13.2 必须由人工确认或明确放行

- AnimBP Parent Class 修改与 `.uasset` 保存。
- 第一、第三人称最终视觉验收。
- 任何会批量重保存资产或修改 Control Rig 的操作。
- 是否接受兼容 Getter 删除造成的蓝图迁移范围。
- 是否进入可选 R9 或预测阶段。

### 13.3 强制暂停条件

- MCP 无法证明目标 AnimBP 的真实父类和继承关系。
- 需要保存 `.uasset`，但编辑器中存在用户未保存改动。
- 当前阶段出现玩法结果变化，而不是纯结构变化。
- CurrentWeapon、ActiveInstanceId、Health 或 AimTarget 出现两个可写权威。
- 网络测试发现 Dedicated 与 Listen 行为分叉。
- 新增模块、Target 或插件后，下一步必须依赖尚未重新加载的 VS/Editor 会话。

---

## 14. 自动验证矩阵

### 14.1 每个 C++ 提交

```text
git diff --check
Editor Build
对应 Focused Automation
```

### 14.2 Aim 提取阶段

- 本地拥有者采样频率和 RPC 频率受控。
- Server 拒绝无效值、过期 Sequence 和不合理目标。
- Owner 不消费自己的复制表现目标。
- Simulated Proxy 和 Listen Server 观察远端 Pawn 均平滑。
- 停止、死亡、重生、切枪、无武器时正确清空。
- Rifle 现有 Aim/LeftHand IK 数值测试继续通过。

### 14.3 Equipment 阶段

- 拾取成功/重复/满槽/生成失败。
- 切枪成功、非法 Instance、非权威请求、死亡中请求。
- Equip 取消 Fire/Reload，失败时保持原武器。
- 弹药随 Instance 保留。
- Inventory Remove/Clear 不留下悬空 CurrentWeapon。
- Server、Owner、Observer 当前武器一致。

### 14.4 Ability 与 Health 阶段

- `State.Dead` 阻止 Fire/Reload/Equip。
- 玩家重生后 Dead/Firing/Reloading/Equipping Tag 清理。
- NPC 死亡后能力不可激活。
- Health 属性复制、HUD、受击、死亡、击杀和计分不回退。

### 14.5 AnimInstance 阶段

- 无 Owner、无 Weapon、切枪、死亡时快照清理。
- 第一/第三人称 AnimBP 父类与子类关系正确。
- 每个 Weapon 配置的 FirstPersonAnimInstanceClass 都派生自 `UShooterFirstPersonAnimInstance`，ThirdPersonAnimInstanceClass 都派生自 `UShooterThirdPersonAnimInstance`。
- AnimBP Compile 0 Error。
- 第一人称仅本地可见且不访问远端 Controller/Camera。
- 第三人称 Aim IK、LeftHand IK、跳跃、快速转向和贴墙表现不回退。
- AnyThread 节点不访问 UObject。

### 14.6 阶段性全回归

R2、R4、R6、R8 完成后执行：

```text
Build
完整 Automation
Dedicated Server + 2 Clients
Listen Server + Remote Client
必要的 Emulated Network
人工第一/第三人称视觉矩阵
```

---

## 15. 推荐提交拆分

每个提交前必须创建对应中文开发记录。

```text
提交 1  测试：冻结重构前行为基线
提交 2  装备：封死 Character 直生武器旧路径
提交 3  瞄准：提取 AimPresentationComponent
提交 4  装备：建立 Equipment facade
提交 5  装备：迁移当前装备权威
提交 6  GAS：Ability 改用 Tag 与 Equipment 边界
提交 7  动画：建立薄 AnimInstance 基类与第一人称子类
提交 8  动画资产：迁移第一/第三人称 AnimBP 父类与数据源
提交 9  生命：收敛 Health 读取与兼容镜像
提交 10 架构：移除 Character 兼容层并更新导航
```

如果某个阶段需要同时修改大量 `.uasset`，资产提交与 C++ 类型提交必须拆开，确保 Parent Class 保存失败时可以独立回退。

---

## 16. 回退原则

- facade 先于所有权迁移，先让调用方向新入口收敛，再移动数据。
- Character 兼容 Getter 至少保留到对应蓝图和测试迁移完成。
- Aim 和 Equipment 不在同一个提交中提取。
- C++ AnimInstance 类型与 AnimBP Parent Class 资产修改分开提交。
- Health 镜像删除不与 Ability Cast 清理混在一起。
- 每阶段失败时优先回退当前提交，不依赖跨多个提交手工拼回旧行为。

---

## 17. 完成定义

本计划只有同时满足以下条件才可归档：

- 玩家武器只能通过 Inventory 加入。
- Inventory 只描述拥有关系、实例、槽位和弹药。
- Equipment 是当前武器的唯一写入方。
- AimPresentationComponent 完整承接表现瞄准网络链路，Character 只保留必要生命周期协调。
- Fire 命中不依赖 PresentationAimTarget。
- Ability 不再通过 Character/NPC Cast 判断死亡，主要依赖 ASC Tag 和稳定 Component 边界。
- ASC + AttributeSet 仍是唯一 Health 权威，没有 HealthComponent。
- 第一人称 AnimBP 使用 `UShooterFirstPersonAnimInstance` 体系。
- 第三人称 AnimBP 使用 `UShooterThirdPersonAnimInstance` 体系。
- 两者共享薄 `UShooterAnimInstanceBase`，但第一/第三人称专用数据没有被错误上提。
- AnimGraph/FAnimNode 只消费值数据，AnyThread 不访问 UObject。
- Character 中不存在 Aim 网络算法、装备提交权威和 Health 权威写入。
- Editor Build、Focused Tests、Dedicated、Listen 和人工动画视觉验收均通过。
- 每个提交都有同提交中文开发记录，计划与最终路线文档已同步。

完成上述目标后，项目才适合继续进入背包 UI、WeaponDefinition、NPC 装备统一或客户端预测阶段。
