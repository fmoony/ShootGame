# Linux Dedicated Server 构建与跟踪

## 当前状态与使用边界

截至 2026 年 10 月 16 日，Linux 专服已完成构建、打包与公网双客户端登录验证。
上次明确核对的游戏状态为已停止；10 月 16 日 SSH 未读到协议 banner，未能再次登录。
此失败发生在认证前，不能据此判定服务器已关机、已回收或原资源已经消失。
用户已暂缓连接问题，当前不继续查询、启动服务或下载备份。

G 盘未完成备份已按用户要求删除，释放约 2.61 GiB；本地无完整构建环境备份。
D 盘已验证的 Linux Server 运行包、Windows 客户端、源码副本和历史日志仍保留。
本地 Windows 交叉编译仅完成条件评估，未安装工具链、下载依赖或开始编译。
具体资源约束见下文；本页启停及连接命令须在未来重新确认服务器可用后使用。

## 本轮范围

新增最小 Server Target，构建 Linux Development Server；不改动 Gameplay。
Cook、打包、运行及公网客户端连接分别验收，编译成功不代表部署成功。
服务器所有者的编译许可已由用户转达；本轮编辑器保存与不自行更改的确认继续有效。
服务器租期约至 10 月 16 日，确切时刻未知；运行包已备份，完整构建环境未取回。

## 已验证部署与最后运行状态

- 源码获取、Linux Development Server 编译、Cook、Stage、Package 均已成功。
- 当前地图为 `/Game/Shooter/Maps/Lvl_Test`；启动脚本默认值已同步。
- 已验证普通用户 `ash-run` 运行专服，UDP 7777 实际监听。
- 当前产物为独立根目录下 `packages/package-20261014T142052Z/LinuxServer`。
- 公网双客户端验收日志为 `logs/server-20261014T142622Z/Server.log`。
- 两个 Windows 打包客户端均通过公网登录，服务端记录两次 Join succeeded。
- 两个客户端均被服务器欢迎并加载 Lvl_Test，分别收到另一个角色的复制。
- 服务端角色 Role=3、NetMode=1；客户端远端角色 Role=1、NetMode=3。
- 这证明 Dedicated 世界与双客户端连接；尚不证明移动和射击等完整 Gameplay。
- 按用户要求，备份后实际重启验证 Lvl_Test 与监听恢复，再停止本轮游戏服务。
- 10 月 14 日已关闭专服与两个测试客户端；服务器操作系统与已有实验保持运行。
- 10 月 15 日按用户要求重新启动专服，确认 Lvl_Test 与 UDP 7777。
- 随后检查时运行约 39 分钟，日志记录一名玩家登录后退出，无 Error 或 Fatal 行。
- 此前按用户要求停止游戏服务，确认原进程退出、UDP 7777 释放。
- 10 月 15 日再次按用户要求启动，PID=2746485；Lvl_Test 与 UDP 7777 已确认。
- 本次运行约 31 分钟后按要求关闭，游戏进程已退出、UDP 7777 已释放。
- 以上为最后成功登录时的核对结果；当前远端状态未经重新确认。
- 日志累计 3 次登录、3 次连接移除；有 1 次客户端 60 秒超时，未见崩溃记录。
- 记录 68 条 Ability 激活拒绝，其中 55 条为开火；部分相邻日志显示射速间隔不足。
- 停服后的完整日志已保存到本地 RuntimeLogs/Server-20261015T125738Z-Stopped.log。

源码 tag 的 Build.version 原始 Changelist 为 0。
UE 的 EffectiveCompatibleChangelist 在此情况下也为 0，导致与 Launcher 客户端不兼容。
已仅在隔离引擎副本中将 Changelist 设为本地 Launcher 5.6.1 的 44394996。
CompatibleChangelist 保持 43139311；增量重编和重新打包均成功。
两端实际 NetCL 均为 43139311，网络校验和均为 131985285；没有绕过版本校验。
原版本文件、修正依据与命令保存在本轮目录，原始源码归档保留。

