# Health 与 MaxHealth 迁入 GAS

- 日期：2026-08-17
- 计划提交说明：`GAS：Health / MaxHealth / 伤害迁入 GAS 基础生命闭环`
- 变更类型：生产代码 / 测试

## 目的

执行 GAS 第一阶段执行计划子阶段 1C：让 GAS 第一次真正承载 Gameplay 数值。只迁移 Health、MaxHealth 与伤害作用路径；保留现有 Projectile、Weapon、开火链、Death/Kill/TeamScore/Respawn 作为已有闭环。伤害最终如何作用到 Health 改为 GameplayEffect，子弹如何发射不变。

## 本提交完成内容

- 新增 `AbilitySystem/ShooterAttributeSet`：Health / MaxHealth（`FGameplayAttributeData`，`ATTRIBUTE_ACCESSORS_BASIC`），`PostGameplayEffectExecute` 收敛 Health 到 `[0, MaxHealth]`；属性通过 `DOREPLIFETIME_CONDITION_NOTIFY` + 复制子对象机制同步到拥有者客户端。
- 新增 `AbilitySystem/ShooterGameplayEffectStatics`：纯代码构建两个瞬时 GameplayEffect（进程级单例，防 GC）：
  - 初始化效果：`MaxHealth = 配置值`、`Health = MaxHealth`（Override + SetByCaller FName 路径，不依赖 GameplayTag 注册）；
  - 伤害效果：`Health -= Damage`（Additive + SetByCaller 负数量）。
- `ShooterPlayerState`：新增 `ShooterAttributeSet` 子对象；`PostInitializeComponents` 中把 Health 属性变化回调**持久绑定在 PlayerState 上**（跨角色重生有效），转发给当前 Avatar 角色。
- `ShooterCharacter`：
  - `TakeDamage` 改为桥接：服务器校验后通过伤害 GE 作用到 ASC 的 Health；击杀者经 `PendingDeathInstigator` 交给 Health 归零桥接。
  - `HandleHealthAttributeChanged`（由 PlayerState 转发）：服务器同步旧 `CurrentHP` 复制镜像、立即 `ForceNetUpdate`（角色 + PlayerState，避免属性值被网络更新合并）、Health<=0 时进入现有 `Die()`（Kill/计分/重生不变）；客户端广播 `OnDamaged` 驱动 HUD 事件链。
  - `InitializeAbilityActorInfo`：注册属性集并在服务器出生时应用初始化效果（`Health = MaxHealth = 角色 MaxHP 配置值`，保留旧资产配置作为桥接）。
- `ShooterNPC`：同款属性集 + 出生初始化（沿用 `CurrentHP` 配置值）+ 伤害桥接；`HandleHealthAttributeChanged` 镜像 `CurrentHP` 并进入现有 `Die()`（队伍分数/布娃娃/延迟销毁不变）；`TakeDamage` 补服务器权威校验；`StopShooting` 补武器未生成判空（监听服务器主机登录早于 NPC BeginPlay 时会触发，属测试暴露的生产健壮性修复）。
- 网络测试协调器扩展（仅 `-ShootGameNetworkTest`）：
  - 服务器：出生满血（Health==MaxHealth）、部分伤害精确（25% 恰扣一次）、致死归零、NPC 初始化+致死死亡桥接（属性归零 + `CurrentHP` 归零 + 胶囊碰撞禁用，不用布娃娃证据——测试 NPC 无物理资产）、重生满血。
  - 客户端：属性复制满血初始化、受伤状态属性与镜像收敛、重生满血收敛、HUD 事件链（协调器直接订阅 ASC 属性变化事件，跨重生无竞态；辅以角色 `OnDamaged` 监听并随角色重绑）。
  - 测试模式停用地图 NPC AI（GameMode PostLogin + 协调器首次轮询两处，不销毁 NPC，ASC 检查仍覆盖真实实例），消除旧基线 NPC 干扰对确定性验证的影响。
  - 成功标记扩展 `GasHealthInit/GasDamage/GasDeath/GasNpcHealth/GasClientHealth/GasHud` 字段；超时诊断同步扩展。
- Automation 测试扩展：`ShootGame.GAS.Configuration` 增加属性集子对象存在性与 Health/MaxHealth 属性定义检查。
- `ShootGame.Build.cs`：新增 `ShootGame/AbilitySystem` 公开 include 路径。

## 验证结果

- 编译：`ShootGameEditor Win64 Development` 通过。
- 聚焦 Automation：`ShootGame.GAS.Configuration`、`ShootGame.Weapon.Configuration` 均 Success。
- 聚焦网络会话（Dedicated，10:33 运行）：两个成功标记全 GAS 字段 true，含 `GasHealthInit=true GasDamage=true GasDeath=true GasNpcHealth=true/true GasClientHealth=true/true/true GasHud=true`。
- Full Regression：`Saved/Automation/Runs/20260817_183845/Summary.json`，顶层 Passed，七阶段全 Passed（Build 0.86s / Automation 18.68s / Standalone / Dedicated 32.23s / Listen 25.24s / Emulated 36.21s / Disconnect 29.39s）。
  - Dedicated/Listen/Emulated 成功标记 2/1/2，所有 GAS 字段全部 true（100ms/2% 丢包下客户端属性收敛与 HUD 事件链同样成立）。
  - Emulated 日志含 `PktLag set to 100` / `PktLoss set to 2`；Disconnect 输出 `AUTOMATION_TEST_DISCONNECT_SUCCESS`；无 `AUTOMATION_TEST_FAILURE`；测试进程全部回收。
