# GAS、动画分层与客户端预测扩展计划

## 目标

在现有 Shooter 多人闭环已经可运行、可自动验证的基础上，引入一条新的学习路线：

1. 接入 Gameplay Ability System（GAS），先建立正确的网络所有权和生命周期。
2. 将生命与开火逐步迁移到 GAS，不一次性推翻现有实现。
3. 修正第三人称瞄准表现，以腰部为边界拆分上半身俯仰/战斗姿势与下半身移动姿势。
4. 增加简单的服务器权威武器背包。
5. 完整探索客户端预测，从本地表现逐步延伸到弹药、弹丸和命中反馈，并根据性能、纠错频率和实际观感决定最终保留范围。

本计划是现有 [多人网络改造计划](../已完成计划/多人网络改造计划.md) 的后续扩展，不重写已经完成的服务器权威闭环。

## 当前基线

当前实现具备以下可复用基础：

- `AShooterCharacter` 负责开火输入、武器引用、生命、死亡、复活请求和动画表现入口。
- `AShooterWeapon` 是服务器生成的复制 Actor，负责射速、弹药、弹丸生成和开火表现。
- `AShooterPlayerState` 已保存队伍、击杀、死亡和个人分数，跨 Pawn 重生持续存在。
- `AShooterPickup` 只在服务器处理重叠和武器发放。
- `ABP_TP_Rifle`、`ABP_TP_Pistol` 已经使用 `Layered Blend Per Bone`，但远端瞄准目前依赖角色在 `PostNetReceive` 中按属性名写入 `PitchN`，类型不安全，也不利于继续扩展 Aim Yaw、换弹和技能动画。
- 开火只有服务器权威路径，没有本地预测；高延迟时输入到枪口反馈之间会有明显等待。
- `OwnedWeapons` 只在服务器 Character 内存中存在，角色销毁时武器也销毁，尚未形成独立背包状态。

因此，本轮不是“把全部射击代码换成 GAS”，而是逐个替换现有职责，并在每次替换后保持旧闭环仍可验证。

## 核心架构决定

### 1. 玩家与 NPC 使用不同宿主、共享 GAS 规则

玩家的 `AbilitySystemComponent`（ASC）放在 `AShooterPlayerState`，Character 作为 Avatar Actor：

```text
PlayerController
      │ Possess / OnRep_PlayerState
      ▼
ShooterPlayerState（Owner Actor）
├─ AbilitySystemComponent
└─ AttributeSet
      │ InitAbilityActorInfo
      ▼
ShooterCharacter（Avatar Actor，可死亡和重生）
```

原因：PlayerState 在 Pawn 死亡和重生期间持续存在，玩家 ASC 不会因为 Character 被销毁而失去宿主。服务器在 `PossessedBy` 后初始化 ActorInfo，拥有者客户端在 `OnRep_PlayerState` 后初始化 ActorInfo。

第一版 ASC 使用 `Mixed` 复制模式。实际接入时必须验证 PlayerState 的 Owner 是对应 PlayerController，不能只假设该关系成立。

NPC 没有可持续存在的 PlayerState，因此 ASC 和 AttributeSet 直接放在 `AShooterNPC`：

```text
ShooterNPC（Owner Actor = Avatar Actor）
├─ AbilitySystemComponent
├─ AttributeSet
└─ StateTree / AIController 提交能力激活意图
```

玩家与 NPC 共享 AttributeSet、GameplayEffect、GameplayTag 和通用 Ability 规则，但分别处理初始化生命周期。第一版不为了统一宿主而额外创建新的公共 Pawn 层级。
NPC ASC 第一版使用 `Minimal` 复制模式，只把观察者需要的 Tag 和 GameplayCue 结果同步出去，不复制完整 GameplayEffect 明细。

### 2. 背包跟随 Character 生命周期

- 背包属于玩家 Character，只向拥有者复制完整条目。
- 第一版条目只表达“武器类型 + 数量”，不提前设计通用道具、耐久度、随机词条和重量系统。
- 当前装备仍由 Character 上的 `CurrentWeapon` 复制给所有观察者，因为其他玩家需要看到第三人称武器。
- 服务器根据背包的激活槽生成或切换武器 Actor。
- Character 死亡后武器 Actor 和背包一起销毁；新 Pawn 以空背包出生，不恢复上一条生命的武器。
- 第一版不持久化每把武器的弹匣余量，避免同时改造背包、弹药和换弹。

### 3. 客户端预测采用逐层实验和保留决策

计划覆盖完整的预测技术路径，但不承诺把每一种预测永久留在最终玩法中。每一层必须与“不预测”的权威基线对照，再根据性能和表现决定保留或回退。

