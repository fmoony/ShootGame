# 第四阶段：GA_Fire ServerOnly 执行计划（已完成）

## 1. 阶段定位

本阶段紧接已完成的 GAS 基础生命闭环与 Inventory 第二阶段。

目标不是立刻实现客户端预测，也不是重写完整武器系统，而是把当前：

```text
Enhanced Input
→ Character ServerStartFire / ServerStopFire
→ WeaponActor
```

迁移为：

```text
Enhanced Input
→ ASC 输入
→ GA_Fire（ServerOnly）
→ WeaponActor
```

阶段结束时，`GA_Fire` 必须成为玩家和 NPC 发起开火事务的唯一 Gameplay 入口；已有 WeaponActor、Inventory Ammo、服务器弹丸和 GAS Health 链路继续复用。

本阶段只建立 P0 服务器权威 Ability 基线，为后续 `GA_Reload`、`GA_Equip` 和 P1 Local Predicted 提供可比较、可自动验证的起点。

---

## 2. 当前已验证基线

当前已完成：

- 玩家 ASC 位于 `AShooterPlayerState`，使用 Mixed 复制模式；
- 玩家 Character 是可替换 Avatar，重生后重新执行 `InitAbilityActorInfo`；
- NPC 自身持有 ASC 与 AttributeSet，使用 Minimal 复制模式；
- Health / MaxHealth 与伤害已迁入 GAS；
- Inventory 使用 OwnerOnly FastArray 保存 WeaponInstance；
- `ActiveWeaponInstanceId` 是逻辑当前武器；
- `Character.CurrentWeapon` 是公开复制的 CurrentWeaponActor；
- MagazineAmmo / ReserveAmmo 权威位于 WeaponInstanceData；
- 玩家死亡会清空 Inventory 并销毁绑定 WeaponActor；
- 旧 `Weapon::Fire` 已通过 Inventory 消耗弹药；
- NPC 没有 Inventory 时继续使用 `CurrentBullets` 兼容路径；
- Dedicated、Listen、弱网和断线清理已纳入现有七阶段回归。

进入本阶段前最近一次完整回归基线：

```text
Saved/Automation/Runs/20260819_175505/Summary.json
```

开始实施前仍需重新执行 Preflight，确认该基线没有被后续资产或源码改动破坏。

---

## 3. 阶段目标

本阶段只回答以下问题：

```text
1. Fire Ability 何时、由谁授予？
2. 本地按下与松开如何进入 ASC？
3. ServerOnly Ability 如何驱动现有 WeaponActor？
4. 玩家死亡、切枪、重生和断线时如何结束开火？
5. NPC 如何通过同一 Ability 规则开火？
6. 如何证明旧 RPC 与 GA 不会同时生成弹丸？
```

最终闭环：

```text
Local Input / AI Intent
→ ASC
→ GA_Fire ServerOnly
→ Validate Avatar / Death / Weapon / Ammo
→ WeaponActor.StartFiring
→ Inventory ConsumeMagazineAmmo
→ Server Projectile
→ ApplyDamage
→ GAS Health
```

---

## 4. 本阶段明确不做

严格禁止顺手实现：

```text
Local Predicted GA_Fire
预测弹药 HUD
预测弹丸
预测命中反馈
服务器倒带
GameplayCue 全面重构
GA_Reload
GA_Equip
WeaponDefinition 正式化
FireBehavior 抽象重构
WeaponActor Pool
Projectile Pool
Lobby / Session
PvP Round
PvE Match
程序化地图
```

发现上述需求时只记录遗留项，不提前实现。

---

## 5. 架构合同

### 5.1 Ability 是开火事务入口，不是弹丸实现

`GA_Fire` 负责：

- Ability 激活与结束；
- 开火条件；
- 输入按下、松开与取消；
- 获取当前 Avatar 和 WeaponActor；
- 调用武器执行接口；
- 为未来预测保留明确边界。

`AShooterWeapon` 继续负责：

- 现有射速与全自动计时；
- 通过 Inventory 消耗弹匣弹药；
- 计算目标位置和弹丸出生 Transform；
- 服务器生成 Projectile；
- 触发现有射击表现入口。

本阶段不把 Weapon 的弹丸逻辑复制进 Ability。

---

### 5.2 Ammo 权威仍在 Inventory

玩家：

