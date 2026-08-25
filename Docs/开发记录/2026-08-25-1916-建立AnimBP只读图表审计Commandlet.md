# 建立 AnimBP 只读图表审计 Commandlet

- 日期：2026-08-25
- 计划提交说明：`动画资产：建立只读图表审计 Commandlet`
- 变更类型：Editor 工具

## 目的

执行《Shooter 核心玩法架构解耦重构执行计划》R6.3 的准备：本会话中只读 MCP 插件工具不可用，因此在 `ShootGameEditor` 内建立等价的只读审计 Commandlet，在不动任何 `.uasset` 的前提下输出四个目标 AnimBP 的父类、蓝图变量、全部图表、节点、Pin 与 Pin 连接。该审计结果用于逐项对比 EventGraph 数据来源，为后续数据迁移和重复节点删除提供证据。

## 本提交完成内容

- 新增 `Source/ShootGameEditor/Migration/ShooterAnimBPGraphAuditCommandlet`：
  - 只读加载 `ABP_FP_Weapon`、`ABP_FP_Pistol`、`ABP_TP_Rifle`、`ABP_TP_Pistol`。
  - 输出父类与 `EBlueprintStatus`、蓝图变量名/类型/容器类型。
  - 遍历 UbergraphPages / EventGraphs / FunctionGraphs / MacroGraphs / DelegateSignatureGraphs。
  - 输出节点类、FullTitle、变量 Get/Set 目标名、CallFunction 目标函数。
  - 输出每个 Pin 的类型、方向与连接关系（`AUTOMATION_ANIMBP_GRAPH_*` 可机器检索）。
  - 不调用 `MarkPackageDirty`，不保存任何资产。
- 未修改 Runtime 生产代码和任何资产。

## 验证结果

- `BuildEditor.ps1`：Passed。
- Commandlet 运行：`-run=ShooterAnimBPGraphAudit`，`AUTOMATION_ANIMBP_GRAPH_AUDIT_SUMMARY Total=4 Failures=0`，引擎退出码 0。
  - 完整日志：`Saved/Automation/AnimBPGraphAudit.log`。
  - 节点摘要：`Saved/Automation/AnimBPGraphAudit.nodes.txt`。
  - 连接摘要：`Saved/Automation/AnimBPGraphAudit.links.txt`。
- 审计后 `git status` 确认没有任何 `.uasset` 因审计被修改。

## 审计发现（R6.3 迁移依据）

- 两个 FP AnimBP 结构一致，EventGraph 各自维护第一人称专用变量（First Person Camera / Mesh、Aim Target、Is Aiming、PitchN、NewVar、NewVar_0、Hand_R_Target、GripOffset 等），并自行做移动判定、LineTrace 和 Control Rotation 计算；继承自 C++ 的 `LocomotionGroundSpeed` / `bIsInAir` / `bFirstPersonDataValid` 尚未接入 AnimGraph。
- 两个 TP AnimBP 的 EventGraph 都重复维护 Character / MovementComponent / Velocity / GroundSpeed / IsFalling / Direction / PitchN 等移动与瞄准值；其中 `GroundSpeed`、`IsFalling` 已被 C++ 基类的 `LocomotionGroundSpeed`、`bIsInAir` 覆盖语义。
- `ABP_TP_Rifle` 存在迁移前已知的 `Get CurrentWeapon` 断链节点，并被旧 HTM/校准握把链引用；该链当前仍编译（0 Error，1 Warning）。
- `ABP_TP_Rifle` 的 AnimGraph 已经消费 C++ 的 `HandToMuzzle`、`AimDirectionWorld`、`bAimIKEnabled`、`bLeftHandIKEnabled`、`HandGripInLeftHandSpace`；但左手 IK 节点的 `WeaponGripInRightHandSpace` 仍连到旧变量 `CalibratedWeaponGripInRightHandSpace`，而 C++ 的 `LeftHandGripInRightHandSpace` 节点存在但未连接。两者语义不同（旧值为 HTM 与 HandGripInLeftHandSpace 的合成校准，C++ 值为直接握把相对关系），不能无依据地互相替换。
- `ABP_TP_Pistol` 无 Rifle 专用 Aim/LeftHand 节点，EventGraph 只做移动与 AimOffset/PitchN 计算。

## 遇到的问题

- 会话中项目内只读 MCP 插件工具不可用；AGENTS 要求蓝图节点与资产引用优先通过 MCP 审计，因此改为在 Editor 模块内实现等价的只读 Commandlet。该工具只读加载，不触碰资产内容。
- UE5.6 `FEdGraphPinType` 已无 `bIsArray` 字段，`FBlueprintEditorUtils::GetAllGraphs` 在本地版本不存在；实现改为直接遍历 `UBlueprint` 的公开 Graph 数组，并只输出容器类型枚举，编译与运行均通过。

## 处理方式

- 本提交只提交审计工具和记录，不执行 R6.3 的 EventGraph 节点删除或重新连线。后续节点迁移属于新的 `.uasset` 保存批次，按执行计划 13.2 需要明确放行；最终视觉一致性也需要人工验收。

## 遗留项

- R6.3 节点迁移需逐项依据本审计日志完成：FP 专用值迁到 C++ FirstPerson 快照、TP 移动值改用基类快照、修复 Rifle 的 `CurrentWeapon` 断链，并对校准握把链做出“保留旧语义”或“迁移 C++ 语义”的明确决策。
- `BP_ShooterCharacter` 中遗留的 `Current Weapon` 蓝图变量属性错误继续挂账，待兼容层退出阶段处理。
