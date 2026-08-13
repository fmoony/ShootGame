# Agent 自动化验证操作手册

本文是面向自动化 Agent 的标准操作流程（SOP）。目标是在不依赖人工操作 PIE 的情况下，完成项目编译、规则测试、真实服务器与多客户端行为测试，并依据机器可读报告给出可复核的结论。

面向开发者的参数说明见 [自动化测试](自动化测试.md)。Agent 执行任务时以本文的操作顺序、成功条件和边界声明为准。

## 1. 适用场景

以下情况应运行完整验证：

- 修改 C++、模块依赖、RPC、复制属性或网络生命周期。
- 修改武器蓝图的类、复制、弹匣或开火间隔配置。
- 修改 Shooter GameMode、Character、Weapon 或测试协调器。
- 用户要求“编译检查”“自动验证”“运行结果测试”或确认某一网络阶段完成。

仅修改说明文档时通常不需要启动 Unreal。只排查单层问题时可以运行对应子脚本，但最终声明网络功能完成前必须再运行一次完整入口。

## 2. 固定入口与关键文件

默认完整入口：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Tests\RunAll.ps1
```

关键实现：

| 位置 | 职责 |
|---|---|
| `Scripts/Tests/BuildEditor.ps1` | 编译 `ShootGameEditor Win64 Development` |
| `Scripts/Tests/RunAutomation.ps1` | 运行 `ShootGame.*` Automation 测试并解析 JSON 报告 |
| `Scripts/Tests/RunNetworkSession.ps1` | 管理服务器和客户端进程、日志标记、超时与进程回收 |
| `Scripts/Tests/RunAll.ps1` | 串联三层并生成最终汇总 |
| `Source/ShootGame/Tests/ShooterWeaponAutomationTests.cpp` | 武器资产和网络配置快速检查 |
| `Source/ShootGame/Tests/Network/ShooterNetworkTestCoordinator.*` | 双客户端权威开火行为驱动与判定 |

默认网络地图是 `/Game/Variant_Shooter/Lvl_Shooter`，默认客户端数量为 2。测试模式只在服务器带有 `-ShootGameNetworkTest` 参数时启用。

## 3. Agent 标准执行流程

### 步骤 A：执行前只读检查

1. 读取根目录 `AGENTS.md` 和本文。
2. 执行 `git status --short`，记录用户已有改动。
3. 不清理、不还原、不提交与当前任务无关的改动。
4. 确认引擎默认路径存在；若项目环境不同，通过 `-EngineRoot` 显式传入。
5. 为重复或并行运行选择未占用端口，例如 `17794`，避免把端口冲突误判为代码错误。

### 步骤 B：运行完整验证

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Tests\RunAll.ps1 `
    -Port 17794
```

执行 UnrealBuildTool 时会写入用户目录日志，并会启动多个隐藏的 `UnrealEditor-Cmd.exe` 进程。在受限执行环境中，如果出现用户目录访问被拒绝，应申请限定于该脚本的沙箱外执行权限后原样重试，不应修改项目代码来绕过权限错误。

命令可能持续一分钟以上。Agent 应持续等待或轮询已有进程，不得因为暂时没有新输出而重复启动同一套服务器与客户端。

### 步骤 C：读取机器结果

终端显示通过后，仍需读取本次文件：

```text
Saved/Automation/Runs/<时间戳>/Summary.json
```

只有同时满足以下条件才能声明完整验证通过：

- 顶层 `status` 为 `Passed`。
- `Build`、`Automation`、`NetworkSession` 三个阶段均为 `Passed`。
- Automation 报告至少完成一个测试，且 `failed` 为 0。
- 服务器日志中的成功标记数量不少于客户端数量。
- 所有测试启动的 `UnrealEditor-Cmd.exe` 均已回收。

不要只根据进程退出码、服务器开始监听或一个客户端成功标记宣布通过。

### 步骤 D：报告结果

Agent 的最终结果至少包含：

- 总状态以及三个阶段状态。
- 运行的客户端数量。
- 当前行为测试实际证明了什么。
- `Summary.json`、Automation 报告或网络日志的路径。
- 本次发现的失败原因及修复（如有）。
- 尚未覆盖的边界。

## 4. 当前端到端行为测试

每个客户端由一个仅对其拥有者相关的测试协调器驱动：

```text
服务器 PostLogin
  → 生成客户端拥有的协调器
  → 服务器授予步枪并记录初始弹药
  → 复制“服务器已记录基线”门闩
  → 客户端确认 CurrentWeapon 已复制
  → 客户端调用 DoStartFiring / DoStopFiring
  → 角色 Server RPC 到达服务器
  → 服务器武器弹药减少
  → 输出该客户端的成功标记