```text
GA_Fire
→ WeaponActor
→ BoundInstanceId
→ Inventory
→ WeaponInstance.MagazineAmmo
```

禁止让 Ability 新增第二份 Ammo。

NPC 没有 Inventory 时继续走 WeaponActor 的 `CurrentBullets` 兼容路径。本阶段只迁移 NPC 的开火入口，不强制给 NPC 接入玩家 Inventory。

---

### 5.3 玩家 Ability 跟随 PlayerState ASC

玩家 ASC 跨 Pawn 重生存在，因此：

- Fire Ability 只由服务器授予；
- 同一个 PlayerState 只允许存在一个 Fire Ability Spec；
- 重生只更新 Avatar，不重复授予；
- 新 Avatar 必须自动使用原 PlayerState ASC 中的 Fire Ability；
- 旧 Avatar 销毁时必须终止仍引用旧武器或旧 Avatar 的开火 Ability。

授予入口必须具备幂等检查，不能依赖“正常情况下只调用一次”。

---

### 5.4 NPC 使用同一 GA_Fire 规则

NPC 的 AIController / StateTree 只提交开火意图，不直接产生弹丸。

目标链路：

```text
StateTree / AI Intent
→ NPC ASC TryActivateAbility
→ GA_Fire ServerOnly
→ NPC Current Weapon
→ WeaponActor
```

玩家与 NPC 可以保留不同的输入入口，但开火条件和 Ability 生命周期应尽量共享。

如需要统一取得当前武器，可以为 `IShooterWeaponHolder` 增加最小的只读“获取当前武器”接口；不为此引入新的大型角色继承层级。

---

### 5.5 只能有一条实际开火入口

迁移期间可以暂时保留旧：

```text
ServerStartFire
ServerStopFire
```

用于源码对照，但一旦 Enhanced Input 切换到 ASC：

- 旧 RPC 不得再有输入调用者；
- 不设置“双路径同时执行”的运行时模式；
- 不允许 GA 和旧 RPC 同时调用 `Weapon::StartFiring`；
- 阶段结束前通过 CodeGraph 确认旧 RPC 已无调用者，再删除旧 RPC。

Git 历史和自动化基线已经足够承担对照作用，不长期保留第二条生产路径。

---

### 5.6 第一版只冻结必要 GameplayTag

本阶段至少建立：

```text
Input.Fire
State.Dead
State.Firing
```

用途：

- `Input.Fire`：输入与 Ability Spec 的稳定映射；
- `State.Dead`：拒绝死亡玩家激活，并在死亡时取消现有开火；
- `State.Firing`：表达当前开火事务，供互斥和测试观察。

射速暂时继续由 WeaponActor 的 `RefireRate` 和计时器负责。本阶段不同时引入另一套 GameplayEffect Cooldown 权威，避免双重射速来源。

---

## 6. 建议新增类型

按真实需要创建：

```text
Source/ShootGame/AbilitySystem/
├─ ShooterAbilitySystemComponent.h/.cpp
├─ ShooterGameplayAbility.h/.cpp
└─ Abilities/
   └─ ShooterGameplayAbility_Fire.h/.cpp
```

### UShooterAbilitySystemComponent

第一版只封装：

- Ability 输入 Tag 按下；
- Ability 输入 Tag 松开；
- 激活匹配 Ability Spec；
- 向已激活 Ability 转发按下/松开；
- 避免 Character 直接遍历 AbilitySpecContainer。

不在本阶段加入通用输入缓冲、Combo、复杂队列或预测统计框架。

### UShooterGameplayAbility

只提供项目通用的最小基类与安全 Avatar 获取入口。

没有出现第二个真实共享需求前，不向基类塞入武器、Inventory、UI 或 Projectile 逻辑。

### UShooterGameplayAbility_Fire

建议：

- Instanced Per Actor；
- Net Execution Policy 为 Server Only；
- 通过 `Input.Fire` 绑定；
- 激活时取得当前 WeaponActor；
- 成功后添加 `State.Firing`；
- 结束、取消或输入释放时停止 Weapon；
- 无武器、死亡、无弹药等条件下拒绝或立即结束。

具体使用 AbilityTask 还是输入事件回调，在 4A 实现时根据 UE5.6 实际 API 选择，不在计划阶段冻结。

---

## 7. 玩家目标调用链

按下：

