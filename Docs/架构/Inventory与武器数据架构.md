# Inventory 与武器数据架构

## 1. 文档定位

本文档记录 Inventory / WeaponActor / WeaponRuntime 领域的长期职责边界，以及当前已经落地的网络数据约束。

- **已落地事实**：当前代码中真实存在并已通过 Build / Automation / 多客户端网络验证的行为。
- **历史实现**：曾存在、已被重构方案替代的模型，只保留为决策记录。
- **后续尚未实现**：属于后续阶段（预测、Dormancy、Pool 容量等）的工作，当前不存在对应代码路径。

本文档以
[武器启动预配置与实体池简化重构方案](../已完成计划/武器启动预配置与实体池简化重构方案.md)
为当前权威，
不替代执行计划；逐阶段实施与验收仍以对应计划为准。

---

## 2. 当前运行时模型（启动预配置重构后已落地）

当前已经成立的组件与数据关系：

```text
World BeginPlay
└─ UShooterWeaponRuntimeSubsystem
   ├─ 一次性读取 /Game/Shooter/Data/DT_WeaponData
   ├─ TMap<FName WeaponId, FShooterWeaponRuntimeBucket>
   │  └─ RuntimeConfig（启动冻结的 FShooterWeaponConfigRow 快照）
   │  ├─ AvailableActors（已归还复位、待租用）
   │  └─ LeasedActors（已租出）
   └─ 服务器按 InitialPoolSize 预创建 WeaponActor

AShooterCharacter
└─ UShooterInventoryComponent（OwnerOnly FastArray）
   ├─ TArray<FShooterInventoryWeaponEntry>
   │  ├─ TObjectPtr<AShooterWeapon> Weapon
   │  └─ int32 SlotIndex
   └─ UShooterEquipmentComponent（所有观察者可见）
      └─ AShooterWeapon* CurrentWeaponActor
```

最终只保留四个运行时概念：

| 概念 | 表达方式 | 职责 |
|---|---|---|
| 武器种类 | `FName WeaponId` | Pickup / NPC 选择、配置快照键、对象池分桶、Inventory 判重 |
| 具体武器 | `AShooterWeapon*` | 当前对局中一件真实存在的池化武器实体 |
| 背包位置 | `int32 SlotIndex` | 切枪顺序与容量约束 |
| 当前装备 | `AShooterWeapon* CurrentWeaponActor` | Equipment 唯一装备权威 |

### 2.1 WeaponRuntimeSubsystem

- `OnWorldBeginPlay` 在所有 Actor BeginPlay 前执行，全部端加载 DT_WeaponData 一次，
  校验行结构并按行值复制形成 RuntimeConfig 快照；
- 服务器按每行 `InitialPoolSize` 预创建复制型 WeaponActor，写入永久 WeaponId 并应用静态配置；
- 客户端不自主创建权威 WeaponActor，只建立快照，供复制到达的 WeaponId 恢复静态表现；
- `AcquireWeapon(WeaponId, Owner, Instigator)` 池命中复用，耗尽时按冻结快照弹性 Spawn，
  成功直接返回 `AShooterWeapon*`；
- `ReleaseWeapon` 只接受本 Bucket 的 LeasedActor，执行开火/换弹 Timer、委托、Owner、
  生命周期与初始弹药的租用复位，静态配置不重复应用；
- 运行时不再访问 UDataTable 或缓存 Row 指针，不设最大容量，不做 Dormancy。

### 2.2 WeaponActor

静态 / 创建后不变：

```text
WeaponId
MagazineSize / InitialReserveAmmo
Fire / Timing / Attack / Socket / Mesh / Anim / FX / Audio 参数
ProjectileClass（由行配置镜像，唯一弹丸生成路径使用）
```

- 开火行为定义（`UShooterWeaponFireBehavior` / `UShooterProjectileFireBehavior`）当前处于休眠状态：
  WeaponActor 不实例化也不调用行为，弹丸由 `FireProjectile` 直接用 `ProjectileClass` 生成；
  `FShooterWeaponConfigRow::FireBehaviorClass` 列与 DT_WeaponData 取值保留，运行时不读该列。

