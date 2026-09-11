# Inventory 与武器数据架构

## 1. 文档定位

本文档记录 Inventory / WeaponInstance / WeaponActor 领域的长期职责边界，以及当前已经落地的网络数据约束。

文档使用三种状态区分事实：

- **已落地事实**：当前代码中真实存在并已通过 Build / Automation / 多客户端网络验证的行为。
- **当前架构方向**：项目已同意的设计目标，但允许尚未实现。
- **后续尚未实现**：属于后续阶段（预测、Reload / Equip 等）的工作，当前不存在对应代码路径。

本文档不替代执行计划。逐阶段实施与验收以对应阶段的执行计划为准。

---

## 2. 当前 Inventory 数据模型（2A / 2B / 2C / 2D 已落地）

当前已经成立的组件与数据关系：

```text
AShooterCharacter
└─ UShooterInventoryComponent
   ├─ FShooterWeaponInventoryList
   │  └─ TArray<FShooterWeaponInstanceEntry>
   │     └─ FShooterWeaponInstanceData
   ├─ ActiveWeaponInstanceId
   └─ BoundWeaponActors（InstanceId -> WeaponActor）
```

2B 已落地：

- Pickup 最终授予路径已迁移到 `UShooterInventoryComponent::TryAddWeaponRow`（武器模板行名）。
- 服务器创建 WeaponInstance 后同步创建并绑定 WeaponActor。
- `AShooterWeapon::BoundInstanceId` 已建立，OwnerOnly 复制。
- 重复 WeaponClass 授予与 Slot 满均明确 Reject。

2C 已落地：

- 切换请求由 GA_Equip（ServerOnly）基于 `ActiveWeaponInstanceId` 与 Slot 顺序选择目标，等待 EquipDuration 后通过 `CommitActiveWeapon` 原子提交。
- `Character.CurrentWeapon` 作为公共 `CurrentWeaponActor` 表现，复制给所有观察者。
- 无效 InstanceId 不会改写 Active 身份。
- Owner 客户端可观察到 Active 与当前 WeaponActor 的 BoundInstanceId 一致。

2D 已落地：

- `MagazineAmmo` 权威位于 `FShooterWeaponInstanceData`；GA_Fire → WeaponActor → Inventory 是唯一消费路径。
- `AShooterWeapon::CurrentBullets` 保留为 OwnerOnly 兼容镜像，未绑定的 NPC 旧路径仍可用。
- 死亡时服务器销毁 WeaponActor、清空 Inventory 与 Active，并置空 CurrentWeapon。
- 重生后 Inventory 为空。

当前 `FShooterWeaponInstanceData`：

```text
FShooterWeaponInstanceData
├─ FGuid InstanceId
├─ FName WeaponRowName
├─ int32 MagazineAmmo
├─ int32 ReserveAmmo
└─ int32 SlotIndex
```

职责定义：

- `Inventory`：当前生命中的武器逻辑数据权威源。
- `WeaponInstanceData`：单把武器的运行时逻辑状态。
- `InstanceId`：稳定逻辑身份，服务器生成且不可变，只用于弹药、装备事务、Actor 池绑定与移除。
- `WeaponRowName`：`DT_WeaponData` 行名，武器类型身份与唯一只读配置来源。
- `ActiveWeaponInstanceId`：当前逻辑武器身份。

单表武器配置（纠偏后已落地）：

- `/Game/Shooter/Data/DT_WeaponData` 是唯一武器模板数据源，一行描述一类完整武器；
- 行结构为 `FShooterWeaponConfigRow : FTableRowBase`，包含 `WeaponActorClass`、弹药、开火节奏、
  时序、攻击、Socket、网格 / 动画、FX / 音频与视角参数；
- `ShooterWeaponTable` 是 `RowName → ConfigRow` 的唯一集中解析入口；
- Pickup 与 NPC 只选择行名，WeaponActor 在绑定时把行的只读配置应用到自身运行时镜像。

当前尚未实现：

- Local Predicted GA_Fire。
- Reload / Equip Montage 表现接点。

