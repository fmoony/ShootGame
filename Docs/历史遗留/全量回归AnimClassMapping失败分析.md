# 全量回归 AnimClassMapping 失败分析

- 调查日期：2026-09-09
- 调查范围：读取源码、历史提交、已有报告；独立 UE 进程只读加载资产；聚焦重现单项测试。
- 当前 HEAD：`51d6d617f52f8da1fe8e7bb40c5e2ad2c45d7e37`。
- 本轮没有修改生产代码、测试断言、蓝图配置或动画资产，没有实施修复。

## 1. 结论

**已确定的直接原因：Rifle 武器类默认配置与 2026-08-29 冻结的测试快照不一致。** 当前 `BP_ShooterWeapon_Rifle` 的 `FirstPersonAnimInstanceClass` 是 `ABP_FP_Rifle_C`，测试要求它与 `ABP_FP_Weapon_C` 为同一个 UClass。两者不同，且都能正常加载，所以失败的是类映射一致性断言，不是资产找不到、蓝图编译错误或网络连接失败。

结合历史记录和提交状态，**最有证据支持的解释是第一人称 Rifle 动画演进后，冻结测试基线没有同步更新**。这是对演进意图的判断；是否将 ABP_FP_Rifle 正式定为新验收基线，仍取决于该动画工作的验收结论，不能只因测试变红就自动修改期望。

Rifle 武器资产与 HEAD 的 Git blob 完全相同，当前测试文件也仍沿用旧提交。因此不能继续把此失败描述为“用户尚未提交的 Rifle 武器资产改动”，也不能归因于本轮 MCP 或 NewAnimBlueprint。

## 2. 当前值与测试期望

2026-09-09 用 `UnrealEditor-Cmd` 的 Python Commandlet 加载磁盘资产，读取武器 CDO 的两个 UPROPERTY；没有执行 compile/save/set_editor_property。

| 武器 / 视角 | 测试期望类 | 当前 CDO 类 | 结果 |
|---|---|---|---|
| Rifle FP | `/Game/Shooter/Animation/FirstPerson/ABP_FP_Weapon.ABP_FP_Weapon_C` | `/Game/Shooter/Animation/FirstPerson/ABP_FP_Rifle.ABP_FP_Rifle_C` | 不匹配 |
| Rifle TP | `/Game/Shooter/Animation/ThirdPerson/ABP_TP_Rifle.ABP_TP_Rifle_C` | 同期望 | 匹配 |
| Pistol FP | `/Game/Shooter/Animation/FirstPerson/ABP_FP_Pistol.ABP_FP_Pistol_C` | 同期望 | 匹配 |
| Pistol TP | `/Game/Shooter/Animation/ThirdPerson/ABP_TP_Pistol.ABP_TP_Pistol_C` | 同期望 | 匹配 |

四个期望类均成功加载；ABP_FP_Weapon、ABP_FP_Rifle、ABP_FP_Pistol 资产也均成功加载。详细本机证据：[AnimClassMappingInspection.json](../Saved/MCP/AnimClassMappingInspection.json)，执行脚本为 `Saved/MCP/InspectAnimClassMapping.py`。

资产版本一致性：

```text
git hash-object Content/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Rifle.uasset
9476411bad0d9237d5cdbf9a9ddeae9c5853a4e0

git rev-parse HEAD:Content/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Rifle.uasset
9476411bad0d9237d5cdbf9a9ddeae9c5853a4e0
```

这些相同字节与 UE 对当前文件的实际加载结果共同证明：HEAD 里的 Rifle 映射也是 ABP_FP_Rifle，不是根据 uasset 中出现某个字符串猜测属性值。

## 3. 失败调用链

测试实现：[ShooterWeaponPresentationBaselineAutomationTests.cpp](../Source/ShootGame/Tests/Equipment/ShooterWeaponPresentationBaselineAutomationTests.cpp)，测试名 `ShootGame.Equipment.Presentation.AnimClassMapping`。

1. `Expected[]` 明确写死 Rifle FP 为 ABP_FP_Weapon。
2. 加载 `BP_ShooterWeapon_Rifle_C`，读取其 CDO。
3. 加载期望的 FP / TP 动画类；本次加载断言没有失败。
4. 在第 115 行附近的 TestTrue 中比较：

```cpp
WeaponDefaults->GetFirstPersonAnimInstanceClass() == ExpectedFirstPersonClass
```

