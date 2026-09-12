# Inventory 移除 InstanceId 并以 WeaponActor 管理背包装备

- 日期：2026-09-12
- 计划提交说明：`Inventory：移除 InstanceId 并以 WeaponActor 管理背包装备`
- 变更类型：生产代码 / 测试

## 目的

完成 [武器启动预配置与实体池简化重构方案](../执行计划/武器启动预配置与实体池简化重构方案.md) S3：
删除 Inventory / Equipment / WeaponActor 上的运行时实例身份，让 FastArray Entry 只保存
WeaponActor 引用与 SlotIndex，Equipment 只保存 CurrentWeaponActor，GAS Ability 全部围绕 Actor 工作。

## 本提交完成内容

- `FShooterInventoryWeaponEntry` 改为 `Weapon + SlotIndex`，FastArray 继续 OwnerOnly；
- `UShooterInventoryComponent` 删除 InstanceId / RowName 授予与查询、Add/Remove/Clear 改为接收
  WeaponActor 并归还 WeaponRuntimeSubsystem；
- `UShooterEquipmentComponent` 删除 `ActiveWeaponInstanceId`，`EquipWeapon` 改为接收 Actor；
- `GA_Reload / GA_Equip` 改为按 CurrentWeaponActor / Inventory Actor+Slot 工作；
- `ShooterGameMode` 断线检查改用 WeaponRuntimeSubsystem 的 IsPooled；
- 同步更新 Inventory、Equipment、Ability、Network Coordinator 与 Architecture 自动化测试；
- 新增 `InitializeWeaponRuntimeForTest` / `GetWeaponTableOverride` 等测试专用入口。

## 验证结果

- `Scripts/Tests/BuildEditor.ps1`：通过；
- `Scripts/Tests/RunAutomation.ps1`：通过，79 项成功（含 38 项带警告），Failed=0、NotRun=0；
- 报告：`Saved/Automation/Reports/20260912_121622_ShootGame/index.json`。

## 遇到的问题

无。

## 处理方式

无。

## 遗留项

- S4 的 Pickup WeaponId 资产迁移、NPC 入池与通用池删除尚未实施；
- 完整七阶段回归按计划延后到 S5。