LinuxServer 与 Windows Cook 均成功：各 800 个资源包、0 错误、2 警告。
Lvl_Test 已包含在 CookAll 产物中，本次切图不需要重新 Cook 或更改原项目默认地图。
Windows UAT 缺失的 23 个程序集已由固定 tag 的源码构建产物补齐，未覆盖已有 DLL。
Linux UAT 缺失的两张 IOS PNG 已按同版依赖清单 SHA1 核对后补齐。
本地保存相关逐文件清单、失败日志及恢复命令。

### 启动与客户端连接

在服务器本轮根目录执行以下控制脚本；控制器创建 unit，游戏仍以 ash-run 运行。

```bash
bash scripts/StartLinuxServer.sh package-20261014T142052Z
```

已有 active 服务时脚本不会重复启动。
重启前须核对 unit 的 User 和 WorkingDirectory，确保仅停止本轮游戏。
停止脚本会核对本轮用户与目录，并回收本轮临时 unit；启动脚本也会处理残留失败状态。

```bash
bash scripts/StopLinuxServer.sh
bash scripts/StartLinuxServer.sh package-20261014T142052Z
```

后台服务不依赖 SSH 存活；当前 Restart=no，进程失败不会自动重启。
该 unit 是临时 unit；机器重启后须手动运行保存的启动脚本，不承诺系统开机自启。

本地 Windows 客户端位于 `Saved/Deployment/20261014/WindowsPackage/ShootGame.exe`。
图形客户端可使用以下命令连接；开发版也可在控制台执行 open 命令。

```powershell
& './Saved/Deployment/20261014/WindowsPackage/ShootGame.exe' '103.236.77.127:7777'
```

实际验收使用同包的 Win64 可执行文件，并以 -nullrhi -nosound 无画面运行两个客户端。
连接与复制证据保存在 `Saved/Deployment/20261014/PublicClientsRelease/`。
沙箱内首轮客户端没有完成握手；使用授权的外部网络执行后收到真实服务器回应。
两端时钟存在约 156 秒差异，跨机日志时间不能直接用作网络延迟。

### 到期前备份

[Linux 构建环境完整备份](Linux构建环境完整备份.md) 记录本轮已授权的 G 盘完整工作区归档。
该归档保留源码、依赖、工具链与编译中间产物；远端曾完成，当前可取回性未知。
本地曾接收约 2.53 GiB 部分分卷，10 月 16 日已连同清单和状态按用户要求删除。
下述运行包备份仍独立保留；不能把它视为完整源码构建环境。

[BackupLinuxServer.sh](../../Scripts/Deployment/BackupLinuxServer.sh) 以 ash-run 备份完整运行包。
归档包括 Paks、可执行文件、receipt、版本文件、启动脚本与服务日志。
调试符号已有首轮备份，新运行包归档排除 .debug 与 .sym。
本地最终运行包和哈希证据位于 `Saved/Deployment/20261014/ReleaseBackup-LvlTest/`。
运行包 SHA256 已与服务器一致，可执行文件与 receipt 也逐一核对通过。
FinalScripts 保存最终启停脚本；RestartEvidence.txt 记录实际重启与最终停止的结果。
此前 SuccessBackup 是首轮 CL=0 产物，不能替代修正版本后的当前运行包。

## 本地 Windows 交叉编译评估

技术上支持 Windows 构建 Linux x86_64 Dedicated Server，不要求先在 WSL 中构建。
UE5.6 对应官方 v25、Clang 18.1.0；本地源码配置指定 v25_clang-18.1.0-rockylinux8。
目前只确认基础条件，不代表构建环境已经准备完成。

