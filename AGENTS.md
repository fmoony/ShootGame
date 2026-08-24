# ShootGame 项目导航

本项目用于学习 Unreal Engine 5.6 的网络机制。当前原则是保持模板结构清晰，以最小、可验证的改动逐步完成多人化，不提前引入复杂框架。

## 项目概况

- **引擎**: UE 5.6
- **模块**: `ShootGame`（Runtime 模块，含 `Engine`、`AIModule`、`UMG`、`EnhancedInput`、`StateTreeModule` 等依赖）
- **入口**: `Source/ShootGame/ShootGame.cpp` → `FDefaultGameModuleImpl`
- **插件**: `ModelingToolsEditorMode`（Editor）、`StateTree`、`GameplayStateTree`、`GameplayAbilities`、`McpAutomationBridge`（Editor）

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
│   ├── ShooterCharacter.h/.cpp
│   └── Animation/              # 第三人称动画数据源与程序化瞄准 IK 节点
│       ├── ShooterThirdPersonAnimInstance.h/.cpp
│       └── AnimNodes/          # ShooterAimIKMath、AnimNode_ShooterAimIK
├── AbilitySystem/              # GAS：ShooterAttributeSet、GameplayEffect 工具
│   ├── ShooterAttributeSet.h/.cpp
│   └── ShooterGameplayEffectStatics.h/.cpp
├── AI/                         # ShooterNPC、ShooterAIController、StateTree 工具
├── Weapons/                    # ShooterWeapon、ShooterProjectile、ShooterPickup
├── UI/                         # ShooterUI、ShooterBulletCounterUI
└── Tests/                      # 自动化测试（网络协调器、武器配置检查）
```

## 文档入口

- [Shooter 模板蓝图分析](Docs/Shooter模板蓝图分析.md)：说明 Shooter 模板中的第一/第三人称动画蓝图、Control Rig、武器蓝图及其网络职责边界。
- [FirstPerson 清理与架构审计](Docs/FirstPerson清理与架构审计.md)：记录旧模板资产依赖闭包、根目录 C++ 类去留和分步清理顺序。
- [Shooter 完整 Demo 最终路线规划](Docs/执行计划/Shooter完整Demo最终路线规划.md)：记录最终 Demo 目标、系统边界、推荐实施顺序与当前阶段。
- [动画分层与射击表现扩展规划](Docs/执行计划/动画分层与射击表现扩展规划.md)：当前表现路线的上层规划，覆盖动画分层、瞄准同步、Reload / Equip 动画、角度散布与射击手感。
- [第三人称动画分层实施计划](Docs/已完成计划/第三人称动画分层实施计划.md)：已完成（2026-08-21）；Rifle / Pistol 分层、视觉验收与一次性迁移工具退出。
- [Rifle 左手 IK 与瞄准同步提速实施计划](Docs/执行计划/Rifle左手IK与瞄准同步提速实施计划.md)：当前表现阶段；建立最终武器姿势后的左手握持约束，并缩短远端快速转向的可见延迟。
- [GA_Reload 与 GA_Equip ServerOnly 执行计划](Docs/已完成计划/GA_Reload与GA_Equip_ServerOnly执行计划.md)：已完成（2026-08-21）；记录换弹原子事务、服务器权威切枪、互斥 Tag、Montage 检查与完整回归。
- [已完成计划归档](Docs/已完成计划/)：仅在追溯历史决策、验收证据或旧阶段边界时进入，不在主导航中逐项列出。
- [代码规范](Docs/代码规范.md)：C++ 命名、文件结构、注释、日志和网络代码约定。
- [自动化测试](Docs/自动化测试.md)：说明编译、Automation 测试及服务器双客户端联机会话脚本的使用方式。
- [Visual Studio 项目同步](Docs/VisualStudio项目同步.md)：规定源码结构变化后的项目文件刷新入口、自动推进范围和必须暂停的人工重载边界。
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

### Visual Studio 项目文件同步

- 在 `Source/` 或 `Plugins/` 中完成本轮源码文件的新建、删除、移动或重命名后，只运行一次 `Scripts/Development/RefreshVisualStudioFiles.ps1`；不得为刷新工程文件删除 `Binaries`、`Intermediate` 或 `.vs`。
- VS 显示“全部重新加载”只表示当前 VS 内存中的工程视图落后于磁盘，不阻塞 Agent 使用外部 UBT 继续编译、测试、记录和提交。
- 新增模块、Target 或插件后也默认先刷新再继续外部自动化；同时提示用户 VS 存在待重新加载状态。
- 只有下一步必须依赖当前 VS / Unreal Editor 会话加载新结构、刷新脚本失败，或用户存在可能被重载丢弃的未保存工程级设置时，才暂停并请求用户处理。
- 禁止自动关闭、重开或强制操纵 VS 执行“全部重新加载”；详细流程以 [Visual Studio 项目同步](Docs/VisualStudio项目同步.md) 为准。

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

### 实现依据与外部资料检索

必须先区分问题类型，不能因为本地源码可读就跳过所需的外部研究：

1. **确认当前项目事实**：理解现有类、调用链、权威边界和当前行为时，先使用 CodeGraph，再按需读取项目源码、配置、蓝图和资产。这只能证明“项目现在怎么做”，不能单独证明“UE 推荐怎么做”。
2. **确认 UE5.6 实际实现**：核对已安装版本的准确签名、宏展开、网络执行细节或引擎内部行为时，可以读取范围明确的本地 UE5.6 Engine 源码；本地 Engine 源码用于最终版本核对，不替代架构研究。
3. **设计新增 UE/GAS/网络架构**：提出新的系统边界、生命周期、复制策略、Ability、Prediction、Session、动画或插件接入方案时，禁止只根据当前项目源码或本地 Engine 源码推导结论。必须至少查询一项与当前问题直接相关的官方外部资料，并与当前项目基线和本地 UE5.6 行为对照。

新增架构、API 使用方式或版本行为的推荐顺序为：

```text
确认当前项目基线（CodeGraph / 项目资产）
→ 查询 UE 官方文档、官方示例、官方源码仓库或发布说明
→ 用本地 UE5.6 Engine 源码核对安装版本的实际行为
→ 对比差异后提出方案、实施或评审结论
```

外部检索要求：

- 工具需要解锁时，优先通过 `dev_tool_search` 获取 `web_search`；技术问题优先使用 Epic Games 官方文档、官方 API Reference、官方示例工程、官方源码仓库和版本发布说明。第三方库或插件优先查询其上游官方文档与仓库。
- 新增架构方案或执行计划必须说明外部依据、本地实现现状以及二者的差异；不能把项目当前写法直接描述为 UE 推荐方案。
- 若官方资料与本地 UE5.6 Engine 源码不一致，以本地安装版本的可验证行为作为实现依据，同时记录版本差异。
- 若无法访问所需外部资料，必须明确说明证据缺口；不得假定当前项目或本地 Engine 的既有写法就是推荐方案。该缺口会实质影响架构选择时，应停止作出不可逆实现并请求用户判断。
- 纯粹定位项目代码、复述已经验证的本地事实、机械性改动或不涉及新设计的修复，不要求为了形式额外检索外部资料。

### 大范围搜索限制

- Windows 下禁止无约束地对整个 Unreal Engine 根目录执行 `grep -R`、`find` 等递归全树扫描。
- 必须先将范围缩小到明确模块、插件或文件目录。

## Notes

<!-- 后续快速补充的临时记录 -->