| 层级 | 候选预测内容 | 仍由服务器决定 | 主要风险 |
| --- | --- | --- | --- |
| P0 | 不预测，保留当前行为作为对照组 | 全部结果 | 高延迟下操作反馈慢 |
| P1 | Ability 激活、第一人称 Montage、枪口闪光、声音、后坐力 | 是否允许开火、权威弹丸 | 重复 Cue、拒绝后的视觉回正 |
| P2 | 弹药 HUD、射速冷却、切枪表现 | 权威弹药与装备状态 | 数值回滚、快速输入竞态 |
| P3 | 本地预测弹丸及与服务器弹丸的匹配/替换 | 权威弹丸轨迹和碰撞 | 双弹丸、替换跳变、额外 CPU 与网络标识 |
| P4 | 本地命中反馈、受击提示 | 伤害、死亡、得分 | 误报命中与纠正体验 |
| P5 | 服务器倒带/历史命中检测实验 | 服务器基于历史状态裁决 | 历史缓存成本、公平性和作弊面 |

拾取和背包增删保持服务器权威。它们是低频事务，预测收益较小而回滚复杂度较高；切枪只预测表现，不预测背包所有权。

每层都记录以下决策数据：

- 输入到本地反馈、输入到服务器确认的时间。
- 预测被服务器拒绝或纠正的比例。
- 是否出现重复 Montage、重复声音、双弹丸或错误命中提示。
- Dedicated Server 与客户端的 CPU、内存和网络流量变化。
- 在 `0ms`、`100ms + 2%` 丢包以及更高延迟下的主观可接受程度。

客户端预测与服务器倒带仍是两个不同机制，因此 P5 必须作为独立闭环实现和评估，不能在 P1 开火预测中顺带加入。

### 4. 第三人称动画采用明确的上下半身管线

第三人称动画的目标结构为（这里的“动画层”明确指腰部以上的骨骼分层，不引入 Linked Anim Layer 框架）：

```text
Locomotion State Machine
        │
        ├──────────────────────────────┐
        │                              │
        │                    武器待机 / Aim Offset
        │                              │
        │                    UpperBody Slot
        │                              │
        └─ Base Pose ── Layered Blend Per Bone
                         （从 spine_01 开始）
                                  │
                              Output Pose
```

- 下半身继续使用速度、方向、落地状态驱动移动状态机。
- 上半身使用武器待机姿势、Aim Offset、开火/换弹 Montage。
- `UpperBody Slot` 放在腰部以上的分支内，避免开火 Montage 覆盖腿部移动。
- 开启 Mesh Space Rotation Blend，减少脊柱与肩部旋转断层。
- Rifle 和 Pistol 保留各自姿势资源，但共享同一套变量来源和分层规则。
- 第一人称 AnimBP 与 Control Rig 保持独立，不让远端第三人称逻辑读取本地摄像机。

第三人称变量改由一个类型安全的 `UShooterThirdPersonAnimInstance` 基类集中计算：

- `GroundSpeed`
- `Direction`
- `bIsInAir`
- `bShouldMove`
- `AimPitch`
- `AimYaw`

`AimPitch/AimYaw` 从 Character 的 `GetBaseAimRotation()` 与 Actor Rotation 推导。模拟代理继续利用 Character 已有的远端视角复制，不新增重复的每帧复制变量。蓝图验证通过后删除当前按字符串查找 `PitchN` 的反射写入代码。

## 建议目录

```text
Source/ShootGame/
├── AbilitySystem/
│   ├── ShooterAbilitySystemComponent.h/.cpp
│   ├── ShooterAttributeSet.h/.cpp
│   ├── ShooterGameplayAbility.h/.cpp
│   └── Abilities/
│       └── ShooterGameplayAbility_Fire.h/.cpp
├── Animation/
│   └── ShooterThirdPersonAnimInstance.h/.cpp
├── Inventory/
│   ├── ShooterInventoryComponent.h/.cpp
│   └── ShooterInventoryTypes.h
├── Characters/
│   └── ShooterCharacter.h/.cpp
└── GameFramework/
    └── ShooterPlayerState.h/.cpp
```

只在对应阶段真正需要时创建目录和类型，不预先生成空框架。

## Content 基线

GAS Phase 1 基于新的 `/Game/Shooter` Content 基线开始：Shooter 正式资产已从 `/Game/Variant_Shooter` 收束到按资产类型组织的 `/Game/Shooter`（Blueprints / Animation / Data / Input / UI / FX / Materials / Meshes），地图位于 `/Game/Shooter/Maps/`。

## 分阶段实施顺序

除阶段 8 按 P2、P3、P4、P5 拆成四个子提交外，每个阶段只形成一个提交。提交前必须按 [开发记录规范](../开发记录/README.md) 新建对应中文记录，并把改动、验证、问题和遗留项与代码放入同一提交。

### 阶段 0：冻结并验证当前基线

**改动**