运行时可变：

```text
MagazineAmmo（OwnerOnly 复制）
ReserveAmmo（OwnerOnly 复制）
Owner / Instigator
LifecycleState（InPool -> Holstered -> Equipping -> Equipped -> Holstered -> InPool）
Timer / Delegate / bIsFiring
```

- 弹药权威在 WeaponActor：玩家与 NPC 共用同一字段，换弹事务 `ReloadFromReserve` 直接读写 Actor；
- `WeaponId` 是创建后不变的初始复制数据；客户端 OnRep 从本地 RuntimeConfig 快照恢复表现，
  不调用 DataTable API；
- Release 时恢复该 WeaponId 的初始弹药，旧 Owner 的延迟事件 / 旧客户端请求由 GAS 提交前校验拒绝。

### 2.3 Inventory

- `FShooterInventoryWeaponEntry` 只保存 `Weapon + SlotIndex`，FastArray 使用 `COND_OwnerOnly`；
- `AddWeapon` 只接收已经 Acquire 的 WeaponActor，按 `Weapon->GetWeaponId()` 判重并自动选空 Slot；
- `RemoveWeapon / ClearInventory` 先广播（Equipment 清空当前装备），再把 Actor Release 回正确 Bucket；
- 查询全部使用短数组线性扫描；首版最多 3 个 Slot；
- Inventory 不再读取 DataTable、生成 GUID、保存弹药或维护 InstanceId 映射。

### 2.4 Equipment

- 只保存 `CurrentWeaponActor` 并复制给所有观察者；`OnRep_CurrentWeaponActor` 发布
  `(Previous, Current)` 逻辑事件并进入幂等表现收敛；
- `EquipWeapon` 校验目标仍在 Inventory、Owner 正确、不在池内后原子提交；
- `ClearEquippedWeapon` 幂等清空；Inventory Remove/Clear 的广播与 Owner Client FastArray
  Remove 回调都能收敛本地装备镜像。

### 2.5 Pickup

- 只保存 `FName WeaponId`、`RespawnTime` 与 `bPickupAvailable`；
- 唯一编辑器例外：`OnConstruction` 在 `WITH_EDITOR` 下读表恢复预览 Mesh；打包后的授予路径不访问表；
- 服务器 Overlap 顺序：
  `校验角色 / 换弹状态 → HasWeaponId → AcquireWeapon → AddWeapon → EquipWeapon → 消费 Pickup`；
- 未知 WeaponId、重复武器、Slot 满、弹性 Spawn 失败、装备提交失败均不消费 Pickup，
  已 Acquire 未提交的 Actor 由调用方 Release 回池。

### 2.6 NPC

- NPC 配置 `FName WeaponId`，服务器 BeginPlay 时从同一 WeaponRuntimeSubsystem 池 Acquire，
  并 `ActivateWeapon` 为当前持有武器；
- NPC 没有 Inventory / Equipment，但武器弹药、静态配置、生命周期与归还语义和玩家完全一致；
- NPC 销毁时 `AShooterWeapon::OnOwnerDestroyed` 把租出武器 Release 回其 WeaponId Bucket。

### 2.7 GAS

- `GA_Fire` → `CurrentWeaponActor->StartFiring / TryFire`，弹药与节拍都在 WeaponActor；
- `GA_Reload` → 校验当前装备仍在 Inventory 且 Owner 正确后 `WeaponActor->ReloadFromReserve`；
- `GA_Equip` → `Inventory->FindNextWeapon(CurrentActor)` → `Equipment->EquipWeapon(TargetActor)`；
- 提交前统一校验：Actor 仍在 Inventory、`Actor->GetOwner() == Character`、
  Equipment CurrentActor 与 Ability 目标一致、Actor 不处于 InPool。