- 旧闭环未回退：武器切换、弹药、开火、弹丸、死亡、计分、重生、断线清理全部保持通过（成功标记的既有字段不变）。

## 遇到的问题

1. `ATTRIBUTE_ACCESSORS` 宏在 UE 5.6 中已改名 `ATTRIBUTE_ACCESSORS_BASIC`，首次编译报 C2061。
2. `FGameplayEffectSpec::MakeOutgoingSpec` 只接受 GE 类，运行时实例需直接构造 `FGameplayEffectSpec(Def, Context, Level)`。
3. `FGameplayTag::RequestGameplayTag` 对未注册标签返回无效标签，SetByCaller 标签路径静默失效，Override 效果把属性覆盖为 0（`MaxHealth=0 Health=0`）。
4. 伤害 Additive 修饰最初传正数：Health 100→200 后又被 `PostGameplayEffectExecute` 钳制回 100，表现与“效果未生效”一致（耗时最久的定位项，经属性集执行探针日志确认修饰器确实执行）。
5. NPC 死亡检查初版用布娃娃物理做证据：测试 NPC 使用 C++ 裸默认网格，没有物理资产，`IsSimulatingPhysics` 永远为 false。
6. 监听服务器主机玩家的 `PlayerState::GetNetConnection()` 为空导致连接检查误报（1B 已修，1C 复验通过）。
7. 地图 NPC 随机攻击测试玩家：曾导致重生满血检查失败（复活后 500→475）与 Dedicated 标记超时；1C 起在测试模式下停用 NPC AI。
8. NPC 抑制代码调用 `StopShooting` 时武器尚未生成，监听服务器启动崩溃（EXCEPTION_ACCESS_VIOLATION，`ShooterWeapon.cpp:173`）。
9. 属性值最初未复制到拥有者客户端（缺少 `DOREPLIFETIME_CONDITION_NOTIFY`），客户端三个 Health 检查全部不通过。
10. 属性与镜像两条复制通道时序不同：部分伤害后紧跟致死，属性中间值可能被合并丢失，客户端伤害收敛检查窗口消失。
11. 复活满血 HUD 事件存在竞态：新 Pawn 的镜像初值不触发 OnRep、属性事件可能早于角色监听器重绑；且属性回调绑定在角色上会随旧角色销毁失效。

## 处理方式

1. 查询 5.6 引擎头文件确认宏名后替换。
2. 改用 `FGameplayEffectSpec` 直接构造（运行时实例的 Modifiers 数据在实例上，CDO 为空，不能走类路径）。
3. SetByCaller 全部改用 FName 路径（`DataName` + `SetSetByCallerMagnitude(FName, ...)`），不依赖标签注册。
4. 伤害量以负数量写入（`-Damage`），与 Additive 修饰语义一致；同时移除临时诊断日志。
5. NPC 死亡证据改为属性归零 + `CurrentHP` 归零 + 胶囊碰撞禁用（Die() 流程的确定性标志）。
6. 维持 1B 修复：远程客户端必须有连接，监听主机（`IsLocalController`）豁免。
7. 测试模式下（GameMode PostLogin + 协调器首次轮询）停用 NPC AI；不销毁 NPC，ASC 生命周期检查仍覆盖真实 `BP_ShooterNPC`。
8. `AShooterNPC::StopShooting` 增加武器判空（生产健壮性修复），并把抑制逻辑重复到协调器首次轮询（覆盖监听模式下晚于 PostLogin 的 NPC 拥有）。
9. 属性集补 `ReplicatedUsing` + `DOREPLIFETIME_CONDITION_NOTIFY`（注册到 ASC 时按 Mixed 模式重写为仅拥有者）。
10. 服务器每次生命变化 `ForceNetUpdate`（角色 + PlayerState）立即推送；客户端伤害收敛检查放宽为“属性与镜像收敛到同一受伤状态”（不要求特定中间值；服务器侧已有精确 25% 断言）。
11. Health 变化回调改为持久绑定在 PlayerState（ASC 宿主）上并转发给当前 Avatar；协调器直接订阅 ASC 属性变化事件作为 HUD 证据源（跨重生无竞态），角色 `OnDamaged` 监听随角色重绑作为辅助证据。

## 遗留项

- `CurrentHP` 仍保留为复制镜像（服务器由属性变化同步写入）：供观察者、回归与既有读取方使用，属旧路径桥接，待后续阶段（GAS 计划阶段 9 清理旧路径）统一处理；`GetCurrentHP/GetMaxHP` 读取镜像。
- `bIsDead` 仍为角色普通复制布尔，未迁移为 GameplayTag（计划明确本阶段不强制）。
- 测试 NPC 死亡会按模板 NPC `Die()` 规则给队伍 1 加 1 分（每协调器一次）；队伍分数断言均为 `>= 1`，不影响判定，已记录为测试已知副作用。
- GAS 插件提示 `GameplayCueNotifyPaths` 未配置（回退扫描全 `/Game/`），属性能警告，本阶段未使用 GameplayCue，暂不处理。
- 属性数值与 `CurrentHP` 镜像并存存在极小窗口的双通道时序差异；已用 ForceNetUpdate 收敛，后续引入预测时重新设计。
- 蓝图资产未改动：`BP_ShooterCharacter.MaxHP`（500）作为初始化效果的配置来源继续生效，多计划中提到的 MaxHP 异常值问题仍属独立资产配置项。
- 换弹、GA_Fire、预测、背包等仍按计划禁止，未实现。