5. [ShooterWeapon.cpp](../Source/ShootGame/Weapons/ShooterWeapon.cpp) 的
   `GetFirstPersonAnimInstanceClass()` 只返回 `FirstPersonAnimInstanceClass` 字段，
   没有运行时选择、Fallback 或网络状态分支。
6. 测试框架记录 `Expected 'Rifle FP AnimClass matches baseline' to be true.`，用例为 Fail，自动化进程因测试失败返回 255。

这个比较是严格 UClass 相等，不是检查共同父类或 `IsChildOf`。本测试虽然注释提及 AnimBP 父类分布，实际此失败断言并不验证父类，也与 NewAnimBlueprint 改成 ShooterThirdPersonAnimInstance 的操作没有直接关系。测试还检查 Pistol/TP 配置及 Equipment 的 BlueprintAssignable 委托；本次没有这些断言的错误。

实际角色应用链见 [ShooterCharacter.cpp](../Source/ShootGame/Characters/ShooterCharacter.cpp) 的 `ApplyWeaponAnimClasses()`：它把武器配置中的 FP/TP AnimClass 交给相应 Mesh 的 `SetAnimInstanceClass()`。因此该差异表示装备 Rifle 时采用新的第一人称动画类，并非单纯更改了一个无用的显示标签；但映射差异本身不能判断新动画的运动、换弹和视觉表现是否合格。

## 4. 时间线与归因

| 时间 / 提交 | 已确认事实 | 能说明什么 |
|---|---|---|
| 2026-08-29，`109da3e` | 加入冻结 FP_Weapon/TP_Rifle、FP_Pistol/TP_Pistol 的测试；当前测试文件最后一次提交仍为此提交 | 期望来自早期快照 |
| 2026-09-03 16:01 | `Saved/Automation/Runs/20260903_160103/Summary.json` 顶层 Passed，七阶段均 Passed | 更早基线曾全量通过，不能代表当前状态 |
| 2026-09-04 开发记录 | 已记录同一 Rifle FP 断言失败，当时工作区把 FP 从 ABP_FP_Weapon 改到新增 ABP_FP_Rifle，彼时 HEAD 仍旧 | 同类差异早于 9 月 8 日 MCP 扩展 |
| 2026-09-05，`b5fbf18` | 提交 ABP_FP_Rifle，新旧资产变更中包含 Rifle 武器 | 第一人称 Rifle 工作进入提交历史；未独立加载该历史提交，不能仅凭二进制 diff 指定其每一字段值 |
| 2026-09-07，`51d6d61` | 整合多武器第一人称表现，更新 ABP_FP_Rifle 和 Rifle 武器；记录只跑 ShootGame.Aim 26 项，未全量回归 | 当前 HEAD 已包含新的动画工作，聚焦 Aim 测试不会发现此映射门禁 |
| 2026-09-08 两轮全量 | 12:34、17:50 均停在同一 AnimClassMapping 断言；17:50 为 80 成功、13 带警告成功、1 失败 | MCP 验证期间持续暴露既有差异 |
| 2026-09-09 本轮 | 当前资产等同 HEAD；只读 CDO 确认 ABP_FP_Rifle；聚焦测试再次复现唯一相同断言 | 当前仍然成立，已不是未提交 Rifle 资产差异 |

历史依据：[冻结武器装备表现事件基线](开发记录/2026-08-29-1254-冻结武器装备表现事件基线.md)、[9 月 4 日诊断记录](开发记录/2026-09-04-1545-左手IK数值诊断与Socket判据.md)、[9 月 7 日阶段整合](开发记录/2026-09-07-1947-瞄准IK与多武器第一人称表现阶段整合.md)。这些旧记录保留当时事实，本轮没有回写历史记录。

## 5. 为何全量入口停止

最近完整入口：`Scripts/Tests/RunAll.ps1 -Port 17847`（2026-09-08）。

| 阶段 | 结果 | 证据边界 |
|---|---|---|
| Build | Passed | 当轮 Editor 构建通过 |
| Automation | Failed | 94 个用例：80 成功、13 带警告成功、1 失败 |
| Standalone | 未执行 | 在前一阶段抛出异常后未进入 |
| DedicatedNetwork | 未执行 | 实际启动客户端数 0 |
| ListenNetwork | 未执行 | 实际启动客户端数 0 |
| EmulatedNetwork | 未执行 | 实际启动客户端数 0，未验证延迟/丢包 |
| DisconnectCleanup | 未执行 | 没有当前断线清理结果 |

