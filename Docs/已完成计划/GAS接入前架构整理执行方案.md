# GAS 接入前架构整理执行方案

## 文档用途

本文件用于把 ShootGame 整理为“Shooter 是唯一正式玩法”的 GAS 接入前基线，并作为 Agent 的直接执行依据。

本轮只整理现有架构，不接入 Gameplay Ability System。执行完成后，才允许从 [GAS、动画分层与客户端预测扩展计划](../执行计划/GAS动画分层与预测扩展计划.md) 的“阶段 1：只接入 GAS 模块”继续。

## 当前基线

- 项目名和 Runtime 模块名继续使用 `ShootGame`。
- 默认地图、服务器默认地图和默认 GameMode 已指向 Shooter 玩法。
- Shooter 网络闭环当前包含武器复制、服务器权威开火、伤害、队伍、计分、死亡、复活和断线清理。
- `Source/ShootGame/Variant_Shooter/` 仍保存 Shooter 玩法源码。
- 根源码目录仍存在旧模板类：
  - `AShootGameCharacter`
  - `AShootGamePlayerController`
  - `AShootGameGameMode`
  - `AShootGameCameraManager`
- `AShooterCharacter` 和 `AShooterNPC` 当前都继承 `AShootGameCharacter`。该基类包含第一人称摄像机、第一人称手臂和玩家输入，因此不适合作为 NPC 公共基类。
- `Config/DefaultEngine.ini` 仍包含 `TP_FirstPerson*` 到 `ShootGame*` 的兼容重定向。
- 仓库可能存在未跟踪的 `.reasonix/` Agent 任务记录。它不属于项目源码，不得删除或提交其中内容；确认用途后只把目录加入 `.gitignore`。

执行前必须重新运行 `git status --short --branch`，以上状态只作为计划形成时的基线，不能替代现场检查。

## 总体目标

完成后的源码结构应为：

```text
Source/ShootGame/
├── ShootGame.Build.cs
├── ShootGame.h
├── ShootGame.cpp
├── GameFramework/
│   ├── ShooterGameMode.h/.cpp
│   ├── ShooterGameState.h/.cpp
│   ├── ShooterPlayerController.h/.cpp
│   ├── ShooterPlayerState.h/.cpp
│   └── ShooterCameraManager.h/.cpp
├── Characters/
│   └── ShooterCharacter.h/.cpp
├── AI/
│   ├── ShooterNPC.h/.cpp
│   ├── ShooterAIController.h/.cpp
│   └── 现有 StateTree 支持代码
├── Weapons/
├── UI/
└── Tests/
```

角色继承关系应收束为：

```text
ACharacter
├── AShooterCharacter
│   ├── 第一人称摄像机
│   ├── 第一人称手臂
│   ├── 玩家移动、视角和跳跃输入
│   └── 玩家射击与现有生命逻辑
└── AShooterNPC
    ├── AI 移动和瞄准
    └── NPC 武器与现有生命逻辑
```

不要为了形式统一新增暂时没有共享职责的角色基类。玩家与 NPC 后续使用不同的 GAS Owner/Avatar 生命周期，也不要求依靠共同 C++ 父类接入 GAS。

## 通用执行规则

1. 理解或定位 C++ 前先使用 CodeGraph，不得先用全仓库文本搜索重建调用链。
2. 每个阶段只处理一种职责，不把架构迁移、玩法改写和 GAS 接入混在一个提交中。
3. 修改蓝图资产前先检查其父类、类属性、图节点和依赖者。
4. 类重定向只能作为迁移工具。必须先确保资产可加载并重保存，再评估是否删除重定向。
5. 不得直接编辑 `.uasset` 二进制内容。
6. 不得误提交 `Saved/`、`Intermediate/`、本地 Agent 日志或用户无关改动。
7. 每个提交前必须按照 [开发记录规范](../开发记录/README.md) 先完成独立开发记录，并把记录与对应改动放入同一提交。
8. 所有新增文档、代码备注和提交说明优先使用中文。
9. 不重写已存在的历史提交。

## 阶段 0：冻结并验证现有基线

### 改动

- 检查当前分支、远端跟踪关系和工作区状态。
- 确认 `.reasonix/` 只包含本地 Agent 任务记录后，将其加入 `.gitignore`；不删除目录内容。
- 检查 Shooter 默认地图、GameMode、PlayerController 和 Pawn 配置。
- 不修改玩法代码。

### 验证

