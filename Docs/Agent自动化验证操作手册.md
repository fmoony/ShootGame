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
| `Scripts/Tests/RunStandaloneSmoke.ps1` | 独立端加载 Shooter 地图并检查启动期崩溃和蓝图空引用 |
| `Scripts/Tests/RunNetworkSession.ps1` | 管理 Dedicated/Listen 服务器和客户端、网络模拟、主动断线、日志标记、超时与进程回收 |
| `Scripts/Tests/RunAll.ps1` | 串联七阶段矩阵并生成最终汇总 |
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

无头运行统一使用 `-DDC-ForceMemoryCache`，避免本机 Zen 或共享 DDC 不可写导致测试在业务代码之前崩溃。其代价是首次加载资产较慢，因此启动超时应覆盖内存缓存预热时间，不能看到短暂无输出就判定卡死。

命令可能持续一分钟以上。Agent 应持续等待或轮询已有进程，不得因为暂时没有新输出而重复启动同一套服务器与客户端。

### 步骤 C：读取机器结果

终端显示通过后，仍需读取本次文件：

```text
Saved/Automation/Runs/<时间戳>/Summary.json
```

只有同时满足以下条件才能声明完整验证通过：

- 顶层 `status` 为 `Passed`。
- `Build`、`Automation`、`Standalone`、`DedicatedNetwork`、`ListenNetwork`、`EmulatedNetwork`、`DisconnectCleanup` 七个阶段均为 `Passed`。
- Automation 报告至少完成一个测试，且 `failed` 为 0。
- Dedicated 与 Emulated 的服务器日志各有两个客户端成功标记，Listen 有一个远程客户端成功标记。
- Emulated 的服务端及客户端日志明确出现 `PktLag set to 100` 与 `PktLoss set to 2`。
- Disconnect 日志出现 `AUTOMATION_TEST_DISCONNECT_SUCCESS`，且 `Orphans=0`。
- 所有日志均不含 `AUTOMATION_TEST_FAILURE`。
- 所有测试启动的 `UnrealEditor-Cmd.exe` 均已回收。

不要只根据进程退出码、服务器开始监听或一个客户端成功标记宣布通过。

### 步骤 D：报告结果

Agent 的最终结果至少包含：

- 总状态以及七个阶段状态。
- Dedicated、Listen 与 Emulated 各自运行的客户端数量。
- 当前行为测试实际证明了什么。
- `Summary.json`、Automation 报告或网络日志的路径。
- 本次发现的失败原因及修复（如有）。
- 尚未覆盖的边界。

## 4. 当前端到端行为测试

每个客户端由一个仅对其拥有者相关的测试协调器驱动：

```text
服务器 PostLogin
  → 生成客户端拥有的协调器
  → 服务器授予步枪和手枪并记录初始状态
  → 复制“服务器已记录基线”门闩
  → 客户端确认 CurrentWeapon 已复制
  → 客户端请求切枪并确认观察者只看见权威武器
  → 客户端调用 DoStartFiring / DoStopFiring，角色 Server RPC 到达服务器
  → 服务器弹药减少，拥有者收到弹药，非拥有者看不到精确弹药
  → 观察远端俯仰与第三人称开火 Montage
  → 服务器施加部分伤害和致死伤害
  → 客户端确认生命、死亡、队伍、击杀、死亡数和队伍分数
  → 输出该客户端的成功标记
```

成功标记格式：

```text
AUTOMATION_TEST_CLIENT_SUCCESS PlayerId=<ID> Switch=true OwnerAmmo=true NonOwnerAmmoHidden=true Bullets=<Before>-><After> HP=<Before>->0 Dead=true AimDot=<Value> Team=<ID> Kills=1 Deaths=1 TeamScore=1 RemotePitch=<Observed>/<Expected> RemoteMontage=<State>
```

失败标记前缀：

```text
AUTOMATION_TEST_FAILURE:
```

脚本要求成功标记数量等于 `ClientCount`。任何失败标记都应立即使网络阶段失败。

### 当前能证明

- Shooter 地图可在 Standalone、Listen Server 和 Dedicated Server 模式启动。
- 两个真实远程客户端可以连接并获得 ShooterCharacter。
- 服务器生成武器，复制 `CurrentWeapon`，并处理客户端切枪 Server RPC。
- 弹药由服务器扣减，只向拥有者复制，观察者看不到精确弹药。
- 开火方向由服务器计算；弹丸、命中、生命、死亡均保持服务器权威。
- GameState 队伍分数和 PlayerState 队伍/击杀/死亡能够到达客户端。
- 远端第三人称俯仰和开火 Montage 状态可被观察。
- 在 100ms 延迟与 2% 丢包下业务链仍完成。
- 客户端断开后不存在失去有效武器持有者的存活武器 Actor。

### 当前不能证明

- Niagara 的最终视觉质量、声音衰减与混音、HUD 排版在有渲染窗口中正确。
- 两人同时争抢同一 Pickup 的确定性胜者规则。
- 长时间全自动射击以及切枪、死亡、断线发生在同一帧附近的所有竞态。
- 客户端预测、延迟补偿、服务器倒带和恶意客户端安全性。
- 完整换弹、备弹池、出生保护和产品化比赛流程。

Agent 必须按以上边界描述结论：可以声明“第一版服务器权威多人网络主闭环完成”，不能扩大为“产品化联网射击游戏完成”。

## 5. 失败定位顺序

| 失败阶段或现象 | 首查位置 | Agent 动作 |
|---|---|---|
| Build 失败 | UnrealBuildTool 输出及用户目录构建日志 | 区分权限、编译、链接和 UHT 错误；只修复实际代码错误 |
| Automation 进程失败 | `Saved/Automation/Logs/` | 查找崩溃、插件冲突和测试队列错误 |
| 测试失败或零测试 | `Saved/Automation/Reports/.../index.json` | 读取失败用例和事件，不用终端截断输出猜测 |
| Standalone 失败 | `Saved/Automation/Standalone/<时间戳>.log` | 检查地图加载、`Accessed None`、崩溃和提前退出 |
| 服务器未监听 | 当次 `Server.log` | 检查地图路径、端口占用、GameMode 和启动崩溃 |
| 客户端未连接 | 对应 `ClientN.log` 和 `Server.log` | 检查 `Welcomed by server`、网络版本和连接关闭原因 |
| 成功标记数量不足 | `Server.log` | 按 PlayerId 判断哪个客户端未完成，再检查门闩、武器复制和 RPC |
| 出现失败标记 | 标记附近上下文 | 以标记中的具体条件为主因，不用延长固定等待掩盖状态错误 |
| DDC/Zen 启动崩溃 | 当次进程日志开头 | 确认脚本仍带 `-DDC-ForceMemoryCache`；不要把机器缓存故障误判为玩法回归 |
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
- Standalone：地图启动与运行健康状态
- DedicatedNetwork：两个客户端及每客户端权威证据
- ListenNetwork：主机与远程客户端证据
- EmulatedNetwork：延迟/丢包参数与业务证据
- DisconnectCleanup：断线后的 ActiveWeapons/Orphans
- 报告：Summary.json 路径
- 修复：本轮修复的具体问题（若有）
- 未覆盖：仍需人工或后续自动化验证的范围
- 提交：本轮相关提交哈希
```

该模板用于保证结论可追溯，也防止 Agent 用宽泛描述替代实际测试证据。