```text
IA_Fire Started
→ ShooterCharacter.DoStartFiring
→ ShooterASC.AbilityInputTagPressed(Input.Fire)
→ TryActivateAbility
→ Server 激活 GA_Fire
→ CanActivateAbility
   ├─ Avatar 是当前 Character
   ├─ Character 未死亡
   ├─ CurrentWeapon 有效
   ├─ Weapon 属于当前 Character
   └─ Weapon 有可消费 Ammo
→ Weapon.StartFiring
```

松开：

```text
IA_Fire Completed
→ ShooterASC.AbilityInputTagReleased(Input.Fire)
→ Server 上活动 GA 收到释放
→ Weapon.StopFiring
→ EndAbility
```

半自动和全自动都继续复用 WeaponActor 当前行为：

- 半自动：一次激活只产生现有规则允许的一发；
- 全自动：Ability 活动期间由 Weapon 计时器继续 Fire；
- 输入释放、取消、死亡、切枪时必须清除 Weapon 计时器。

---

## 8. 强制取消条件

以下事件必须停止当前 Weapon 并结束或取消 GA_Fire：

```text
输入释放
Character Die
CurrentWeapon 切换
CurrentWeapon 销毁
Inventory Clear
Avatar 更换
Player 断线
Ability 被外部取消
Ammo 耗尽
```

取消逻辑必须幂等；多条清理链同时到达时不能重复生成表现或访问已销毁 WeaponActor。

---

## 9. 子阶段拆分

本阶段拆成四个可独立验证的子阶段：

```text
4A：ASC 输入基础设施 + Ability 授予生命周期
↓
4B：玩家 GA_Fire ServerOnly 闭环
↓
4C：取消边界 + NPC GA_Fire
↓
4D：删除旧 RPC + 完整回归
```

每个子阶段都必须：

- 先写对应开发记录；
- 独立 Build；
- 运行当前 Feature Tests；
- 只在稳定后提交；
- 不夹带无关资产或用户工作区修改。

---

## 10. 子阶段 4A：ASC 输入基础设施与授予

### 目标

建立 Ability 输入和授予基础，但不切换现有实际开火路径。

### 实施

- 新增 `UShooterAbilitySystemComponent`；
- 玩家 PlayerState 与 NPC 改用该 ASC 类型；
- 新增最小 `UShooterGameplayAbility`；
- 新增 `UShooterGameplayAbility_Fire` 壳体及必要 Tag；
- 服务器幂等授予玩家 Fire Ability；
- NPC 服务器初始化时幂等授予 Fire Ability；
- 重生时只更新 Avatar，不新增重复 Spec；
- 现有输入仍走旧 RPC，保证该提交不改变开火行为。

### Feature Tests

```text
ShootGame.Ability.Fire.Grant.Player
ShootGame.Ability.Fire.Grant.RespawnNoDuplicate
ShootGame.Ability.Fire.Grant.NPC
ShootGame.Ability.Fire.ActorInfo
```

### 验收

- 玩家服务器 ASC 有且只有一个 Fire Ability Spec；
- 拥有者客户端能观察到正确 Spec；
- Remote 不获得不应复制的完整 Ability 数据；
- 重生后 Spec Handle 不重复增长；
- 新 Avatar 正确绑定原 ASC；
- NPC ASC 独立持有一个 Fire Ability。

建议提交：

```text
GAS：建立开火 Ability 输入与授予生命周期
```

---

## 11. 子阶段 4B：玩家 GA_Fire ServerOnly 闭环

### 目标

把玩家 Enhanced Input 的实际开火入口切换到 GA_Fire。

### 实施

- `DoStartFiring / DoStopFiring` 改为向 ASC 提交 `Input.Fire`；
- GA_Fire 在服务器取得 Character 和 CurrentWeapon；
- 激活时调用 `Weapon::StartFiring`；
- 输入释放或 Ability 结束时调用 `Weapon::StopFiring`；
- 复用现有 Ammo、Projectile、Damage、Montage 与命中表现；
- 旧 `ServerStartFire / ServerStopFire` 暂时保留但必须无输入调用者；
- 不设置同时执行旧 RPC 的兼容分支。

### Feature Tests

```text
ShootGame.Ability.Fire.ServerOnly
ShootGame.Ability.Fire.SingleActivation
ShootGame.Ability.Fire.SingleProjectile
ShootGame.Ability.Fire.AmmoConsume
ShootGame.Ability.Fire.FullAutoRelease
```