执行完整自动化矩阵：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Tests\RunAll.ps1 -Port <未占用端口>
```

必须检查以下七个阶段：

1. Build
2. Automation
3. Standalone
4. DedicatedNetwork
5. ListenNetwork
6. EmulatedNetwork
7. DisconnectCleanup

把实际 `Saved/Automation/Runs/<时间>/Summary.json` 路径写入开发记录。

### 计划提交

```text
配置：忽略本地 Agent 任务记录
```

如果 `.reasonix/` 已经被其他规则正确忽略，且本阶段没有文件改动，则不创建空提交，只记录基线验证结果供下一阶段引用。

## 阶段 1：由 Shooter 接管框架入口

### 改动

- 新增 `AShooterCameraManager`，迁移原有摄像机俯仰限制：

```cpp
ViewPitchMin = -70.0f;
ViewPitchMax = 80.0f;
```

- 在 `AShooterPlayerController` 构造函数中明确设置：

```cpp
PlayerCameraManagerClass = AShooterCameraManager::StaticClass();
```

- 检查并迁移以下资产的父类和类属性：
  - `BP_ShooterGameMode`
  - `BP_ShooterPlayerController`
- 确认 Shooter 的 GameMode、GameState、PlayerController 和 PlayerState 都直接承担正式玩法职责。
- 迁移期间暂时保留必要的旧类重定向，不能先删重定向再尝试加载资产。
- 资产完成重保存且验证通过后，删除：
  - `ShootGameGameMode.h/.cpp`
  - `ShootGamePlayerController.h/.cpp`
  - `ShootGameCameraManager.h/.cpp`

### 验证

- Shooter PlayerController 实际使用 `AShooterCameraManager`。
- 摄像机俯仰范围保持原行为。
- 输入映射仍只添加到本地玩家。
- 本地 HUD 不在专用服务器或非本地控制器上创建。
- 计分、死亡和复活不回退。
- 完整七阶段自动化矩阵通过。

### 计划提交

```text
架构：由 Shooter 接管框架入口
```

## 阶段 2：拆分玩家与 NPC 角色职责

### 玩家角色

把 `AShootGameCharacter` 中属于玩家的内容迁入 `AShooterCharacter`：

- 第一人称摄像机组件。
- 第一人称手臂 Skeletal Mesh。
- 第一/第三人称 Mesh 可见性配置。
- Enhanced Input 绑定。
- 移动、视角和跳跃输入处理。
- 玩家所需的胶囊体和 CharacterMovement 配置。

迁移后 `AShooterCharacter` 直接继承 `ACharacter`，继续实现 `IShooterWeaponHolder`。

### NPC 角色

将 `AShooterNPC` 改为直接继承 `ACharacter`，继续实现 `IShooterWeaponHolder`：

- 不再创建玩家第一人称摄像机和第一人称手臂。
- 不再继承玩家 Enhanced Input 绑定。
- NPC 只挂接第三人称武器 Mesh。
- NPC 瞄准起点改用 Pawn 视点、Actor Eyes 或 AI Controller 控制方向，不再借用第一人称摄像机组件。
- 保留现有 StateTree、AI 感知、武器和伤害行为。

### 蓝图与动画资产迁移

先建立必要的临时类重定向，保证旧资产可加载。审计并重保存至少以下资产：

- `BP_ShooterCharacter`
- `BP_FirstPersonCharacter`
- `ABP_FP_Weapon`
- `ABP_FP_Pistol`
- 资产依赖扫描找到的其他 `ShootGameCharacter` 父类、Cast 或类型引用

优先使用项目现有的只读蓝图 MCP 检查父类、图节点和依赖。如果命令行重保存不能安全修改父类或 Cast 节点，再请求用户启动编辑器，并给出明确的单步操作。

资产完成迁移后删除 `ShootGameCharacter.h/.cpp`。不得仅依赖永久重定向掩盖未迁移的蓝图类型。

### 验证

- 玩家仍拥有第一人称摄像机、手臂和输入。
- NPC 不再拥有玩家第一人称组件和输入绑定。
- NPC 武器挂接、瞄准和开火正常。
- 蓝图和动画蓝图编译无错误。
- 日志中不存在 `Accessed None`、失效父类或找不到旧 C++ 类。
- 完整七阶段自动化矩阵通过。

### 计划提交

```text
重构：拆分玩家与 NPC 角色职责
```

如果资产迁移量较大，可以把“添加临时重定向并迁移资产”和“删除旧角色类并移除重定向”拆成两个独立提交，但每个提交都必须保持项目可加载、可编译。

## 阶段 3：重组 Shooter 源码目录

### 目录迁移

| 当前路径 | 目标路径 |
| --- | --- |
| `Variant_Shooter/ShooterGameMode*` | `GameFramework/` |
| `Variant_Shooter/ShooterGameState*` | `GameFramework/` |
| `Variant_Shooter/ShooterPlayerController*` | `GameFramework/` |
| `Variant_Shooter/ShooterPlayerState*` | `GameFramework/` |
| `Variant_Shooter/ShooterCharacter*` | `Characters/` |
| `Variant_Shooter/AI/` | `AI/` |
| `Variant_Shooter/Weapons/` | `Weapons/` |
| `Variant_Shooter/UI/` | `UI/` |

### 约束

- 使用 Git 可识别的文件移动，保留历史可追踪性。
- 更新全部 include 路径和 `ShootGame.Build.cs`。
- 删除不再需要的 `Variant_Shooter` include path。
- 不在这个阶段改写生命、开火、背包或网络协议。
- C++ 文件目录变化不会改变 `/Script/ShootGame.ClassName`；不得为纯目录移动增加无关类重定向。

### 验证

- Editor Target 编译通过。
- Game Target 编译通过。
- CodeGraph 能从新目录解析主要 Shooter 类型和调用关系。
- `Source/ShootGame/Variant_Shooter/` 已为空并移除。
- 完整七阶段自动化矩阵通过。

### 计划提交

```text
整理：重组 Shooter 源码目录
```

## 阶段 4：收束旧类引用和兼容重定向

### 改动

- 扫描代码、配置、蓝图、动画蓝图和地图中的旧类引用。
- 重保存所有迁移资产及 Shooter 主地图。
- 在内容浏览器或可靠命令行流程中执行 Fix Up Redirectors。
- 检查 Asset Registry、蓝图编译结果和运行日志。
- 只有确认没有有效引用者后，才删除对应 `ActiveClassRedirects`。
- `ActiveGameNameRedirects` 只有在确认资产不再包含旧模块包引用后才删除。

最终不应存在以下有效引用：

```text
ShootGameCharacter
ShootGamePlayerController
ShootGameGameMode
ShootGameCameraManager
TP_FirstPersonCharacter
TP_FirstPersonPlayerController
TP_FirstPersonGameMode
TP_FirstPersonCameraManager
```

如果某个重定向仍有可证明的历史资产依赖，可以保留，但必须在开发记录中写明具体依赖资产和不能删除的原因，不能笼统写“为了兼容”。

### 验证

- 所有 Shooter 蓝图和动画蓝图可以加载并编译。
- Shooter 地图可以 Standalone、Listen 和 Dedicated 启动。
- 日志中没有找不到类、找不到包、失效父类和 Blueprint Runtime Error。
- 完整七阶段自动化矩阵通过。

### 计划提交

```text
清理：收束旧类引用与重定向
```

## 阶段 5：冻结 GAS 接入前架构基线

### 文档更新

- 更新根目录 `AGENTS.md` 的源码结构和文档导航。
- 更新 [FirstPerson 清理与架构审计](../FirstPerson清理与架构审计.md) 的最终结果。
- 更新 [GAS、动画分层与客户端预测扩展计划](../执行计划/GAS动画分层与预测扩展计划.md) 中的实际目录入口。
- 更新蓝图分析、多人网络计划和 Agent 自动化验证文档中已失效的类名或路径。
- 记录最后一次完整自动化的 `Summary.json`。

### 最终验证

```powershell
git diff --check
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Tests\RunAll.ps1 -Port <未占用端口>
git status --short --branch
```

同时使用 CodeGraph 验证：

- 旧根模板类已经没有定义和调用者。
- Shooter GameFramework、Character、AI 和 Weapon 调用链都能从新路径解析。
- 不存在同时承担同一职责的旧、新两套框架入口。

### 计划提交

```text
文档：冻结 GAS 接入前架构基线
```

## 最终完成标准

只有同时满足以下条件，才允许宣布本计划完成：

- Shooter 是唯一默认玩法入口。
- 根目录旧模板框架类全部移除。
- 玩家与 NPC 不再共享带玩家第一人称职责的错误基类。
- `Source/ShootGame/Variant_Shooter/` 已移除。
- 蓝图、动画蓝图和地图没有失效父类、旧 Cast 或运行时错误。
- 现有射击、武器复制、伤害、队伍、计分、死亡、复活和断线清理保持正常。
- 七阶段自动化矩阵全部通过。
- 每个新提交都有同提交的中文开发记录。
- 工作区不存在未解释的项目改动。
- 尚未启用 GAS 插件，也没有 ASC、AttributeSet、GameplayAbility 或预测实现。

## 本轮禁止越界的内容

- 不启用 `GameplayAbilities`、`GameplayTags` 或 `GameplayTasks` 依赖。
- 不创建 ASC、AttributeSet、GameplayEffect 或 GameplayAbility。
- 不迁移生命、伤害或开火到 GAS。
- 不实现背包和客户端预测。
- 不实施第三人称动画分层改造。
- 不重命名项目或 Runtime 模块 `ShootGame`。
- 不把 `/Game/Variant_Shooter` 大规模改名为 `/Game/Shooter`。

`/Game/Variant_Shooter` 是二进制资产路径，批量改名会产生资产 Redirector、地图引用和 World Partition 外部 Actor 风险。它不是 GAS 的技术前置条件，应在本计划完成后作为独立资产迁移任务重新审计和执行。

## Agent 交接要求

Agent 开始或恢复工作时，应先汇报：

1. 当前分支和 `git status`。
2. 当前执行到哪个阶段。
3. 上一阶段的提交和自动化结果。
4. 本阶段准备修改的文件和明确验收条件。
5. 是否存在需要用户启动、保存或关闭 Unreal Editor 的资产操作。

遇到失败时必须保留失败日志并定位根因，不能为了得到绿色结果而删除断言、跳过测试或放宽服务器权威规则。
