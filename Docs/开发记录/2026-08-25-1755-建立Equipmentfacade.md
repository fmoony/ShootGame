# 建立 Equipment facade

- 日期：2026-08-25
- 计划提交说明：`装备：建立 Equipment facade`
- 变更类型：生产代码 / 测试

## 目的

执行《Shooter 核心玩法架构解耦重构执行计划》R3：建立 `UShooterEquipmentComponent` facade，让 Pickup、GA_Equip、AddWeaponClass 与网络测试协调器都通过 `Equipment->EquipWeapon(Id)` 提交装备，为 R4 一次性迁移 CurrentWeapon / ActiveWeaponInstanceId 权威做准备。

## 本提交完成内容

- 新增 `Source/ShootGame/Characters/Equipment/ShooterEquipmentComponent.h/.cpp`：
  - `EquipWeapon` facade 转发现有 `AShooterCharacter::CommitActiveWeapon`；
  - `GetActiveWeaponInstanceId()` / `GetCurrentWeaponActor()` 先读旧权威（Inventory / Character）；
  - 建立 `OnEquippedWeaponChanged` 动态多播事件，旧事务成功后发布；
  - R3 阶段组件不复制任何自有字段，复制结果保持原状。
- `AShooterCharacter`：构造函数创建 `EquipmentComponent`，`AddWeaponClass` 改为通过 facade 装备。
- `AShooterPickup`：拾取成功后的“立即装备”只调用 `Equipment->EquipWeapon`。
- `GA_Equip`：最终提交改调 `Equipment->EquipWeapon`。
- `ShooterNetworkTestCoordinator`：四处“创建后立即装备”测试路径改调 facade。
- `ShooterArchitectureBaselineAutomationTests` OwnershipSurface：新增 EquipmentComponent 存在且 R3 不复制自有字段的断言。

## 验证结果

- `git diff --check`：通过。
- `BuildEditor.ps1`：Passed（首次编译缺少完整类型 include，补齐后通过）。
- `RunAutomation.ps1 -TestFilter ShootGame`：65 Passed / 1 Warning / 0 Failed。
- `RunAutomation.ps1 -TestFilter ShootGame.Architecture`：2 Passed / 1 Warning / 0 Failed；WeaponGrantSurface 日志确认 `Equipment facade transaction completed`，即真实 World 测试已通过 facade。
- 未运行网络多进程回归：R3 是 facade 无字段迁移，后续 R4 网络门一起覆盖。

## 遇到的问题

- `ShooterNetworkTestCoordinator.cpp` 只包含 `ShooterCharacter.h`（前向声明），调用 `GetEquipmentComponent()->EquipWeapon` 时报“使用了未定义类型 UShooterEquipmentComponent”。

## 处理方式

- 在协调器 cpp 中显式 include `Characters/Equipment/ShooterEquipmentComponent.h`，与项目 include 顺序约定一致。

## 遗留项

- `AShooterCharacter::CommitActiveWeapon` 仍是旧事务实现，R4 迁入 Equipment 后改为兼容转发。
- `Character.HandleWeaponAddedToInventory` 仍保留为旧蓝图兼容入口，R8 按调用方清理。