### 网络验收

- Owner 按下一次只激活一个 GA_Fire；
- 服务器一次合法射击只扣一发对应 WeaponInstance Ammo；
- 一次服务器确认只生成一颗权威弹丸；
- 客户端不能直接生成 Projectile；
- Listen 与 Dedicated 结果与迁移前一致；
- 观察者继续看到服务器确认的第三人称射击表现。

建议提交：

```text
GAS：将玩家开火迁移为 ServerOnly Ability
```

---

## 12. 子阶段 4C：取消边界与 NPC GA_Fire

### 目标

补齐生命周期清理，并让 NPC 使用同一服务器 Ability 入口。

### 实施

- 死亡时设置 `State.Dead` 并取消 Fire Ability；
- 新 Avatar 初始化时清理不应跨生命保留的死亡/开火状态；
- 切枪前停止或取消旧 Weapon 对应的 Fire Ability；
- Inventory Clear、Weapon 销毁、EndPlay 与断线清理全部保持幂等；
- NPC `StartShooting / StopShooting` 改为通过 ASC 激活/结束 GA_Fire；
- NPC Weapon 继续使用无 Inventory 的兼容 Ammo；
- AI 只提交意图，不在 StateTree 内复制 Ability 的射击规则。

### Feature Tests

```text
ShootGame.Ability.Fire.Reject.Dead
ShootGame.Ability.Fire.Reject.NoWeapon
ShootGame.Ability.Fire.Reject.NoAmmo
ShootGame.Ability.Fire.Cancel.SwitchWeapon
ShootGame.Ability.Fire.Cancel.Death
ShootGame.Ability.Fire.Cancel.Disconnect
ShootGame.Ability.Fire.NPC
```

### 验收

- 死亡、无武器、无弹药时不生成弹丸；
- 全自动开火中死亡或切枪会立即停止旧 Weapon 计时器；
- 重生后不会继承 `State.Dead` 或 `State.Firing`；
- NPC 只在服务器激活 Ability 并生成权威弹丸；
- 玩家与 NPC 的伤害仍进入同一 GAS Health 闭环。

建议提交：

```text
GAS：补齐开火取消边界与 NPC Ability 链路
```

---

## 13. 子阶段 4D：旧 RPC 清理与阶段收尾

### 目标

保证 GA_Fire 是唯一生产入口，并完成阶段级全量验收。

### 实施

- 使用 CodeGraph 检查 `ServerStartFire / ServerStopFire` 调用者；
- 删除已无调用者的旧 RPC 声明和实现；
- 保留 WeaponActor 的 `StartFiring / StopFiring / Fire`，它们是 Ability 调用的执行接口；
- 更新网络测试协调器，报告 Ability 激活、弹丸数量、Ammo 变化和拒绝结果；
- 更新架构、自动化和蓝图分析文档中的开火入口；
- 运行完整七阶段回归。

### Feature Tests

本阶段所有：

```text
ShootGame.Ability.Fire.*
ShootGame.Inventory.*
ShootGame.GAS.*
```

必须全部通过。

