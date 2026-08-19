# 第二阶段：Inventory 基础闭环执行计划

## 1. 阶段定位

本阶段紧接 GAS 第一阶段之后。

目标不是“做完整背包系统”，而是建立一个稳定、服务器权威、可复制、可测试的武器持有数据层，为后续：

```text
GA_Fire
GA_Reload
GA_Equip
WeaponActor Pool
背包 UI
Prediction
```

提供统一数据源。

本阶段只回答四个问题：

```text
1. 玩家拥有哪些武器？
2. 每一把具体武器的运行时数据放在哪里？
3. 当前逻辑装备的是哪一把？
4. 远端玩家如何知道这个角色当前手里拿着哪把 WeaponActor？
```

阶段目标闭环：

```text
Pickup
→ Server TryAddWeapon
→ WeaponInstanceData 加入 Inventory
→ Owner 收到 FastArray
→ ActiveWeaponInstanceId 更新
→ Character.CurrentWeaponActor 正确同步
→ 玩家可以切换已拥有武器
```

本阶段不进入 GAS Fire，不做 Reload，不做 Prediction。

---

## 2. 当前基线与迁移策略

当前旧链大致仍是：

```text
ShooterCharacter
├─ OwnedWeapons
└─ CurrentWeapon

ShooterWeapon
├─ CurrentBullets
├─ Fire Rate
├─ Fire
└─ Projectile
```

地图已有 Blueprint Pickup / 武器拾取流程。

本阶段采用：

> 渐进式迁移，不一次推翻旧射击系统。

允许暂时保留：

```text
旧 Fire
旧 Projectile
旧射速逻辑
旧射击表现
```

本阶段只迁移：

```text
武器持有关系
武器实例身份
MagazineAmmo / ReserveAmmo 权威位置
当前逻辑选中武器
CurrentWeaponActor 公共同步
切换武器
Pickup 接入 Inventory
```

阶段末旧 Fire 仍必须工作。

---

## 3. 高层目标结构

```text
ShooterCharacter
├─ ShooterInventoryComponent
│  ├─ WeaponInstances (FastArray)
│  └─ ActiveWeaponInstanceId
│
└─ CurrentWeaponActor
```

每把武器实例：

```text
FShooterWeaponInstanceData
├─ InstanceId
├─ DefinitionId
├─ MagazineAmmo
├─ ReserveAmmo
└─ SlotIndex
```

WeaponActor：

```text
AShooterWeapon
├─ BoundInstanceId
├─ Mesh / Muzzle / Presentation
└─ 现有射击逻辑（本阶段暂时保留）
```

职责：

```text
WeaponInstanceData
= 逻辑数据 / 权威状态

WeaponActor
= 世界实体 / 表现实体

InstanceId
= 稳定逻辑身份

WeaponActor*
= 世界中的 Actor
```

---

## 4. 架构合同

### 4.1 Inventory 是武器持有的逻辑权威源

以后：

```text
玩家拥有什么武器
```

不能再以：

```text
Character.OwnedWeapons Actor Array
```

作为唯一事实来源。

应以：

```text
Inventory WeaponInstances
```

作为逻辑权威。

---

### 4.2 Ammo 只有一份权威数据

第一版：

```text
MagazineAmmo
ReserveAmmo
```

都属于：

```text
FShooterWeaponInstanceData
```

禁止长期存在两套互相同步的权威状态：

```text
WeaponInstance.MagazineAmmo
WeaponActor.CurrentBullets
```

如果为了兼容旧 Fire 暂时保留旧字段：

- 必须标记为兼容镜像；
- 开发记录中写明删除路径；
- 优先让旧 Fire 通过 Inventory 接口读写 Ammo。

---

### 4.3 CurrentWeaponActor 不等于逻辑身份

逻辑当前武器：

```text
ActiveWeaponInstanceId
```

公共世界表现：

```text
Character.CurrentWeaponActor
```

WeaponActor 当前绑定：

```text
BoundInstanceId
```

必须满足：

```text
ActiveWeaponInstanceId
→ 对应一条 WeaponInstanceData

CurrentWeaponActor
→ BoundInstanceId == ActiveWeaponInstanceId
```

不要把 `AShooterWeapon*` 当成永久武器身份。

---

### 4.4 完整 Inventory 只向 Owner 复制

完整 FastArray：

```text
OwnerOnly
```

远端客户端不需要知道：

```text
全部背包内容
全部 Ammo
全部 Slot
```

远端只需要：

