# P1 Local Predicted 基础射击反馈执行计划

> **归档说明（2026-09-17）**：本阶段已完成并归档；正文保留为历史计划，不再更新。

- 完成提交：`b66723b`（P1-A）→ `2bf3d59`（P1-B）→ `a4466ca` → `3c05044` → `757545f`
- 验证证据：Automation `Passed=81 Failed=0 NotRun=0`；Dedicated / Emulated / Listen 全部 Failure=0
- 遗留项见开发记录 `Docs/开发记录/2026-09-17-1930-按本地预测语义收敛开火表现与本地开火节拍.md`
- 后续阶段：[输入缓冲与 Reload 本地预测执行计划](输入缓冲与Reload本地预测执行计划.md)

## 1. 阶段定位

本文承接 [Shooter 完整 Demo 最终路线规划](../执行计划/Shooter完整Demo最终路线规划.md)。
[武器启动预配置与实体池简化重构方案](武器启动预配置与实体池简化重构方案.md)
已经完成并验收，因此本文是当时记录的下一候选执行阶段（P1 已于 2026-09-17 完成）：

```text
GA_Fire / GA_Reload / GA_Equip ServerOnly 基线
→ DT_WeaponData 启动快照 + WeaponId 预热池
→ WeaponActor 作为唯一运行时武器实例
→ Inventory（WeaponActor + SlotIndex）/ Equipment.CurrentWeaponActor
→ 七阶段正式验收
→ P1 Local Predicted 基础射击反馈
```

> 术语纠偏（2026-09-12）：本文后续出现的 `WeaponDefinition` / `DefinitionId`、
> `WeaponRowName` / `InstanceId` 术语统一按「历史身份模型」理解；
> 当前冻结 API 只有 `WeaponId` / `AShooterWeapon*` / `SlotIndex` /
> `CurrentWeaponActor`。

本阶段只解决一个体验问题：

> 拥有者按下开火后，不再等待一次客户端到服务器再返回客户端的网络往返，立即看到和听到第一人称开火反馈。

本阶段不是命中预测、弹丸预测或弹药预测。服务器继续拥有所有 Gameplay 结果的最终决定权。

### 1.1 当前验收治理

自 2026-09-16 起，P1 的最终验收、证据质量和对外报告统一遵循
[网络射击 AI 自主验证契约](../架构/网络射击AI自主验证契约.md)。本文中的 P1 子阶段、网络场景、
Phase / Case 和建议测试名只用于组织实现，不再要求用户理解或指定。

Agent 必须在每次修改前声明受影响的不变量，自主选择最小充分验证链，并按五条不变量报告结果。
本文后续旧测试矩阵若与该契约冲突，以契约为准；本文冻结的 Gameplay 范围、生产职责、API 边界与
非目标继续有效。

---

## 2. 开始实施前的工作区门槛

Pickup 重生逻辑门的生产修复已由用户完成，P1 不再负责修改。其定向验证、
Projectile 权威生成链、WeaponActor 生命周期和 Pickup WeaponId 接入均已完成。

进入 P1 前必须证明：

```text
玩家 A 拾取
→ Pickup 隐藏
→ RespawnPickup / BP_OnRespawn / FinishRespawn
→ 玩家 B 拾取
→ A、B 各自拥有独立 WeaponActor（同一 WeaponId 池的不同租用实体）
→ 同一重生周期没有重复授予
```

此外，启动预配置重构的七阶段正式回归必须通过，且预测实现只能依赖已经冻结的
`WeaponId` / `AShooterWeapon*` / `SlotIndex` / `CurrentWeaponActor` /
WeaponActor Lifecycle API。

当前前置状态（2026-09-14，只读复核）：

```text
S1 启动快照与 WeaponId 池：已完成并提交（cf158d0）
S2 WeaponActor 成为运行时实例：已完成并提交（def1fbb）
S3 Inventory 与 Equipment 去 InstanceId：已完成并提交（e84951e）
S4 Pickup 接入与旧路径删除：已完成并提交（832109f）
弱网切枪输入时序修复：已完成并提交（310faf9）
S5 七阶段完整回归与正式验收：已通过
最新玩法收口基线：329444d（玩法：收口武器开火与双向切枪）
Pickup 跨重生授予：ShootGame.Pickup.RespawnGate.CrossRespawnGrant Passed
最新七阶段回归：Saved/Automation/Runs/20260914_151623/Summary.json
Automation：112 项，80 项无警告成功，32 项带警告成功，0 失败，0 NotRun
```

