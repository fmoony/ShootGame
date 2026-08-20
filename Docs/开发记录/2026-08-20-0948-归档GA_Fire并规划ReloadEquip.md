# 归档 GA_Fire 并规划 Reload Equip

- 日期：2026-08-20
- 计划提交说明：Docs：归档 GA_Fire 并规划 Reload 与 Equip
- 变更类型：文档

## 目的

收尾已经完成但尚未归档的 Inventory 第二阶段与 GA_Fire ServerOnly 计划，使路线导航与当前代码状态一致；同时为下一阶段建立基于项目现状、Epic 官方资料和本地 UE 5.6 实现三方对照的可执行计划。

## 本提交完成内容

- 将 Inventory 第二阶段执行计划移动到 Docs/已完成计划；
- 将 GA_Fire ServerOnly 执行计划移动到 Docs/已完成计划，并标记为已完成；
- 新建 GA_Reload 与 GA_Equip ServerOnly 执行计划，冻结 Ammo 原子事务、Ability 权威时钟、GameplayTag 互斥、提交点、NPC 边界、四个子阶段和自动化验收；
- 更新 Shooter 完整 Demo 最终路线规划，将当前阶段从 GA_Fire 调整为 GA_Reload / GA_Equip；
- 更新 AGENTS.md 文档导航；
- 将此前已确认的 Agent 外部研究约束纳入 AGENTS.md：新增架构必须先核对项目基线，再查询官方资料，并用本地 UE5.6 源码确认版本行为。

## 验证结果

- 使用 CodeGraph 核对当前 GA_Fire、Inventory Ammo、CurrentWeapon、ServerSwitchWeapon 和 Ability 授予链路；
- 查询 Epic 官方 GAS、Lyra Inventory/Equipment、Lyra Ability 与 Gameplay Ability Task 文档；
- 限定范围读取本地 UE 5.6 GameplayAbilities 源码，确认 GiveAbility 权威限制、WaitDelay 及 PlayMontageAndWait 的完成/打断/取消语义；
- 使用资产文件名审计确认仓库存在 Pistol/Rifle 的 Reload 与 Equip 候选资源；
- 执行 git diff --check，通过；
- 本提交仅修改文档，未重新编译生产代码；采用最近一次完整回归结果作为现有代码基线：

~~~text
Saved/Automation/Runs/20260820_080029/Summary.json
status = Passed
~~~

## 遇到的问题

- 当前会话没有可调用的 Unreal 只读资产 MCP 工具，无法在本次文档提交中核对候选 Montage 的 Skeleton、Slot 与第一/第三人称兼容性；
- git status 显示 ShooterGameMode.h、ShooterPlayerController.cpp 和 ShooterPlayerController.h 为修改状态，但 git diff 没有对应内容差异。

## 处理方式

- 计划中将 Montage 标记为“候选资源”，并把编辑器只读验证设为 5D 接入前强制门槛；服务器事务不依赖动画资源成功；
- 暂存时使用明确文件列表，不暂存上述无内容差异的源码状态。

## 遗留项

- 尚未实施 GA_Reload 与 GA_Equip 的生产代码；
- 候选 Montage 必须在编辑器连接可用后验证，不能仅根据文件名接入；
- NPC 当前没有 Inventory 与 ReserveAmmo，本阶段不授予 GA_Reload / GA_Equip；未来出现 NPC 多武器需求时单独设计；
- 完成 ServerOnly 阶段后仍需根据弱网数据单独规划 P1 Local Predicted 范围。
