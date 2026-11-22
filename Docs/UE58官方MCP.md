# UE 5.8 官方 MCP

ShootGame 使用 UE 5.8.4 自带的 Unreal MCP，插件标识为 `ModelContextProtocol`。
旧项目桥接及其协议测试已退役，不保留同名工具、兼容适配层或复制能力的自定义 Toolset。
Epic 将该功能标为 Experimental，接口和返回格式可能继续变化。

## 启用范围与连接

项目仅为 Editor 启用以下官方插件：

- `ModelContextProtocol`：HTTP MCP 服务与工具发现。
- `EditorToolset`：资产、蓝图、对象及编辑器操作。
- `ConfigSettingsToolset`：设置面板中的属性查询与编辑。
- `SlateInspectorToolset`：编辑器控件检查与截图。

依赖的 ToolsetRegistry、PythonScriptPlugin 等由引擎解析。
不启用 AllToolsets，不把 MCP 用作正式 Game 或 Dedicated Server 的运行接口。
自动化、网络与截图脚本通过命令行禁用服务，避免多个编辑器争用端口。

编辑器默认自动启动服务，地址为 `http://127.0.0.1:8000/mcp`。
如本机用户设置覆盖默认值，以 Editor Preferences 的实际配置和监听地址为准。
只使用回环连接，不添加 Public 入站允许规则，不对外暴露无鉴权服务。

项目的 `.codex/config.toml` 只声明本机回环 URL，不含引擎绝对路径或令牌。
应从当前正式 Worktree 打开受信任的 Codex 项目，再重新连接 MCP。
现有聊天不保证动态加载新配置；未受信任的项目不会加载项目级配置。
不修改全局配置，不在源码引擎中执行 GenerateClientConfig。
源码构建的该命令可能把配置写入引擎根目录。

## 调用与保存边界

先 initialize，再 tools/list，按服务实际协商的协议版本发送后续请求。
默认发现入口是 list_toolsets、describe_toolset、call_tool。
本机初始化响应的 serverInfo 名称和版本为空，源码中也未赋值。
该差异未通过修改引擎修复；严格依赖身份字段的客户端仍需单独验证。
先发现工具集和参数，再执行具体操作；不发送旧 manage_blueprint 等 action。
以实际 schema 的 required 为准，不能从 Python 默认参数推断可以省略字段。
例如 find_nodes 的 title 过滤器需要显式传入空字符串。
服务端工具在游戏线程执行，同一编辑器的修改必须串行。

先检查目标蓝图、父类、图、节点、引脚和当前连接，再修改并读回。
编译成功、保存成功和真实运行表现是不同证据，不互相替代。
保存时必须明确列出目标资产，禁止用空列表调用 save_assets。
官方空列表语义是保存所有脏资产，不符合项目按目标保存的边界。

旧桥接的路径白名单、PIE/SIE 拒绝及连接替换保护不能假定仍然存在。
编辑、编译和保存前确认已退出 PIE/SIE；不自动扩大资产范围。
失败或超时后先读取目标状态，禁止盲目重复创建、移动或连线。
截图需要实际渲染窗口；不能把接口成功当作画面已验收。
普通配置工具不等价于旧桥接的 GConfig 合并缓存查询。

## 退役范围

Plugins/McpAutomationBridge 的源码、描述文件和生成物从当前工作区移除。
以下专用测试随旧桥接退役，不移植旧客户端和返回格式：

- Scripts/Tests/TestMcpEditorTools.py。
- Scripts/Tests/TestMcpAnimBlueprintCopy.py。

旧 copy_anim_blueprint 的图移植、嵌套状态机和备份契约不再提供。
官方整资产复制不能直接视为该流程的等价替代。
既有动画资产保留；本轮不做资产删除、父类替换或生产 AnimClass 修改。
历史报告保持原文，其中旧路径及命令只用于解释历史验证环境。

## Blender 与下蹲内容

RifleTools 使用后台 Blender、UE Python Commandlet 和原生 Editor 夹具。
它不依赖旧 MCP，脚本、blend、fbx、姿态数据和导出设置全部保留。
路径适配与动画重放另行验收，见 [Blender 工具链](Blender动画编辑工具链.md)。

下蹲与滑铲已迁入 UE58 Worktree 的原隔离地图，使用独立动画脚本与 Editor 夹具。
当前来源动作与验证边界见[下蹲实验][stance]，不依赖 MCP。
原 main 的实验原件与其他未提交修改保持不变。

## 本机构建与验证边界

本轮使用 -UsePrecompiled 与 -NoEngineChanges 复用已验收引擎。
构建只生成项目 Editor 模块，未重编引擎；项目与引擎 Build ID 由 UBT 正常保持一致。
常规共享目标构建曾计划改写 IrisCore、NetCore 和引擎模块清单，已由保护门禁阻止。
如后续构建仍计划覆盖引擎 DLL、PDB 或模块清单，先核对原因，不直接放开保护。

官方工具已验证发现、资产和图读取、配置分类及临时蓝图编辑、编译和指定保存。
Rifle 事件图截图已实际查看；探针资产已通过官方 API 删除。
旧动画图复制和 Blender 重放不属于本轮验收，正式打包联机也没有重新执行。
Codex 项目配置已提供，本轮直接通过 HTTP 验证服务；新聊天的发现状态需重连确认。

## 官方依据

- [Epic Unreal MCP](https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor)
- [Codex MCP 配置](https://learn.chatgpt.com/docs/extend/mcp?surface=cli)

[stance]: 验证/Stance_20261022/README.md