- 不修改玩法代码。
- 记录当前未提交的用户资产和源码改动，后续提交不得误纳入。
- 运行当前 Build、Automation、Standalone、Listen、Dedicated、延迟丢包和断线清理矩阵。

**验收**

- 确认新计划开始前的已知失败项；后续不能把基线失败误判为 GAS 回归。
- 保存本次运行的 `Summary.json` 路径到阶段开发记录。

### 阶段 1：只接入 GAS 模块

**改动**

- 在 `.uproject` 启用 `GameplayAbilities` 插件。
- 在 `ShootGame.Build.cs` 增加 `GameplayAbilities`、`GameplayTags`、`GameplayTasks`。
- 暂不添加 ASC、AttributeSet 或 Ability。

**为什么单独提交**

这是纯依赖变化，能独立暴露插件、模块和编译时间问题，不与玩法错误混在一起。

**验收**

- Editor 与 Game Target 均可编译。
- Standalone、Listen、Dedicated 可启动，旧射击链路不变。

### 阶段 2：建立 ASC 生命周期

**改动**

- `AShooterPlayerState` 实现 `IAbilitySystemInterface`，添加玩家 ASC。
- `AShooterNPC` 实现 `IAbilitySystemInterface`，添加 NPC 自身的 ASC。
- Character 在服务器 `PossessedBy`、客户端 `OnRep_PlayerState` 中调用 `InitAbilityActorInfo`。
- NPC 在服务器初始化 Owner/Avatar 都指向自身，并为后续 StateTree 激活 Ability 提供入口。
- 暂不迁移生命或开火。

**验收**

- Standalone、监听服务器主机、远程客户端、专用服务器上的 ActorInfo 都只初始化到正确的 Owner/Avatar。
- Pawn 重生后 ASC 不被重建，Avatar 正确切换到新 Character。
- NPC ASC 在 Dedicated Server 上初始化有效，不依赖本地 PlayerController。
- 旧射击、伤害、计分、复活行为不变。

### 阶段 3：稳定第三人称瞄准与动画分层

**改动**

- 新增 `UShooterThirdPersonAnimInstance`，集中计算移动与 Aim 数据。
- 将 `ABP_TP_Rifle`、`ABP_TP_Pistol` 改为该基类。
- 调整 AnimGraph 为“下半身 Locomotion + 上半身 Aim/Slot”的明确分支。
- 确认从 `spine_01` 开始分层，并检查 Rifle/Pistol 的骨骼过滤配置。
- 验证后移除 `ApplyRemoteAimPitch` 的属性名反射写入。

**验收**

- 远程玩家上下瞄准时，观察端肩、手臂和脊柱发生变化。
- 远程玩家边走边瞄准时，腿部继续播放正确方向的 Locomotion。
- 开火 Montage 不冻结或覆盖腿部移动。
- 第一人称手臂和 Control Rig 表现不回退。
- 自动化验证 AnimInstance 类型和 Aim 数值；最终姿势使用带渲染双客户端人工验收。

### 阶段 4：将玩家和 NPC 生命迁移到 AttributeSet

**改动**

- 玩家和 NPC 共享 `Health`、`MaxHealth` Attribute 定义与伤害 GameplayEffect。
- 使用初始化 GameplayEffect 设置出生生命值。
- 保留 `TakeDamage` 作为现有弹丸的桥接入口，由服务器把 Damage 转换为 GameplayEffect。
- Character 监听 Attribute 变化更新 HUD，并在 Health 到零时进入现有死亡/计分/复活闭环。
- 验证完成后删除重复的 `CurrentHP` 手写复制；`bIsDead` 是否迁移为 GameplayTag 在后续独立决定。

**验收**

- 同一次命中只应用一次伤害。
- 两端 Health 一致，HUD 更新一致。
- 死亡、击杀、队伍得分和重生不回退。
- 重生时使用 GameplayEffect 重置 Health，而不是依赖旧 Pawn 内存。
- NPC 生命归零后仍触发现有 StateTree/AIController 依赖的死亡通知与延迟销毁。

### 阶段 5：Character 生命周期内的服务器权威简单武器背包

**改动**

- 在玩家 Character 添加 `UShooterInventoryComponent`。
- 使用 owner-only 复制的 Fast Array 保存“武器 Class + 数量 + 激活槽”。
- Pickup 在服务器调用背包 `TryAddWeapon`，不再直接把 `OwnedWeapons` 当作永久状态。
- 激活槽变化后，服务器驱动 Character 生成/切换公开的 `CurrentWeapon` Actor。
- 本地 PlayerController 创建简单查看/选择入口，UI 不直接修改背包。

**验收**

