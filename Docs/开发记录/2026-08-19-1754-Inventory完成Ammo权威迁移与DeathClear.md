# Inventory：Ammo 权威迁移、Death Clear 与旧 Fire 兼容

- 日期：2026-08-19
- 计划提交说明：`Inventory：完成 Ammo 权威迁移与 Death Clear`
- 变更类型：生产代码 / 测试 / 文档

## 目的

完成 Inventory 第二阶段最后一个子阶段 2D：把 Ammo 权威位置迁移到 WeaponInstanceData，补齐死亡清空与重生为空，并保持旧 Fire 路径继续工作。

## 本提交完成内容

- `FShooterWeaponInventoryList`：
  - 新增 `ConsumeMagazineAmmo`；
  - 新增 Owner Client FastArray Add/Change/Remove 通知委托。
- `UShooterInventoryComponent`：
  - 绑定 FastArray 通知并刷新 WeaponActor 弹药镜像；
  - 新增 `GetMagazineAmmo`、`GetReserveAmmo`、`CanConsumeMagazineAmmo`、`ConsumeMagazineAmmo`；
  - `ClearInventory` 与 `RemoveWeaponInstance` 同步销毁 BoundWeaponActor。
- `AShooterWeapon`：
  - `CurrentBullets` 明确为 OwnerOnly 兼容镜像；
  - 新增 `RefreshAmmoMirror`、`CanConsumeAmmo`、`ConsumeAmmo`；
  - 绑定 Inventory 时 `GetBulletCount` 从 `MagazineAmmo` 读取；
  - 旧 Fire 通过 Inventory 消费弹药，耗尽后停火且不自动换弹；
  - 未绑定 Inventory 的 NPC 旧路径保留原 CurrentBullets 兼容行为。
- `AShooterCharacter`：
  - `Die` 执行 Death Clear：停火 -> 销毁 WeaponActor -> Inventory Clear -> Active Invalid -> CurrentWeapon null；
  - `EndPlay` 复用同一回收入口；
  - 重生 Pawn 的 Inventory 自然为空。
- 网络测试协调器：
  - 验证初始 Ammo、开火扣减与 Ammo 隔离；
  - 验证服务器/拥有者客户端 Death Clear；
  - 验证服务器/拥有者客户端 Respawn Empty；
  - 删除旧 `AddWeaponClass` 测试补枪路径，避免死亡后重新装备武器。
- 自动化测试新增 `ShootGame.Inventory.AmmoConsume`。
- 更新架构文档的 2D 已落地事实。

## 验证结果

- `Scripts/Tests/BuildEditor.ps1`：通过。
- `Scripts/Tests/RunAutomation.ps1 -TestFilter ShootGame`：Passed=11 Warnings=0 Failed=0 NotRun=0。
- Dedicated 双客户端：通过，2 个成功标记，均含 `AmmoIsolation=true DeathClear=true/true RespawnEmpty=true/true`。
  - 日志：`Saved/Automation/Network/20260819_175215`
- Listen 单客户端：通过，1 个成功标记，含 `AmmoIsolation=true DeathClear=true/true RespawnEmpty=true/true`。
  - 日志：`Saved/Automation/Network/20260819_175252`
- 本子阶段未单独运行完整七阶段 `RunAll.ps1`。

## 遇到的问题

- FastArray Entry 回调最初在 `FShooterWeaponInventoryList` 未完整定义时以内联方式调用其方法，编译报不完整类型错误。
- 回调移到头文件定义后，非 `FORCEINLINE` 导致链接期 LNK2005 多重定义。
- 死亡清空后网络协调器仍保留旧 `AddWeaponClass` 补枪分支，PollServerState 在 `CurrentWeapon == null` 时重新装备武器，造成 DeathClear 断言失败。
- 客户端 PollClientState 顶部原先 `!Weapon` 即返回，导致死亡/重生阶段永远无法上报 Inventory 清空与重生为空。

## 处理方式

- Entry 回调改为先声明、后 `FORCEINLINE` 定义。
- 删除网络协调器的旧补枪分支；无 CurrentWeapon 时继续执行死亡/重生检测。
- 客户端 PollClientState 拆分为“有武器阶段”与“死亡/重生阶段”，仅把切换、开火和弹药观察包在 `if (Weapon)` 内。
- 所有修复后重新 Build、完整 `ShootGame` Automation 与 Dedicated / Listen 网络场景，均通过。

## 遗留项

- GA_Fire 尚未实现，阶段结束按计划暂停。
- Reload 未实现；MagazineAmmo 耗尽后停火，不自动补弹。
- ReserveAmmo 当前仍由 WeaponClass 估算兼容桥接，等待正式 WeaponDefinition。
- WeaponActor 生命周期仍为 Spawn/Destroy，尚未决定是否需要 Actor Pool。
- 真实地图 Pickup Overlap 的自动化覆盖仍以授予 API 等价验证为主。