---

## 3. 网络可见性

| 字段 | 范围 | 说明 |
|---|---|---|
| Inventory Entries（Weapon + Slot） | OwnerOnly | 完整背包只对拥有者可见 |
| Equipment.CurrentWeaponActor | 所有观察者 | 第三人称当前持枪表现 |
| WeaponActor.WeaponId | 初始复制 / 所有观察者 | 客户端从启动快照恢复静态表现 |
| WeaponActor.MagazineAmmo / ReserveAmmo | OwnerOnly | HUD 与本地表现 |
| WeaponActor.Owner | UE Actor Owner 复制 | 租用归属 |

- Actor 引用与 WeaponId 的 RepNotify 到达顺序不作假设；所有表现入口必须幂等，
  任一字段晚到后都能再次收敛；
- 首轮保持“隐藏池 Actor 仍存在于 World”的边界，不引入 Net Dormancy；
  若池规模实测造成复制压力，再单独评估 DORM_DormantAll 与唤醒时机。

---

## 4. FastArray 设计

- `FShooterWeaponInventoryList` 继承 `FFastArraySerializer`，Items 只含 `FShooterInventoryWeaponEntry`；
- 增删必须经 `AddItem / RemoveItem / ClearItems`，保证 `MarkItemDirty / MarkArrayDirty`；
- `PreReplicatedRemove` 广播 `OnWeaponEntryRemoved`，由 InventoryComponent 在 Owner Client
  同步清空本地装备镜像；
- 结构属性上没有自定义 NetSerialize；复制规则由 `COND_OwnerOnly` 与 UE 内建 Struct Delta 处理。

---

## 5. DataTable 只负责开局输入

`/Game/Shooter/Data/DT_WeaponData` 继续是一张完整武器配置表，但职责为：

```text
编辑器配置源
→ World 启动时完整读取、校验并复制
→ 形成当前 World / 对局不可变的 RuntimeConfig
→ 运行时不再访问 UDataTable 或缓存 Row 指针
```

- 表的 RowName 只在启动导入边界转换为 `WeaponId`，不泄漏到 Inventory、Equipment、
  WeaponActor 授予 API 或 GAS Ability；
- `ShooterWeaponTable` 仍是 RowName → ConfigRow 的集中解析入口，调用方只剩：
  RuntimeSubsystem 启动导入、自动化测试构造表、Pickup 编辑器预览；
- 不需要在行结构中增加重复的 `WeaponId` 列。

---

## 6. 历史实现（已被本重构替代）

以下模型在 `武器启动预配置与实体池简化重构方案` 完成后删除，不再保留为兼容分支：

```text
Pickup.FDataTableRowHandle WeaponType / 运行时 GetRow
Inventory.FShooterWeaponInstanceData（FGuid InstanceId / WeaponRowName / Ammo / Slot）
Inventory.TryAddWeaponRow
Inventory.ActiveWeaponInstanceId / BoundInstanceId 映射
Equipment.ActiveWeaponInstanceId
AShooterWeapon.BoundInstanceId / WeaponRowName / SetInstanceBinding
AShooterWeapon.CurrentBullets 镜像命名
UShooterActorPoolSubsystem / IShooterPoolableActor 通用对象池
NPC 直接 Spawn + WeaponClass / WeaponRowName 兼容路径
```

这些名称只用于历史检索与旧回归基线对照，不是当前运行时 API。

---

## 7. 为什么不需要自定义实例 GUID

当前对局中，WeaponActor 本身就是 UE 网络复制的主要实体：

- Inventory 直接复制 `TObjectPtr<AShooterWeapon>`，UE 网络层负责 Actor 身份与引用解析；
- 需要查询某一件具体武器时直接使用 Actor 引用；
- 池 Actor 跨玩家复用时，WeaponId 永久不变，租用归属由 Owner 复制与 Inventory / Equipment 引用表达；
- 旧 Owner 的延迟事件或旧客户端请求，通过“Actor 仍在 Inventory + Owner 一致 + 非 InPool”校验拒绝。

