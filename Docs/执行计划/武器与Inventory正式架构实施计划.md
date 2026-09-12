# 武器与 Inventory 正式架构实施计划

## 1. 文档定位

本文承接 [Shooter 完整 Demo 最终路线规划](Shooter完整Demo最终路线规划.md)，用于补齐“Weapon / Inventory 第二阶段基础闭环”尚未覆盖的正式架构，并作为 [P1 Local Predicted 基础射击反馈执行计划](P1_LocalPredicted基础射击反馈执行计划.md) 的强制前置计划。

执行顺序冻结为：

```text
Pickup 重生逻辑门定向验证（生产修复已由用户完成）
→ 大阶段 A：首次正式化（历史实现，配置层随后由单表纠偏取代）
→ 单表纠偏：DT_WeaponData + WeaponRowName
→ 大阶段 B：通用 Actor Pool + WeaponActor 生命周期 + Pickup/死亡集成
→ 正式架构验收
→ P1 Local Predicted 基础射击反馈
```

本文只规划，不授权改写当前工作区中的用户修改。实际实施时，每个提交仍须遵守开发记录规范。

## 1.1 纠偏插入项（2026-09-11，已被 1.2 进一步取代）

大阶段 A 的 `WeaponDefinition + PrimaryAssetId` 配置层已由
[单表武器配置纠偏小计划](单表武器配置纠偏小计划.md) 撤销并完成实施：

```text
DT_WeaponData（唯一武器模板库）
→ Pickup / NPC 只选择一行
→ 该行决定 WeaponActorClass、玩法参数与表现资源
```

因此：

- 本文第 3.1、4.1、4.2、4.5、6.2、6.4、10 节中关于 `UShooterWeaponDefinition`、
  `FPrimaryAssetId DefinitionId`、AssetManager 扫描与 Definition 资产迁移的描述，
  属于**已被纠偏取代的历史设计**，只保留为决策记录，不代表当前实现；
- 第 7 节大阶段 B 的 B1～B4 曾按 RowName 架构完成，并通过收口回归与正式验收，
  该验收结果只作为历史基线保留。

## 1.2 实体池简化重构插入项（2026-09-12）

[武器启动预配置与实体池简化重构方案](武器启动预配置与实体池简化重构方案.md) 已批准并实施，
重新打开了本文的正式架构验收：

```text
DT_WeaponData（仅 World 启动导入）
→ WeaponId → RuntimeConfig 快照 + 预热池
→ Pickup / NPC 租用 AShooterWeapon
→ Inventory（WeaponActor + SlotIndex）
→ Equipment.CurrentWeaponActor
```

- 本文关于 `WeaponRowName`、`InstanceId`、`ActiveWeaponInstanceId`、`BoundInstanceId`、
  通用 `UShooterActorPoolSubsystem` 与 `IShooterPoolableActor` 的正式职责描述，
  全部属于**历史设计**，不作为当前实现或 P1 前置；
- 当前实现以 [Inventory 与武器数据架构](../架构/Inventory与武器数据架构.md) 为准，
  武器身份统一使用 `FName WeaponId`，具体武器统一使用 `AShooterWeapon*`；
- 新模型已于 2026-09-12 完成七阶段正式验收
  （`Saved/Automation/Runs/20260912_130521/Summary.json`）；
  P1 Local Predicted 重新获得前置条件，待用户批准后实施；
- 本文其余章节只保留为阶段决策记录，不再逐节修改。


---

## 2. 当前基线与缺口

### 2.1 已完成且应保留

- `FShooterWeaponInstanceData` 已具备 `InstanceId / DefinitionId / MagazineAmmo / ReserveAmmo / SlotIndex`；
- Inventory FastArray 是服务器权威、OwnerOnly 的武器数据源；
- Equipment 已拥有 `ActiveWeaponInstanceId / CurrentWeaponActor` 和原子装备事务；
- 弹药消费、换弹、拾取授予、死亡清理和 HUD 已形成基础网络闭环；
- 已拥有的武器目前各有一个 WeaponActor，切枪不重复 Spawn/Destroy；
- `GA_Fire / GA_Reload / GA_Equip` 已具备 ServerOnly 基线。

