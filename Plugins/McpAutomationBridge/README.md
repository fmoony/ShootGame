# MCP Automation Bridge

项目内的 UE 5.6 编辑器插件，使用原生 Streamable HTTP MCP。提供资产查询、有效配置读取、普通 Actor 蓝图编辑、限定范围的动画图复制与真实图表截图。

**外部 Agent 首先阅读 [MCP 使用契约与能力边界](EXTERNAL_AGENT_GUIDE.md)**：完整列出 3 个公开工具 / 27 个 action、协议与结果解析、参数默认值、保存与重试规则、动画复制的实际限制、错误处理和已验证范围。本页是快速入口，不能代替该契约。

## 连接

- 地址：`http://127.0.0.1:3000/mcp`，默认随编辑器启动。
- 设置：Project Settings → Plugins → MCP Automation Bridge。
- C++ 工具或 schema 更新后重新编译并重启编辑器；客户端工具列表缓存可能也需要重连。
- 编辑请求在游戏线程串行执行；PIE / SIE 期间拒绝蓝图编辑。

## manage_asset

保留只读 `list`、`search_assets`、`list_folders`、`exists`，路径限制为 `/Game`。

## manage_editor_settings

| action | 参数与结果 |
|---|---|
| `list_sections` | `file`、可选 `query`；返回匹配配置节 |
| `read_config` | `file`、`section`、可选 `key`；返回字符串、数组，或所选配置节全部条目 |
| `get_settings` | 原生 `classPath`、可选 `propertyName` / `query`；返回当前 CDO 上配置属性的 `valueText` |

`file` 仅接受 `Engine`、`Game`、`Input`、`Editor`、`EditorPerProjectUserSettings`、`GameUserSettings`，默认 `EditorPerProjectUserSettings`。不接受任意磁盘路径，不写配置。

`read_config` 的 `source=merged_in_memory_config_cache` 表示 GConfig 当前合并缓存；`sourceFile` 是该缓存的目标配置路径，不能据此认定值最初来自哪个 Default/Base/User INI。`found=false` 区分不存在的键，`valueFound=true` 且 `value=""` 表示存在的空字符串；`values` 保留数组。`get_settings` 则读取编辑器当前内存配置对象，两者可能因尚未写回的编辑而不同。

```json
{"action":"read_config","file":"Engine","section":"/Script/EngineSettings.GameMapsSettings","key":"GameDefaultMap"}
```

```json
{"action":"get_settings","classPath":"/Script/UnrealEd.LevelEditorViewportSettings","propertyName":"CameraSpeed"}
```

## manage_blueprint

保留 `list_blueprints`、`get_blueprint`、`get_graph_details`、`get_node_details`、`get_pin_details`。

`get_class_defaults` 读取蓝图 Class Defaults 中可编辑属性，传 `propertyName` 可指定字段；返回的 `components` 列出可用组件模板，传 `componentName` 可读取该组件的默认配置（包括继承组件）。这些是 CDO / 组件模板值，不是关卡 Actor 实例或运行时值。读图包含嵌套子图。

| 新增 action | 主要参数 |
|---|---|
| `create_blueprint` | 新的 `blueprintPath`，`parentClass` 默认 `/Script/Engine.Actor`，可选 `save` |
| `move_blueprint` | `blueprintPath`、`destinationPath`、必须 `save=true` |
| `reparent_blueprint` | `blueprintPath`、`parentClass`、可选 `save` |
| `add_node` | `blueprintPath`、`graphName`、`nodeType`、`x` / `y`、可选 `comment` |
| `connect_pins` / `disconnect_pins` | `sourceNodeId` / `sourcePinName`、`targetNodeId` / `targetPinName` |
| `set_pin_default` | `nodeId`、`pinName`、`defaultValue`（UE 文本格式） |
| `move_node` / `delete_node` | `nodeId`；移动使用 `x` / `y` |
| `compile_blueprint` / `save_blueprint` | `blueprintPath`；返回编译错误、警告与消息，编译失败不保存 |
| `open_blueprint` | `graphName`，可选 `viewX` / `viewY` / `zoom`，否则缩放到全部节点 |
| `capture_graph` | `graphName`；截图写入 `Saved/MCP/Screenshots`，返回绝对 `imagePath` |

图操作默认 `graphName=EventGraph`。节点编辑以 GUID 或 UObject 名定位，不用可能重复的显示标题。新增节点返回 `nodeId`，随后用读取接口获取准确引脚名。

支持的 `nodeType`：

- `call_function`：提供 `functionClass` / `functionName`，例如 `/Script/Engine.KismetSystemLibrary` / `PrintString`。
- `event`：提供父类中的事件 `functionName`，可选 `functionClass`；不能重复添加已覆盖事件。
- `custom_event`：提供唯一 `eventName`。
- `branch`、`sequence`、`comment`。

连线由 `UEdGraphSchema_K2` 校验类型和方向；需要替换已有连接时默认拒绝，显式 `breakExisting=true` 才允许。重复连接/断开是幂等操作。编辑使用撤销事务并标记蓝图待保存；单纯节点编辑不会自动保存。