[RunAutomation.ps1](../Scripts/Tests/RunAutomation.ps1) 第 67 行附近在 Editor-Cmd 返回非零退出码时抛错；[RunAll.ps1](../Scripts/Tests/RunAll.ps1) 的阶段包装器记录该失败并继续向外抛出，总控 catch 设置 Failed，其余阶段不会启动。Summary 中只出现已进入的两个阶段；不能把缺失的五个阶段说成“失败”，也不能当作“通过”。

日志中的 `GIsCriticalError=1` 与退出码 255 在本例由 Automation 的失败退出路径产生，不能仅凭这两个字样判断发生了编辑器崩溃。

最近完整报告：[Summary.json](../Saved/Automation/Runs/20260908_175054/Summary.json)、[用例 index.json](../Saved/Automation/Reports/20260908_175056_ShootGame/index.json)。13 个 Warning 成功用例没有使本次门禁失败，但不等于其警告全部已排查；本轮专门分析唯一失败断言。

## 6. 本轮复现和排除项

实际执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Tests\RunAutomation.ps1 `
  -TestFilter ShootGame.Equipment.Presentation.AnimClassMapping `
  -ReportRoot D:\Unreal_Projects\ShootGame\Saved\Automation\McpBoundaryAudit
```

2026-09-09 09:42 的聚焦报告为 **0 成功、0 警告成功、1 失败、0 未运行**；唯一 Error 与昨天相同。日志还有一条 FlushAsyncLoading 的 Info，不是新增失败。报告：[index.json](../Saved/Automation/McpBoundaryAudit/Reports/20260909_094239_ShootGame.Equipment.Presentation.AnimClassMapping/index.json)。本轮没有重复运行完整七阶段，因为直接原因已由已有全量报告和当前聚焦重现锁定；没有宣称当前全量通过。

已排除/可区分：

- **编辑器未退出造成 DLL 锁定**：先前属于构建环境问题；本次聚焦是独立进程，测试实际运行到明确类比较并失败。
- **MCP 通信、schema 或插件连线异常**：RunAutomation 显式 `-DisablePlugins=McpAutomationBridge`；错误来自武器映射测试，不是桥接调用。
- **NewAnimBlueprint 复制或第三人称父类错误**：断言读取的是正式 Rifle 武器 CDO 的第一人称字段，没有读取该测试副本；Rifle TP 映射匹配。
- **期望资产丢失/路径无法加载**：独立读取与 TestNotNull 均证明期望类存在。
- **今天未提交的 Rifle 配置**：Rifle 当前字节与 HEAD 相同。工作区其他 Character/Pistol/AWP/GrenadeLauncher 改动不能解释这个已固定的 Rifle CDO 字段差异。

只读调查脚本首次尝试额外读取 AnimBlueprint 的 ParentClass，被 UE Python 标记为 protected 而拒绝；移除该非必要字段后检查成功。没有绕过访问控制或改写资产；ParentClass 未作为本次只读脚本证据。

## 7. 后续决策路径（本轮不执行）

| 如果后续确认的产品意图是 | 对应方向 | 仍需验证 |
|---|---|---|
| ABP_FP_Rifle 已是正式新基线 | 同步精确映射快照、相关说明与验收证据 | 聚焦映射测试、完整七阶段、FP 动画视觉验收 |
| 仍要求 Rifle 使用 ABP_FP_Weapon | 通过 UE 资产编辑恢复相应武器配置，先审计新动画依赖和行为差异 | 保存/重载、装备应用链、完整回归 |
| 正在比较两种 FP 方案，尚无正式结论 | 先完成选择和验收，记录当前门禁暂时不通过 | 不把临时多选或删断言当作验收完成 |

本例需要判断的是“冻结快照是否仍代表产品意图”。把严格等值断言改成“非空即可”会丢失意外映射变化的检测能力；同时，即使以后对齐该断言，也只能解除当前阻塞，不能保证尚未执行的五个阶段自动通过。

## 8. 本轮未改动范围

没有修改 `ShooterWeaponPresentationBaselineAutomationTests.cpp`、`BP_ShooterWeapon_Rifle`、ABP_FP_Weapon、ABP_FP_Rifle 或其他资产；没有修改默认 AnimClass、网络逻辑、测试过滤器的默认值或失败退出行为；没有暂存/提交。新增的是使用文档、诊断报告及 Saved 下的只读调查产物。