- CPU 为 i9-13900HX，24 核 32 线程；物理内存约 16 GiB，是主要并发约束。
- 已有 VS2022、MSVC 14.38 与 Windows SDK；未检测到已配置的 Linux 交叉工具链。
- 已有固定 5.6.1-release 源码副本和 ShootGameServer.Target.cs，源码依赖尚不完整。
- Launcher 为 5.6.1、CL=44394996；原始源码 CL=0，未来仍须核对网络版本兼容性。
- 磁盘快照：D 盘约 8 GiB、E 盘约 179 GiB；G 盘清理后约 84.1 GiB 可用。
- 用户此前全量源码构建超过 300 GB；不能据此承诺 E 盘余量足够本次交叉构建。

首次构建仍需编译 Server 所需的大量引擎模块、项目代码及必要工具。
优先评估仅构建 Linux Server，并复用 Launcher Cook 的流程；完整 Windows 源码编辑器另算。
这是减少目标范围的候选路线，尚未验证 Windows 宿主上的完整构建与打包闭环。
此前 Ubuntu 原生工作区约 103 GiB，不能直接作为 Windows 交叉编译峰值的上限。
下载缓存、依赖、调试符号、中间文件与 Cook 产物可能同时占用磁盘。

当前不建议直接开编；先核算依赖清单、缓存与产物预算，并预留磁盘余量。
若后续资源评估通过，再在独立目录构建，初始建议 2 并发，不套用远端的 8 并发。
本地首次耗时尚未测得；远端 Linux 缓存不保证能被 Windows 宿主增量复用。
本轮没有为交叉编译安装、下载、编辑引擎或启动任何编译。

## 固定输入与隔离目录

- 源码来自 EpicGames/UnrealEngine 的 `5.6.1-release` tag。
- tag 对象：`cef1527368284855f68e631bec945b262bdfb050`。
- 解引用提交：`6978b63c8951e57d97048d8424a0bebd637dde1d`。
- 源码与 Launcher 的 CompatibleChangelist 均为 `43139311`；实际网络校验仍须核对。
- 工具链：官方 `v25_clang-18.1.0-rockylinux8`。
- 本地输入及日志：`Saved/Deployment/20261014/`。
- 服务器根目录：`/home/ash-run/shootgame-dedicated-20261014`。
- 工程副本：该根目录下的 `project`；引擎副本：`engine`。
- 原有 `/opt/ash-validation`、`/opt/gemma4-golden-validation` 及用户资源全部保留。

依赖使用官方 GitDependencies 和 SetupToolchain 脚本，缓存明确指向独立目录。
本轮不运行自动引擎注册或系统软件安装；编译和游戏均使用普通用户 `ash-run`。
本地源码浅克隆曾遇到长路径失败，已仅对新副本开启 `core.longpaths` 并恢复检出。

## 按契约标题确认覆盖

### Invariant 1 Owner Immediate Feedback

- 触及维度：Dedicated 模式与 Windows 客户端之间的角色分离。
- 不应改变：本地 Shot Intent、预测表现与 Authority Result 的分离和时间窗口。
- 新增证据：打包客户端预测表现、确认回放为零及四种最终路径的实际日志。
- 范围外：不调整预测策略或增加表现补播；构建目标不修改这些生产入口。

### Invariant 2 Local Prediction Obeys Weapon Rules

- 触及维度：跨平台产物中的现有预测与规则代码是否正确编译及运行。
- 不应改变：服从本地已知规则、不越权、误预测收敛三个维度。
- 新增证据：射速、动作互斥、Reject 清理与客户端无权威副作用的实际样本。
- 范围外：不改变武器规则、输入缓冲或预测边界；本轮仅增加构建入口。

### Invariant 3 Server Is Final Authority

- 触及维度：Linux 专服承担动作校验、权威结果及拒绝后的清理。
- 不应改变：客户端只能提交意图，弹药、换弹、弹丸、伤害与死亡由服务器产生。
- 新增证据：Linux 端 NetMode、玩家登录、Accept、Reject、Cleanup 与结果计数。
- 范围外：不引入新的裁决协议；源码构建不能替代运行时证明。

### Invariant 4 Remote Is Confirmed Only

