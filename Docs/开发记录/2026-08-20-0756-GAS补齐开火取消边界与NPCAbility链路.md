# GAS 补齐开火取消边界与 NPC Ability 链路

- 日期：2026-08-20
- 计划提交说明：`GAS：补齐开火取消边界与 NPC Ability 链路`
- 变更类型：生产代码 / 测试

## 目的

执行 GA_Fire ServerOnly 执行计划子阶段 4C：补齐死亡、切枪、Weapon 销毁、断线和重生等生命周期清理，并让 NPC 通过同一 GA_Fire ServerOnly 规则开火，AI 只提交意图。

## 本提交完成内容

- `UShooterAbilitySystemComponent` 新增 `CancelAbilitiesByTag`，作为死亡 / 切枪 / 断线共用的幂等取消入口。
- `IShooterWeaponHolder` 增加最小只读接口 `GetCurrentWeapon()`；`AShooterCharacter` 与 `AShooterNPC` 实现，GA_Fire 统一通过该接口取得当前武器。
- 玩家生命周期：
  - `InitializeAbilityActorInfo` 在新 Avatar 初始化时防御性移除 `State.Dead` / `State.Firing`；
  - `Die` 先设置 `State.Dead`、取消 GA_Fire，再执行原有 Death Clear；
  - `ServerSwitchWeapon` 在切枪前取消旧 Weapon 对应的 GA_Fire；
  - `EndPlay` 在 ClearInventory 前取消 GA_Fire，覆盖断线 / 角色销毁。
- NPC 生命周期：
  - `StartShooting / StopShooting` 改为通过 ASC 提交 `Input.Fire` 按下 / 松开，不再直接调用 `Weapon::StartFiring / StopFiring`；
  - `Die` 设置 `State.Dead` 并取消 GA_Fire；`EndPlay` 幂等取消；
  - NPC Weapon 继续使用无 Inventory 的 `CurrentBullets` 兼容 Ammo。
- `UShooterGameplayAbility_Fire` 支持 NPC Avatar：CanActivate / Activate 同时校验 Character 与 NPC 的死亡状态、当前 WeaponActor、所有权、隐藏状态与弹药。
- Feature Tests 新增 `ShootGame.Ability.Fire.Reject.Dead / NoWeapon / NoAmmo`、`Cancel.SwitchWeapon / Death / Disconnect`、`NPC` 七个测试。
- 网络测试协调器新增 4C 验证：
  - 全自动保持中切枪：服务器观察到活动 GA，切枪后 0.5 秒内无新弹丸且步枪 Ammo 不再下降；
  - 弹药耗尽后服务器 TryActivate 被拒绝且无弹丸；
  - 死亡后 `State.Dead` 存在且无法再次激活；
  - 重生后 `State.Dead` / `State.Firing` 与活动 Ability 全部清空；
  - 重生 Inventory 为空时激活被拒绝；
  - 测试 NPC 通过 ASC 激活 GA_Fire 并生成权威弹丸，停止意图后 0.5 秒内无残留弹丸；
  - 断线阶段复用 `AUTOMATION_TEST_DISCONNECT_SUCCESS`，确认 EndPlay 取消后仍无孤儿武器。

## 验证结果

- 编译：`BuildEditor.ps1` Succeeded。
- Feature Tests：`RunAutomation.ps1 -TestFilter ShootGame` Passed=27 Warnings=0 Failed=0。
- Dedicated 双客户端：`Saved/Automation/Network/20260820_075202`，2 个成功标记，`FireGA=true/true/true`、`Cancellation=true/true/true/true/true/true/true`，无失败标记。
- Listen 一远程客户端：`Saved/Automation/Network/20260820_075238`，1 个成功标记，`Cancellation=true/...`，无失败标记。
- DisconnectCleanup：`Saved/Automation/Network/20260820_075310`，`AUTOMATION_TEST_DISCONNECT_SUCCESS ActiveWeapons=4 Orphans=0`。
- 本子阶段未运行 Full Regression；最终收尾时执行。

## 遇到的问题

- 首次编译失败：`FNativeGameplayTag` 没有 `IsValid()` 成员；测试协调器 4C 成员 `bClientReportedSwitchCancel` 重复定义；辅助函数参数 `Instigator` 与 `AActor::Instigator` 成员重名。
- Dedicated 首轮 0 成功标记：4C 切枪取消阶段会把 CurrentWeapon 切回初始手枪，而旧流程的早期门 `Weapon == WeaponBeforeSwitch` 仍然返回，导致服务器轮询卡死。
- 测试 NPC 默认没有 WeaponClass / ProjectileClass，无法验证 NPC 开火。

## 处理方式

- Tag 合法性检查改为 `GetTag().IsValid()`；删除重复成员；参数改名 `ProjectileInstigator`。
- 早期门增加 `!bSwitchCancelPhaseTriggered` 条件，4C 切枪取消阶段允许合法回到初始手枪。
- 为 `AShooterNetworkTestNPC` / `AShooterNetworkTestWeapon` 添加测试专用构造配置，分别设置测试武器类和 BP 子弹弹丸类，并保留单发模式以减少停火验证噪音。

## 遗留项

- 旧 `ServerStartFire / ServerStopFire` 仍保留且无输入调用者；4D 用 CodeGraph 确认后删除。
- 切枪 / 死亡 / 断线发生在同一帧附近的极端竞态仍未被专项覆盖。
- 客户端预测、预测弹药 HUD 与服务器倒带仍不在本阶段范围。