在后续阶段落地前，不得在代码或文档中当作“已经存在”。

---

## 3. 网络可见性

当前两个 Inventory 复制属性均为 OwnerOnly：

```text
ReplicatedInventory        = COND_OwnerOnly
ActiveWeaponInstanceId     = COND_OwnerOnly
```

设计原因：

- 远端玩家不需要知道完整背包、Ammo 和 Slot 数据。
- 后续公共当前武器表现由 `Character.CurrentWeaponActor` 承担，而不是把完整 Inventory 复制给所有观察者。

状态说明：

- `Character.CurrentWeapon` 已作为公共 `CurrentWeaponActor` 表现复制给所有观察者，并由 ActiveWeaponInstanceId 驱动切换。
- 当前远端客户端看到的是空 Inventory 组件，这是 OwnerOnly 的预期结果。

---

## 4. FastArray 设计

项目继续使用 UE 原生 FastArray 机制：

- `FFastArraySerializer`
- `FFastArraySerializerItem`
- `FastArrayDeltaSerialize`
- `MarkItemDirty`
- `MarkArrayDirty`

维护规则：

- Add / 修改 Item：调用 `MarkItemDirty`。
- Remove / Clear 等结构变化：调用 `MarkArrayDirty`。
- Item Payload 使用 UE 默认反射序列化，不再手写 `NetSerialize`。

当前边界：

- `FShooterWeaponInventoryList::NetDeltaSerialize` 交给 UE 原生 `FastArrayDeltaSerialize`。
- `FShooterWeaponInstanceEntry` 不再有自定义 `NetSerialize`，也不再声明 `WithNetSerializer`。

---

## 5. 武器模板行取代 Definition 主资产

纠偏前 `FShooterWeaponInstanceData::DefinitionId` 使用 `FPrimaryAssetId`，而 `FPrimaryAssetId`
不是 UHT 反射类型，无法声明为 `UPROPERTY`，因此 Item Payload 必须手写 `NetSerialize`。
单表武器配置纠偏撤销了 `UShooterWeaponDefinition` / AssetManager 扫描这一层，改为：

```text
FShooterWeaponInstanceData::WeaponRowName : FName
```

`FName` 是标准 UHT 反射类型，带来的直接结果：

- 武器类型身份可以声明为 `UPROPERTY`，进入默认属性网络复制；
- 不再需要 `WithNetSerializer`，也不再需要 `SetDeltaSerializationEnabled(false)`；
- 武器配置只复制行名，不复制整行配置：客户端与服务器通过同一张 `DT_WeaponData` 恢复只读配置。

准确表述：

> `WeaponRowName` 是反射类型，走 UE 默认复制路径；是否需要恢复自定义序列化，
> 只有在出现真实压缩或兼容需求时才重新评估。

不要使用"FName 不能复制"或"FastArray 必须手写 Payload"这类模糊说法。

---

## 6. 为什么不再需要自定义 NetSerialize

当前 `FShooterWeaponInstanceEntry` 只包含：

```text
InstanceId      FGuid
WeaponRowName   FName
MagazineAmmo    int32
ReserveAmmo     int32
SlotIndex       int32
```

全部字段都是 UHT 反射类型，因此：

- 删除 `FShooterWeaponInstanceEntry::NetSerialize`；
- 删除 `TStructOpsTypeTraits<FShooterWeaponInstanceEntry>` 的 `WithNetSerializer` 特化；
- 恢复 Item 内部 Struct Delta，由反射布局自动比较字段差异。

---

## 7. Struct Delta 开关状态

当前 `FShooterWeaponInventoryList` **不再**调用 `SetDeltaSerializationEnabled(false)`。

- FastArray 仍按 Item 的 Add / Change / Remove 工作；
- Item 内部 Struct Delta 也恢复为 UE 默认行为。

原因：

- 关闭 Struct Delta 的唯一理由是 `DefinitionId` 不是反射字段；
- 改为 `FName WeaponRowName` 后该理由消失；
- 保留关闭状态会让每个 dirty Item 走完整 Payload，属于没有收益的协议放大。

