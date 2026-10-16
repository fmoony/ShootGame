# Linux 构建环境完整备份

## 范围与保存位置

截至 2026 年 10 月 16 日，用户已要求删除 G 盘未完成下载，删除已完成。
已删除目录 `G:/ShootGame_Backups/FullBuild-20261015T140629Z`，不能再从原本地断点续传。
共删除 26 个文件、2798811395 字节，释放约 2.61 GiB；删除后 G 盘约有 84.1 GiB 可用。
其中部分分卷为 2717908992 字节，其余为清单、状态、日志及校验器自检文件。
D 盘的完整 Linux Server 运行包仍保留，但本地没有完整的构建环境备份。

任务 `full-20261015T140629Z` 的远端归档曾完成，共 54 卷、57638613519 字节。
约 53.7 GiB 的压缩归档通过来源一致性检查，但下载暂停前没有完整本地分卷。
10 月 16 日 SSH 检查未读到协议 banner；无法据此确认服务器关闭或归档仍可取回。
用户已暂缓服务器连接问题；本次清理没有连接服务器，也没有删除远端资源。

- 来源：服务器 `/home/ash-run/shootgame-dedicated-20261014`。
- 原本地目录：`G:/ShootGame_Backups/FullBuild-20261015T140629Z`，已删除。
- 远程输出：来源目录下 `backups/full-20261015T140629Z`。
- 完整保留 engine、project、cache、scripts、logs、incoming、packages、stage 等。
- 包括各处 Binaries、Intermediate、官方 Clang SDK、NuGet 与 GitDependencies 缓存。
- 排除 backups/full-*，避免把完整备份再次装入自身；原资源全部保留。
- 原始逻辑文件量约 102.2 GiB，文件与目录清单共 402960 项。

这是构建工作区备份，不包含整台服务器的系统镜像或其他用户的模型实验。
固定引擎为 5.6.1-release，发布 CL=44394996，兼容 CL=43139311。
Linux Editor 尚未编译，当前资源 Cook 仍依赖本地 Windows Launcher UE5.6.1。

## 制作与传输

服务端由 ash-run 运行 FullLinuxBuildBackup.py，使用 GNU tar 的 PAX 格式与 zstd。
保留 Linux 权限、所有者、链接、ACL、扩展属性与纳秒时间戳。
使用约 1 GiB 分卷，逐卷及完整压缩流均计算 SHA256。
完整输出上限为 72 GiB，磁盘余量低于 80 GiB 时停止本轮归档。
本次 unit 限制 4 CPU，MemoryHigh 4 GiB、MemoryMax 6 GiB。

本地 DownloadLinuxBuildBackup.py 等待服务端完成并核对来源稳定性，再检查 G 盘空间。
下载使用 8 路 SSH 连接，保留至少 10 GiB 余量；SSH 密码只在进程内存中使用。
已有分卷须通过哈希检查，未完成分卷须通过远端前缀哈希检查，才能继续下载。
不覆盖其他用户文件，不启动 WSL，不修改系统防火墙，不删除原构建资源。

## 验收标准与状态文件

- Status.json：远端归档阶段；只有 Complete 才表示归档制作成功。
- BackupManifest.json：分卷名称、大小、SHA256、完整压缩流 SHA256 与元数据清单。
- FileManifest.jsonl：来源文件与目录的类型、大小、权限、所有者和时间戳。
- SourceConsistency.json：归档前后来源元数据与数量的一致性检查。
- SHA256SUMS：分卷与关键元数据的独立校验清单。
- DownloadStatus.json：本地下载进度；PartsVerified 仅表示分卷接收并逐一校验。
- Verification.json：最终流式归档校验；须为 Verified，不代表已执行恢复或编译。

VerifyLinuxBuildBackup.py 连接分卷后进行流式解压校验，不在 Windows 展开文件树。
除逐卷与完整流的 SHA256，还检查归档成员、大小、权限、UID/GID、时间戳和链接目标。
源码清单中的零字节备份锁文件也匹配 tar 排除模式，校验报告会单独记录该项。
小型独立样本已验证中文路径、符号链接、纳秒时间戳，以及对错误文件大小的拒绝。

## 日后恢复边界

当前没有完整本地归档，下述流程仅供未来重新取得全部分卷后使用。
压缩归档存储不需要容纳解压后的工作区；恢复则需要另外准备充足的 Linux 磁盘空间。
恢复前先执行 SHA256SUMS 检查，再按归档附带的 RESTORE.md 操作。
必须使用空目录，避免覆盖已有用户文件；建议恢复原路径与 ash-run UID/GID 1000。
系统依赖版本见 Environment.json；同版编译工具链已包含在归档中。
保留增量编译条件，不承诺换机后零重编；实际复用情况须由日后的 UBT 验证确认。

只有确认远端仍可访问、归档完整且用户重新授权下载后，才可运行以下脚本。
原本地分卷及状态已删除，此次将重新下载；凭据通过交互提示输入。
下载器取消时的 Paramiko 预取线程关闭异常尚未修复，重新启用前需处理。

```powershell
python Scripts/Deployment/DownloadLinuxBuildBackup.py full-20261015T140629Z
```

全部分卷下载完成后，校验需读取整个归档，但不会展开或编译：

```powershell
python Scripts/Deployment/VerifyLinuxBuildBackup.py G:/ShootGame_Backups/FullBuild-20261015T140629Z
```

完整运行包独立位于 `Saved/Deployment/20261014/ReleaseBackup-LvlTest/LinuxServer.tar.gz`。
该文件此前已通过 SHA256 核对，本次删除后也确认仍存在；它支持部署既有版本。
运行包不包含完整源码构建环境，不能替代未来修改 C++ 后的重新编译。