- 拥有者能读取自己的背包；其他客户端不能获得完整背包条目。
- 双人争抢同一 Pickup 只产生一次权威入包结果。
- 非拥有客户端不能请求修改另一玩家背包。
- 玩家死亡并生成新 Pawn 后背包为空，旧武器不会自动恢复。
- 查看和选择槽位只影响自己的背包；观察者只接收公开的 `CurrentWeapon`。

### 阶段 6：将开火迁移为服务器权威 Ability

**改动**

- 添加输入 GameplayTag，例如 `Input.Fire`。
- 服务器授予 `GA_Fire`，Enhanced Input 只把按下/松开传给 ASC。
- Ability 从 Avatar 获取 Character，从 Character 获取当前 Weapon。
- 先使用非预测或服务器确认路径，复用当前服务器弹丸生成、弹药和伤害实现。
- 使用 GameplayTag 或 GameplayEffect 表达死亡、冷却等激活条件。
- 旧 `ServerStartFire/ServerStopFire` 暂时保留为对照，测试通过后再删除，不能让两条路径同时实际生成弹丸。

**验收**

- 每次输入只激活一次 Ability，只生成一份弹丸。
- 死亡、无武器、冷却中的玩家无法开火。
- Listen 与 Dedicated 下弹丸、命中和弹药结果与迁移前一致。

### 阶段 7：实现 P1 基础开火预测

**改动**

- 将 `GA_Fire` 设为 `Local Predicted`。
- 拥有者立即播放第一人称 Montage、枪口 Cue、声音和后坐力。
- 服务器确认后生成弹丸并向模拟代理广播确认表现。
- 设计 Cue 去重，保证拥有者不会因为“预测一次 + 服务器确认一次”看到双枪口闪光或双 Montage。
- Ability 被服务器拒绝时，停止可持续表现并让权威弹药纠正 HUD。
- 先完成半自动单发预测，为后续层级建立可测量的预测键、确认、拒绝和 Cue 去重基础。

**验收**

- 在 100ms 延迟下，本地输入后立即出现第一人称反馈。
- 服务器每次确认射击只生成一颗弹丸。
- 观察者只看到服务器确认的第三人称表现。
- 冷却、死亡、无武器等拒绝场景不生成弹丸，预测表现能结束且不残留。
- 拥有者没有重复声音、重复 Montage 或重复枪口闪光。
- 2% 丢包下权威弹药、生命和得分最终收敛一致。

### 阶段 8：依次实验 P2 至 P5 并做保留决策

**改动**

- P2：预测弹药 HUD、冷却、切枪表现和全自动持续开火节奏，建立数值纠正日志。
- P3：生成纯表现的本地预测弹丸，并用 ShotId/PredictionKey 与服务器弹丸匹配、替换或销毁。
- P4：预测准星命中反馈和受击提示，服务器否决时明确纠正，绝不在客户端直接扣血或计分。
- P5：单独建立服务器历史状态缓存和倒带命中实验，不复用客户端报告的命中 Actor 作为最终结果。
- 每个层级必须各自提交、各自记录性能与表现；不能把 P2 至 P5 合并成一个大提交。

**保留门槛**

- 能显著改善高延迟下的可感知反馈。
- 纠正事件可解释、无长期残留，并且不会产生双重表现。
- CPU、内存和带宽成本与当前学习项目规模相称。
- 自动化能稳定验证权威结果最终收敛。

任何层级不满足门槛，都保留实验记录但从生产路径撤回。最终方案可以停在 P1、P2 或 P3，不以“预测层级越高越好”作为完成标准。

### 阶段 9：清理旧路径并扩展自动化

**改动**

- 删除已经没有调用者的旧开火 RPC、反射动画桥接和 Character 内永久武器数组。
- 更新 CodeGraph 后检查旧路径是否仍有调用者。
- 为玩家/NPC GAS ActorInfo、Attribute、背包 owner-only、Ability 拒绝、预测纠正和 Cue 去重增加自动化断言。
- 更新多人网络计划、蓝图分析和 Agent 自动化操作文档中的入口。

**验收**

- 全量自动化矩阵通过。
- 带渲染 Listen Server 双客户端完成动画和预测观感验收。
- 没有同时存在的两套生命、背包或开火权威来源。

## 仍不纳入本计划

- 通用物品定义、消耗品、装备词条、重量、拖拽 UI 和存档。
- 预测拾取和背包事务回滚。
- 背包跨死亡、跨关卡或存档持久化。
- 在完成单发预测前实现全自动武器的长时间预测循环。

这些内容都可以继续学习，但必须在上述闭环稳定后各自建立独立计划。

## 推荐的第一步

先执行“阶段 0：冻结并验证当前基线”，再只提交“阶段 1：接入 GAS 模块”。不要在第一次 GAS 提交中创建 AttributeSet 或修改蓝图。这样如果编译时间、插件加载或 Target 配置出现问题，可以在最小范围内定位。
