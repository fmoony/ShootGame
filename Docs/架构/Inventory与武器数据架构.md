# Inventory 与武器数据架构

## 1. 文档定位

本文档记录 Inventory / WeaponInstance / WeaponActor 领域的长期职责边界，以及当前已经落地的网络数据约束。

文档使用三种状态区分事实：

- **已落地事实**：当前 `0c36702` 提交中真实存在并已通过验证的行为。
- **当前架构方向**：项目已同意的设计目标，但允许尚未实现。
- **后续尚未实现**：属于 Inventory 第二阶段 2B / 2C / 2D 等后续工作，当前不存在对应代码路径。

本文档不替代执行计划。逐阶段实施与验收仍以 [Inventory 第二阶段基础闭环执行计划](../执行计划/Inventory第二阶段_基础闭环执行计划.md) 为准。

---

## 2. 当前 Inventory 数据模型（2A / 2B / 2C 已落地）

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

- Pickup 最终授予路径已迁移到 `UShooterInventoryComponent::TryAddWeapon`。
- 服务器创建 WeaponInstance 后同步创建并绑定 WeaponActor。
- `AShooterWeapon::BoundInstanceId` 已建立，OwnerOnly 复制。
- 重复 WeaponClass 授予与 Slot 满均明确 Reject。

2C 已落地：

- 切换请求由服务器基于 `ActiveWeaponInstanceId` 与 Slot 顺序选择目标。
- `Character.CurrentWeapon` 作为公共 `CurrentWeaponActor` 表现，复制给所有观察者。
- 无效 InstanceId 不会改写 Active 身份。
- Owner 客户端可观察到 Active 与当前 WeaponActor 的 BoundInstanceId 一致。

当前 `FShooterWeaponInstanceData`：

```text
FShooterWeaponInstanceData
├─ FGuid InstanceId
├─ FPrimaryAssetId DefinitionId
├─ int32 MagazineAmmo
├─ int32 ReserveAmmo
└─ int32 SlotIndex
```

职责定义：

- `Inventory`：当前生命中的武器逻辑数据权威源。
- `WeaponInstanceData`：单把武器的运行时逻辑状态。
- `InstanceId`：稳定逻辑身份，服务器生成且不可变。
- `DefinitionId`：指向静态 WeaponDefinition 的资产身份。
- `ActiveWeaponInstanceId`：当前逻辑武器身份。

当前尚未实现：

- Ammo 实际消费迁移。
- Death Clear。
- GA_Fire。

上述内容在 2D 落地前，不得在代码或文档中当作“已经存在”。

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
- 自定义的是 Item Payload 序列化，不是重新实现 FastArray。

当前自定义边界：

- `FShooterWeaponInventoryList::NetDeltaSerialize` 继续交给 UE 原生 `FastArrayDeltaSerialize`。
- `FShooterWeaponInstanceEntry::NetSerialize` 只负责单个 Item 的 Payload 序列化。

---

## 5. FPrimaryAssetId 特殊约束

UE5.6 当前使用的 `FPrimaryAssetId` 不是 UHT `USTRUCT` 反射类型，因此：

```text
FShooterWeaponInstanceData::DefinitionId
```

不能声明为 `UPROPERTY`。

虽然 `FPrimaryAssetId` 支持普通 `FArchive <<` 序列化，但必须准确区分：

```text
Archive Serialization
≠
UHT Reflection
≠
UPROPERTY
≠
默认属性网络复制
```

准确表述：

> `FPrimaryAssetId` 不是当前 UHT 属性系统可直接声明为 UPROPERTY 的反射类型，因此不能依赖标准反射属性路径自动复制该字段。

不要使用“FPrimaryAssetId 网络不支持”这类模糊说法。

---

## 6. 为什么存在自定义 NetSerialize

当前 `FShooterWeaponInstanceEntry::NetSerialize` 显式序列化：

```text
InstanceId
DefinitionId
MagazineAmmo
ReserveAmmo
SlotIndex
```

原因：

- `DefinitionId` 不在 UPROPERTY 反射布局中。
- 如果直接删除自定义 NetSerialize，默认反射序列化看不到 `DefinitionId`。

因此：

```text
WithNetSerializer = true
```

是当前正确性的必要组成部分。

---

## 7. 为什么关闭 Struct Delta

当前 `FShooterWeaponInventoryList` 调用：

```text
SetDeltaSerializationEnabled(false)
```

这并不是关闭整个 FastArray Delta Replication。

准确含义：

- FastArray 仍然按 Item 的 Add / Change / Remove 工作。
- 关闭的是 Item 内部 Struct Delta 路径。

原因：

- Struct Delta 的字段差异检测依赖反射属性。
- `DefinitionId` 不属于反射字段。
- 如果只改变 `DefinitionId`，而其他 UPROPERTY 没有变化，Struct Delta 可能无法可靠识别该变化。

因此当前强制 Item 走完整自定义 NetSerialize Payload。

错误表述需要避免：

> “关闭 FastArray Delta”

这是不准确的。正确表述是：

> “保留 FastArray Item Delta，关闭 Item 内部 Struct Delta。”

---

## 8. 最重要的维护约束

> **IMPORTANT**
>
> `FShooterWeaponInstanceEntry` 当前维护完整 WeaponInstance 网络 Payload。
>
> 任何新增到 `FShooterWeaponInstanceData` 且需要复制给 Owner Client 的字段，都必须：
>
> 1. 更新 `FShooterWeaponInstanceData`；
> 2. 同步更新 `FShooterWeaponInstanceEntry::NetSerialize`；
> 3. 同步更新对应网络复制测试。
>
> 新增字段不能仅仅写入 `UPROPERTY` 后就认为已经进入当前网络协议。

失败模式：

> 如果只加 UPROPERTY 而不更新 NetSerialize，可能出现“服务器存在该字段、Owner Client 静默缺失”的问题。

---

## 9. 为什么当前不改成自定义 DefinitionId USTRUCT

理论上可以建立反射兼容的：

- `FShooterWeaponDefinitionId`
- 或使用两个 `FName`：`DefinitionType` + `DefinitionName`

从而恢复完全依赖默认反射序列化。但当前选择继续保留：

```text
FPrimaryAssetId DefinitionId
```

理由：

- 直接表达 UE Primary Asset 身份。
- 与 AssetManager 语义一致。
- 不需要建立额外转换类型。
- 当前特殊处理只限制在 Replication Boundary。
- 已通过 Dedicated / Listen 网络测试。

原则：

> 优先使用 UE 原生数据语义；原生机制存在局部连接缺口时，在边界做最小定制；不要仅为了消除少量自定义序列化代码而扭曲上层 Gameplay 数据模型。

如果未来反射、Blueprint、SaveGame、通用 Property 工具等大量系统都需要直接访问 `DefinitionId`，可以重新评估项目级可反射包装类型。

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

> 2B 已建立 `WeaponActor.BoundInstanceId` 绑定，2C 已建立 Active 驱动切换与公共 CurrentWeapon 表现；Ammo 权威迁移与死亡清空仍属于 2D。

---

## 相关文档与基线

- [Inventory 第二阶段基础闭环执行计划](../执行计划/Inventory第二阶段_基础闭环执行计划.md)
- [2A 开发记录](../开发记录/2026-08-19-1459-Inventory建立WeaponInstanceFastArray数据模型.md)
- [ShootGame 代码规范](../代码规范.md)

当前架构事实基线：

- 提交：`0c36702 Inventory：建立 WeaponInstance FastArray 数据模型`
- Dedicated 双客户端与 Listen 单客户端网络验证均已通过，具体日志见 2A 开发记录。