---

## 8. 最重要的维护约束

> **IMPORTANT**
>
> 任何新增到 `FShooterWeaponInstanceData` 且需要复制给 Owner Client 的字段，
> 必须声明为 UHT 反射类型并通过 `UPROPERTY` 暴露，同时补充对应网络复制测试。
>
> 只有在字段确实无法声明为 UPROPERTY，或存在明确压缩 / 兼容需求时，
> 才重新引入自定义 `NetSerialize`，并在本文档说明理由与删除条件。

失败模式：

> 引入非反射字段而忘记更新序列化，会出现“服务器存在该字段、Owner Client 静默缺失”。

---

## 9. 单表配置的取舍记录

本项目的武器规模是“一张表 + 少量武器类型”，没有独立 Definition 资产生命周期需求，因此选择：

```text
DT_WeaponData（唯一武器模板库）
→ Pickup / NPC 只选择一行
→ 该行决定 WeaponActorClass、玩法参数与表现资源
```

取舍理由：

- 一处配置、一次选择，避免“表、Definition、蓝图默认值”三份来源；
- 不需要 AssetManager 扫描、PrimaryAssetId 解析或额外的迁移工具链；
- `FDataTableRowHandle` 与行结构足以表达“一行完整武器”。

已明确放弃：

- `UShooterWeaponDefinition`、`FPrimaryAssetId DefinitionId` 与 AssetManager 扫描配置；
- 武器蓝图默认值中的可表格化字段（已从蓝图 CDO 移除，行是唯一权威来源）。

`BP_ShooterWeapon_*` 目前仍作为 `WeaponActorClass` 候选保留，只承载确有必要的结构或逻辑差异；
是否合并为通用武器蓝图由后续计划单独评估，不属于本次纠偏。

