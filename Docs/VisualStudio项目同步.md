# Visual Studio 项目同步

本项目继续使用传统 `ShootGame.sln`。UnrealBuildTool 负责实际编译；Visual Studio 中已加载的 `.vcxproj` 只影响解决方案资源管理器、IntelliSense 和由当前 VS 会话提供的工程视图。

## 固定刷新入口

在项目根目录执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Development\RefreshVisualStudioFiles.ps1
```

需要覆盖默认引擎或项目路径时：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\Scripts\Development\RefreshVisualStudioFiles.ps1 `
    -EngineRoot "E:\Unreal_Engine\UE_5.6" `
    -ProjectPath ".\ShootGame.uproject"
```

脚本直接调用 UnrealBuildTool 的 `-ProjectFiles` 模式，不删除 `Binaries`、`Intermediate` 或 `.vs`。脚本使用进程间互斥量阻止两个本项目刷新任务同时写入工程文件。

在受限 Agent 环境中，UnrealBuildTool 可能因无法轮换 `%LOCALAPPDATA%\UnrealBuildTool` 日志而失败。此时应为同一脚本申请限定的沙箱外执行权限后原样重试，不得通过删除项目目录规避。

## 何时刷新

以下变化完成后，在本轮任务中集中刷新一次：

- 在现有模块中新增、删除、移动或重命名 `.h`、`.cpp`；
- 新增、删除或重命名 `*.Build.cs`、`*.Target.cs`；
- 新增、删除或重命名插件、插件模块或 `.uplugin`；
- 改变 `.uproject` / `.uplugin` 的模块或插件结构；
- 修改模块依赖后，需要同步 Visual Studio 的 IntelliSense 工程数据。

只修改已有 `.h/.cpp` 内容、文档、配置值或 Content 资产时，不需要为了形式刷新。

## Agent 推进与阻塞规则

普通源码结构变化采用以下流程：

```text
完成本轮全部文件结构改动
→ 刷新一次 VS 项目文件
→ 自动执行外部 UBT 编译与测试
→ 提醒用户 VS 可能显示“全部重新加载”
```

即使 VS 尚未点击“全部重新加载”，后续串行刷新仍可覆盖磁盘上的生成结果；用户最终重新加载时读取最后一次生成状态。因此，VS 的待重新加载提示本身不阻塞外部编译、自动化测试、文档记录或提交。

只有以下情况暂停并请求用户处理，得到确认前不继续依赖该环境的步骤：

1. 下一步必须由用户在当前 VS 会话中手动构建、调试或检查新工程结构；
2. 新增或启用模块、Target、插件后，下一步必须由当前 Unreal Editor / VS 会话加载该结构；
3. 项目文件刷新失败，自动化无法证明磁盘工程状态已更新；
4. 用户明确表示 VS 存在未保存的工程级设置，重新加载可能造成丢失。

不要自动关闭或重开 Visual Studio，也不要通过 VS 自动化强制执行“全部重新加载”。这类操作可能干扰用户未保存的编辑器状态。

## 结果判定

脚本退出码为 `0` 且输出以下标记，表示磁盘工程文件刷新成功：

```text
[Passed] Visual Studio project files were refreshed.
```

刷新只证明工程索引生成成功，不替代 `BuildEditor.ps1` 编译验证。新增模块、Target 或插件后，仍应使用项目的外部自动化入口验证 UBT 能实际发现并构建新结构。
