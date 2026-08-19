# GAS 将玩家开火迁移为 ServerOnly Ability

- 日期：2026-08-20
- 计划提交说明：`GAS：将玩家开火迁移为 ServerOnly Ability`
- 变更类型：生产代码 / 测试

## 目的

执行 GA_Fire ServerOnly 执行计划子阶段 4B：把玩家 Enhanced Input 的实际开火入口从旧 Server RPC 切换到 ASC → GA_Fire（ServerOnly），复用现有 WeaponActor、Inventory Ammo、服务器弹丸、Damage 与 GAS Health 闭环。

## 本提交完成内容

- `AShooterCharacter::DoStartFiring / DoStopFiring` 改为向 `UShooterAbilitySystemComponent` 提交 `Input.Fire` 按下/松开；不再调用旧 RPC，也不保留运行时双路径。
- `UShooterGameplayAbility_Fire` 接入真实开火：
  - 服务器 CanActivate：Avatar 是 ASC 当前 Avatar、角色未死亡、CurrentWeapon 有效且属于该角色、武器未隐藏且有可消耗 Ammo；客户端本地预检只检查 Avatar，完整校验留给服务器。
  - 服务器 Activate：缓存 WeaponActor、绑定 OutOfAmmo 回调并调用 `Weapon::StartFiring()`。
  - `InputReleased`：服务器收到 ASC 可靠释放 RPC 后停止 Weapon 并 EndAbility。
  - `EndAbility`：幂等解绑 OutOfAmmo、`Weapon::StopFiring()`，再调用 Super 移除 `State.Firing`。
- `AShooterWeapon` 新增 `OnOutOfAmmo` 委托；`Fire()` 在 Inventory Ammo 耗尽时停止计时器并广播，GA_Fire 收到后立即 EndAbility。
- `UShooterAbilitySystemComponent` 新增活动 Ability 计数查询，供测试证明保持期间只有一个活动 GA_Fire。
- 网络测试协调器新增 4B 验证：
  - 单次按下/松开恰好生成 1 颗权威弹丸、只扣 1 发对应 WeaponInstance Ammo；
  - 全自动保持期间服务器只观察到一个活动 GA_Fire；
  - 保持到至少再打出 2 发后松开，0.5 秒静默期内弹丸与 Ammo 不再变化，证明 Weapon 计时器无残留；
  - 成功/超时标记新增 `FireGA` 字段。
- Feature Tests 新增 `ShootGame.Ability.Fire.ServerOnly`、`SingleActivation`、`SingleProjectile`、`AmmoConsume`、`FullAutoRelease`（网络行为由协调器执行，静态测试守住配置与入口边界）。
- 旧 `ServerStartFire / ServerStopFire` 声明和实现仍保留，但已无输入调用者，按计划留到 4D 删除。

## 验证结果

- 编译：`BuildEditor.ps1` Succeeded。
- Feature Tests：`RunAutomation.ps1 -TestFilter ShootGame` Passed=20 Warnings=0 Failed=0。
- Dedicated 双客户端：`Saved/Automation/Network/20260820_073952`，2 个成功标记，`FireGA=true/true/true`，单发 Projectiles=1、全自动 Projectiles=1->3、Ammo 40->37，无失败标记。
- Listen 一远程客户端：`Saved/Automation/Network/20260820_074030`，1 个成功标记，`FireGA=true/true/true`，无失败标记。
- 本子阶段未运行 Full Regression；最终收尾时执行。

## 遇到的问题

- 首次编译失败 3 项：`bRetriggerInstancedAbility` 在 UE 5.6 是 protected 成员；GA_Fire cpp 使用不完整类型 `UAbilitySystemComponent`；测试协调器局部变量与成员 `bSingleProjectileVerified` 重名。
- 全自动验证需要证明“保持期间单活动 Ability”与“释放后计时器无残留”，但旧流程按下/松开在同一客户端帧内完成，观察不到中间态。

## 处理方式

- 在 GA_Fire 增加只读 `CanRetriggerInstancedAbility()` 测试接口；显式 include `AbilitySystemComponent.h`；局部变量改为 `bSingleShotVerified`，成员只在验证块中赋值。
- 网络流程拆为两阶段：先验证单次按下只产生一发，再由服务器设置 `bServerReadyForFullAuto`，客户端保持开火至 Ammo 再降 2 发后松开；服务器记录弹丸/弹药快照并等待 0.5 秒静默期，确认无残留计时器后进入伤害阶段。

## 遗留项

- 死亡、切枪、断线等取消边界尚未接入 GA 生命周期；4C 完成。
- NPC 的 StartShooting / StopShooting 仍直接调用 WeaponActor；4C 迁移到 ASC。
- 旧开火 RPC 尚未删除；4D 处理。
- 客户端仍没有本地预测，P0 输入到服务器确认延迟作为后续对照数据保留。
