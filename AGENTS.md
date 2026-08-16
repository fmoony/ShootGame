# ShootGame 项目导航

本项目用于学习 Unreal Engine 5.6 的网络机制。当前原则是保持模板结构清晰，以最小、可验证的改动逐步完成多人化，不提前引入复杂框架。

## 项目概况

- **引擎**: UE 5.6
- **模块**: `ShootGame`（Runtime 模块，含 `Engine`、`AIModule`、`UMG`、`EnhancedInput`、`StateTreeModule` 等依赖）
- **入口**: `Source/ShootGame/ShootGame.cpp` → `FDefaultGameModuleImpl`
- **插件**: `ModelingToolsEditorMode`（Editor）、`StateTree`、`GameplayStateTree`、`McpAutomationBridge`（Editor）

## 源码结构

```
Source/ShootGame/
├── ShootGame.Build.cs          # 模块依赖和 include 路径
├── ShootGame.h / .cpp          # 模块入口 + LogShootGame 日志分类
├── GameFramework/              # 框架入口
│   ├── ShooterGameMode.h/.cpp
│   ├── ShooterGameState.h/.cpp
│   ├── ShooterPlayerController.h/.cpp
│   ├── ShooterPlayerState.h/.cpp
│   └── ShooterCameraManager.h/.cpp
├── Characters/                 # 玩家角色
│   └── ShooterCharacter.h/.cpp
├── AI/                         # ShooterNPC、ShooterAIController、StateTree 工具
├── Weapons/                    # ShooterWeapon、ShooterProjectile、ShooterPickup
├── UI/                         # ShooterUI、ShooterBulletCounterUI
└── Tests/                      # 自动化测试（网络协调器、武器配置检查）
```

## 文档入口

- [Shooter 模板蓝图分析](Docs/Shooter模板蓝图分析.md)：说明 Shooter 模板中的第一/第三人称动画蓝图、Control Rig、武器蓝图及其网络职责边界。
- [FirstPerson 清理与架构审计](Docs/FirstPerson清理与架构审计.md)：记录旧模板资产依赖闭包、根目录 C++ 类去留和分步清理顺序。
- [多人网络改造计划](Docs/已完成计划/多人网络改造计划.md)：记录当前网络化状态、分阶段实施顺序和每阶段验收条件。
- [GAS、动画分层与客户端预测扩展计划](Docs/执行计划/GAS动画分层与预测扩展计划.md)：规划 GAS 生命周期、第三人称上下半身分层、简单武器背包与开火预测的渐进式接入路线。
- [GAS 接入前架构整理执行方案](Docs/已完成计划/GAS接入前架构整理执行方案.md)：供 Agent 执行 Shooter 主玩法收束、旧模板类清理、源码目录重组、资产引用迁移和最终自动化验收。
- [Shooter Content 资产目录整理执行方案](Docs/执行计划/Shooter_Content_Browser_资产目录整理执行方案.md)：供 Agent 执行 Shooter 正式资产从 `/Game/Variant_Shooter` 收束到 `/Game/Shooter` 的目录迁移。
- [代码规范](Docs/代码规范.md)：C++ 命名、文件结构、注释、日志和网络代码约定。
- [自动化测试](Docs/自动化测试.md)：说明编译、Automation 测试及服务器双客户端联机会话脚本的使用方式。
- [Agent 自动化验证操作手册](Docs/Agent自动化验证操作手册.md)：Agent 执行、判定、排错和扩展自动化测试时必须遵循的标准流程。
- [开发记录规范](Docs/开发记录/README.md)：规定每个新提交在提交前必须完成的改动、验证、问题与遗留项记录。

## 协作约定

- 项目说明、代码备注、提交说明和新增文档优先使用中文。
- 每次只完成一个可验证的网络闭环，避免同时改造装备、开火、伤害和 UI。
- 服务器负责产生权威游戏结果；客户端负责输入和本地表现。
- 第一人称摄像机、第一人称手臂和本地 HUD 只能由本地控制器访问。
- 修改蓝图资产前先确认其第一人称、第三人称以及服务器职责。
- 每次新提交前必须先按 [开发记录规范](Docs/开发记录/README.md) 新建或完成一份提交记录，写明本次提交做了什么、验证结果、遇到的问题及遗留项；记录必须与对应改动进入同一个提交。
- 已有历史提交不因本规范补写、修改或重写；本规范只约束其生效后的新提交。

## Content 资产规则

- Shooter 正式资产根为 `/Game/Shooter`。
- 共享资产只有在被多个长期目录实际使用时才进入 `/Game/Shared`。
- 资产整理先审计依赖，再执行迁移。
- 不根据文件名判断资产是否无用。
- 每次只处理一个明确迁移 Batch。
- `.uasset` 必须通过 Unreal Editor 资产系统移动。
- 移动后必须 Save All 并 Fix Up Redirectors。
- World Partition 的 `__ExternalActors__` 和 `__ExternalObjects__` 禁止人工整理。
- 核心 Blueprint、AnimBP、Map 迁移后必须重新验证。
- 无法证明可以删除的资产默认保留。

## CodeGraph

仓库根目录存在 `.codegraph/`。需要理解或定位 C++ 代码时，应先使用 CodeGraph：

```powershell
codegraph.cmd explore "要查找的符号或问题"
```

只有 CodeGraph 无法覆盖资产或配置问题时，再使用文本搜索或直接读取文件。蓝图节点与资产引用优先通过项目内的只读 MCP 插件检查。

## Notes

<!-- 后续快速补充的临时记录 -->