这些能力不是本计划重写对象。

### 2.2 尚未正式化的部分

当前 `DefinitionId` 仍由 WeaponClass 名临时构造：

```text
WeaponClass
→ GetDefaultObject<AShooterWeapon>()
→ 读取 Magazine / Reserve / Fire / Presentation 配置
→ 临时生成 FPrimaryAssetId
```

当前主要缺口：

- 没有正式 `UShooterWeaponDefinition` 资产；
- Inventory 的授予入口仍接收 `TSubclassOf<AShooterWeapon>`；
- Pickup DataTable 仍保存 `WeaponToSpawn`；
- Ammo、Fire、Projectile 与 Presentation 配置仍集中在 WeaponActor CDO；
- `AShooterWeapon::FireProjectile` 同时承担攻击结果与表现广播；
- 没有 `FireBehavior` 边界；
- Inventory Remove/Clear 仍直接 `Destroy()` WeaponActor；
- 没有通用 Actor Pool 和明确的 `InPool / Holstered / Equipping / Equipped` 生命周期；
- NPC 未绑定 Inventory 时仍使用 WeaponActor 内旧弹药兼容路径。

因此当前状态只能称为“Inventory 基础数据闭环”，不能称为最终武器架构。

---

## 3. 依据与本项目取舍

### 3.1 UE5.6 官方依据

