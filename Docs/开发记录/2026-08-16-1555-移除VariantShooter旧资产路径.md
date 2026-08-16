# 移除 Variant_Shooter 旧资产路径

- 日期：2026-08-16
- 计划提交说明：`清理：移除 Variant_Shooter 旧资产路径`
- 变更类型：文档

## 目的

执行 [Shooter Content Browser 资产目录整理执行方案](../执行计划/Shooter_Content_Browser_资产目录整理执行方案.md) 阶段 5：清理 `/Game/Variant_Shooter` 旧路径，使其不再作为有效资产根存在，并同步剩余当前有效文档中的旧路径引用。

## 本提交完成内容

- 删除 `/Game/Variant_Shooter` 空目录结构（Anims/Blueprints/FX/Input/UI 及其子目录，迁移后已无任何文件）。
- 更新当前有效文档中的旧路径：
  - `Shooter模板蓝图分析.md`：引言 `/Game/Variant_Shooter` → `/Game/Shooter`。
  - `Agent自动化验证操作手册.md`：默认网络地图 `/Game/Variant_Shooter/Lvl_Shooter` → `/Game/Shooter/Maps/Lvl_Shooter`（按 GBK 编码精确替换，保持文件编码）。

## 验证结果

- 执行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Tests\RunAll.ps1 -Port 17794`，七阶段（Build / Automation / Standalone / DedicatedNetwork / ListenNetwork / EmulatedNetwork / DisconnectCleanup）全部 Passed。
- 实际结果文件：`Saved/Automation/Runs/20260816_155125/Summary.json`，顶层 `status: Passed`、`error: null`。
- 全仓库扫描：`Source/Config/Scripts` 无 `/Game/Variant_Shooter` 引用；`Content/Variant_Shooter` 目录已删除。

## 遇到的问题

无。

## 处理方式

无。

## 遗留项

- 历史开发记录与执行方案文档中的 `/Game/Variant_Shooter` 历史原文保持不变（按规范不重写历史）。
- `FirstPerson清理与架构审计.md` 中的历史迁移表（2026-08-14 资产迁入 Variant_Shooter 的记录）保留为历史事实。