```

成功标记格式：

```text
AUTOMATION_TEST_CLIENT_SUCCESS PlayerId=<ID> Bullets=<Before>-><After>
```

失败标记前缀：

```text
AUTOMATION_TEST_FAILURE:
```

脚本要求成功标记数量等于 `ClientCount`。任何失败标记都应立即使网络阶段失败。

### 当前能证明

- Shooter 地图可启动为专用服务器模式。
- 两个真实客户端可以连接并获得 ShooterCharacter。
- 服务器生成的武器及 `CurrentWeapon` 引用可以到达拥有客户端。
- 客户端通过现有角色入口发送开火 Server RPC。
- 服务器权威武器执行开火并减少弹药。

### 当前不能证明

- 弹药值已复制回客户端。
- 投射物已复制并能被其他客户端观察。
- 枪口特效、声音、动画和 HUD 在有渲染窗口中正确。
- 命中、伤害、死亡和重生已经形成网络闭环。

Agent 必须按以上边界描述结论，不能把“服务器扣弹成功”扩大为“完整多人射击已完成”。

## 5. 失败定位顺序

| 失败阶段或现象 | 首查位置 | Agent 动作 |
|---|---|---|
| Build 失败 | UnrealBuildTool 输出及用户目录构建日志 | 区分权限、编译、链接和 UHT 错误；只修复实际代码错误 |
| Automation 进程失败 | `Saved/Automation/Logs/` | 查找崩溃、插件冲突和测试队列错误 |
| 测试失败或零测试 | `Saved/Automation/Reports/.../index.json` | 读取失败用例和事件，不用终端截断输出猜测 |
| 服务器未监听 | 当次 `Server.log` | 检查地图路径、端口占用、GameMode 和启动崩溃 |
| 客户端未连接 | 对应 `ClientN.log` 和 `Server.log` | 检查 `Welcomed by server`、网络版本和连接关闭原因 |
| 成功标记数量不足 | `Server.log` | 按 PlayerId 判断哪个客户端未完成，再检查门闩、武器复制和 RPC |
| 出现失败标记 | 标记附近上下文 | 以标记中的具体条件为主因，不用延长固定等待掩盖状态错误 |
| 进程残留 | 系统进程列表 | 只终止本次脚本记录并启动的进程，禁止按模糊路径批量清理 |

`McpAutomationBridge` 在 Automation、服务器和客户端子进程中默认禁用，以避免多个进程争用 MCP 端口；这不改变项目插件配置。

## 6. 扩展新测试的约定

### 适合 Automation Test 的内容

- 纯规则、默认值、资产可加载性、类配置和不需要真实网络连接的逻辑。
- 测试名统一使用 `ShootGame.<领域>.<行为>`。
- 测试应放在 `Source/ShootGame/Tests/`，并使用 `WITH_DEV_AUTOMATION_TESTS` 限定开发测试代码。
- 单个测试失败时应指出具体资产、属性或预期值。

### 适合多进程网络测试的内容

- 客户端所有权、Server RPC、复制到拥有者或观察者、Multicast、断线和时序相关行为。
- 客户端必须调用生产代码已有入口，不得直接调用服务器内部实现来制造通过结果。
- 服务器必须先记录权威基线，再通过复制门闩允许客户端动作。
- 等待条件应采用“轮询状态 + 明确超时”，不要依赖固定 `Sleep` 猜测复制完成时间。
- 每个客户端输出独立成功证据；脚本按期望客户端数计数。
- 任一路径失败都输出包含原因的 `AUTOMATION_TEST_FAILURE:`。
- 测试 Actor 只由显式命令行参数启用，正常游戏流程不得自动生成。

### 接入总控入口

新增快速测试只要保持 `ShootGame` 前缀，就会被 `RunAll.ps1` 自动发现。新增网络行为测试时应：

1. 为服务器增加唯一测试启动参数。
2. 定义唯一成功标记和统一失败标记。
3. 在 `RunAll.ps1` 传入启动参数、成功标记和期望标记数量。
4. 先单独运行 `RunNetworkSession.ps1` 调试。
5. 最后运行完整 `RunAll.ps1`，防止只在局部入口下通过。

## 7. Git 与交付约定

- 测试驱动、脚本接入、文档更新分别提交，提交说明使用中文。
- 暂存时显式列出文件，避免混入用户的蓝图、资产或其他未提交代码。
- `Saved/Automation/` 是运行产物，不提交 Git。
- 每个功能提交前至少执行 `git diff --check`；网络功能提交前必须有一次实际多客户端通过记录。
- 若验证暴露了生产缺陷，先记录可复现证据，再单独提交修复，不把测试和无关重构混在一起。

## 8. Agent 交付模板

```text
自动验证：Passed / Failed

- Build：状态与耗时
- Automation：通过/警告/失败数量
- NetworkSession：客户端数量与每客户端权威证据
- 报告：Summary.json 路径
- 修复：本轮修复的具体问题（若有）
- 未覆盖：仍需人工或后续自动化验证的范围
- 提交：本轮相关提交哈希
```

该模板用于保证结论可追溯，也防止 Agent 用宽泛描述替代实际测试证据。