- [Asset Management in Unreal Engine 5.6](https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-management-in-unreal-engine?application_version=5.6)：Primary Asset 可通过稳定的 `FPrimaryAssetId` 被 Asset Manager 发现和加载；`UPrimaryDataAsset` 已提供 PrimaryAssetId 与 Asset Bundle 支持。
- [Lyra Inventory and Equipment in Unreal Engine 5.6](https://dev.epicgames.com/documentation/en-us/unreal-engine/lyra-inventory-and-equipment-in-unreal-engine?application_version=5.6)：Inventory 保存长期逻辑实例，Equipment 管理装备后的世界表现；定义资产、实例数据和装备实体应分离。
- [Replicating Actor Components in Unreal Engine 5.6](https://dev.epicgames.com/documentation/en-us/unreal-engine/replicating-actor-components-in-unreal-engine?application_version=5.6)：组件随其 Owner Actor 复制，动态世界实体仍由服务器创建和控制。

本地 UE 5.6.1 源码已核对：

- `UPrimaryDataAsset::GetPrimaryAssetId()` 会为原生子类资产生成稳定类型与名称；
- `PreSave()` 会更新 AssetBundleData；
- 默认 `UAssetManager` 已能扫描并加载 Primary Asset，不要求本项目立刻自定义 AssetManager 子类。

### 3.2 不照搬 Lyra

本项目当前只有武器背包，不引入 Lyra 的完整 Fragment、QuickBar、EquipmentDefinition、AbilitySet 和 UObject 实例体系。采用最小正式化方案：

```text
UShooterWeaponDefinition（共享只读资产）
        ↓ DefinitionId
FShooterWeaponInstanceData（每把武器的可变权威数据）
        ↓ InstanceId
AShooterWeapon（池化世界实体与表现入口）
```

现有 Inventory FastArray 与 EquipmentComponent 保留，避免为“架构一致”重写已经验证的网络闭环。

---

## 4. 冻结的正式职责

### 4.1 WeaponDefinition

新增 `UShooterWeaponDefinition : UPrimaryDataAsset`，只保存所有实例共享的只读配置：

```text
WeaponActorClass
AmmoConfig
├─ MagazineSize
└─ InitialReserveAmmo
FireConfig
├─ bFullAuto
├─ RefireRate
└─ ShotNoise
PresentationConfig
├─ First/Third Person Mesh 与 AnimClass
├─ Montage / Niagara / Sound / Recoil
└─ Pickup Preview Mesh
FireBehavior
└─ 第一版仅 UShooterProjectileFireBehavior
```

约束：

- Definition 不保存 MagazineAmmo、ReserveAmmo、当前装备或计时器；
- PrimaryAssetType 固定，不依赖 WeaponActor 蓝图类名；
- Primary Asset 扫描目录固定在 `/Game/Shooter/Weapons/Definitions`；
- 第一版同步加载即可，但必须验证 Cook/Standalone 下可解析；不提前引入异步加载状态机或自定义 AssetManager。

### 4.2 WeaponInstance 与 Inventory

`FShooterWeaponInstanceData` 继续是每把已拥有武器的永久逻辑身份和权威弹药数据。

Inventory 正式入口改为：

```text
WeaponDefinition
→ 校验有效 PrimaryAssetId
→ 创建 WeaponInstance
→ 初始化 Ammo / Slot
→ Acquire WeaponActor
→ Bind(DefinitionId, InstanceId, Owner)
```

Inventory 不再从 WeaponClass CDO 推导弹药或伪造 DefinitionId。重复判定继续按 DefinitionId，Actor 指针仍不作为武器身份。

### 4.3 FireBehavior

新增无复制、无持久可变状态的行为边界：

```text
UShooterWeaponFireBehavior
└─ ExecuteFire(const FShooterWeaponFireContext&)

UShooterProjectileFireBehavior
└─ 服务器生成 Projectile
```

`FShooterWeaponFireContext` 至少提供：

- WeaponActor；
- Instigator / Owner；
- Muzzle Transform；
- Target Location 或 Aim Direction；
- 当前 Definition 与 InstanceId。

约束：

- `GA_Fire` 仍管理 Ability 生命周期和激活条件；
- Inventory 仍管理 Ammo 消耗；
- FireBehavior 只回答“这一枪如何产生攻击结果”；
- WeaponActor 只负责 Muzzle、Mesh、Attach、表现入口和调用行为；
- Projectile、Damage 和 Ammo 继续只由服务器产生；
- 第一版只有 Projectile Behavior，不为未出现的 Hitscan、Beam、Melee 预建注册表。

### 4.4 Actor Pool 与 WeaponActor 生命周期

新增 World 级通用池和最小 Poolable 协议：

```text
UShooterActorPoolSubsystem
├─ Acquire(ActorClass)
├─ Release(Actor)
├─ Prewarm(ActorClass, Count)
└─ 按 ActorClass 分池

IShooterPoolableActor
├─ OnAcquiredFromPool
└─ OnReleasedToPool
```

第一位正式消费者仅为 WeaponActor。底层保持通用，但 Projectile 池化延后到武器池稳定后单独实施，避免一次同时改变武器和弹丸的网络生命周期。

WeaponActor 状态：

```text
InPool
→ Holstered
→ Equipping
→ Equipped
→ Holstered
→ InPool
```

Release 必须幂等清理：

- 开火与换弹相关 Timer/Delegate；
- Owner、Instigator、WeaponOwner；
- BoundInstanceId、DefinitionId 与运行时行为引用；
- Attach Parent、可见性、碰撞和 Tick；
- 当前 Equipment 引用和 Inventory Actor 映射。

第一版不做复杂 Dormancy、跨 World 全局池、动态容量回收算法或客户端自主 Acquire。

### 4.5 Pickup

保留现有 `AShooterPickup`、Blueprint 重生动画和生成点，只更换授予数据源：

```text
WeaponToSpawn
→ WeaponDefinition 软引用
→ Inventory.TryAddWeapon(Definition)
```

Pickup 只负责服务器 Overlap、一次性闸门、隐藏/重生和请求授予，不直接 Spawn/Release WeaponActor。

---

## 5. 前置验证：Pickup 重生逻辑门

生产修复已由用户完成，本文不再修改其逻辑。开始大阶段 A 前只做验证：

```text
玩家 A 拾取成功
→ Pickup 隐藏且碰撞关闭
→ 等待 Respawn
→ 视觉重生完成并恢复碰撞
→ 玩家 B 拾取成功
→ A、B 各有独立 WeaponInstance
```

同时检查：

- 隐藏期间连续 Overlap 不重复授予；
- Slot 满、重复 Definition 或 Equip 回滚后 Pickup 仍可再次尝试；
- 第二次拾取后重新进入隐藏/计时状态；
- Dedicated Server 下授予发生在服务器。

验证策略：

- 优先复用现有网络协调器和日志，不为一次前置核实另建测试框架；
- 运行 Pickup/Inventory/Equipment 定向测试和一个双客户端跨重生场景；
- 不在本前置项运行七阶段完整回归；
- 若验证失败，停止大阶段 A，只修 Pickup 并做同范围复验。

---

## 6. 大阶段 A：定义、Inventory 与开火行为正式化

### A1：Definition 类型与解析闭环

实施：

- 新增 WeaponDefinition、Ammo/Fire/Presentation 配置结构；
- 新增固定 PrimaryAssetType 与解析辅助入口；
- 配置 Asset Manager 扫描目录；
- 建立无效 ID、重复 ID、未注册资产、非法弹匣容量的失败路径；
- 创建最小测试 Definition，先证明 Editor、Standalone 和 Cook 配置可发现。

定向验证：Definition ID 稳定、资产可解析、非法配置 fail closed。

### A2：Inventory 切换到 Definition

实施：

- 新增按 Definition 授予的唯一生产入口；
- 从 Definition 初始化 WeaponInstance；
- WeaponActorClass 只由 Definition 决定；
- 保持现有 FastArray Payload、OwnerOnly 条件、Slot 与 Ammo 事务不变；
- 短期保留 WeaponClass 适配入口，仅供资产迁移和旧测试，标记待删除。

定向验证：新增、重复定义、SlotFull、SpawnFailed 回滚、OwnerOnly 复制、换弹与 HUD。

### A3：Projectile FireBehavior 落地

实施：

- 提取 `UShooterWeaponFireBehavior` 与 `FShooterWeaponFireContext`；
- 将 Projectile Spawn 从 `AShooterWeapon::FireProjectile` 移入首个行为实现；
- WeaponActor 继续计算可靠 Muzzle/Target Context 并触发表现；
- Ammo 消耗顺序保持“权威消费成功后才执行行为”；
- NPC 兼容路径先明确隔离，不在本子阶段强制重写成玩家 Inventory。

定向验证：每次 Authority Commit 仅扣一发、仅生成一个 Projectile；客户端调用不生成 Gameplay 结果；Rifle/Pistol/GrenadeLauncher 配置差异由 Definition 驱动。

### A4：资产迁移与旧 CDO 配置退出生产路径

实施：

- 为所有当前可达武器创建 Definition 资产；
- Pickup DataTable 增加 Definition 软引用并迁移现有行；
- 修改资产前先阅读 `Plugins/McpAutomationBridge/EXTERNAL_AGENT_GUIDE.md`；蓝图、DataTable 和地图资产只通过 Unreal Editor 资产系统修改并 Save All，不直接写 `.uasset`；
- 用引用审计确认生产路径不再读取 `WeaponToSpawn` 或 Weapon CDO 配置；
- 删除兼容授予入口和不再使用的 CDO 配置字段；若仍有真实调用者，则记录并延后删除，不用硬删换取形式完成。

定向验证：所有正式 Pickup 可授予和装备；Definition 丢失时不消费 Pickup；资产扫描与打包引用完整。

### 大阶段 A 收口回归

只在 A1～A4 全部完成后运行一次完整回归：

```text
Build
Automation
Standalone
DedicatedNetwork
ListenNetwork
EmulatedNetwork
DisconnectCleanup
```

通过门槛：

- 生产代码不存在“由 WeaponClass 名伪造 DefinitionId”的路径；
- Inventory 不从 Weapon CDO 初始化 Ammo；
- Projectile 只从 Projectile FireBehavior 的服务器路径生成；
- 现有 Fire/Reload/Equip/Pickup/HUD 网络闭环无回退；
- Summary 和关键日志进入该大阶段最后一个开发记录。

---

## 7. 大阶段 B：通用池与 WeaponActor 生命周期集成

### B1：池容器与 Poolable 契约

实施：

- 新增 WorldSubsystem、按 ActorClass 分池的容器和 Poolable 接口；
- 只实现 Acquire、Release、Prewarm、Capacity 上限与 Register Existing 的必要部分；
- 重复 Release、错误 World、错误 Class、PendingKill Actor 必须 fail closed；
- 测试先使用轻量测试 Actor，不立即接入武器。

定向验证：复用同一 Actor、跨 Class 不串池、World 销毁后无悬空对象、Release 幂等。

### B2：WeaponActor 生命周期状态机

实施：

- WeaponActor 接入 WeaponRowName/Instance 绑定和生命周期状态；
- 把 Activate/Deactivate 收敛为明确状态转换；
- 每次 Acquire 重新绑定 Owner、Instigator、WeaponRowName、InstanceId；
- 每次 Release 完整停止 Timer、解除 Delegate、隐藏、Detach 并清空绑定；
- 保持 Equipment 的 Active Instance 与 Current Actor 原子提交语义。

定向验证：非法状态转换被拒绝；切枪不 Release；移除武器才 Release；复用后不继承旧 Owner、Ammo 镜像、Timer 或表现。

### B3：Inventory、Pickup 与死亡清理接入池

实施：

- Inventory Add 从 Spawn 改为 Acquire；
- Remove/Clear 从 Destroy 改为 Release；
- Spawn/Acquire 失败继续回滚 WeaponInstance；
- Pickup 仍只请求 Inventory 授予，不直接接触池；
- 死亡/Clear 事务沿用当前已验证顺序：冻结待释放 Actor → 清 Inventory Entries 并广播 → Equipment 清空 Active/Current → Release WeaponActor；
- Disconnect、EndPlay 和 World teardown 使用同一幂等清理边界。

定向验证：双玩家拾取、切枪、死亡、断线、重复 Clear、Acquire 失败回滚、复用后的远端表现。

### B4：兼容路径与可观测性收口

实施：

- 审计旧 `Destroy()`、直接 `SpawnActor<AShooterWeapon>`、WeaponClass 授予和 CDO 配置读取；
- 删除本计划产生的临时桥接与调试入口；
- NPC 的旧弹药镜像若仍被真实使用，保留为明确的 PvE 遗留项，不伪装成已完成；
- 为 Acquire/Release、WeaponRowName/Instance 绑定和状态转换保留低噪声测试标记；
- 更新最终路线规划与 P1 前置状态。

定向验证：生产 WeaponActor 只有池入口负责创建/复用；所有 Release 后引用收敛；无双重事件或旧武器残留。

### 大阶段 B 收口回归

只在 B1～B4 全部完成后运行一次七阶段完整回归。

额外必须核对：

- Dedicated 与 Listen 下 Actor 复用后 Owner/CurrentWeaponActor 正确；
- Emulated 网络下没有旧 WeaponActor 短暂复活或串给另一玩家；
- DisconnectCleanup 后池、Inventory、Equipment 和 Ability 无悬挂引用；
- Pickup 跨重生仍允许另一玩家拾取；
- Summary 和关键网络日志进入最后一个开发记录。

---

## 8. 验证节奏与 Token 控制

本计划不在每个子阶段运行完整回归。

| 时点 | 验证范围 | 是否完整回归 |
|---|---|---|
| Pickup 前置核实 | Pickup + Inventory + Equipment 定向测试、双客户端跨重生 | 否 |
| A1～A4 各子阶段 | Build + 本子阶段相关自动化/网络场景 | 否 |
| 大阶段 A 收口 | 七阶段 RunAll + Summary/日志审计 | 是，1 次 |
| B1～B4 各子阶段 | Build + Pool/Lifecycle/受影响功能定向测试 | 否 |
| 大阶段 B 收口 | 七阶段 RunAll + Summary/日志审计 | 是，1 次 |

失败处理：

```text
定向测试失败
→ 只修当前失败边界
→ 重跑失败项和直接依赖项
→ 不因一次局部失败提前启动完整回归
```

只有出现跨系统污染证据（例如 Ammo、Equipment、死亡和网络复制同时异常）时，才允许在大阶段中途追加一次完整回归，并在开发记录中写明理由。

每次实际提交仍需：

```text
git diff --check
git diff --cached --check
git diff --cached --name-only
对应开发记录
```

Source/ 或 Plugins/ 有新增、删除、移动、重命名时，在该批结构变化完成后只刷新一次 Visual Studio 项目文件。

---

## 9. 明确非目标

- 不在本计划实施 LocalPredicted、Ammo Prediction 或 Projectile Prediction；
- 不引入完整 Lyra Inventory/Fragment/QuickBar；
- 不做掉落、交易、堆叠物品、持久化存档；
- 不做 Projectile Pool；
- 不为未来 Hitscan/Beam 预建多行为注册系统；
- 不优化复杂 Net Dormancy 或 Replication Graph；
- 不重做 Pickup Blueprint 重生动画；
- 不顺手重构 GAS、UI 或动画系统。

---

## 10. 最终验收与进入 P1 的门槛

验收时间：2026-09-11。

以下条件已全部满足，正式架构验收通过，允许进入 P1：

### 数据与身份

- `/Game/Shooter/Data/DT_WeaponData` 是唯一正式武器模板库，所有正式行可解析；
- WeaponInstance 只保存 `WeaponRowName / InstanceId` 和实例可变数据；
- Inventory 不再依赖 WeaponClass CDO、WeaponDefinition 或 PrimaryAssetId 构造正式数据；
- Pickup 只选择 `FDataTableRowHandle`，表行决定 WeaponActorClass、玩法参数与表现资源。

### 行为与权威

- Projectile FireBehavior 是唯一正式弹丸生成边界；
- Ammo、Projectile、Damage 继续服务器权威；
- GA、FireBehavior、WeaponActor、Inventory、Equipment 职责与第 4 节一致。

### 生命周期

- WeaponActor 通过通用 Actor Pool Acquire/Release；
- 切枪、移除、死亡、断线和复用均无 Timer、Delegate、Owner、Instance 或表现残留；
- 客户端 Owner 复制切换会先解除旧 Pawn 的 OnDestroyed 委托，旧 Pawn 延迟销毁不影响已复用武器；
- Actor 指针不替代 InstanceId 成为永久身份。

### Pickup 与网络

- Pickup 使用 WeaponRowName 请求 Inventory 授予；
- 玩家 A 拾取并重生后，玩家 B 可再次成功拾取；
- Dedicated、Listen、Emulated 和 DisconnectCleanup 均通过。

### 回归证据

- 单表纠偏 C4 与大阶段 B 均有完整回归 Summary；正式验收修复后的最终证据为
  `Saved/Automation/Runs/20260911_182343/Summary.json`；
- 最终工作区不含本计划产生的临时兼容代码；
- NPC 非池化兼容边界已在架构文档中明确记录，不阻塞玩家正式架构。

验收后执行：

```text
冻结 WeaponRowName / InstanceId / FireBehavior / WeaponActor Lifecycle API
→ 重新核对 P1 Local Predicted 计划的表现入口
→ 再开始预测实现
```
