# 迁移 Shooter 依赖的 FirstPerson 资产

## 日期

2026-08-14 20:02（Asia/Shanghai）

## 计划提交说明

`整理：迁移 Shooter 依赖的 FirstPerson 资产`

## 变更类型

- UE 资产迁移
- 引用修复
- 架构文档

## 目的

在删除旧 FirstPerson 模板前，将 Shooter 仍在使用的最小资产依赖闭包迁入职责明确的 Shooter 与 Shared 目录，解除 Shooter 对 `/Game/FirstPerson` 路径的目录外依赖。

本提交只迁移 4 个保留资产并更新引用，不删除 FirstPerson 地图、模板 GameMode、PlayerController 或 External Actor/Object。

## 本提交完成内容

- 通过 UE `AssetTools.rename_assets` 批量迁移并保持资产名称不变：
  - `ABP_FP_Copy` → `/Game/Variant_Shooter/Anims/Base`
  - `CtrlRig_FPWarp` → `/Game/Variant_Shooter/Anims/Base`
  - `BP_FirstPersonCharacter` → `/Game/Variant_Shooter/Blueprints/AnimationSupport`
  - `MI_FirstPersonColorway` → `/Game/Shared/Materials`
- UE 自动重写 `BP_ShooterCharacter`、`BP_ShooterNPC`、`ABP_FP_Weapon`、`ABP_FP_Pistol` 等直接引用者。
- 运行 `ResavePackages -fixupredirects`，重写材质引用涉及的 79 个包，其中包含 Shooter/FirstPerson World Partition External Actor、投射物、泡沫弹网格和原型目标资产。
- 更新 `Docs/FirstPerson清理与架构审计.md` 的迁移状态。

## 验证结果

- 迁移脚本逐项确认 4 个目标路径全部存在。
- UE Asset Registry 复查确认 4 个旧 FirstPerson 路径的目录外引用者均为 0，总数为 0。
- 完整验证命令：

  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Tests\RunAll.ps1 -Port 17794
  ```

- 完整验证结果：`Passed`，七个阶段全部通过：
  - Build
  - Automation（Passed=1，Failed=0）
  - Standalone
  - DedicatedNetwork（2 个客户端）
  - ListenNetwork（1 个远程客户端）
  - EmulatedNetwork（2 个客户端，`PktLag=100`、`PktLoss=2`）
  - DisconnectCleanup（两次检查均为 `Orphans=0`）
- Dedicated、Listen 和 EmulatedNetwork 均完成武器切换、弹药归属可见性、开火、伤害、死亡、复活、队伍计分、远端 Pitch 与 Montage 校验。
- 汇总文件：`Saved/Automation/Runs/20260814_195915/Summary.json`。
- 验证完成后无残留 `UnrealEditor`、`UnrealEditor-Cmd` 或 `ShootGame` 进程。

## 遇到的问题

- UE 5.6 Python `AssetTools` 不再暴露旧版 `fixup_referencers` 方法，因此不能在迁移脚本中直接删除 Redirector。
- `ResavePackages -fixupredirects` 成功重写 79 个引用包，但报告旧 `MI_FirstPersonColorway` Redirector 仍被未保存包引用，未将其删除。
- Headless Commandlet 在受限环境中持续报告 Derived Data Cache 无可写节点；该环境错误不影响资产迁移、Asset Registry 结果或随后完整自动化验证。

## 处理方式

- 资产移动使用 UE AssetTools，Redirector 处理改用引擎自带 `ResavePackages -fixupredirects`，全程不直接搬运或编辑二进制 `.uasset`。
- 不强制删除旧材质 Redirector；Asset Registry 已证明它没有 FirstPerson 目录外引用，因此暂留到下一提交随旧 FirstPerson 模板一起删除更安全。
- 第一次验证脚本未处理 `get_referencers` 在零引用时返回 `None` 的行为；修正为空集合后重跑通过，未重复执行资产迁移。

## 遗留项

- `/Game/FirstPerson/MI_FirstPersonColorway` 目前是只被 FirstPerson 内部包使用的 Redirector。
- FirstPerson 示例地图、模板蓝图、65 个 External Actor 和 1 个 External Object 尚未删除。
- 33 个 FirstPerson External Actor 因本次材质 Redirector 修复被重存；它们将在下一次模板删除提交中一并移除。
- `BP_FirstPersonCharacter` 保留原名称和语义，待编辑器 MCP 可用时再分析其在两套 Shooter 武器动画蓝图中的具体节点用途。
