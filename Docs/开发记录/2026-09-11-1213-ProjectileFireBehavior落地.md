# Projectile FireBehavior 落地（A3）

- 日期：2026-09-11
- 计划提交说明：`武器：提取 Projectile FireBehavior 开火行为边界`
- 变更类型：生产代码 / 测试 / 资产

## 目的

实施《武器与 Inventory 正式架构实施计划》大阶段 A3：把弹丸生成从 `AShooterWeapon::FireProjectile` 提取为无复制、无持久可变状态的 FireBehavior 边界。WeaponActor 只负责计算可靠 Muzzle/Target Context、触发表现与调用行为；GA_Fire 生命周期与 Inventory 弹药消费职责不变；NPC 兼容路径明确隔离。

## 本提交完成内容

- 新增 `Weapons/ShooterWeaponFireBehavior.h`：
  - `FShooterWeaponFireContext`：WeaponActor、Instigator、服务器权威 MuzzleTransform、TargetLocation、Definition（可能为空）与 InstanceId（兼容路径无效）；
  - `UShooterWeaponFireBehavior`（Abstract）：唯一接口 `ExecuteFire(Context)`，注释冻结 4.3 节职责边界。
- 新增 `Weapons/ShooterProjectileFireBehavior.h/.cpp`：第一版唯一正式行为。弹丸类配置在行为实例上；缺 WeaponActor / Instigator / 弹丸类与非服务器调用全部 fail closed；生成参数与旧路径一致（AlwaysSpawn、OverrideRootScale、Owner/Instigator）。
- `UShooterWeaponDefinition` 新增 `FireBehavior`（Instanced，EditInlineNew）。
- `AShooterWeapon`：
  - `Fire()` 在权威弹药消费成功后调用新 `ExecuteFireAtTarget(TargetLocation)`；
  - `ExecuteFireAtTarget`：命中 Definition 时构建 Context 委托行为，否则走兼容 `FireProjectile`（注释明确 PvE / 旧测试遗留边界，B4 记录）；Montage / MulticastFX / Recoil / NPC 镜像统一留在 Actor 侧执行；
  - 新增 public `ResolveFireBehavior()`：行为只从绑定实例 DefinitionId 对应 Definition 解析，不读 WeaponActor CDO；
  - `FireProjectile` 保留旧弹丸生成实现，仅承担兼容路径。
- 工具测试为 WD_TestAuto 配置 `UShooterProjectileFireBehavior`（弹丸类为 Pistol 子弹，与 Rifle 常规弹丸刻意不同）。
- 新增测试 `Tests/Weapon/ShooterProjectileFireBehaviorAutomationTests.cpp`：
  - `FireBehavior.ProjectileSpawn`：合法服务器上下文恰好生成一个行为配置类的弹丸并使用 Context 变换；缺 WeaponActor、非服务器 Role、缺弹丸类均不生成；
  - `FireBehavior.DefinitionWiring`：Definition 授予的武器解析到 Definition 内的行为实例与弹丸类；适配入口（伪造 DefinitionId）授予的武器解析不到正式行为。

## 验证结果

- `BuildEditor.ps1`：Passed。
- `RunAutomation.ps1 -TestFilter ShootGame.Tools.WeaponDefinition`：Passed=1（WD_TestAuto 行为落盘）。
- `RunAutomation.ps1 -TestFilter ShootGame.Weapon`：Passed=4 Warnings=3 Failed=0（含新增 2 项；警告为无网格良性日志）。
- `RunAutomation.ps1 -TestFilter ShootGame.Inventory`：Passed=15 Failed=0。
- `RunAutomation.ps1 -TestFilter ShootGame.Ability.Fire`：Passed=16 Failed=0（GA_Fire / 单弹丸 / 弹药权威 / 取消链无回退）。
- "每次 Authority Commit 仅扣一发 + 客户端调用不生成 Gameplay 结果"的网络级验证保留在大阶段 A 收口回归（协调器既有覆盖）。

## 遇到的问题

- ProjectileSpawn 测试初版漏设 `Context.Instigator`（fail closed 直接返回，0 生成）；补上后又发现"缺 WeaponActor"用例因误设有效武器而变成合法上下文（计数 1/2 全部错位）。
- 编译小错：protected `ResolveFireBehavior` 供测试访问需移到 public；const 指针不能走 `TestNotNull` 重载。

## 处理方式

- 用例改为显式 `Context.WeaponActor = nullptr` 先测缺 Actor 再恢复，逐用例计数复核通过。
- `ResolveFireBehavior` 移入 public；const 断言改用 `TestTrue(x != nullptr)`。

## 遗留项

- `AShooterWeapon::FireProjectile` 旧弹丸路径与 NPC 旧弹药镜像保留为 PvE 遗留边界，B4 统一审计记录。
- Rifle / Pistol / AWP / GrenadeLauncher 的正式 Definition 行为配置在 A4 资产迁移时统一生成。
