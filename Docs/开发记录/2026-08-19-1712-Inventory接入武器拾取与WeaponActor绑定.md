# Inventory：接入武器拾取与 WeaponActor 绑定

- 日期：2026-08-19
- 计划提交说明：`Inventory：接入武器拾取与 WeaponActor 绑定`
- 变更类型：生产代码 / 测试 / 文档

## 目的

完成 Inventory 第二阶段子阶段 2B：把 Pickup 最终授予路径从旧的 `AddWeaponClass` 迁移到 Inventory，并由服务器创建 WeaponInstance 与 WeaponActor 绑定关系。

## 本提交完成内容

- `UShooterInventoryComponent`：
  - 新增 `TryAddWeapon(WeaponClass)`，服务器自动选择空 Slot、创建 `FShooterWeaponInstanceData`、Spawn WeaponActor 并写入 `BoundInstanceId`；
  - 新增 Slot 上限、重复 Definition 与 SlotFull 明确 Reject；
  - 新增 InstanceId -> WeaponActor 绑定注册与查找。
- `AShooterWeapon`：
  - 新增 `BoundInstanceId`，`COND_OwnerOnly` 复制；
  - `InitializeWeaponOwner` 与 `OnRep_BoundInstanceId` 会向 Inventory 注册绑定。
- `AShooterCharacter`：
  - 新增 `HandleWeaponAddedToInventory` 薄桥接：更新兼容列表、装备新武器并设置 Active；
  - `OwnedWeapons` 明确标记为兼容镜像。
- `AShooterPickup`：
  - 改为调用 Inventory 授予路径；
  - 只在 `Added` 时消费 Pickup，重复定义 / SlotFull 时保持可用；
  - 连续 Overlap 由服务器端 `bPickupAvailable` 闸门去重。
- 网络测试协调器：
  - 改为通过 `TryAddWeapon` 创建步枪和手枪；
  - 验证 SingleGrant、SlotFull、WeaponActorBinding；
  - Owner Client 直接调用授予入口必须返回 `NotAuthoritative`。
- 自动化测试新增：
  - `ShootGame.Inventory.WeaponActorBinding`
  - `ShootGame.Inventory.Pickup.ServerAuthority`
- 更新 `Docs/架构/Inventory与武器数据架构.md` 的 2B 已落地事实。

## 验证结果

- `Scripts/Tests/BuildEditor.ps1`：通过。
- `Scripts/Tests/RunAutomation.ps1 -TestFilter ShootGame.Inventory`：Passed=7 Warnings=0 Failed=0 NotRun=0。
- `Scripts/Tests/RunAutomation.ps1 -TestFilter ShootGame`：Passed=9 Warnings=0 Failed=0 NotRun=0。
- Dedicated 双客户端（`-ShootGameNetworkTest`）：通过，2 个成功标记，均含 `PickupAuthority=true InventoryOwner=true InventoryRemoteHidden=true`。
  - 日志：`Saved/Automation/Network/20260819_171058`
- Listen 单客户端（`-ShootGameNetworkTest -ShootGameSkipRemoteMontage`）：通过，1 个成功标记。
  - 日志：`Saved/Automation/Network/20260819_171142`
- 本子阶段未运行完整七阶段 `RunAll.ps1`。

## 遇到的问题

- 首次编译失败 C3861：测试中使用全局 `FindFunction`，当前编译单元不可用。
- 自动化网络会话未直接驱动地图中真实 Pickup Actor 的 Overlap，而是通过测试协调器调用最终授予入口；真实 Pickup Overlap 路径靠编译与代码路径审计覆盖。

## 处理方式

- 改用 `UClass::FindFunctionByName` 检查 `TryAddWeapon` 不是 UFUNCTION。
- 保留 Pickup Actor 的服务器端授权、去重与 Reject 逻辑；授予链路本身在 Dedicated / Listen 中由协调器按同一条 Inventory API 验证。

## 遗留项

- 真实地图 Pickup 的 Overlap 自动化覆盖留待人工 PIE 或后续测试入口补强。
- ActiveWeaponInstanceId 切换闭环尚未实现（2C）。
- Ammo 权威消费、Death Clear、旧 Fire 兼容迁移尚未实现（2D）。
- WeaponDefinition 仍以 WeaponClass 名构造 DefinitionId 作为兼容桥接；InitialReserveAmmo 暂按弹匣容量估算。
- `AShooterPickup` 的旧 `IShooterWeaponHolder` include 尚未清理，等 2D 收尾时统一处理。