依据参考：
[Data Driven Gameplay Elements](https://dev.epicgames.com/documentation/en-us/unreal-engine/data-driven-gameplay-elements-in-unreal-engine?application_version=5.6)、
[Asset Management](https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-management-in-unreal-engine?application_version=5.6)。

---

## 10. 未来 WeaponActor 边界

只记录当前已经同意的高层方向：

```text
WeaponInstanceData = 逻辑身份和运行时数据
WeaponActor         = 世界实体 / 表现实体
InstanceId          = 永久逻辑身份
WeaponActor*        = 不能作为永久武器身份
```

未来方向：

- `WeaponActor.BoundInstanceId` → 对应 WeaponInstance。
- `Character.CurrentWeaponActor` → 对所有观察者提供公共当前持枪表现。
- 完整 Inventory → OwnerOnly。

状态说明：

> 2B 已建立 `WeaponActor.BoundInstanceId` 绑定，2C 已建立 Active 驱动切换与公共 CurrentWeapon 表现，2D 已完成 Ammo 权威迁移与 Death Clear。

---

## 11. GA_Fire ServerOnly 已落地

GA_Fire 阶段（4A / 4B / 4C / 4D）已落地事实：

```text
Enhanced Input / AI Intent
→ UShooterAbilitySystemComponent.Input.Fire
→ GA_Fire（ServerOnly，InstancedPerActor）
→ 服务器校验 Avatar / Death / Weapon / Ammo
→ WeaponActor.StartFiring
→ Inventory.ConsumeMagazineAmmo
→ 服务器 Projectile / GAS Health
```

- 玩家 ASC 仍位于 `AShooterPlayerState`，NPC ASC 位于自身；两端都只保留一个 GA_Fire Spec。
- 玩家重生不重复授予；NPC 与玩家共用同一 GA_Fire 规则。
- `Input.Fire`、`State.Dead`、`State.Firing` 是 GA_Fire 阶段冻结的 Native GameplayTag。
- 死亡、切枪、Weapon 销毁、Inventory Clear、断线均会取消 GA_Fire；释放输入结束 Ability 并清除 Weapon 计时器。
- 旧 `ServerStartFire / ServerStopFire` 已由 CodeGraph 确认无调用者并删除。
- 客户端预测、预测弹药 HUD、服务器倒带仍未实现。

## 12. GA_Reload / GA_Equip ServerOnly 已落地

GA_Reload / GA_Equip 阶段（5A / 5B / 5C / 5D）已落地事实：

```text
IA_Reload
→ ASC.Input.Reload
→ GA_Reload（ServerOnly，InstancedPerActor）
→ 服务器校验 CurrentWeapon / Instance / Ammo
→ WaitDelay(ReloadDuration)
→ Inventory.ReloadMagazine 原子提交

IA_SwitchWeapon
→ ASC.Input.Equip.Next
→ GA_Equip（ServerOnly，InstancedPerActor）
→ 取消 Fire / Reload
→ WaitDelay(EquipDuration)
→ Character.CommitActiveWeapon
→ ActiveWeaponInstanceId + CurrentWeapon 原子提交
```

- `State.Reloading` / `State.Equipping` 与 `State.Dead` / `State.Firing` 共同构成 Fire / Reload / Equip 互斥合同。
- 客户端非权威预检只确认 ASC 与 Avatar 对应，不读取可能过期的 ActivationBlockedTags；完整校验全部在服务器执行。
- 旧 `ServerSwitchWeapon` 已由 CodeGraph 确认无调用者并删除。
- 候选 Mannequin Reload / Equip 资产经编辑器只读检查为 `AnimSequence` 而非 `UAnimMontage`，且 Skeleton 为 `SK_Mannequin`；本阶段不接入表现，列为 Demo Polish 遗留项。

## 相关文档与基线

- [GA_Fire ServerOnly 执行计划](../执行计划/GA_Fire_ServerOnly执行计划.md)
- [2A 开发记录](../开发记录/2026-08-19-1459-Inventory建立WeaponInstanceFastArray数据模型.md)
- [4A 开发记录](../开发记录/2026-08-20-0735-GAS建立开火Ability输入与授予生命周期.md)
- [4B 开发记录](../开发记录/2026-08-20-0745-GAS将玩家开火迁移为ServerOnlyAbility.md)
- [4C 开发记录](../开发记录/2026-08-20-0756-GAS补齐开火取消边界与NPCAbility链路.md)
- [GA_Reload / GA_Equip 执行计划](../已完成计划/GA_Reload与GA_Equip_ServerOnly执行计划.md)
- [5C / 5D 开发记录](../开发记录/2026-08-21-1349-GAS完成5C装备事务与5D收尾.md)
- [ShootGame 代码规范](../代码规范.md)

当前架构事实基线：

- 2A：`0c36702 Inventory：建立 WeaponInstance FastArray 数据模型`
- 2B：`d077118 Inventory：接入武器拾取与 WeaponActor 绑定`
- 2C：`ddb89b2 Inventory：建立当前武器与网络切换闭环`
- 2D：`14f5de1 Inventory：完成 Ammo 权威迁移与 Death Clear`
- 4A：`44d28cc GAS：建立开火 Ability 输入与授予生命周期`
- 4B：`15fb5b0 GAS：将玩家开火迁移为 ServerOnly Ability`
- 4C：`4cb9003 GAS：补齐开火取消边界与 NPC Ability 链路`
- 5A：`d4135f6 GAS：建立换弹装备 Ability 与弹药事务基础`
- 5B：`b2a2f8c GAS：实现服务器权威换弹事务`
- Reload 弱网修复：`eccc65c 修复：避免弱网下换弹输入被残留标签吞掉`
- Fire 弱网修复：`df35777 修复：Fire ServerOnly 客户端预检与换弹后单发弱网验证`
- 5C / 5D：`766198d GAS：完成5C装备事务与5D收尾`
- Inventory 完整七阶段回归：Passed，Summary 见 `Saved/Automation/Runs/20260819_175505/Summary.json`。
- GA_Fire 阶段完整回归：由 4D 开发记录保存最终 `Summary.json` 路径。
- GA_Reload / GA_Equip 阶段完整回归：Passed，Summary 见 `Saved/Automation/Runs/20260821_134252/Summary.json`。
