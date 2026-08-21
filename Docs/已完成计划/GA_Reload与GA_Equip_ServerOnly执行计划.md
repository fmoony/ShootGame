# 第五阶段：GA_Reload 与 GA_Equip ServerOnly 执行计划（已完成）

## 1. 阶段定位

本阶段紧接已完成的 Inventory 基础闭环与 GA_Fire ServerOnly。

目标是把玩家的换弹和切枪收束为两条服务器权威 Ability 事务：

~~~text
IA_Reload
→ ASC Input.Reload
→ GA_Reload（ServerOnly）
→ Inventory 原子转移 ReserveAmmo → MagazineAmmo

IA_SwitchWeapon
→ ASC Input.Equip.Next
→ GA_Equip（ServerOnly）
→ Inventory.ActiveWeaponInstanceId
→ Character.CurrentWeapon
~~~

本阶段先建立 P0 权威基线，不做本地预测。完成后再用真实弱网结果决定是否进入 P1 射击表现预测。

---

## 2. 设计依据与项目差异

### 2.1 官方参考

- [Lyra Inventory and Equipment](https://dev.epicgames.com/documentation/en-us/unreal-engine/lyra-inventory-and-equipment-in-unreal-engine) 将“拥有的物品状态”和“当前装备的世界表现”分开；
- [Abilities in Lyra](https://dev.epicgames.com/documentation/en-us/unreal-engine/abilities-in-lyra-in-unreal-engine) 展示了通过输入 Tag 激活 Ability，以及装备提供 Fire/Reload 等 Ability 的方向；
- [Understanding the Gameplay Ability System](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-the-unreal-engine-gameplay-ability-system) 将 Ability 定位为可异步、可取消并受 GameplayTag 约束的 Gameplay 事务；
- [Using Gameplay Abilities](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-gameplay-abilities-in-unreal-engine) 说明 Ability 的授予、激活、结束与网络执行策略；
- [UAbilityTask_PlayMontageAndWait](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/GameplayAbilities/UAbilityTask_PlayMontageAndWait) 提供完成、混出、打断和取消回调。

### 2.2 本地 UE 5.6 核对

已核对安装版本：

~~~text
E:/Unreal_Engine/UE_5.6/Engine/Plugins/Runtime/GameplayAbilities/
Source/GameplayAbilities/Public/AbilitySystemComponent.h
Source/GameplayAbilities/Public/Abilities/Tasks/AbilityTask_WaitDelay.h
Source/GameplayAbilities/Public/Abilities/Tasks/AbilityTask_PlayMontageAndWait.h
Source/GameplayAbilities/Public/Abilities/Tasks/AbilityTask_WaitGameplayEvent.h
~~~

确认：

- GiveAbility 在非权威 Actor 上会被忽略；
- WaitDelay 可作为 Ability 内的服务器事务时钟；
- PlayMontageAndWait 明确区分 Completed、Interrupted 与 Cancelled；
- Ability 或 Task 被显式取消时，Montage 会停止；
- WaitGameplayEvent 支持精确 Tag、单次触发和外部目标。

### 2.3 ShootGame 不照搬 Lyra

当前项目已拥有：

- Character 上的 OwnerOnly FastArray Inventory；
- ActiveWeaponInstanceId 作为逻辑当前武器；
- CurrentWeapon 作为公开复制的当前 WeaponActor；
- 每个已拥有 WeaponInstance 对应一个已生成、非当前时隐藏的 WeaponActor；
- PlayerState 持有跨重生存在的 ASC；
- Ammo 权威位于 Inventory，不是 GAS Attribute。

因此本阶段明确不引入：

~~~text
Lyra EquipmentManager
EquipmentDefinition / AbilitySet
WeaponDefinition 全面重构
切枪时 Spawn / Destroy WeaponActor
Ammo GameplayAttribute
~~~

只借用“Inventory 保存拥有状态、装备层负责当前表现、Ability 负责事务”的边界思想。

---

## 3. 当前基线与真实缺口

已完成：

- GA_Fire 是玩家与 NPC 的唯一开火事务入口；
- Fire Ability 为 ServerOnly；
- 玩家 Ability 输入已进入自定义 ASC；
- 玩家重生不会重复授予 Fire Ability；
- MagazineAmmo / ReserveAmmo 已按 WeaponInstance 保存；
- 开火只消费当前实例的 MagazineAmmo；
- 切枪仍能改变 ActiveWeaponInstanceId 与 CurrentWeapon；
- 完整七阶段回归通过：

~~~text
Saved/Automation/Runs/20260820_080029/Summary.json
~~~

尚缺：

- 没有 Input.Reload 或 Reload InputAction 绑定；
- Inventory 没有 Reserve → Magazine 的原子事务 API；
- 没有 GA_Reload；
- 切枪仍通过 ServerSwitchWeapon，没有 GA_Equip；
- Fire、Reload、Equip 之间尚未形成 GameplayTag 互斥合同；
- WeaponActor 没有独立的 Reload/Equip 权威持续时间配置；
- 尚未验证 Reload/Equip 在死亡、切枪、重生和断线时的取消语义。

---

## 4. 本阶段明确不做

~~~text
Local Predicted Reload / Equip
预测弹药与客户端先行 CurrentWeapon
GameplayCue 全面接入
WeaponDefinition 正式化
EquipmentManager / AbilitySet
WeaponActor / Projectile Pool
NPC Inventory
新的动画资产制作
背包 UI 重做
Lobby / Match / Procedural Map
~~~

已有动画只能作为候选表现资源，不得反过来决定服务器事务是否成功。

---

## 5. 架构合同

### 5.1 Ammo 只由 Inventory 事务修改

新增最小权威接口，建议语义：

~~~cpp
bool ReloadMagazine(
    const FGuid& InstanceId,
    int32& OutTransferredAmmo);
~~~

事务规则：

~~~text
Need = MagazineCapacity - MagazineAmmo
Transfer = Min(Need, ReserveAmmo)

Transfer > 0:
MagazineAmmo += Transfer
ReserveAmmo -= Transfer
~~~

必须一次性校验 InstanceId、计算 Transfer、同时修改两类 Ammo、标记对应 FastArray Item Dirty，并刷新绑定 WeaponActor 镜像与 Owner HUD。

禁止 Ability 分别调用“减 Reserve”和“加 Magazine”形成中间非法状态。

### 5.2 Ability 是事务，不是第二份状态

GA_Reload 不保存长期 Ammo；GA_Equip 不保存长期当前武器。

~~~text
Inventory = 唯一长期逻辑状态
Ability = 激活、等待、提交、取消
WeaponActor = 世界表现与现有武器执行入口
~~~

### 5.3 权威时钟不依赖动画

服务器事务以 WeaponActor 上可配置的 ReloadDuration、EquipDuration 为准，并通过 UAbilityTask_WaitDelay 等待。

Montage 只负责表现：

- 有兼容 Montage 时可以播放；
- Dedicated Server 无表现资源时事务仍能完成；
- Montage 丢失或第一/第三人称资源不匹配时不能改变权威 Ammo 与 ActiveWeapon；
- 后续如使用 AnimNotify / GameplayEvent，只能在服务器设置明确超时和失败回退。

当前文件名审计发现候选：

~~~text
Content/Characters/Mannequins/Anims/Pistol/MM_Pistol_Reload.uasset
Content/Characters/Mannequins/Anims/Pistol/MM_Pistol_Equip.uasset
Content/Characters/Mannequins/Anims/Rifle/MM_Rifle_Reload.uasset
Content/Characters/Mannequins/Anims/Rifle/MM_Rifle_Equip.uasset
~~~

实施前必须用编辑器只读检查确认 Skeleton、Slot、Montage 类型与第一/第三人称适用范围；仅凭文件名不得直接接入。

### 5.4 明确提交点

GA_Reload：

~~~text
Activate
→ 校验当前武器与 Ammo
→ 添加 State.Reloading
→ 等待 ReloadDuration
→ 再次校验仍是同一 ActiveWeaponInstanceId
→ ReloadMagazine 原子提交
→ EndAbility
~~~

提交前被死亡、切枪、断线或外部取消时，Ammo 不变。

GA_Equip：

~~~text
Activate
→ 服务器计算下一个合法 InstanceId
→ 取消 Fire / Reload
→ 添加 State.Equipping
→ 等待 EquipDuration
→ 再次校验目标仍存在
→ 原子提交 ActiveWeaponInstanceId + CurrentWeapon
→ EndAbility
~~~

取消发生在提交前时保持旧武器，不出现逻辑 ID 与 Actor 指针分离。

### 5.5 GameplayTag 合同

至少新增：

~~~text
Input.Reload
Input.Equip.Next
State.Reloading
State.Equipping
~~~

互斥：

~~~text
State.Dead       阻止 Fire / Reload / Equip
State.Reloading  阻止 Fire，并被 Equip / Death 取消
State.Equipping  阻止 Fire / Reload
State.Firing     在 Equip 前必须取消；Reload 激活前必须结束
~~~

同一时刻只允许一个改变当前武器或弹药的事务处于提交窗口。

### 5.6 Ability 授予生命周期

GA_Reload 与 GA_Equip 跟随 PlayerState ASC：

- 只由服务器授予；
- 每个 PlayerState 各一个 Spec；
- 重生只更新 Avatar，不重复授予；
- 新 Character 使用原 ASC，但 Inventory 是新 Character 的当局状态；
- Ability 激活时重新取得当前 Avatar 和 Inventory，禁止缓存上一生命的 Character。

### 5.7 NPC 边界

NPC 已通过 GAS 使用 GA_Fire，但没有玩家式 Inventory 和 ReserveAmmo。

本阶段：

- 不给 NPC 虚构一套玩家 Inventory；
- 不授予 NPC 的 GA_Reload / GA_Equip；
- NPC 继续使用单武器和现有兼容 Ammo；
- 未来只有在 NPC 确实需要多武器、备弹与掉落状态时，才设计 NPC Inventory。

这不是放弃“NPC 接入 GAS”，而是避免在缺少真实状态模型时制造空壳 Ability。

---

## 6. 子阶段与提交边界

~~~text
5A：Inventory Reload 原子事务 + Ability 输入/授予壳体
↓
5B：GA_Reload ServerOnly 完整闭环
↓
5C：GA_Equip ServerOnly 替换旧切枪 RPC
↓
5D：动画表现接点、旧入口清理与完整回归
~~~

每个子阶段必须独立编写提交记录、Build、运行新增 Feature Tests 和相关网络验证，通过后再提交；不得夹带资产迁移或用户的无关修改。

用户已明确指定：本计划建立后预留的 `AGENTS.md` 已完成计划导航精简，随 5A 提交进入，不单独创建文档提交。这是 5A 唯一允许附带的既有文档改动，开发记录必须单独列明。

---

## 7. 5A：数据事务与 Ability 基础设施

### 实施

- 为 Inventory List/Component 增加服务器专用 ReloadMagazine；
- 为 WeaponActor 增加最小 ReloadDuration、EquipDuration 配置；
- 新增 Input.Reload、Input.Equip.Next、State.Reloading、State.Equipping；
- 新增 UShooterGameplayAbility_Reload 与 UShooterGameplayAbility_Equip 壳体；
- 服务器幂等授予两个 Ability；
- 添加 IA_Reload；Switch 输入暂时保持旧路径；
- 将已预留的 `AGENTS.md` 导航精简纳入本提交，并在 5A 开发记录中说明；
- 本提交不改变实际 Reload/Equip 行为。

### Feature Tests

~~~text
ShootGame.Inventory.Reload.Transfer
ShootGame.Inventory.Reload.ClampCapacity
ShootGame.Inventory.Reload.NoReserve
ShootGame.Inventory.Reload.FullMagazine
ShootGame.Inventory.Reload.InstanceIsolation
ShootGame.Ability.ReloadEquip.Grant.Player
ShootGame.Ability.ReloadEquip.Grant.RespawnNoDuplicate
~~~

建议提交：

~~~text
GAS：建立换弹装备 Ability 与弹药事务基础
~~~

---

## 8. 5B：GA_Reload ServerOnly

### 实施

- IA_Reload 通过 ASC 提交 Input.Reload；
- Ability 激活时记录目标 WeaponInstanceId，仅作为本次事务快照；
- 校验未死亡、当前武器有效、弹匣未满、ReserveAmmo 大于 0；
- 结束 Fire，添加 State.Reloading；
- 等待服务器 ReloadDuration；
- 提交前二次校验目标仍为当前武器；
- 调用 Inventory 原子 Reload；
- 完成、取消和失败路径统一清理 Tag/Task；
- 暂不依赖 Montage 决定事务结果。

### Feature Tests

~~~text
ShootGame.Ability.Reload.ServerOnly
ShootGame.Ability.Reload.TransferOnce
ShootGame.Ability.Reload.Reject.FullMagazine
ShootGame.Ability.Reload.Reject.NoReserve
ShootGame.Ability.Reload.Cancel.Death
ShootGame.Ability.Reload.Cancel.Equip
ShootGame.Ability.Reload.Cancel.Disconnect
ShootGame.Ability.Reload.RespawnNoCarry
~~~

### 验收

- 客户端不能直接修改 Ammo；
- 一次 Ability 最多提交一次弹药转移；
- 取消发生在提交前时 Ammo 不变；
- OwnerOnly Inventory 和 HUD 最终一致；
- 观察者不获得完整 ReserveAmmo。

建议提交：

~~~text
GAS：实现服务器权威换弹事务
~~~

---

## 9. 5C：GA_Equip ServerOnly

### 实施

- IA_SwitchWeapon 改为 ASC 提交 Input.Equip.Next；
- 服务器根据 Inventory Slot 顺序计算下一个合法实例；
- 激活时先取消 Fire 与 Reload；
- 等待服务器 EquipDuration；
- 提交前确认目标实例和 WeaponActor 仍有效；
- 统一提交 ActiveWeaponInstanceId、Weapon 可见性与 Character.CurrentWeapon；
- 旧 ServerSwitchWeapon 暂时保留但不得再有输入调用者；
- 切换提交后不会自动恢复之前按住的 Fire。

### Feature Tests

~~~text
ShootGame.Ability.Equip.ServerOnly
ShootGame.Ability.Equip.NextSlot
ShootGame.Ability.Equip.CancelFire
ShootGame.Ability.Equip.CancelReload
ShootGame.Ability.Equip.Reject.SingleWeapon
ShootGame.Ability.Equip.Reject.Dead
ShootGame.Ability.Equip.CurrentWeaponReplication
ShootGame.Ability.Equip.InstanceActorConsistency
~~~

### 验收

- 一次输入只产生一次权威切换；
- 远端只看到确认后的 CurrentWeapon；
- 切换期间不允许旧武器继续开火；
- 逻辑 InstanceId 与公开 WeaponActor 始终对应；
- 死亡、清空背包和断线不会遗留 Equip 事务。

建议提交：

~~~text
GAS：将切枪迁移为服务器权威装备 Ability
~~~

---

## 10. 5D：表现接点与阶段收尾

### 实施

- 用编辑器只读检查候选 Reload/Equip Montage；
- 只接入已证明 Skeleton/Slot/视角兼容的表现资源；
- 第一人称表现仅本地可见，第三人称表现供服务器和观察者收敛；
- 表现失败不得阻止服务器事务；
- 使用 CodeGraph 确认并删除无调用者的 ServerSwitchWeapon；
- 更新自动化协调器、架构文档和网络链路说明；
- 运行完整七阶段回归。

如现有 Montage 不满足第一人称：

- 功能阶段仍可完成；
- 记录为 Demo Polish 遗留项；
- 不在本阶段制作或迁移新动画资产。

建议提交：

~~~text
清理：移除旧切枪 RPC 并完成 Reload Equip 回归
~~~

---

## 11. 网络场景验收

### Reload

~~~text
Rifle Magazine 5/30, Reserve 20
→ Reload
→ Server Wait
→ Magazine 25, Reserve 0

Pistol Magazine 4/12, Reserve 20
→ Reload
→ Magazine 12, Reserve 12

Reload Waiting
→ Die / Equip / Disconnect
→ Ability Cancel
→ Ammo Unchanged
~~~

### Equip

~~~text
Rifle Firing
→ Switch
→ GA_Fire Cancel
→ GA_Equip Wait
→ Pistol Becomes Active
→ Rifle Timer Cleared

Equip Waiting
→ Death / Inventory Clear
→ Ability Cancel
→ No Dangling CurrentWeapon
~~~

---

## 12. 自动化与完整回归

必须保留并扩展：

~~~text
Build
Automation
Standalone
DedicatedNetwork
ListenNetwork
EmulatedNetwork
DisconnectCleanup
~~~

重点断言：

- Dedicated 与 Listen 的 Reload/Equip 结果一致；
- 弱网下不会重复转移 Ammo 或重复切枪；
- Fire、Reload、Equip 不会同时保持活动；
- OwnerOnly Ammo 与公开 CurrentWeapon 的可见范围不倒退；
- 死亡、重生和断线后没有活动 Task、Timer 或状态 Tag 残留；
- GA_Fire、Projectile、Damage、Score 与 NPC 回归保持通过。

最终必须记录：

~~~text
Saved/Automation/Runs/<timestamp>/Summary.json
~~~

---

## 13. 阶段完成定义

只有同时满足以下条件才算完成：

- Reload 与 Equip Ability 只由服务器授予且重生不重复；
- IA_Reload、IA_SwitchWeapon 都通过 ASC；
- Reload 只有一条 Inventory 原子事务入口；
- Equip 只有一条 ActiveWeapon/CurrentWeapon 提交入口；
- ServerSwitchWeapon 已无调用者并删除；
- Fire、Reload、Equip、Dead 的 Tag 互斥与取消行为有自动化证据；
- 取消 Reload 不扣弹，重复网络消息不重复提交；
- 逻辑 WeaponInstance 与公开 WeaponActor 始终一致；
- 完整七阶段回归 Passed；
- 每个提交都附带同提交开发记录。

---

## 14. 阶段结束后暂停

完成后不自动进入预测，先复盘：

1. ServerOnly Reload/Equip 的延迟是否影响可玩性；
2. 现有 Montage 是否足够支撑第一/第三人称表现；
3. Inventory 与 WeaponActor 的双层身份是否仍稳定；
4. 是否已经出现必须引入 WeaponDefinition 的第二个真实需求；
5. P1 应只预测枪口/音效/后坐力，还是也需要预测 Ammo UI；
6. 高丢包下 Fire、Reload、Equip 的取消和最终收敛数据。

默认后续候选仍是 P1 Local Predicted 基础射击反馈，但只有复盘与新的详细计划完成后才能实施。

---

## 15. 完成确认（2026-08-21）

第 13 节的完成条件已全部满足：

- 5A `d4135f6`：Inventory Reload 原子事务、Tag 合同、两个 Ability 壳体与服务器幂等授予；
- 5B `b2a2f8c`：GA_Reload ServerOnly 完整闭环；
- 弱网修复 `eccc65c`：换弹单次输入不被残留 `State.Reloading` 吞掉；
- Fire 弱网修复 `df35777`：Fire 客户端预检同规则 + 换弹后单发弱网验证；
- 5C / 5D `766198d`：GA_Equip ServerOnly 替换旧切枪 RPC、`CommitActiveWeapon` 原子提交、删除 `ServerSwitchWeapon`、Montage 只读检查与文档更新；
- 5D 收尾补充（本次提交）：`Die()` / `EndPlay()` 显式取消 GA_Equip，重生防御性清理 `State.Equipping`，Disconnect / Respawn 自动化断言同时检查活动 GA_Equip 与 `State.Equipping` 残留；
- 完整七阶段回归 Passed（含本次收尾改动）：`Saved/Automation/Runs/20260821_140505/Summary.json`；
- Montage 候选为 `AnimSequence`（非 `UAnimMontage`），按本计划第 10 节作为 Demo Polish 遗留项，不在本阶段接入。

按第 14 节要求：阶段结束后暂停，不自动进入预测；默认后续候选 P1 Local Predicted 基础射击反馈，只有复盘与新的详细计划完成后才实施。

---

## 16. 严格验收补充（2026-08-21）

本次收尾补齐了首次完成确认中仍偏弱的自动化证据：

- `Equip.NextSlot` 行为测试改为直接调用生产代码 `FShooterWeaponInventoryList::FindNextItemId`，不再在测试中复制一份同构算法；
- 断线脚本支持等待服务端就绪标记，确认 `GA_Reload` 已处于活动状态且持有 `State.Reloading` 后才断开客户端，避免固定延时使 Ability 自然结束后产生假阳性；
- 新增活动中 `GA_Equip` 的死亡清理验证：死亡前确认 Ability 与 `State.Equipping` 均处于活动状态，死亡后确认 Ability、状态 Tag、Inventory、ActiveWeapon 和 CurrentWeapon 全部清理；
- 新增活动中 `GA_Equip` 的断线清理验证：断线前确认 Ability 与状态 Tag 已建立，断线后确认活动 Ability、Tag、公开武器 Actor 与孤儿 Actor 均无残留；
- `DisconnectCleanup` 仍作为七阶段中的一个逻辑阶段，但内部依次运行 Reload 断线与 Equip 死亡/断线两场独立会话。

最终验证证据：

- 自动化测试：`ShootGame` 共 50 项，50 Passed、0 Warning、0 Failed、0 NotRun；
- Reload 清理日志：`Saved/Automation/Network/20260821_143742/Server.log`；
- Equip 清理日志：`Saved/Automation/Network/20260821_143805/Server.log`；
- 完整七阶段回归：`Saved/Automation/Runs/20260821_143516/Summary.json`，状态 `Passed`。

至此，GA_Reload 与 GA_Equip ServerOnly 计划的实现、异常结束清理和自动化证据均已完整收尾。历史上 5C / 5D 合并提交的流程偏差按项目约定不重写历史。