因此不需要再叠加 `InstanceId / BoundInstanceId / PoolHandle / WeaponObjectId` 等中间身份。

---

## 8. 最重要的维护约束

1. 生产代码中只有 `UShooterWeaponRuntimeSubsystem` 启动导入允许访问 `DT_WeaponData`；
   Pickup、Inventory、Equipment、WeaponActor 与 GAS 不得调用 DataTable API。
2. WeaponId 一旦由池写入不得改写；静态配置只在创建 / 弹性 Spawn 时应用一次。
3. 归还池必须经 `UShooterWeaponRuntimeSubsystem::ReleaseWeapon`，不得直接 Destroy 池化 Actor。
4. Inventory 提交失败时，调用方（Pickup / 测试）必须把已 Acquire 的 Actor Release 回池。
5. 新武器种类 = 在 DT_WeaponData 增加一行 + 对应 Pickup BP 设置 WeaponId；
   不要新增 Runtime 身份字段或第二张配置表。
6. `InitialPoolSize` 只控制启动预热，不限制运行时增长；修改表后需要重启 World 才生效。
7. 新增测试应通过 `UShooterWeaponRuntimeSubsystem::SetWeaponTableOverride` 注入瞬态表，
   不得修改磁盘 DT_WeaponData 来满足测试。

---

## 9. 启动配置的取舍记录

- 采用“复制行值快照”而不是长期缓存 Row 指针：UE DataTable 返回的行指针不应跨局部作用域长期保存，
  且本方案需要世界开始时冻结配置；
- 采用“预热 + 弹性 Spawn”而不是容量上限：首轮保持简单，后续有测量数据再决定缩容 / Dormancy；
- 采用 WeaponId 池而不是通用 ActorPool：当前只有 WeaponActor 一个池化消费者，通用抽象被删除；
- 不引入 WeaponDefinition、PrimaryAsset、DataRegistry 或新的配置资产层。

---

## 10. GA 与装备事务已落地

- `GA_Fire / GA_Reload / GA_Equip` 都是 `InstancedPerActor + ServerOnly`；
- Fire 校验 Avatar、死亡 Tag、当前 WeaponActor 归属、可见与可消耗弹药；
- Reload 缓存当前 WeaponActor，等待 `ReloadDuration` 后二次校验并执行唯一一次原子转移；
- Equip 按 Slot 顺序解析下一个合法 Actor，等待 `EquipDuration` 后二次校验并提交
  `Equipment->EquipWeapon`。

---

## 正式验收基线

2026-09-12 七阶段完整回归通过：

```text
Saved/Automation/Runs/20260912_130521/Summary.json
Build / Automation / Standalone / DedicatedNetwork / ListenNetwork /
EmulatedNetwork / DisconnectCleanup：全部 Passed
Automation：80 项成功，Failed=0、NotRun=0
Dedicated 2 Clients / Listen 1 Remote Client / Emulated 2 Clients 成功标记数量：2 / 1 / 2
Emulated：PktLag=100、PktLoss=2
Disconnect：DirtyPooled=0、Orphans=0
```

---

## 相关文档与基线

- [武器启动预配置与实体池简化重构方案](../已完成计划/武器启动预配置与实体池简化重构方案.md)
- [武器与 Inventory 正式架构实施计划](../已完成计划/武器与Inventory正式架构实施计划.md)
  （旧身份模型部分为历史）
- [Shooter 完整 Demo 最终路线规划](../执行计划/Shooter完整Demo最终路线规划.md)
- [P1 Local Predicted 基础射击反馈执行计划](../已完成计划/P1_LocalPredicted基础射击反馈执行计划.md)
  （已完成）
- [输入缓冲与 Reload 本地预测执行计划](../执行计划/输入缓冲与Reload本地预测执行计划.md)
- [Agent 自动化验证操作手册](../Agent自动化验证操作手册.md)