- 触及维度：打包客户端是否正确接收专服确认并表现远端角色。
- 不应改变：远端表现来源、与拥有者预测分离及无额外远端结果。
- 新增证据：冻结权威批次后，对同一批次记录观察端的 Montage、枪口和声音计数。
- 范围外：不修改纯表现投递可靠性；无渲染测试不证明最终画面和声音质量。

### Invariant 5 Exactly One Authority Result

- 触及维度：跨平台服务器执行一次认可动作时是否仍只有一份权威结果。
- 不应改变：单次弹药消费、换弹事务、弹丸、伤害和拥有者表现的数量边界。
- 新增证据：按稳定武器和场景基线比较 Authority、Ammo、Projectile 及事务增量。
- 范围外：不改变生产 Ability 生命周期；不得为旧测试保留活动实例。

## 后台构建与资源限制

- systemd unit：`shootgame-build-20261014.service`，单次运行，不自动重启。
- 用户：`ash-run`；当前 UBT 并发为 8，CPUQuota 为 800%。
- MemoryHigh 为 20 GiB、MemoryMax 为 24 GiB、MemorySwapMax 为 4 GiB。
- `WatchBuildDisk.py` 每 15 秒检查余量，低于 80 GiB 时只终止本轮进程组。
- 每次运行产生单独的 `logs/<UTC时间>/Build.log` 和 UBT 日志。
- 根目录 `BuildStatus.json` 记录阶段、状态、退出码和当前日志目录。
- 输入清单及 SHA256 位于 `InputsReady.json`；归档保留在 `incoming`。
- 失败后保留依赖缓存、中间产物及日志，避免重复下载和重新全量编译。

流程为 VerifySource → Dependencies → Toolchain → UBTBootstrap → ServerBuild。
UBTBootstrap 使用官方 BuildUBT.sh 执行 build 和 publish，再检查 DLL、deps 与 runtimeconfig。
ServerBuild 使用官方 RunUBT.sh；UBT_BOOTSTRAP_SUCCESS 仅表示工具就绪，不代表 Server 编译成功。
只有退出码为零、出现 `SERVER_TARGET_BUILD_SUCCESS` 且二进制与 receipt 均存在才算编译成功。
此后台入口只负责编译，未自动执行 Cook、打包或启动游戏。
运行中的 `exit_code=0`、`Result=success` 是当前默认状态，不能当作编译成功。
异常退出时状态文件可能滞后，必须同时核对 unit、实际进程和最近日志。

2026 年 10 月 14 日首次启动已确认 unit 为 active/running，用户为 ash-run。
首次日志目录为 `/home/ash-run/shootgame-dedicated-20261014/logs/20261014T104628Z`。
依赖下载已实际推进，GitDependencies 报告所需下载约 20347 MiB；C++ 编译尚未开始。
首个状态快照已备份到本地 `Saved/Deployment/20261014/InitialBuildStatus.json`。

首次运行在 ServerBuild 入口失败：Linux/Build.sh 的 -buildubt 只执行 .NET build，
产物落在 Source/Programs/UnrealBuildTool/bin/Development，未发布到启动要求的 Binaries 路径。
这不是 ShootGame C++ 错误；官方 BuildUBT.sh 包含所缺的 publish 步骤，无须修改引擎源码。
首轮完整日志已保存为 `Saved/Deployment/20261014/FirstFailedBuild.log`，服务器旧日志保留。
恢复脚本已处理 reset-failed 回收临时 unit 的情况；原脚本备份在服务器
`logs/20261014T1231-entry-backup`。重新启动仍使用原缓存、普通用户与相同资源限制。

修复后运行的日志目录为 `logs/20261014T123247Z`。
已出现 UBT_BOOTSTRAP_SUCCESS，UHT 反射代码生成通过，开始 892 个构建任务。
已实际观察到官方 clang++ 进程构建 C++ 预编译头；并发为 4，剩余磁盘约 255 GiB。
这只确认进入真实 C++ 编译；最终二进制、receipt、Cook、运行与公网连接仍须分别确认。

