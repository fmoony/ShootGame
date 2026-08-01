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
├── ShootGameCharacter.h/.cpp   # 基础角色（第一人称骨骼、摄像机、移动/视角输入、网络测试 RPC）
├── ShootGamePlayerController.h/.cpp  # 基础 PlayerController（输入映射、移动端控件）
├── ShootGameGameMode.h/.cpp    # 基础 GameMode
├── ShootGameGameState.h/.cpp   # GameState（含 MatchCounter 复制测试）
├── ShootGameCameraManager.h/.cpp   # 自定义 PlayerCameraManager
├── Variant_Shooter/            # 🔫 射击玩法变体
│   ├── ShooterCharacter        # 射击角色（武器管理、开火、拾取）
│   ├── ShooterPlayerController # 射击 PlayerController
│   ├── ShooterGameMode         # 射击 GameMode
│   ├── AI/                     # ShooterNPC、ShooterAIController、StateTree 工具
│   ├── Weapons/                # ShooterWeapon、ShooterProjectile、ShooterPickup
│   └── UI/                     # ShooterUI、ShooterBulletCounterUI
└── Variant_Horror/             # 👻 恐怖玩法变体
    ├── HorrorCharacter         # 恐怖角色
    ├── HorrorPlayerController  # 恐怖 PlayerController
    ├── HorrorGameMode          # 恐怖 GameMode
    └── UI/                     # HorrorUI
```

## 文档入口

- [Shooter 模板蓝图分析](Docs/Shooter模板蓝图分析.md)：说明 Shooter 模板中的第一/第三人称动画蓝图、Control Rig、武器蓝图及其网络职责边界。
- [多人网络改造计划](Docs/多人网络改造计划.md)：记录当前网络化状态、分阶段实施顺序和每阶段验收条件。
- [代码规范](Docs/代码规范.md)：C++ 命名、文件结构、注释、日志和网络代码约定。

## 协作约定

- 项目说明、代码备注、提交说明和新增文档优先使用中文。
- 每次只完成一个可验证的网络闭环，避免同时改造装备、开火、伤害和 UI。
- 服务器负责产生权威游戏结果；客户端负责输入和本地表现。
- 第一人称摄像机、第一人称手臂和本地 HUD 只能由本地控制器访问。
- 修改蓝图资产前先确认其第一人称、第三人称以及服务器职责。

## CodeGraph

仓库根目录存在 `.codegraph/`。需要理解或定位 C++ 代码时，应先使用 CodeGraph：

```powershell
codegraph.cmd explore "要查找的符号或问题"
```

只有 CodeGraph 无法覆盖资产或配置问题时，再使用文本搜索或直接读取文件。蓝图节点与资产引用优先通过项目内的只读 MCP 插件检查。

## Notes

<!-- 后续快速补充的临时记录 -->
