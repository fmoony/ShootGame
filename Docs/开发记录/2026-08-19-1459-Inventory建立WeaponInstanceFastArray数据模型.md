# Inventory：建立 WeaponInstance FastArray 数据模型

- 日期：2026-08-19
- 计划提交说明：`Inventory：建立 WeaponInstance FastArray 数据模型`
- 变更类型：生产代码 / 测试 / 文档

## 目的

完成 `Docs/执行计划/Inventory第二阶段_基础闭环执行计划.md` 的子阶段 2A：
建立 Inventory 逻辑数据模型、WeaponInstance FastArray、OwnerOnly 复制与 ActiveWeaponInstanceId 数据字段。
本提交不接 Pickup、不创建 WeaponActor、不执行切换表现，也不迁移 Ammo。

## 本提交完成内容

- 新增 `Source/ShootGame/Inventory/ShooterInventoryTypes.h`：
  - `FShooterWeaponInstanceData`：`FGuid InstanceId`、`FPrimaryAssetId DefinitionId`、`MagazineAmmo`、`ReserveAmmo`、`SlotIndex`。
  - `FShooterWeaponInstanceEntry : FFastArraySerializerItem`：自定义 `NetSerialize` 覆盖全部字段。
  - `FShooterWeaponInventoryList : FFastArraySerializer`：关闭 Struct Delta 路径，提供 Add / Remove / Clear / Find，并正确调用 `MarkItemDirty` / `MarkArrayDirty`。
- 新增 `Source/ShootGame/Inventory/ShooterInventoryComponent.h/.cpp`：
  - `UShooterInventoryComponent` 持有 `ReplicatedInventory` 与 `ActiveWeaponInstanceId`。
  - 两个复制属性均注册为 `COND_OwnerOnly`。
  - 服务器权威 Add / Remove / Clear / Find / SetActive 入口；新增校验 InstanceId 唯一、SlotIndex 唯一与数据合法性。
- 修改 `AShooterCharacter`：
  - 构造时创建 `UShooterInventoryComponent`；
  - 暴露 `GetInventoryComponent()` 最薄宿主入口。
- 修改 `ShootGame.Build.cs`：加入 `NetCore` 依赖与 `ShootGame/Inventory`、`ShootGame/Tests/Inventory` include 路径。
- 新增 `Source/ShootGame/Tests/Inventory/ShooterInventoryAutomationTests.cpp`：
  - `ShootGame.Inventory.AddWeapon`
  - `ShootGame.Inventory.UniqueInstanceId`
  - `ShootGame.Inventory.SlotUniqueness`
  - `ShootGame.Inventory.OwnerReplication`
  - `ShootGame.Inventory.RemoteHidden`
- 扩展 `ShooterNetworkTestCoordinator`：
  - 服务器测试入口插入两把测试武器并设置 Active 为第一把；
  - Owner 客户端确认 Inventory 数量为 2 且 Active ID 与服务器一致；
  - Owner 客户端确认远端角色的 Inventory 数量为 0（完整列表不向非 Owner 复制）；
  - 成功标记新增 `InventoryOwner=true InventoryRemoteHidden=true`。
- 提交计划文档 `Docs/执行计划/Inventory第二阶段_基础闭环执行计划.md`。

## 验证结果

- `Scripts/Tests/BuildEditor.ps1`：通过。
- `Scripts/Tests/RunAutomation.ps1 -TestFilter ShootGame.Inventory`：Passed=5 Warnings=0 Failed=0 NotRun=0。
- `Scripts/Tests/RunAutomation.ps1 -TestFilter ShootGame`：Passed=7 Warnings=0 Failed=0 NotRun=0。
  - 报告：`Saved/Automation/Reports/20260819_145807_ShootGame/index.json`
- Dedicated 双客户端（`RunNetworkSession.ps1 -ShootGameNetworkTest`）：通过，2 个 `AUTOMATION_TEST_CLIENT_SUCCESS`，均含 `InventoryOwner=true InventoryRemoteHidden=true`。
  - 日志：`Saved/Automation/Network/20260819_145830`
- Listen 单客户端（`RunNetworkSession.ps1 -ShootGameNetworkTest -ShootGameSkipRemoteMontage`）：通过，1 个 `AUTOMATION_TEST_CLIENT_SUCCESS`，含 `InventoryOwner=true InventoryRemoteHidden=true`。
  - 日志：`Saved/Automation/Network/20260819_145902`
- 本子阶段未运行完整七阶段 `RunAll.ps1`；2A Stage Gate 按计划只覆盖 Dedicated 与 Listen。

## 遇到的问题

- 首次编译失败 C2665：自动化测试使用 `TestEqual` 比较 `TObjectPtr<UScriptStruct>` 与 `UScriptStruct*`，模板无法匹配。
- 最初尝试在 Automation 中直接调用 CDO 的 `GetLifetimeReplicatedProps` 来反射验证 `COND_OwnerOnly`，触发 `Condition == Other.Condition` 断言：类运行时复制数据尚未建立时 `FProperty::RepIndex` 为 0，直接登记会与父类属性冲突。
- 命令行验证时 Git Bash 将 `/Game/...` 地图路径转换为 `D:/Git/Game/...`；PowerShell 数组参数还被拼成单个 `-ShootGameNetworkTest,-ShootGameSkipRemoteMontage` 参数。

## 处理方式

- 将结构体类型断言改为 `TestTrue` 的指针相等比较。
- 移除直接调用 `GetLifetimeReplicatedProps` 的测试方式：Automation 只验证 `CPF_Net`、RepNotify、FastArray 序列化契约；`COND_OwnerOnly` 的实际网络行为交由网络测试协调器在 Dedicated / Listen 会话中验证。
- 网络脚本调用使用 `MSYS_NO_PATHCONV=1`，Listen 多参数使用 PowerShell `-Command` 与 `@('-ShootGameNetworkTest','-ShootGameSkipRemoteMontage')` 正确传参。
- 所有修复后重新执行 Build、完整 `ShootGame` Automation 与 Dedicated / Listen 网络场景，均通过。

## 遗留项

- Pickup 尚未接入 Inventory（留待 2B）。
- 尚未创建并绑定 WeaponActor，`BoundInstanceId` 未建立（留待 2B）。
- ActiveWeaponInstanceId 目前只做数据复制，未连接切换与 `Character.CurrentWeaponActor` 表现（留待 2C）。
- Ammo 权威迁移、死亡清空、旧 Fire 兼容未在本子阶段实施（留待 2D）。
- `FPrimaryAssetId` 不是 UPROPERTY 兼容类型，当前由 FastArray Entry 的自定义 `NetSerialize` 负责复制；后续 WeaponDefinition 正式化时需保持该路径或显式迁移。
- 未运行完整七阶段 `RunAll.ps1`，后续子阶段稳定后按计划执行。