```text
Character.CurrentWeaponActor
```

来表现当前持枪。

---

## 5. 推荐类与数据结构

### 5.1 UShooterInventoryComponent

类型：

```text
UActorComponent
```

宿主：

```text
AShooterCharacter
```

原因：

- 当前 Inventory 是一条命的武器；
- 玩家死亡后清空；
- 不跨 Pawn 保留；
- 比放 PlayerState 更符合当前规则。

职责：

```text
Add Weapon
Remove Weapon
Find Weapon Instance
Find Slot
Switch Active Weapon
Expose ActiveWeaponInstanceId
Ammo Query / Modify
FastArray Replication
```

不负责：

```text
如何开枪
如何生成 Projectile
如何播放 Montage
如何造成 Damage
```

---

### 5.2 FShooterWeaponInstanceData

第一版至少：

```cpp
FGuid InstanceId;
FPrimaryAssetId DefinitionId;
int32 MagazineAmmo;
int32 ReserveAmmo;
int32 SlotIndex;
```

禁止提前加入：

```text
Attachments
Skins
Durability
Rarity
SaveGame
Prediction bookkeeping
```

除非当前真实实现必须。

---

### 5.3 FastArray

建议：

```text
FShooterWeaponInstanceEntry
: FFastArraySerializerItem

FShooterWeaponInventoryList
: FFastArraySerializer
```

InventoryComponent 持有：

```text
FShooterWeaponInventoryList ReplicatedInventory
```

正确处理：

```text
MarkItemDirty
MarkArrayDirty
NetDeltaSerialize
OwnerOnly Replication
```

本阶段不做客户端预测。

---

## 6. WeaponDefinition

如果当前项目还没有正式 WeaponDefinition，本阶段只建立最小：

```text
UShooterWeaponDefinition
: UPrimaryDataAsset
```

第一版仅提供 Inventory / WeaponActor 创建所需静态配置，例如：

```text
WeaponActorClass
MagazineSize
InitialReserveAmmo
DisplayName（可选）
```

不要本阶段提前塞入：

```text
完整 FireBehavior
完整 Montage
完整 FX
完整 Sound
预测参数
AI 参数
```

如果当前已有稳定的 Weapon Class 配置路径，可以先桥接，不强制立刻把全部武器数据 DataAsset 化。

原则：

> 为 Inventory 当前需求而建 Definition，不为未来想象过度设计。

---

## 7. WeaponActor 本阶段职责

WeaponActor 暂时保留现有：

```text
StartFiring
Fire
FireProjectile
Fire FX
```

因为 GA_Fire 尚未迁移。

但建立明确绑定：

```text
BoundInstanceId
```

本阶段逐步去除：

```text
WeaponActor 自己作为 Ammo 权威源
```

目标：

```text
WeaponActor
→ 通过 BoundInstanceId
→ 找 Inventory
→ 找 WeaponInstanceData
```

如需要兼容旧 Fire，可以提供明确 Ammo 接口，但真实修改最终落在 Inventory / InstanceData。

---

## 8. Pickup 接入

不重写现有 Blueprint Pickup。

只迁移最终授予路径。

旧：

```text
Pickup
→ Character.AddWeaponClass
→ Character.OwnedWeapons
```

新：

```text
Pickup
→ Server Validate
→ Inventory.TryAddWeapon
→ 创建 WeaponInstanceData
→ Spawn / 绑定 WeaponActor
→ 必要时自动设为 Active
```

必须保证：

- Pickup 服务器权威；
- 客户端不能直接添加；
- 同一 Pickup 不重复授予；
- 多人同时 overlap 只能有一个合法结果；
- Slot 满时明确 Reject。

---

## 9. Slot 规则

第一版使用固定少量 Slot，具体数量可根据现有工程最小改动决定。

规则：

```text
SlotIndex 唯一
一个 Slot 最多一把武器
```

拾取：

```text
找到空 Slot
→ 放入

无空 Slot
→ Reject
```

第一版不做：

```text
自动丢弃旧武器
替换 UI
拖拽换位
```

---

## 10. Active Weapon / Switch

Inventory 持有：

```text
ActiveWeaponInstanceId
```

切换：

```text
Request Switch
↓
Server Validate
↓
目标 Instance 存在？
Slot 合法？
Character Alive？
↓
ActiveWeaponInstanceId 更新
↓
找到绑定 WeaponActor
↓
Character.CurrentWeaponActor 更新
↓
旧 WeaponActor Hide / Holster
↓
新 WeaponActor Show / Equip
```