### Full Regression

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Tests\RunAll.ps1 -Port <unused>
```

必须读取并记录：

```text
Saved/Automation/Runs/<timestamp>/Summary.json
```

建议提交：

```text
清理：移除旧开火 RPC 并完成 GA_Fire 回归
```

---

## 14. 网络场景验收

### 场景 A：玩家半自动

```text
Pickup Pistol
→ Press Fire
→ GA_Fire ServerOnly
→ Ammo -1
→ One Projectile
→ Release
→ Ability End
```

### 场景 B：玩家全自动

```text
Pickup Rifle
→ Hold Fire
→ 单个活动 GA_Fire
→ Weapon 按 RefireRate 连续射击
→ Release
→ 不再生成 Projectile
```

### 场景 C：切枪取消

```text
Rifle Hold Fire
→ Switch Pistol
→ Rifle StopFiring
→ Rifle Timer Cleared
→ Pistol 不自动开火
```

### 场景 D：死亡与重生

```text
Hold Fire
→ Die
→ GA_Fire Cancel
→ Inventory Clear
→ WeaponActors Destroy
→ Respawn
→ 原 ASC 无残留 Firing/Dead 状态
→ Inventory Empty
```

### 场景 E：NPC

```text
AI Start Intent
→ NPC ASC
→ GA_Fire
→ Server Projectile
→ AI Stop Intent
→ Ability End
```

---

## 15. 弱网验收

本阶段仍是 P0，不要求本地立即反馈。

EmulatedNetwork 只要求：

- 客户端输入最终抵达服务器；
- 每次服务器激活只生成合法弹丸；
- Ammo、Health、死亡和得分最终收敛；
- 输入释放最终停止全自动开火；
- 丢包下不出现无限开火或残留 `State.Firing`；
- 不出现旧 RPC 与 Ability 双重生成。

必须把 P0 的输入到服务器确认延迟记录为后续 P1 对照数据，但不在本阶段优化。

---

## 16. 开发与验证流程

每个子阶段遵循：

```text
Preflight
→ Implementation
→ Feature Tests
→ Build
→ Focused Listen / Dedicated
→ Read Logs
→ Fix
→ Re-test
→ 开发记录
→ Commit
```

完整 `RunAll.ps1` 只在子阶段稳定或阶段最终收尾时运行，不作为普通编译循环。

同一失败项最多自主修复 3 轮。3 轮仍失败时：

- 停止继续扩展；
- 保留工作区；
- 输出失败测试和日志路径；
- 记录根因假设与已尝试修复；
- 请求用户判断。

---

## 17. 每个提交的开发记录要求

每个子阶段必须在提交前按 `Docs/开发记录/README.md` 新建独立记录，至少写明：

- Ability 授予位置；
- ASC Owner / Avatar；
- 输入如何进入 ASC；
- GA_Fire 的网络执行策略；
- Weapon、Inventory、Ammo 的权威边界；
- 旧 RPC 是否仍存在及调用者状态；
- 玩家/NPC 覆盖范围；
- Feature Test 与网络测试结果；
- Full Regression Summary 路径；
- 遇到的问题、处理方式与遗留项。

禁止只写：

```text
完成 GA_Fire
测试通过
```

---

## 18. 最终验收

只有同时满足以下条件，本阶段才算完成：

### Ability 生命周期

- 玩家和 NPC 都只有一个 Fire Ability Spec；
- 玩家重生不重复授予；
- 新 Avatar 使用原 PlayerState ASC；
- 旧 Avatar 不残留活动开火 Ability。

### 唯一入口

- Enhanced Input 不再调用旧开火 RPC；
- AI 不再直接调用 Weapon 生成攻击结果；
- CodeGraph 确认旧 `ServerStartFire / ServerStopFire` 无调用者并已删除；
- 没有一次输入生成两份弹丸的可能。

### 权威边界

- GA_Fire 只在服务器执行 Gameplay 结果；
- 玩家 Ammo 只由 Inventory WeaponInstance 修改；
- Projectile 只由服务器生成和碰撞；
- Damage 继续通过 GAS Health 收敛；
- 客户端不能直接扣 Ammo、生成弹丸、扣血或计分。

### 生命周期

- 输入释放、切枪、死亡、Weapon 销毁、Inventory Clear、重生和断线均能停止开火；
- 全自动计时器没有残留；
- `State.Dead` 与 `State.Firing` 不错误跨生命保留。

### 回归

```text
Pickup
Inventory
Switch
Ammo
Fire
Projectile
Damage
GAS Health
Death
Score
Respawn
NPC
```

全部保持正常。

### 自动化

```text
Build
Automation
Standalone
DedicatedNetwork
ListenNetwork
EmulatedNetwork
DisconnectCleanup
```

全部 Passed，并在开发记录中保存最终 `Summary.json` 路径。

---

## 19. 阶段结束后暂停

完成 GA_Fire ServerOnly 后：

> 不自动进入预测。

先复盘：

1. GA_Fire 是否真正成为唯一事务入口；
2. Ability 与 WeaponActor 的职责是否清晰；
3. 玩家和 NPC 是否能共享规则而不互相污染；
4. 输入释放和生命周期取消是否稳定；
5. P0 高延迟体验与服务器确认时间；
6. 是否应先执行 `GA_Reload / GA_Equip`，还是先补 WeaponDefinition / FireBehavior 的真实缺口。

按最终路线，默认下一阶段是：

```text
GA_Reload / GA_Equip
```

只有完成新的阶段复盘和详细计划后才能开始。