上述前置项已经完成；P1 可在用户另行明确要求开始后实施，且只能依赖
`WeaponId / AShooterWeapon* / SlotIndex / CurrentWeaponActor` 的新身份模型。

---

## 3. 设计依据

### 3.1 UE5.6 官方依据

- [Using Gameplay Abilities in Unreal Engine 5.6](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-gameplay-abilities-in-unreal-engine?application_version=5.6)：`Local Predicted` 在拥有者客户端立即执行，同时把激活请求交给服务器；服务器可以确认或拒绝预测结果。
- [Gameplay Ability System Overview in Unreal Engine 5.6](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-the-unreal-engine-gameplay-ability-system?application_version=5.6)：预测激活被服务器拒绝时必须撤销本地副作用；GameplayCue 适合纯表现，但不能承载权威 Gameplay 结果。
- [Abilities in Lyra in Unreal Engine 5.6](https://dev.epicgames.com/documentation/en-us/unreal-engine/abilities-in-lyra-in-unreal-engine?application_version=5.6)：输入 Tag 驱动 Ability，Locally Predicted Ability 在拥有者与服务器执行，表现和权威结果按网络角色分工。

### 3.2 本地 UE5.6 源码核对

本机安装版本 `5.6.1-44394996` 已确认：

- `UGameplayAbility` 提供 `IsPredictingClient()`、`IsLocallyControlled()`、`HasAuthority()` 与 `HasAuthorityOrPredictionKey()`；
- 客户端预测激活通过 `ServerTryActivateAbility(..., FPredictionKey)` 请求服务器；
- `ClientActivateAbilityFailed_Implementation` 广播 PredictionKey Reject，并对匹配的 Ability 实例调用结束流程；
- 预测 Ability 的 `EndAbility` 因而必须能够幂等清理本地表现计时器和缓存引用。

核对位置：

```text
Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Public/Abilities/GameplayAbility.h
Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Private/AbilitySystemComponent_Abilities.cpp
```

### 3.3 官方方向与当前项目的差异

官方推荐 GameplayCue 作为多人纯表现通道；当前项目已经使用：

```text
Character.MulticastPlayFiringMontage
Weapon.MulticastPlayFiringFX
```

而且连续射击节拍仍由 `AShooterWeapon::RefireTimer` 管理，不是每一发都对应一个新的 PredictionKey。若现在直接把完整连续射击表现迁入 GameplayCue，会同时引入：

- GameplayCue 资产和路径配置；
- 每发预测键与服务器确认关联；
- Weapon 连续射击事务重新分层；
- 新旧 Multicast 迁移期双路径。

这超过 P1“基础反馈”的必要范围。因此本阶段采用：

```text
LocalPredicted GA_Fire
├─ Owning Client：纯本地第一人称表现循环
└─ Authority：保留现有 Ammo / Projectile / Damage / 远端确认表现
```

GameplayCue 迁移只作为 P1 结束后的复盘候选，不在本阶段预先实施。

---

## 4. 当前已验证基线

### 4.1 GAS 与输入

- 玩家 ASC 位于 `AShooterPlayerState`，PlayerState 是 Owner，Character 是 Avatar；
- ASC 使用 Mixed 复制模式并跨 Pawn 重生保留；
- `Input.Fire` 通过 `UShooterAbilitySystemComponent::AbilityInputTagPressed/Released` 进入 GAS；
- `GA_Fire` 为 `InstancedPerActor + ServerOnly`；
- `State.Dead`、`State.Reloading`、`State.Equipping` 阻塞开火；
- `State.Firing` 是 GA_Fire 活动期间的 Owned Tag；
- 输入释放、切枪、死亡、换弹、装备、弹药耗尽和断线均已有取消路径。

### 4.2 权威 Gameplay

- `AShooterWeapon::Fire()` 显式拒绝非 Authority；
- 玩家与 NPC Ammo 权威都在 WeaponActor.MagazineAmmo / ReserveAmmo（OwnerOnly 复制）；
- Projectile 只在服务器生成；
- Damage、Health、Death、Score 继续由服务器和 GAS 收敛；
- NPC 通过同一个 GA_Fire 类在服务器执行，并从同一 WeaponId 池租用与玩家相同的 WeaponActor；

### 4.3 当前表现链路

```text
Server Weapon::Fire
├─ Character.MulticastPlayFiringMontage
├─ Weapon.MulticastPlayFiringFX
└─ Character.AddWeaponRecoil
```

当前问题：Dedicated Server 下拥有者的第一人称 Montage、枪口闪光、声音需要等待服务器确认后才能出现；Recoil 还依赖服务器侧 Controller 路径，不能作为可靠的拥有者本地反馈。

最近一次已提交基线的完整回归：

```text
Saved/Automation/Runs/20260914_151623/Summary.json
Build / Automation / Standalone / Dedicated / Listen / Emulated / DisconnectCleanup 全部 Passed
Automation：112 项，80 项无警告成功，32 项带警告成功，0 失败，0 NotRun
```

---

## 5. P1 目标与非目标

### 5.1 必须实现

拥有者本地立即反馈：

- 第一人称开火 Montage；
- 第一人称枪口 Niagara；
- 本地开火声音；
- 本地镜头 Recoil；
- 全自动武器的本地表现节拍；
- 输入释放或 Ability 结束后立即停止本地连续表现。

服务器继续执行：

- 激活合法性最终校验；
- 射速与真实开火次数；
- MagazineAmmo 消耗；
- Projectile Spawn；
- Hit / Damage / Health / Death / Score；
- AI Noise；
- 其他客户端的第三人称确认表现。

### 5.2 明确不做

```text
预测扣除 MagazineAmmo / ReserveAmmo
预测 HUD 弹药数字
预测 Projectile
客户端命中判定
预测 Damage / Health / Death / Score
命中标记预测
服务器倒带或 Lag Compensation
Spread Random Seed 同步
GA_Reload LocalPredicted
GA_Equip LocalPredicted
完整 GameplayCue 迁移
FireBehavior / 武器配置架构重构
现有 WeaponRuntimeSubsystem 武器实体池改造
新增 Projectile Pool
Lobby / Session / Match Flow
```

本地预测表现可以被服务器否决。一次已经播放的极短枪口闪光、声音或 Recoil 不要求反向“抹除”，但任何持续计时器、Montage 循环、缓存和 `State.Firing` 必须收敛结束。

---

## 6. 冻结的职责边界

### 6.1 GA_Fire

`GA_Fire` 改为：

```cpp
NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
```

它负责：

- 同一次激活在预测客户端和服务器上的角色分流；
- 缓存本次 Weapon，但每次执行前确认其仍为当前武器；
- 拥有者本地表现开始/结束；
- 持有全自动拥有者表现使用的 `PredictedFeedbackTimer`；
- 服务器权威 `Weapon::StartFiring/StopFiring`；
- 在 `EndAbility` 中统一清理预测 Timer、缓存引用和持续表现；
- 输入释放、取消、Reject、切枪、死亡和弹药耗尽的幂等收敛。

它不直接生成弹丸或修改 Ammo。

### 6.2 WeaponActor

Weapon 保留权威射击状态，并提供按武器隔离节拍的单次拥有者表现入口：

```text
Authority Fire Path
→ StartFiring / Fire / RefireTimer
→ Ammo / Projectile / Multicast Remote Feedback

Per-Weapon Owner Presentation
→ PlayOwnerPredictedShotFeedback
→ Montage / Niagara / Sound / Recoil
```

要求：

- `PredictedFeedbackTimer` 归 GA_Fire，权威 `RefireTimer` 归 Weapon；
- 本地表现路径不得写 `TimeOfLastShot`、`bIsFiring`、Inventory 或 Projectile；
- 每个 WeaponActor 独立记录下一次允许普通预测表现的本地时间，间隔读取自身 `RefireRate`，
  不跨武器共享，也不建立第二个 Timer；
- Weapon 归还对象池或重新绑定 Owner 时复位纯表现冷却，但不承担 GA Timer 生命周期；
- Listen Host 可以同时执行权威事务与无状态 Owner Presentation，两条路径不能重复可见反馈；
- GA 的本地节拍只读取 Weapon 已冻结的 `bFullAuto`、`RefireRate` 与表现资产；
- 所有本地表现函数必须在 Dedicated Server 直接返回。

### 6.3 Character

Character 只提供本地视角表现入口：

- 播放第一人称 Montage；
- 应用本地 Recoil；
- 不根据表现回调修改 Gameplay 状态。

现有 Multicast 对拥有者的第一人称部分必须避免重复播放；第三人称 Mesh 的服务器确认播放仍保留，
供远端玩家观察。具体冻结为：

- Owner 第一人称 Montage、枪口、声音和 Recoil 只由本地预测播放一次；
- Remote 第三人称 Montage、枪口和声音只由服务器确认播放一次；
- Owner 仍可接收第三人称服务器确认，以维持同场景第三人称 Mesh 的一致性；
- Listen Host 不得因同时执行预测与 Authority 分支而双播第一人称表现。

### 6.4 ASC 输入

保留 `Input.Fire` 单入口，不新增直接 Weapon 输入路径。

按下：

```text
IA_Fire Started
→ AbilityInputTagPressed(Input.Fire)
→ 本地预测激活 GA_Fire
→ 同一 PredictionKey 请求服务器激活
```

松开：

```text
IA_Fire Completed
→ 本地活动 GA_Fire 立即 InputReleased
→ 立即停止 PredictedFeedbackTimer
→ 可靠通知服务器结束 Authority GA_Fire
```

本地 UE 5.6.1 已核对：当前释放路径会先对本地活动实例调用 `AbilitySpecInputReleased`，
并通过 `ServerSetInputReleased` 可靠通知服务器。继续复用该 GAS 通道，不新增第二条 StopFire RPC。

---

## 7. 客户端预检与服务器最终校验

当前 ServerOnly GA_Fire 在客户端故意采用宽松预检，避免弱网下迟到的 `State.Reloading / State.Firing` 移除复制吞掉唯一一次输入。

P1 继续冻结以下规则：

### 客户端只做请求与表现就绪预检

- ASC 和当前 Avatar 有效；
- Avatar 是本地控制；
- ASC 当前 Avatar 与本次 Avatar 一致；
- 当前 WeaponActor 存在且足以播放本地表现。

客户端不得把迟到的复制 Tag、Ammo、Owner 或隐藏状态当成最终裁决。表现对象尚未就绪时可以跳过
本次本地瞬时反馈，但不能据此批准 Gameplay 结果；存在不确定性时仍允许请求服务器激活。

### 本地预测表现的独立门控

上面的客户端放宽只针对“是否把请求发给服务器”。**本地预测表现另有一道独立门控**，
本机已知处于下列任一状态时只跳过本地表现，请求仍然照常发送，由服务器完整校验并 Reject：

- 本机 ASC 已挂 `State.Reloading` / `State.Equipping` / `State.Dead`；
- 当前 Weapon 已隐藏，或已不是 `Equipment.CurrentWeaponActor`。

该门控同时作用于首次预测射击与全自动 `PredictedFeedbackTimer` 的每一次 Tick。
服务器尚未裁决时，该门控不会因为“Tag 可能是迟到的”而自行取消；但如果客户端因此没有播放，
而 GAS 随后确认同一次预测激活成功，则 `ConfirmActivateSucceed` 必须在复核当前武器仍有效后补播一次。
确认回退后，全自动后续 Tick 在这批迟到 Tag 首次清除前继续播放；清除后恢复正常门控，Reject 不补播。

普通预测表现还必须通过当前 WeaponActor 自己的纯表现冷却。冷却按 `RefireRate` 推进，不能放在
GA、Character 或 ASC 上跨武器共享。服务器确认当前激活成功、但普通预测被该冷却误挡时，
`ConfirmActivateSucceed` 必须旁路冷却补播一次，并重新推进该武器的纯表现冷却；Reject 不进入确认回调，
也不得由冷却机制安排任何延迟反馈。该冷却不修改权威 `TimeOfLastShot` 或 `RefireTimer`。

全自动本地循环**不等待服务器 Confirm**：等待会在高延迟下产生
“第一枪立即播放 → 停一个 RTT → 才继续连射”的断层。因此允许客户端在尚未收到
`State.Reloading` 等复制状态时，于 Reject 到达前短暂播放若干次纯本地连续表现。

### 服务器执行完整校验

- `Super::CanActivateAbility`；
- Avatar 与 ASC 当前 Avatar 一致；
- 非 Dead / Reloading / Equipping；
- 当前 Weapon 与 Equipment.CurrentWeaponActor 一致；
- Weapon Owner、可见性和生命周期有效；
- 权威 MagazineAmmo 可消费；
- 半自动武器通过只读射速资格查询，确认 `RefireRate` 允许立即射击。

服务器 Reject 后，客户端必须通过 GAS 的预测拒绝结束 Ability，并清理本地持续表现。

半自动不得沿用当前“Ability 激活成功但 `StartFiring` 因冷却静默不发射”的语义。P1-B 应在 Weapon
提供 `CanStartImmediateShot()` 或等价的无副作用查询，供服务器 `GA_Fire::CanActivateAbility` 使用。

全自动在冷却期重新按下时仍允许向服务器请求激活：服务器沿用剩余权威冷却；Owner 的普通预测表现
同样受本武器纯表现冷却约束。若服务器最终接受而本地冷却误挡，确认回退补播一次；不得提前生成
Projectile 或消耗 Ammo。

---

## 8. 子阶段执行顺序

### P1-0：ServerOnly 复盘与基线冻结

目标：证明当前权威链没有双入口，再引入预测。

实施：

- 直接引用第 2 节已经通过的 Pickup 与七阶段回归证据，不重复运行完整回归；
- CodeGraph 检查 GA_Fire 是玩家唯一开火入口；
- 记录 P0 在 Dedicated 与 `PktLag=100 / PktLoss=2` 下从输入到拥有者表现的日志时序；
- 记录半自动、全自动的权威 Projectile 数和 Ammo 消耗数；
- 仅在现有证据不足时运行 `ShootGame.Ability.Fire.*` 或定向网络场景。

通过条件：

- 一次权威射击只扣 1 发并生成 1 个 Projectile；
- 玩家没有旧 Fire RPC 或直接 Weapon 开火调用者；
- NPC 只在服务器执行 GA_Fire；
- 当前完整回归全部 Passed。

该阶段只生成证据，不修改预测策略；没有文件变化时不创建空提交。

### P1-A：抽取纯本地表现路径，不改变 ServerOnly 行为

目标：先建立可单测的表现 API，再切换网络策略。

实施：

- 在 Weapon 中提取单次本地表现函数；
- 分离第一人称与第三人称表现选择；
- 本地函数只播放 Montage、Niagara、Sound、Recoil；
- 保持该入口无状态，不在 Weapon 中建立本地 Timer；
- 增加测试计数或测试壳，证明本地路径不会改 Ammo、生成 Projectile 或写权威字段；
- 保持 GA_Fire `ServerOnly`，全量 Gameplay 行为不变。

建议 Feature Tests：

```text
ShootGame.Ability.Fire.Prediction.LocalFeedbackCosmeticOnly
ShootGame.Ability.Fire.Prediction.LocalFeedbackStateless
ShootGame.Ability.Fire.Prediction.DedicatedNoLocalFeedback
```

建议提交：

```text
射击：分离拥有者本地开火表现路径
```

### P1-B：GA_Fire 切换 LocalPredicted，先完成半自动闭环

目标：让一次按下立即产生一次拥有者表现，同时服务器仍只产生一次真实射击。

实施：

- 将 GA_Fire 改为 `LocalPredicted`；
- 用 `IsPredictingClient / IsLocallyControlled / HasAuthority` 明确分流；
- 预测客户端只调用单次本地表现；
- Authority 只调用现有 `Weapon::StartFiring`；
- 为半自动增加服务器只读射速资格检查，冷却期必须 Reject；
- Listen Host 同时执行本地表现和权威事务，但不得重复可见的第一人称反馈；
- Server Multicast 到达拥有者时跳过已经由本地路径承担的第一人称 Montage、枪口和声音；
- 若初始本地门控跳过反馈但服务器接受激活，GA_Fire 在确认回调中只补播一次拥有者纯表现；
- 远端客户端仍只播放服务器确认的第三人称表现；
- Reject、InputReleased 与 EndAbility 共用幂等清理。

建议 Feature Tests：

```text
ShootGame.Ability.Fire.Prediction.Policy
ShootGame.Ability.Fire.Prediction.OwnerImmediateSemiAuto
ShootGame.Ability.Fire.Prediction.ListenNoDuplicate
ShootGame.Ability.Fire.Prediction.RemoteConfirmedOnly
ShootGame.Ability.Fire.Prediction.ServerRejectCleanup
ShootGame.Ability.Fire.Prediction.RejectRefireCooldown
ShootGame.Ability.Fire.Prediction.SingleAuthorityProjectile
```

通过条件：

- 高延迟下 Owner 本地反馈日志早于 Authority Commit；
- 一次输入的 Owner 第一人称反馈计数为 1（仅限客户端未处于换弹 / 切枪 / 死亡，
  且武器可见且为当前装备时）；
- Remote 第三人称反馈只在 Authority Commit 后出现且计数为 1；
- Reject 后 Projectile、Ammo、Damage 均不变；
- Reject 后没有活动 Ability、Local Timer 或错误 `State.Firing`。

建议提交：

```text
GAS：实现半自动开火本地预测反馈
```

### P1-C：全自动本地表现节拍与取消边界

目标：按住 Rifle 时 Owner 按本地 `RefireRate` 连续看到表现，松开立即停止。

实施：

- 仅对 `bFullAuto` 在 GA_Fire 中开启 `PredictedFeedbackTimer`；
- Timer 每次只播放纯表现，不扣本地 Ammo；
- 普通预测表现由当前 WeaponActor 按自身 `RefireRate` 限速，不跨武器共享；
- 服务器接受但本地冷却误挡的激活，由确认回退旁路冷却补播一次；Reject 不安排后续反馈；
- 输入释放通过 `EndAbility` 停止本地 Timer，并通知服务器停止权威循环；
- 切枪、Reload、Equip、Death、Inventory Clear、Avatar 更换、OutOfAmmo、Disconnect、
  Prediction Reject 必须沿既有取消链进入 `EndAbility`；
- `EndAbility` 幂等清理 Timer、CachedWeapon 和持续表现；
- Weapon 不再是当前装备、已经隐藏或生命周期失效时，下一次本地 Tick 必须结束 Ability；
- 服务器权威 `RefireTimer` 与射击次数完全不受本地 Timer 影响；
- 不修改权威 `TimeOfLastShot`，网络用例不得通过反射篡改客户端 `bFullAuto`；
- 接受服务器拒绝前已经发生的短暂纯表现，但不接受无限循环或跨武器残留。

建议 Feature Tests：

```text
ShootGame.Ability.Fire.Prediction.FullAutoImmediate
ShootGame.Ability.Fire.Prediction.FullAutoRelease
ShootGame.Ability.Fire.Prediction.CancelReload
ShootGame.Ability.Fire.Prediction.CancelEquip
ShootGame.Ability.Fire.Prediction.CancelDeath
ShootGame.Ability.Fire.Prediction.CancelWeaponDestroyed
ShootGame.Ability.Fire.Prediction.CancelAvatarChanged
```

建议提交：

```text
射击：补齐全自动预测表现与取消收敛
```

### P1-D：网络证据、清理与阶段验收

目标：证明 P1 只缩短 Owner 表现延迟，没有改变权威结果。

实施：

- 扩展既有 Network Test Coordinator，不另建第二套网络测试框架；
- Owner 上报预测表现开始、停止和计数；
- Server 记录 Authority Commit、Ammo、Projectile 与拒绝结果；
- Remote 上报第三人称确认表现；
- Dedicated、Listen、Emulated 都检查无双表现、无双弹丸；
- 删除 P1 产生的临时日志、调试字段和无调用者旧表现入口；
- 更新最终路线规划的当前阶段状态；
- 运行七阶段全量回归并记录 Summary。

建议提交：

```text
测试：完成P1预测射击反馈网络回归
```

---

## 9. 网络场景验收矩阵

| 场景 | Owner | Server | Remote | 必须证明 |
|---|---|---|---|---|
| 半自动合法开火 | 立即一次 FP 反馈 | Ammo -1、1 Projectile | 确认后一次 TP 反馈 | Owner 无重复，Gameplay 仅一份 |
| 换弹后迟到 Tag | Confirm 补一次 FP | 接受并提交一发 | 一次 TP | 不先见弹丸后听枪声 |
| 半自动无弹 | 至多一次反馈 | Reject，无消耗/弹丸 | 无 | Reject 后无延迟反馈/残留 |
| 全自动按住 | 本地 RefireRate 表现循环 | 权威 RefireRate 扣弹/生成 | 每发权威确认后表现 | 两套 Timer 不互相污染 |
| 全自动松开 | 当帧停止本地循环 | 收到释放后停止权威循环 | 不再收到新确认 | 无尾部无限开火 |
| 开火中 Reload | 本地 Fire 表现停止 | Fire 取消，Reload 权威开始 | 只见已确认射击 | Tag 与 Timer 收敛 |
| 开火中切枪 | 旧武器本地表现停止 | 旧 Weapon Stop、Equip 提交 | 新旧武器无错串 | CachedWeapon 清空 |
| 开火中死亡 | 立即结束本地循环 | Damage/Death 权威完成 | 死亡确认 | 无跨生命 Firing |
| Prediction Reject | 结束预测 Ability | 0 Gameplay 结果 | 无确认表现 | PredictionKey 对应实例结束 |
| Listen Host | 立即本地反馈 | 同进程权威事务 | 其他客户端确认表现 | Host 不双播 |
| NPC 开火 | 无 Owner 预测 | Server GA_Fire + Projectile | TP 确认表现 | NPC 行为不回退 |

还必须定向覆盖以下边界：

- 半自动冷却期再次按下：Owner 普通预测反馈被当前武器冷却挡下，服务器必须 Reject，
  Ammo 和 Projectile 不变；
- 全自动冷却期重新按下：Owner 普通预测同样受当前武器冷却限制，服务器只在剩余权威冷却结束后继续射击；
- 两把武器的纯表现冷却必须相互独立；切换到另一把武器不继承前一把武器的剩余冷却；
- 本地冷却误挡但服务器接受：确认回退必须只补播一次，Reject 不补播且不产生延迟反馈；
- 本地 Weapon 表现对象未就绪：允许缺少一次预测反馈，但不得产生客户端 Gameplay 结果或崩溃；
- Weapon 在预测 Timer Tick 前被切换、隐藏或归还池：Ability 必须结束且不得串到新 Owner。

---

## 10. 自动化与可观测性要求

### 10.1 不以肉眼作为唯一证据

Niagara、Sound 和 Montage 本身难以在 NullRHI 自动化中直接判断。应在真实表现入口维护测试可见计数，要求：

- 仅在 `WITH_DEV_AUTOMATION_TESTS` 或既有测试协调器范围暴露；
- 计数来自生产表现入口，不在测试中复制实现；
- 区分 `OwnerPredicted`、`AuthorityCommit`、`RemoteConfirmed`；
- 能记录 Weapon、Activation PredictionKey、ShotOrdinal、客户端角色和相对时序；
- Shipping 构建不增加预测测试日志带宽。

### 10.2 必须持续通过的既有测试

```text
ShootGame.Ability.Fire.*
ShootGame.Ability.Reload.*
ShootGame.Ability.Equip.*
ShootGame.Inventory.*
ShootGame.Equipment.*
ShootGame.GAS.*
ShootGame.UI.BulletCounter.*
```

原 `ShootGame.Ability.Fire.ServerOnly` 测试必须重命名或改写为新的权威边界测试，禁止简单删除断言来让策略切换通过。

### 10.3 网络日志最低标记

建议使用统一、可搜索的测试日志字段：

```text
FIRE_PREDICTED_OWNER
FIRE_AUTHORITY_COMMIT
FIRE_REMOTE_CONFIRMED
FIRE_PREDICTION_REJECTED
FIRE_LOCAL_FEEDBACK_STOPPED
```

日志至少包含：

```text
PlayerId / Weapon / PredictionKey / ShotOrdinal / Count / WorldTime / NetMode
```

`PredictionKey` 标识一次 Ability 激活或一次 Burst；`ShotOrdinal` 标识该 Burst 中的第几次反馈。
不得假设全自动每一次反馈都拥有新的 PredictionKey。

---

## 11. 预期修改范围

主要文件：

```text
Source/ShootGame/AbilitySystem/Abilities/ShooterGameplayAbility_Fire.h/.cpp
Source/ShootGame/Weapons/ShooterWeapon.h/.cpp
Source/ShootGame/Characters/ShooterCharacter.h/.cpp
Source/ShootGame/Tests/Ability/ShooterAbilityFireAutomationTests.cpp
Source/ShootGame/Tests/Ability/ShooterAbilityFireBehaviorAutomationTests.cpp
Source/ShootGame/Tests/Ability/ShooterAbilityFireCancellationAutomationTests.cpp
Source/ShootGame/Tests/Network/ShooterNetworkTestCoordinator.h/.cpp
```

`ShooterAbilitySystemComponent.cpp` 原则上不需要生产改动；只有新增断言或修正与实际行为不符的注释时
才进入修改范围，禁止另建 StopFire RPC。

原则上不需要修改 Blueprint 或现有音效、Montage、Niagara 资产。若实现证明必须新增 GameplayCue 资产或修改 AnimBP，停止当前子阶段，先记录证据并重新评审范围。

如新增、删除、移动或重命名 `Source/`、`Plugins/` 下源码文件，只在该轮结构变化完成后运行一次：

```powershell
Scripts/Development/RefreshVisualStudioFiles.ps1
```

仅修改现有文件时不刷新项目文件。

---

## 12. 验证流程

每个产生代码改动的子阶段：

```text
Preflight / CodeGraph
→ Test First 或先建立可观测失败证据
→ 最小实现
→ BuildEditor
→ ShootGame.Ability.Fire.Prediction.*
→ 既有 Fire / Reload / Equip / Inventory 回归
→ Dedicated 或 Listen 定向网络场景
→ 读取日志并确认计数
→ 开发记录
→ 独立提交
```

P1-0 只复用现有七阶段基线并按证据缺口定向验证。子阶段不提前运行 `RunAll.ps1`；
七阶段完整回归只在 P1-D 大阶段收口时运行一次。

阶段最终：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Tests\RunAll.ps1 -Port <unused>
```

必须逐项读取：

```text
Saved/Automation/Runs/<timestamp>/Summary.json
Automation index.json
Dedicated Server / Client 日志
Listen Server / Client 日志
Emulated Server / Client 日志
DisconnectCleanup 两个子会话日志
```

Emulated 必须确认 Server 与两个 Client 均实际启用：

```text
PktLag=100
PktLoss=2
```

---

## 13. 失败与回退规则

### 必须立即回退当前子阶段的情况

- 客户端能直接扣 Ammo、Spawn Projectile、Apply Damage 或计分；
- 一次输入在服务器生成两个 Projectile；
- Owner 收到预测与 Multicast 两次可见的第一人称反馈；
- Listen Host 与 Dedicated Client 的 Gameplay 权威不一致，或出现双 Gameplay、预测循环残留、
  Reject 后继续射击；
- Prediction Reject 后 Local Timer、Ability 或 `State.Firing` 残留；
- 全自动松开后仍持续产生权威弹丸；
- P1 修改破坏 NPC ServerOnly 开火；
- 为消除测试失败而降低既有 Gameplay 断言。

Host 与远端 Owner 在 Reject 之前的纯表现次数不要求一致：
Host 本地同时拥有权威信息，可能一次假反馈都没有；远端 Owner 因复制状态迟到，
可能短暂预测有限次数。每次仍必须受当前 WeaponActor 的 `RefireRate` 表现冷却约束。

### 可接受的 P1 边界

- Reject 到达前允许有限的纯本地预测表现，包括全自动武器的短暂连续反馈；
  前提是不产生任何客户端 Gameplay 结果（不扣 Ammo、不生成 Projectile、不结算伤害），
  且所有普通预测尝试均受当前 WeaponActor 的 `RefireRate` 表现冷却约束，
  且 Reject 到达后必须立即停止 `PredictedFeedbackTimer` 并清理
  Ability / `State.Firing` / `CachedWeapon` 等预测状态；
- 本机已知处于 `State.Reloading` / `State.Equipping` / `State.Dead`，
  或武器已隐藏、已不是当前装备时，只禁止本地预测表现，请求仍照常发给服务器；
- 若上述 Tag 是迟到状态且服务器接受激活，拥有者必须在确认回调中补一次纯表现，
  已经播放过的激活不得重复补播；
- 若普通预测被本地表现冷却误挡但服务器接受，确认回调必须旁路冷却补播一次；Reject 不补播；
- 全自动本地预测循环不等待服务器 Confirm，因此允许与权威节拍存在短暂相位偏移；
- HUD Ammo 仍等待 OwnerOnly FastArray 权威复制；
- 命中、伤害和远端表现仍有服务器确认延迟。

同一失败项最多自主修复 3 轮。仍不能收敛时停止扩展，保留工作区并报告：

- 失败测试；
- 日志路径；
- PredictionKey；
- Owner / Authority / Remote 三方计数；
- 已尝试的修复；
- 是否建议回退至上一子阶段提交。

---

## 14. P1 最终验收

P1 只有在 [网络射击 AI 自主验证契约](../架构/网络射击AI自主验证契约.md) 的五条不变量全部
获得有效证据时才算完成：

1. Owner Immediate Feedback；
2. Local Prediction Obeys Weapon Rules；
3. Server Is Final Authority；
4. Remote Is Confirmed Only；
5. Exactly One Authority Result。

Agent 自主决定复用、组合、修正或补充哪些测试。内部 Phase / Case 完成、Coordinator 总门变绿或
历史测试报告均不能单独作为 P1 验收结论。恒真断言、假 0、未执行目标分支、`Inconclusive` 和
不可归因的间接现象不得计入通过证据。

最终报告必须逐条使用契约第 6 节格式，不再只按“响应性 / 权威性 / 一致性”汇总。

### 回归

```text
Build
Automation
Standalone
DedicatedNetwork
ListenNetwork
EmulatedNetwork
DisconnectCleanup
```

全部 Passed，并在最后一个开发记录中保存 Summary 与关键网络日志路径。

---

## 15. 阶段结束后的强制复盘

P1 完成后不自动进入 Lobby。先回答：

1. Owner 表现延迟降低是否达到可感知收益？
2. 全自动本地节拍与权威节拍的偏移是否可接受？
3. 是否需要把开火表现迁移为 GameplayCue？
4. 是否值得进入 P2 预测弹药 HUD，还是按最终路线转入 Lobby + LAN Session？
5. 当前 Weapon 计时器是否已经成为继续预测的主要限制？

默认决策仍遵循最终路线：

```text
P1 验收并复盘
→ 若基础体验已足够：Lobby + LAN Session
→ 若有明确高收益且证据充分：单独制定 P2 计划
```

禁止仅因 P1 已使用 PredictionKey，就顺手预测 Ammo、Projectile 或 Damage。