第一版非当前武器直接 Hidden 即可。

暂不要求：

```text
BackSocket
HipSocket
复杂换枪动画
```

---

## 11. Character.CurrentWeaponActor

Character 保留：

```text
Replicated AShooterWeapon* CurrentWeaponActor
```

对所有观察者复制。

用途：

```text
Remote Client
→ 不需要 OwnerOnly Inventory
→ 直接读取 CurrentWeaponActor
→ 表现当前持枪
```

必须验证：

```text
Server
Owner Client
Remote Client
```

最终 CurrentWeaponActor 一致。

---

## 12. 玩家死亡 / 重生

当前规则：

```text
玩家死亡
→ 当前生命武器全部清空
```

本阶段：

```text
Death
↓
Stop Firing
↓
Destroy 所有 owned WeaponActor
↓
Inventory Clear
↓
ActiveWeaponInstanceId Invalid
↓
CurrentWeaponActor = nullptr
```

新 Pawn：

```text
Inventory Empty
```

本阶段先 Destroy Actor。

> 不提前实现 Actor Pool。

---

## 13. Ammo 迁移

### 13.1 MagazineAmmo

从：

```text
AShooterWeapon::CurrentBullets
```

迁移到：

```text
WeaponInstanceData.MagazineAmmo
```

旧 Fire 每次开枪最终通过 Inventory 消耗 Ammo。

---

### 13.2 ReserveAmmo

第一版明确：

```text
ReserveAmmo 属于具体 WeaponInstance
```

不是玩家共享 Ammo Pool。

例如：

```text
Rifle
Magazine = 24
Reserve = 90

Pistol
Magazine = 10
Reserve = 36
```

本阶段即使 Reload 尚未实现，也要把 ReserveAmmo 的权威位置确定。

---

### 13.3 初始化

创建 WeaponInstance 时：

```text
MagazineAmmo = Definition.MagazineSize
ReserveAmmo = Definition.InitialReserveAmmo
```

如果 Definition 尚未完整建立，可从旧 Weapon Class 配置桥接，但必须记录兼容路径。

---

## 14. 旧 Fire 兼容

阶段末当前 Fire 必须继续工作。

目标：

```text
Weapon::Fire
↓
BoundInstanceId
↓
Inventory
↓
Find Instance
↓
CanConsumeMagazineAmmo
↓
ConsumeMagazineAmmo
↓
继续现有 FireProjectile
```

本阶段不改变：

```text
Fire RPC
Fire Rate
Projectile Spawn
Damage
GAS Health
```

禁止同时把 Fire 改成 GameplayAbility。

---

## 15. 本阶段明确不做

严格禁止顺手实现：

```text
GA_Fire
GA_Reload
GA_Equip
LocalPredicted
GameplayCue 重构
Projectile Prediction
Actor Pool
Projectile Pool
正式 Backpack UI
拖拽背包
武器配件
丢枪
换枪动画精修
第三人称动画重构
Lobby
PvP Match Flow
PvE Match Flow
Procedural Map
```

发现未来需求：

> 记录 TODO，不提前实现。

---

## 16. 阶段拆分

拆成四个子阶段：

```text
2A：Inventory 数据模型 + FastArray
↓
2B：Pickup → Inventory + WeaponActor 绑定
↓
2C：Active Weapon / Switch / CurrentWeaponActor
↓
2D：Ammo 权威迁移 + Death Clear + 旧 Fire 兼容
```

每个子阶段独立：

- Implementation；
- Focused Tests；
- Build；
- Stage Gate；
- 开发记录；
- 提交。

完整七阶段 Full Regression：

> 每个子阶段最终最多一次。

不要把 RunAll 当普通 Debug Loop。

---

## 17. 测试策略

### Fast Loop

```text
Build
+
当前 Feature Tests
```

失败时优先只重跑相关测试。

### Stage Gate

只跑当前功能需要的网络场景：

```text
Listen
Dedicated
```

### Full Regression