用户随后明确授权提高到 8 并发；停止前已完成 173/892 个任务。
2026 年 10 月 14 日 12:44 UTC 主动停止本轮 unit，保留缓存和已完成产物后增量恢复。
旧运行的退出记录属于主动调整；判断当前状态必须读取根目录 BuildStatus.json 的日志目录。
恢复后任务编号可能从 1 开始，总任务数可能减少，不能把编号重置解释为全量重编译。
原 4 并发脚本、停止前后状态与日志备份位于
`logs/20261014T124429Z-parallel8-adjustment`。
需要回退时由主执行模型恢复本轮 4 并发脚本，将本轮 unit 的 CPUQuota 恢复为 400% 后重启。
当前仍保留 20/24 GiB 内存限制、4 GiB swap 上限和 80 GiB 磁盘保护，不清理原有资源。
新运行目录为 `logs/20261014T124459Z`，UBT 实际报告 up to 8 actions。
增量恢复仅需执行 718 个任务，已复用原来 892 个任务中的 174 个完成产物。
本地恢复证据位于 `Saved/Deployment/20261014/Parallel8`，保留调整前日志与新运行初始快照。

## 已知验证缺口

本轮 Windows Dedicated 预检已接入两个 Client，并有权威射击和换弹的局部证据。
完整测试卡在换弹后检查：旧测试期待 GA_Fire 持续活动，当前生产实现完成单发立即结束。
后续应修正测试取证条件；不得为测试更改生产生命周期。
Linux 公网双客户端登录与角色复制已验收。
移动、开火、换弹、伤害、死亡、重生尚未在当前 Linux 公网部署中完成操作验收。
无画面客户端未验证视觉和音效；五条不变量均不能宣称完整 PASS。

## 给跟踪模型的提示词

定时任务使用 [精简心跳提示词](../../Scripts/Deployment/LinuxServerHeartbeatPrompt.txt)。
这是同一聊天的续接任务；已有基线时不重复读取 AGENTS.md、本文、源码或工具实现。
每次只查询一轮，正常推进保持安静，只有实质变化或需要用户处理时通知。
本地短检查点位于 Saved/Deployment/20261014/HeartbeatState.json，不保存凭据。
首轮编译终态确认后，该编译定时任务已暂停；本轮部署不继续轮询旧构建。

## SSH 连接持续性

2026 年 10 月 14 日北京时间 21:09 的只读审查发现到服务器的 Established 连接，
对应交互式 ShootGameRemote.py 进程；这证明当时连接存活，不证明会永久持续。
交互模式可在同一聊天内复用工具 session_id，Paramiko keepalive 为 20 秒。
--status 模式每次查询后主动关闭连接，不适合作为每次唤醒都重新登录的默认方案。
交互模式没有自动重连，也未持久保存密码；网络断开、进程或应用退出后仍需要重新认证。
优先复用本聊天连接；断线时尝试用本聊天已有授权凭据重新登录一次，缺少凭据只告警一次。
密码不加入定时提示词、检查点或日志；可靠的无人值守重连需另行配置受保护凭据或只读密钥。
本次没有新增凭据存储，也没有修改服务器认证配置；已有后台 systemd 构建不依赖 SSH 存活。

## 外部依据

- [Epic 源码构建流程](https://dev.epicgames.com/documentation/en-us/unreal-engine/building-unreal-engine-from-source?application_version=5.6)
- [Epic Linux 工具链版本表](https://dev.epicgames.com/documentation/unreal-engine/linux-development-requirements-for-unreal-engine?application_version=5.6)
- [Epic Dedicated Server 流程](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-dedicated-servers-in-unreal-engine?application_version=5.6)
- [Epic 引擎版本字段](https://dev.epicgames.com/documentation/en-us/unreal-engine/versioning-of-assets-and-packages-in-unreal-engine?application_version=5.6)

本地实际差异：Launcher 安装版无 Server 构建支持，因此 Server 使用固定 tag 的源码副本。
本轮单独编译 Server，已验证 Windows Editor 能 Cook LinuxServer；原生工具完成 Linux 打包。