资产创建当前限定普通 Actor 蓝图；改父类支持普通 Actor 蓝图和 AnimBlueprint，并校验 Actor / AnimInstance 类型及继承环。移动使用 `IAssetTools::RenameAssets`，记录移动前引用者，保存目标并仅 Fix Up 源重定向器；失败结果保留已完成步骤，不伪称回滚。改父类可能使旧节点失效，返回实际编译结果，失败时保留待检查的内存状态。

`copy_anim_blueprint` 使用 `sourceBlueprintPath` 指定源动画蓝图，`blueprintPath` 指定经审计确认可替换的初始目标。要求源使用原生 AnimInstance 父类、具体 Skeleton；目标已有 Skeleton 时必须相同，为空时采用源 Skeleton，不进行动画重定向。实现的初始图检查不是完整的“没有用户编辑”证明，详见使用契约。复制前经 Asset Tools 生成并保存 `_BeforeAnimCopy` 备份（即使 `save=false`），然后在原目标资产内复制 AnimGraph、EventGraph、嵌套状态机、转换图、缓存姿势引用、变量及有效默认值。源会编译但不主动保存；目标仅在 `save=true` 且编译成功时保存。接口暂不支持带接口或父资产动画覆盖的源。

`get_blueprint` 返回动画骨架和每个图的 `graphPath`。同名转换图必须将完整 `graphPath` 传入 `graphName`，避免误选第一张图。`get_node_details.editableProperties` 包含动画节点的可编辑结构参数，供检查 Sequence、Blend Mask、IK 配置等。

先 `open_blueprint`，等编辑器完成一帧布局，再 `capture_graph`。支持 Blueprint Editor 和 Animation Blueprint Editor，包括动画状态机图；截图来自实际 Slate 图表控件，不是重新绘制的示意图，需要有渲染的编辑器窗口。

## 验证

`Scripts/Tests/TestMcpEditorTools.py` 使用 Python 标准库直接调用同一个 MCP endpoint，适用于客户端还缓存旧 schema 的场景。

```powershell
# 默认只列出工具，不修改资产。
& 'E:/Unreal_Engine/UE_5.6/Engine/Binaries/ThirdParty/Python3/Win64/python.exe' Scripts/Tests/TestMcpEditorTools.py

# 显式运行可修改资产的验收，默认使用用户指定的 NewBlueprint。
& 'E:/Unreal_Engine/UE_5.6/Engine/Binaries/ThirdParty/Python3/Win64/python.exe' Scripts/Tests/TestMcpEditorTools.py --exercise
```

验收保留 `NewBlueprint` 中标记为 `MCP acceptance:` 的演示节点，以及 `McpToolTests/BP_McpCreatedAndMoved` 生命周期测试资产（Pawn 父类）。重复运行只替换脚本标记节点，拒绝覆盖 BeginPlay 的其他连接。报告保存到 `Saved/MCP/EditorToolsValidation.json`。

`Scripts/Tests/TestMcpAnimBlueprintCopy.py` 通过真实 MCP 比较 ABP_TP_Rifle 与 NewAnimBlueprint 的所有嵌套图、节点、引脚连接、可编辑节点参数和类默认值。默认只读，`--prune` 显式删除已审计的副本遗留计算并编译保存；清理后以 `--expect-pruned` 只读复验。报告为 `Saved/MCP/AnimBlueprintCopyValidation.json`。

## 官方依据与版本边界

2026-09-08 查阅 [UE 5.8 发布说明](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes) 和 [官方 Unreal MCP 文档](https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor?application_version=5.8)：Epic 官方 MCP 在 5.8 新增，依赖 `ModelContextProtocol`、`ToolsetRegistry` 及 `AllToolsets` / 指定 Toolsets。本机 `Build.version` 为 5.6.1，相关实验插件不存在，本轮没有升级引擎或安装 5.8 插件。

本实现使用 UE 官方编辑器 API，并已对照本地 UE 5.6 头文件/实现核对签名：

- [TryCreateConnection](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/EdGraph/UEdGraphSchema/TryCreateConnection?application_version=5.5)：本地 `EdGraphSchema.cpp` / `EdGraphSchema_K2.h`。
- [ReparentBlueprint](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/BlueprintEditorLibrary/UBlueprintEditorLibrary/ReparentBlueprint?application_version=5.5)：本地 BlueprintEditorLibrary；会刷新节点并编译，不能只修改 `ParentClass`。
- [IAssetTools](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Developer/AssetTools/IAssetTools)：本地 AssetTools，移动/重定向器修复通过资产系统执行。
- 配置读取使用本地 `ConfigCacheIni.h` / `UnrealType.h`；截图使用 `FSlateApplication::TakeScreenshot` / `FImageUtils::PNGCompressImageArray`。

UE 5.6 的已知配置类别可能以 `Engine` 等短名称作为 GConfig 查询键，因此返回值单独区分 `cacheKey` 与 `FConfigBranch::IniPath` 提供的 `sourceFile`。

未恢复 `McpAutomationBridge_FullSourceArchive`，只增加当前功能直接使用的模块依赖。