子阶段稳定后才运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Tests\RunAll.ps1 -Port <unused>
```

读取：

```text
Saved/Automation/Runs/<timestamp>/Summary.json
```

---

## 18. 自主修复上限

同一个失败项：

```text
最多自主修复 3 轮
```

3 轮仍失败：

- 停止；
- 保留工作区；
- 输出失败测试；
- 输出最近日志；
- 给出根因假设；
- 说明已经尝试的修复；
- 请求用户判断。

禁止无限自修复。

---

## 19. 子阶段 2A：Inventory 数据模型 + FastArray

## 2A0：建立 Inventory 源码目录边界

必须创建：
Source/ShootGame/Inventory/
Source/ShootGame/Tests/Inventory/

Inventory 新代码不得放入：
Characters/
Weapons/
GameFramework/

例外：
WeaponDefinition 属于 Weapons/
Character 只做 Component 宿主及最薄的桥接

### 目标

只建立：

```text
InventoryComponent
WeaponInstanceData
FastArray
OwnerOnly Replication
ActiveWeaponInstanceId 数据字段
```

暂时不接 Pickup。

### 实施

新增：

```text
UShooterInventoryComponent
FShooterWeaponInstanceData
FastArray Serializer
```

Character 创建 InventoryComponent。

服务器可通过测试入口插入两把测试武器。

### Feature Tests

建议：

```text
Shooter.Inventory.AddWeapon
Shooter.Inventory.UniqueInstanceId
Shooter.Inventory.SlotUniqueness
Shooter.Inventory.OwnerReplication
Shooter.Inventory.RemoteHidden
```

验证：

- Item 数量正确；
- InstanceId 有效且唯一；
- Slot 唯一；
- Owner 收到；
- 非 Owner 不收到完整列表。

### 验收

```text
Server Inventory
=
Owner Inventory

Remote Client
无完整 Inventory 数据
```

建议提交：

```text
Inventory：建立 WeaponInstance FastArray 数据模型
```

---

## 20. 子阶段 2B：Pickup → Inventory + WeaponActor 绑定

### 目标

真实地图拾取进入新 Inventory。

```text
Touch Pickup
↓
Server
↓
Inventory.TryAddWeapon
↓
创建 WeaponInstance
↓
创建 WeaponActor
↓
WeaponActor.BoundInstanceId
```

### 实施要求

不重写 Blueprint Pickup。

防止：

```text
客户端直接授予
重复 overlap 多次添加
同一 Pickup 多人同时成功
Slot 满仍添加
```

### Feature Tests

建议：

```text
Shooter.Inventory.Pickup.ServerAuthority
Shooter.Inventory.Pickup.SingleGrant
Shooter.Inventory.Pickup.SlotFullReject
Shooter.Inventory.WeaponActorBinding
```

验证：

```text
InstanceId
==
WeaponActor.BoundInstanceId
```

### 验收

```text
走到 Pickup
→ 成功拾取
→ Inventory +1
→ WeaponActor 创建并绑定
```

建议提交：

```text
Inventory：接入武器拾取与 WeaponActor 绑定
```

---

## 21. 子阶段 2C：Active Weapon + Switch

### 目标

建立：

```text
ActiveWeaponInstanceId
CurrentWeaponActor
```

以及切换。

### 输入

优先复用现有切枪输入。

如果现有没有合适入口，可用：

```text
1 / 2 / 3
```

做最低限度验证。

鼠标滚轮可暂缓。

### 切换流程

```text
Client Request
↓
Server Validate
↓
目标 Instance 存在？
Slot 合法？
Character Alive？
↓
ActiveWeaponInstanceId 更新
↓
找到绑定 WeaponActor
↓
CurrentWeaponActor 更新
↓
旧枪 Hide
↓
新枪 Show
```

### Feature Tests

建议：

```text
Shooter.Inventory.Switch.Valid
Shooter.Inventory.Switch.InvalidInstance
Shooter.Inventory.Switch.OwnerCurrentWeapon
Shooter.Inventory.Switch.RemoteCurrentWeapon
```

### 验收

玩家拥有 Rifle + Pistol 时：

```text
Rifle → Pistol → Rifle
```

Server / Owner / Remote 最终一致。

建议提交：

```text
Inventory：建立当前武器与网络切换闭环
```

---

## 22. 子阶段 2D：Ammo 权威迁移 + Death Clear + 旧 Fire 兼容

### 目标

完成关键旧系统迁移：

```text
Ammo
```

从 WeaponActor 迁到 WeaponInstanceData。

### Fire 兼容流程

旧：

```text
Weapon
→ CurrentBullets--
```

目标：

```text
Weapon
↓
BoundInstanceId
↓
Inventory
↓
Find Instance
↓
CanConsumeMagazineAmmo
↓
ConsumeMagazineAmmo
↓
继续 FireProjectile
```

不改变：

```text
Fire RPC
Fire Rate
Projectile
Damage
GAS Health
```

### Death Clear

```text
StopFiring
↓
Destroy WeaponActors
↓
Inventory.Clear
↓
ActiveWeaponInstanceId Invalid
↓
CurrentWeaponActor = nullptr
```

重生：

```text
Inventory Empty
```

### Feature Tests

建议：

```text
Shooter.Inventory.AmmoInitialState
Shooter.Inventory.AmmoConsume
Shooter.Inventory.AmmoSingleAuthority
Shooter.Inventory.SwitchAmmoIsolation
Shooter.Inventory.DeathClear
Shooter.Inventory.RespawnEmpty
```

至少验证：

```text
Rifle Ammo 不影响 Pistol
切枪后各自 Ammo 保持
开枪只扣当前实例
死亡清空
重生为空
```

---

## 23. 网络场景验收

至少覆盖：

```text
Standalone
Listen
Dedicated
```

### 场景 A：单武器

```text
Pickup Rifle
→ Inventory 1
→ Rifle Active
→ Fire
→ Ammo 下降
```

### 场景 B：双武器

```text
Pickup Rifle
Pickup Pistol
→ Inventory 2
→ Switch
→ Remote 看到正确 WeaponActor
```

### 场景 C：Ammo 隔离

```text
Rifle Fire 5
→ Rifle -5

