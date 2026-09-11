# Inventory 切换 Definition 授予入口（A2）

- 日期：2026-09-11
- 计划提交说明：`武器：Inventory 新增 Definition 生产授予入口并保留 WeaponClass 适配`
- 变更类型：生产代码 / 测试 / 资产

## 目的

实施《武器与 Inventory 正式架构实施计划》大阶段 A2：Inventory 授予数据源从 WeaponClass CDO 切换到正式 WeaponDefinition。新增按 Definition 授予的唯一生产入口，实例弹药与 WeaponActorClass 完全由 Definition 决定；保留 WeaponClass 适配入口仅供旧测试与资产迁移，标记 A4 删除。

## 本提交完成内容

- `UShooterInventoryComponent`：
  - 新增 `TryAddWeaponDefinition(const UShooterWeaponDefinition*, FGuid&)` 生产入口：校验 `IsValidForGrant()`（非法配置返回新枚举值 `InvalidDefinition`），DefinitionId / MagazineAmmo / ReserveAmmo / WeaponActorClass 全部来自 Definition；
  - 原 `TryAddWeapon(WeaponClass)` 保留为适配入口，注释明确"以 WeaponClass 名伪造 DefinitionId、生产路径禁止调用、A4 删除"；
  - 两个入口收敛到共用私有事务 `TryAddWeaponInternal`（重复定义 / Slot 选取 / 实例写入 / Actor 生成 / 失败回滚）；
  - `ReloadMagazine` 事务容量优先 `ResolveDefinitionSync(Instance->DefinitionId)->AmmoConfig.MagazineSize`，Definition 不可解析时（适配入口伪造 ID）回落 WeaponActor CDO，A4 删除适配入口后一并移除回落；
  - `EShooterInventoryAddResult` 末尾追加 `InvalidDefinition`。
- 工具测试修复：已存在的 Definition 资产必须先 `LoadObject` 完整加载再改写；WD_TestAuto 容量调整为弹匣 7 / 备弹 21，与 Rifle CDO 刻意不同以证明换弹容量边界来自 Definition。
- 新增测试 `Tests/Inventory/ShooterInventoryDefinitionGrantAutomationTests.cpp`：
  - `DefinitionGrant.Initialize`：实例数据（DefinitionId / 弹匣 / 备弹 / Slot）与 WeaponActor（类 / 绑定 / 隐藏）全部由 Definition 驱动；
  - `DefinitionGrant.Rejection`：空指针、非法弹匣配置、重复定义、SlotFull 全部拒绝且不残留实例；
  - `DefinitionGrant.ReloadCapacity`：WD_TestAuto（7/21）消耗两发后换弹只补到 7、备弹 19，证明容量来自 Definition 而非 WeaponActor CDO。
- FastArray Payload、OwnerOnly 条件、Slot 与弹药事务未改动。

## 验证结果

- `BuildEditor.ps1`：Passed。
- `RunAutomation.ps1 -TestFilter ShootGame.Tools.WeaponDefinition`：Passed=1（WD_TestAuto 更新落盘）。
- `RunAutomation.ps1 -TestFilter ShootGame.Inventory`：Passed=15 Warnings=8 Failed=0（含新增 3 项；警告为测试角色无网格的既有良性日志与故意触发的解析失败日志）。
- `RunAutomation.ps1 -TestFilter ShootGame.Equipment`：Failed=0（Passed=1 Warnings=9，警告为既有无网格良性日志）。
- `RunAutomation.ps1 -TestFilter ShootGame.Ability`：Passed=34 Failed=0。
- OwnerOnly 复制的真实网络验证保留在大阶段 A 收口回归（网络协调器既有覆盖）。

## 遇到的问题

- 工具测试在资产已存在时的更新路径直接 `CreatePackage + NewObject`，保存时报 Critical error："Asset cannot be saved as it has only been partially loaded"，进程 exit code 3，且后续 Inventory 测试拿到旧资产值（30/90）导致 ReloadCapacity 断言失败。
- 编译小错：`Test.NotNull` 应为 `Test.TestNotNull`。

## 处理方式

- 更新路径改为先 `LoadObject` 完整加载已存在资产并复用其 Outermost，仅新建路径走 `CreatePackage + NewObject`；重跑工具后 WD_TestAuto 值更新为 7/21，ReloadCapacity 测试通过。
- 修正方法名拼写。

## 遗留项

- `TryAddWeapon` 适配入口与 `ReloadMagazine` 的 CDO 容量回落将在 A4 资产迁移完成后删除。
- 生产路径（Pickup）仍调用适配入口，A4 一并切换到 Definition 软引用。