Switch Pistol
→ Pistol Ammo 不变
```

### 场景 D：死亡

```text
拥有两把枪
→ Die
→ Inventory = 0
→ CurrentWeaponActor = nullptr
→ Respawn
→ Inventory = 0
```

---

## 24. 弱网验收

本阶段不做预测。

EmulatedNetwork 只要求最终收敛。

验证：

- Owner Inventory 最终正确；
- Switch 最终一致；
- CurrentWeaponActor 最终一致；
- Ammo 最终一致；
- 无重复 Pickup；
- 无重复 WeaponActor。

---

## 25. 开发记录要求

每个子阶段记录：

- 修改的类；
- 数据权威位置；
- 网络复制策略；
- 旧链桥接；
- Feature Test；
- Focused Network 结果；
- Full Regression Summary 路径；
- 遇到的问题；
- 修复；
- 遗留兼容字段。

禁止只写：

```text
完成 Inventory
测试通过
```

---

## 26. 最终验收

只有同时满足以下条件才完成。

### Inventory

```text
WeaponInstance FastArray
OwnerOnly Replication
```

正确。

### 武器身份

```text
InstanceId
BoundInstanceId
```

关系正确。

### Pickup

当前 Blueprint Pickup：

```text
→ Server
→ Inventory
```

正确。

### Current Weapon

```text
ActiveWeaponInstanceId
```

是逻辑当前武器。

```text
Character.CurrentWeaponActor
```

是公共表现。

### Switch

两把以上武器可切换，Remote 表现正确。

### Ammo

```text
MagazineAmmo
ReserveAmmo
```

权威位于 WeaponInstanceData。

旧 Fire 能正常消耗当前实例 Ammo。

### Death

```text
Death
→ Inventory Clear
→ WeaponActors Destroy
→ Active Invalid
→ CurrentWeaponActor null
```

重生为空。

### Gameplay Regression

现有：

```text
Pickup
Fire
Projectile
Damage
GAS Health
Death
Score
Respawn
```

仍正常。

### Automation

Focused Tests 全部 Passed。

最终：

```text
Build
Automation
Standalone
DedicatedNetwork
ListenNetwork
EmulatedNetwork
DisconnectCleanup
```

全部 Passed。

必须检查：

```text
Saved/Automation/Runs/<timestamp>/Summary.json
```

---

## 27. 阶段结束后暂停

完成 2D 后：

> 不自动进入 GA_Fire。

人工复盘：

1. Inventory / WeaponInstance / WeaponActor 三者关系；
2. Ammo 是否只有一个权威源；
3. 旧 Fire 通过 Inventory 的桥接是否自然；
4. CurrentWeaponActor 远端表现是否稳定；
5. FastArray OwnerOnly 是否符合预期；
6. 是否发现真实实现与原架构假设冲突。

之后再决定：

```text
A. WeaponActor 生命周期 / Actor Pool
```

或：

```text
B. GA_Fire ServerOnly
```

如果当前 Spawn/Destroy 稳定：

> 优先 GA_Fire ServerOnly。

如果 WeaponActor 生命周期已经明显混乱：

> 先整理生命周期 / Pool。
