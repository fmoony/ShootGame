# MCP 外部 Agent 使用契约与能力边界

核对日期：2026-09-09。适用实现：本仓库 `McpAutomationBridge`、UE 5.6.1、服务器版本 `0.3.0-editor-tools`。本文依据当前 schema、handler、transport 源码和 2026-09-08 的实际验收整理；能力是否已加载，以目标编辑器的 `initialize` / `tools/list` 为准。相同版本字符串下也可能有尚未重启加载的代码更新。

## 1. 首次接入必须知道的事

1. 这是随 Unreal Editor 运行的项目内桥接。普通 Commandlet 不启动 MCP；不提供独立资产编辑服务。编辑器关闭、插件禁用、端口冲突都会导致无法连接。
2. 当前公开注册 **3 个工具、27 个 action**：`manage_asset` 4 项、`manage_editor_settings` 3 项、`manage_blueprint` 20 项。不要根据旧插件归档、registry 名称白名单或其他 Unreal MCP 的文档推测能力。
3. 先读目标资产、父类、图路径、节点和引脚，再修改；使用返回的标识，不从界面显示名猜参数。
4. 所有 Blueprint 编辑、编译、保存、打开和截图 action 均经过 PIE/SIE 拒绝检查。读接口没有这个统一限制，但不因此成为运行时实例调试接口。
5. 对同一编辑器的修改必须串行，并等待每次最终响应；多个 Agent 共享同一内存和资产，没有会话级隔离、资产锁、修订号或原子批次。
6. 工具可调用不等于资产已获授权。历史验收使用 NewBlueprint / NewAnimBlueprint，不构成外部 Agent 自动修改它们的长期许可。按当前用户任务确定目标和第一人称、第三人称、服务器职责。
7. 只读任务不要“顺便”compile/save，也不要运行 `--exercise` / `--prune` 验收脚本；这些动作会改动编辑器或资产。

## 2. 连接、协议和返回格式

### 2.1 地址与鉴权

- 默认 `http://127.0.0.1:3000/mcp`。设置入口：Project Settings → Plugins → MCP Automation Bridge。
- 默认 `bEnableNativeMCP=true`、`bLoadAllToolsOnStart=true`、`bRequireCapabilityToken=false`、`bAllowNonLoopback=false`。以实际环境设置为准。
- 启用令牌后，每次 HTTP 请求发送 `X-MCP-Capability-Token`；缺失或不匹配返回 HTTP 401。此令牌是整个桥接的通行凭据，没有按 action / 资产 / Agent 分权。
- 非回环监听需显式允许；桥接自身没有 HTTPS/TLS 或 OAuth。不要把它当作已具备互联网部署条件的服务。
- 原生客户端通常没有 `Origin`。浏览器请求不是默认放行：实现仅在回环监听且启用非空令牌时允许合法的本机 http/https Origin；其他非空 Origin 返回 403。不要用伪造 Origin 处理鉴权问题。
- 配置读取没有敏感字段脱敏。`get_settings` 也可读取原生配置类的 Config 属性；外部 Agent 应只查询任务所需字段，不把配置全集、令牌或密钥复制到共享报告。

### 2.2 会话顺序

```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"external-agent","version":"1.0"}}}
```

1. POST 使用 `Content-Type: application/json`，推荐 `Accept: application/json, text/event-stream`；请求体使用正常 Content-Length，不依赖 chunked 请求体支持。
2. 保留 initialize 响应头的 `Mcp-Session-Id`。服务器固定返回协议 `2025-03-26`，没有完整的多版本协商实现。
3. 后续携带该会话头及 `MCP-Protocol-Version: 2025-03-26`，发送无 id 的 `notifications/initialized`，随后请求 `tools/list`。合法 notification 返回 HTTP 202 空体，当前实现忽略 notification 内容；它不执行取消操作。
4. 用 `tools/call` 包装下文 action 参数。正常工具调用返回 SSE；其他结果也可能是普通 JSON。依 Content-Type 解码。
5. SSE 只在收到与本请求 id 相同的最终 `result` 或 `error` 后结束等待，不能把 progress / keepalive 当作结果，也不应等到持久连接关闭才读取结果。
6. GET `/mcp` 可建立通知 SSE，需 `Accept: text/event-stream`；DELETE `/mcp` 结束会话。无需为普通调用建立 GET 流。

```json
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"manage_blueprint","arguments":{"action":"get_blueprint","blueprintPath":"/Game/Shooter/Blueprints/NewBlueprint"}}}
```

### 2.3 三层成功判定

- HTTP 成功不代表 JSON-RPC 或工具成功。先检查 JSON-RPC `error`，再检查 `result.isError`。
- 工具结果当前是 `content:[{"type":"text","text":"说明\n\n{JSON数据}"}]`，没有独立 `structuredContent` / `outputSchema`。以第一处双换行拆分说明与数据；有些错误没有 JSON 数据，不要无条件解析。
- 错误代码在说明文本 `Error [CODE]: ...` 中。编译、保存、移动等部分失败可能同时包含状态数据，应保留这些诊断。
- `compileStatus` 是 UE 数值枚举；优先查看 `errors`、`warnings`、`messages`、`isError`。0 错误不保证已经保存；需要 `saved=true`。
- 通用 `saved` 表示本次 action 是否保存了 Blueprint 包。`capture_graph` 可成功写出 PNG，同时 `saved=false`；截图文件看 `imagePath`，不能用该字段判断截图失败。

### 2.4 超时与重试

当前实现：最多 16 个并发连接；请求头上限 8 KiB、请求体上限 5 MiB；HTTP 头和 socket 写超时约 5 秒；工具请求超时 300 秒；会话空闲超时 30 天；GET 通知流每会话最多 4 条、最长 1 小时、keepalive 约 30 秒。编辑器游戏线程被阻塞时，清理 tick 本身也可能延后。仓库示例 Python 客户端的 HTTP 超时是 180 秒，短于服务器值。

- 缺少会话头为 400，过期/未知会话为 404；重新 initialize 并重新读取工具列表。
- socket 断开、客户端超时、会话 DELETE、`TIMEOUT` **不保证撤销或取消已排队/正在执行的编辑**。源码没有客户端取消对应的资产回滚。
- 结果不确定时先读回目标路径、图和保存状态。禁止盲目重试 create/move/copy/add_node，避免重复节点、额外备份或部分移动。
- 编辑器在后台可能降低 tick 频率。先看日志和进程状态，不因短暂无响应重复发送修改。

## 3. 统一路径、定位与分页

| 参数类型 | 外部 Agent 应发送的形式 |
|---|---|
| 资产文件夹 | `/Game/Shooter/Blueprints`，不是 Windows 路径或 Content Browser 显示名称 |
| Blueprint 资产 | `/Game/.../BP_Name` 或 `/Game/.../BP_Name.BP_Name` |
| 父类 / 函数所属类 | 原生 `/Script/Engine.Actor` 等，或适用时完整 `...BP_Name_C` 生成类路径 |
| 图 | 优先 `get_blueprint.graphs[].graphPath`；顶层可用 `EventGraph` / `AnimGraph` |
| 节点 | `nodeId` 使用 `get_graph_details.nodes[].id` 的 GUID；编辑也接受 UObject `name` |
| 引脚 | `pins[].name`，保留大小写与方向。执行输出常为 `then`，输入常为 `execute`，不能仅凭显示文案猜测 |

编辑仅允许合法 `/Game/` 包路径，拒绝 `..`、`__ExternalActors__`、`__ExternalObjects__`。读接口对部分 `Content/...`、生成类 `_C` 有兼容处理；不要依赖这些别名，尤其编辑不自动把 `_C` 转成资产名。原生 `/Script` 类引用与动画使用的 Engine 资源不等于允许编辑 `/Engine` 资产。

列表与 `get_graph_details` 的分页：`offset=0`、`limit=100`；limit 限制 1～500，offset 不小于 0；按 `totalCount` 或 `totalNodeCount` 持续翻页。`list_folders`、配置结果、单节点和默认值读取无该分页。`recursive` 默认 true。

不要用节点标题作为可靠主键。读取兼容标题并取首个匹配，编辑不接受标题。嵌套图可能重名，例如两张 Transition；使用完整 graphPath。特别注意：`get_node_details` / `get_pin_details` 的图名无效时，当前实现可能回退到所有图搜索；不能把成功响应视为你提供的图路径已被验证。先用 `get_graph_details` 确认图，再用 GUID 查询。

## 4. manage_asset：4 个只读 action

| action | 输入 | 数据与边界 |
|---|---|---|
| `list` | 可选 path/query/class/recursive/offset/limit | 返回 root、totalCount、count、assets；资产含 name/objectPath/packageName/folder/class |
| `search_assets` | 同 list | 与 list 相同的过滤逻辑；query 是名称或包路径的大小写不敏感子串，不是语义搜索 |
| `list_folders` | 可选 path/recursive | 返回 Registry 中的子目录 folders；不创建文件夹，不枚举任意磁盘 |
| `exists` | path | 查询 Asset Registry；返回 exists，可附 asset。不是依赖完整性或资产可编译证明 |

`class` 过滤精确匹配短类名或完整资产类路径，不自动包含派生类；Blueprint 类型发现优先用 `list_blueprints`。Registry 可能包含编辑器中尚未保存的资产；列表成功不证明文件已经落盘。

**没有**通用资产导入/导出/删除/复制、资产依赖图、引用者查询、材质编辑、音频参数编辑等公开 action。`move_blueprint` 附带的引用者结果不是独立依赖查询 API。

## 5. manage_editor_settings：3 个只读 action

| action | 必要及可选输入 | 返回/限制 |
|---|---|---|
| `list_sections` | 可选 file/query | sections 配置节名称列表 |
| `read_config` | section；可选 file/key | 无 key：found、entries（`Key=Value` 文本）；有 key：found、valueFound、可选 value、values 数组 |
| `get_settings` | classPath；可选 propertyName/query | 原生 `/Script` 且具有 CLASS_Config 的类 CDO，返回 CPF_Config 属性的 name/type/valueText |

file 仅为 `Engine`、`Game`、`Input`、`Editor`、`EditorPerProjectUserSettings`、`GameUserSettings`；默认 `EditorPerProjectUserSettings`。action/file 使用文档中的精确拼写。

- `read_config.source=merged_in_memory_config_cache`：读取当前 GConfig 合并缓存，不重载磁盘。`cacheKey` 与 `sourceFile` 分开；sourceFile 为合并配置分支路径，可能为空，**不能定位具体值最初出自哪一层 INI**。
- `found=false` 表示没找到；存在空字符串为 `valueFound=true,value=""`；显式空数组可以 `found=true,valueFound=false,values=[]`。不要把这三种情况混同。
- `get_settings.source=live_class_default_object`：当前配置 CDO 值，可能不同于 GConfig 缓存。未指定 propertyName 时，found=true 不保证 query 匹配到属性，仍需检查 properties。
- 不能读任意文件、不能写配置、不能查询任意对象所有属性；propertyName 为顶层精确属性名，不是 `A.B[0]` 路径表达式。

## 6. manage_blueprint：6 个读取 action

除 list 外，使用 blueprintPath。读取会加载 Blueprint/CDO，可能触发 UE 正常加载初始化，但 handler 不主动保存资产。

| action | 其他输入 | 返回/边界 |
|---|---|---|
| `list_blueprints` | 可选 path/query/recursive/offset/limit | 包括 UBlueprint 派生资产，如 AnimBP/WidgetBP；返回名称、路径和资产类型 |
| `get_blueprint` | 无 | parentClass、graphs、graphCount；图含 graphPath/class/type/nodeCount；AnimBP 另含 skeleton/isTemplate |
| `get_graph_details` | graphName；可选 offset/limit | 节点 id/name/title/class/x/y/comment 及 pins/links；包含嵌套状态和转换图 |
| `get_node_details` | nodeId（读也兼容 nodeName）；建议 graphName | 节点、引脚和 editableProperties，后者为 CPF_Edit 且非 CPF_Transient 的 UE 导出文本 |
| `get_pin_details` | nodeId、pinName；建议 graphName | 精确引脚类型、方向、默认值和 links；读接口无 direction 参数，同名引脚应结合图详情判断 |
| `get_class_defaults` | 可选 propertyName/componentName | CPF_Edit 或 CPF_BlueprintVisible 属性，source/objectPath/packageDirty/components/properties/found |

`get_class_defaults` 默认读取 Blueprint 生成类 CDO；指定 componentName 则读取原生默认子对象或 SCS 组件模板（含继承）。先省略 componentName 获取列表。返回值不是关卡实例、PIE Pawn、动画预览实例或网络角色的实时值；packageDirty 也不表示具体哪个字段与磁盘不同。

`editableProperties` 是反射文本快照，不是全部内部/非反射状态，也不是任意属性写入接口。图 `type=unknown` 可能是状态机或转换图，结合 `class` 判断，不能当作读取失败。

## 7. manage_blueprint：14 个有副作用的 action

以下均需 blueprintPath；图操作默认 EventGraph。所有 action 均要求编辑器可用且不处于 PIE/SIE。

| action | 输入与限制 | 编译、保存及其他副作用 |
|---|---|---|
| `create_blueprint` | 新路径；parentClass 默认 `/Script/Engine.Actor`，需可创建 Blueprint 的 Actor 父类；可选 save | 创建普通 UBlueprint 并注册、编译；save 默认 false；目标存在拒绝覆盖 |
| `move_blueprint` | destinationPath；**save=true 必须** | 编译源→Asset Tools 移动→保存目标→尝试修复源 redirector；报告 referencersBeforeMove/moved/saved/redirectorFixed；可能保存相关引用包 |
| `reparent_blueprint` | parentClass；可选 save | 普通 Actor BP 或 AnimBP，父类分别需 Actor/AnimInstance；拒绝继承环；刷新节点并编译；不自动迁移不兼容逻辑，编译失败保留待检查状态 |
| `copy_anim_blueprint` | sourceBlueprintPath；可选 save | 见第 8 节；**即使 save=false 也会创建并保存目标备份** |
| `compile_blueprint` | 可选 save | 编译并可能重建生成类、引脚等；save=true 时成功后保存，默认不保存 |
| `save_blueprint` | 无 | 总是先编译，成功后保存目标包；不是 Save All，也不保存所有动画依赖 |
| `add_node` | nodeType、graphName；可选 x/y/comment 和类型参数 | 在内存中创建节点，返回 nodeId/nodeName；不自动保存 |
| `connect_pins` | sourceNodeId/sourcePinName/targetNodeId/targetPinName；可选 breakExisting=false | 同一图内输出到输入，经实际 Schema 检查；可能创建自动转换节点；不自动保存 |
| `disconnect_pins` | 同上四个端点参数 | 断开指定一条连接，不自动保存 |
| `set_pin_default` | nodeId/pinName/defaultValue 字符串 | 仅未连接、可编辑的非 exec 输入，经 Schema 校验；不设置类默认值、节点结构或组件属性；不自动保存 |
| `move_node` | nodeId；x/y | 缺少某坐标时保持原值；移动节点而非资产；不自动保存 |
| `delete_node` | nodeId | 尊重 CanUserDeleteNode；删除及断线，不自动保存 |
| `open_blueprint` | graphName；可选 zoom/viewX/viewY | 打开并激活图，影响 UI；省略 zoom 为 ZoomToFit；zoom 限制 0.1～1.0，指定 zoom 时 viewX/Y 默认 -100 |
| `capture_graph` | graphName | 打开对应图并捕获真实 Slate 控件，写 PNG；需要渲染窗口，输出 imagePath/width/height；不是角色视口截图 |

### 7.1 图编辑实际支持范围

- 节点增删、移动、默认值、连线要求可编辑、继承 K2 Schema 的图。AnimGraph 和某些状态内部图也可能满足；**不等于支持任意 UE 图类型**。
- 状态机图可读、可打开、可截图、可随整个 AnimBP 复制；没有独立“创建 State / Transition / 状态机”的 action。普通 connect_pins 不能替代动画状态机专用编辑 API。
- add_node 的六种 nodeType：`call_function`、`event`、`custom_event`、`branch`、`sequence`、`comment`。
- call_function 必须给 functionClass/functionName，函数需 Blueprint 可调用；event 使用父类可作为事件放置的函数，functionClass 可省略，禁止重复覆盖，且必须在事件图；custom_event 必须有唯一 eventName。
- 没有通用节点 class 工厂；不能直接新建 SequencePlayer、BlendSpace、IK、自定义变量 Getter/Setter、动画状态机或任意宏实例。
- connect/disconnect 在“目标状态已成立”时返回 changed=false，是幂等；其他多数 action 不提供幂等承诺。
- 默认拒绝占用执行输出、占用数据输入或 Schema 要求打断现有连接的情况；breakExisting=true 仅放开替换限制，不绕过类型检查。自动转换可能使实际结果成为两条连线，必须读回。
- 图编辑标记结构修改，UE 可能触发生成类/骨架更新；“不自动保存”不应理解成“完全不编译、不触发编辑器更新”。
- 事务有助于编辑器 Undo，但没有远程 undo/redo action，也没有“失败必定还原”的保证；已落盘的资产、备份和 redirector 处理不能视为同一内存事务的原子回滚。

## 8. 动画复制的精确边界

`copy_anim_blueprint` 用于把一个具体 AnimBP 的编辑图结构放进新建目标。父类采用 **源的原生 AnimInstance 父类**；这里不读取额外 parentClass 参数。

实现拒绝：源/目标不是 AnimBP、同一路径、源为 template 或无 Skeleton、源父类非 native AnimInstance、源或目标有 ImplementedInterfaces、源有 ParentAssetOverrides、目标有 NewVariables/MacroGraphs/DelegateSignatureGraphs、目标含非初始类型的图/节点。若目标已有非空 Skeleton，必须与源为同一 Skeleton 对象；若目标 Skeleton 为空，会采用源 Skeleton。**不是跨骨架 retarget 工具**。

“空目标”检测是有限的结构检查：每图最多 3 节点、图名只允许 EventGraph/AnimGraph、允许几种初始节点和 TryGetPawnOwner/BlueprintUpdateAnimation 调用；并未完整证明所有节点、事件或引脚都未经用户编辑。因此外部 Agent 仍须先完整读取目标，确认允许替换，不能只依赖 TARGET_NOT_EMPTY 保护。该限制本轮只记录，没有修改实现。

复制内容：源变量描述、FunctionGraphs/UbergraphPages/MacroGraphs/DelegateSignatureGraphs 及内部对象；映射源/目标 Blueprint、生成类、骨架类、CDO 和图对象引用；复制 Skeleton、Groups、部分动画更新选项、预览 Mesh；编译后复制同名同类型、可编辑或 BlueprintVisible、非瞬态/非实例化引用的默认属性。清空目标父动画覆盖和 Pose Watches，刷新编译扩展。

不承诺复制所有 Blueprint 元数据、接口实现、任意插件扩展状态或用户工作区 UI 配置。不会深拷贝外部动画 Sequence、BlendSpace、AimOffset、Skeleton、Blend Mask 等资产；这些仍引用已有共享资源。不会自动清理源中的无用节点，不自动重命名状态，不自动把副本设为角色 AnimClass。NewAnimBlueprint 的 19 个遗留节点由专门验收脚本另行清理，不是 copy action 自带行为。

执行顺序：验证条件和源编译→Asset Tools 生成唯一 `目标_BeforeAnimCopy` 备份并保存→关闭目标资产编辑器→替换目标内部图及父类→编译→按 save 决定保存。源会被编译但不主动保存。备份是独立资产，不是自动还原机制。失败时保留 backupPath、编译信息；若调用中断，先检查编辑器和备份，不再次盲目 copy。

## 9. 未提供的能力

- 任意 Python、C++、控制台命令执行；文件系统通用读写；安装插件；编译 C++；启动/关闭 Editor；启动 PIE 或控制游戏。
- 通用 Actor 创建、选择、关卡移动、世界实例属性、运行时变量/复制状态、网络测试控制。
- 任意 Blueprint 变量/函数/宏/接口/组件创建；直接修改 Class Defaults、AnimClass、Skeleton、资产引用、节点结构体参数；通用 AnimBP/WidgetBP 创建工厂。
- 通用资产删除、任意资产移动、动画重定向、动画序列编辑、材质/音频/GAS/AI 图编辑。
- 跨编辑器同步、资产锁、多人冲突解决、通用依赖审计、自动源控制提交、远程 Undo/Redo。

源码白名单中的 control_editor、control_actor、animation_physics、manage_audio 等名称，以及 `McpAutomationBridge_FullSourceArchive`，都不能证明当前支持这些能力。

### 隐藏兼容入口 manage_tools

当前没有注册 manage_tools schema，正常 tools/list 不公布；transport 仍保留同名特殊分支。源码有 list_tools/list_categories/get_status，以及 enable_tools/disable_tools、enable_category/disable_category、reset（tools 数组或 category 参数）。它修改当前服务器工具开关，影响所有会话，不保存为逐 Agent 权限。core 分类不能整体禁用，但分类内普通工具可以单独禁用。

此处披露是为了完整说明实际边界，不把未发现的入口当成稳定客户端 API；普通外部 Agent 只依赖 tools/list 公布的工具。维护诊断若使用此兼容入口，须按操作结果检查 notFound/protected 等字段，不能靠 enable 请求恢复未注册的旧工具。

## 10. 错误后的处理

| 代码 / 现象 | 应如何处理 |
|---|---|
| `INVALID_ARGUMENT` / `INVALID_PAYLOAD` / `ACTION_NOT_SUPPORTED` | 核对 tools/list、参数名和 action，不能把未知 action 当作服务离线 |
| `INVALID_PATH` / `INVALID_CONFIG_FILE` | 使用合法资产路径或六个配置类别；不尝试绕过路径限制 |
| `EDITOR_BUSY` / `NO_RENDERING` | 编辑器状态不满足；只读分析可继续，依赖操作待环境满足 |
| `ASSET_EXISTS` / `TARGET_NOT_EMPTY` / `SKELETON_MISMATCH` / `UNSUPPORTED_ANIM_BLUEPRINT` | 先读回目标和依赖，不能自动覆盖/清空以规避错误 |
| `BLUEPRINT_NOT_FOUND` / `GRAPH_NOT_FOUND` / `NODE_NOT_FOUND` / `PIN_NOT_FOUND` | 重新发现路径/GUID/引脚；检查同名图、分页和节点重建 |
| `CLASS_NOT_READY` | 读取需要生成类；只读任务先报告限制，不自动编译 |
| `CLASS_NOT_FOUND` / `COMPONENT_NOT_FOUND` | 核对原生配置类或默认组件列表 |
| `INVALID_PARENT` / `FUNCTION_NOT_FOUND` / `INVALID_FUNCTION` / `INVALID_EVENT` | 核对类型、继承、可调用性或重复事件 |
| `INCOMPATIBLE_PINS` / `LINK_REPLACEMENT_REQUIRED` / `CONNECTION_FAILED` | 读回连线和 Schema 诊断；只有当前意图确需替换才传 breakExisting=true |
| `INVALID_PIN` / `INVALID_DEFAULT` / `DELETE_REFUSED` / `UNSUPPORTED_GRAPH` | 图/节点/输入引脚不满足支持边界；不要猜测通用工厂或任意属性写入 |
| `COMPILE_FAILED` / `SAVE_FAILED` / `MOVE_FAILED` / `REDIRECTOR_REMAINS` | 保留结果中的已完成步骤；检查内存及磁盘状态，不能声称回滚或已保存 |
| `BACKUP_FAILED` / `CREATE_FAILED` | 核对资产状态与日志，失败不等于没有留下任何新对象 |
| `OPEN_FAILED` / `UNSUPPORTED_EDITOR` / `GRAPH_NOT_READY` / `CAPTURE_FAILED` | 确认支持的资产编辑器和渲染帧，open 后再 capture |
| `CONFIG_UNAVAILABLE` | 配置类别或缓存未加载，不等于值为默认 0/false |
| `TOOL_DISABLED` | 检查当前工具列表和服务状态；未知/未注册工具也可能呈现此错误 |
| `TIMEOUT` / 无最终响应 | 先读回，按“结果未知”处理，不自动重放修改 |

## 11. 可复用调用流程

### 只读审计

```json
{"action":"list_blueprints","path":"/Game/Shooter","query":"ABP_TP_Rifle"}
```

随后 get_blueprint → 按每个 graphPath 分页 get_graph_details → 对关键节点 get_node_details → 对所需字段 get_class_defaults。配置则独立调用 manage_editor_settings：

```json
{"action":"read_config","file":"Engine","section":"/Script/EngineSettings.GameMapsSettings","key":"GameDefaultMap"}
```

```json
{"action":"get_class_defaults","blueprintPath":"/Game/Shooter/Blueprints/Weapons/BP_ShooterWeapon_Rifle","propertyName":"FirstPersonAnimInstanceClass"}
```

### 已授权的普通连线

先 get_graph_details，确认目标意图 → add_node 返回 GUID → get_node_details 查准确引脚 → connect_pins → get_graph_details 读回所有影响的连接 → save_blueprint 检查 errors/warnings/saved → open_blueprint → 等待渲染 → capture_graph 并实际查看 PNG。

```json
{"action":"add_node","blueprintPath":"/Game/YourTest/BP_Test","graphName":"EventGraph","nodeType":"call_function","functionClass":"/Script/Engine.KismetSystemLibrary","functionName":"PrintString","x":400,"y":0}
```

`/Game/YourTest/BP_Test` 是占位路径，必须换成当前任务明确允许修改的目标。

### 已授权的动画复刻

先审计源/目标 Skeleton、父类、全部嵌套图与依赖 → copy_anim_blueprint(save=false) 并记录 backupPath → 比较图路径、节点类型、连接、动画节点参数、缓存姿势与默认值 → 单独处理已证明无用的复制节点 → save_blueprint → 实际截图 → 必要时重新加载验证。不要将复制成功当成运行时动作、网络表现或正式 AnimClass 替换验收。

## 12. 已验证范围与维护入口

2026-09-08 实测：六类配置选择中的典型 Engine / EditorPerProjectUserSettings 读取、缺失/非法输入、原生配置 CDO、蓝图及组件默认值；Actor 创建/改 Pawn 父类/移动与 redirector 修复；六类中的典型函数、Branch、Comment 编辑、连线幂等/替换保护/显式替换/断线；普通图截图；Rifle AnimBP 复制、20 张嵌套图、最终 96 节点对照、类默认值、重新加载及动画图截图。自动转换占用引脚保护有代码，尚未单独构造完整转换场景测试；不宣称所有节点类型、派生资产或参数组合已覆盖。

测试脚本与报告：

- [普通编辑验收脚本](../../Scripts/Tests/TestMcpEditorTools.py)：默认只查询 tools/list；`--exercise` 会修改验收蓝图。报告 `Saved/MCP/EditorToolsValidation.json`。
- [动画复刻验证脚本](../../Scripts/Tests/TestMcpAnimBlueprintCopy.py)：默认只读对照；清理后的目标用 `--expect-pruned`；`--prune` 修改并保存副本。报告 `Saved/MCP/AnimBlueprintCopyValidation.json`。脚本针对固定项目资产，外部 Agent 不可当通用无副作用健康检查。
- 两者使用 Python 标准库和真实 HTTP MCP；`MCP_CAPABILITY_TOKEN` 环境变量只供示例客户端发送令牌，不会自动配置服务器。
- 编辑器日志：`Saved/Logs/ShootGame.log`，关注 LogMcpNativeTransport / LogMcpAutomationBridgeSubsystem。Saved 下证据属于本机运行产物，不保证存在于另一个 checkout。
- schema：`Source/McpAutomationBridge/Private/MCP/Tools/`；执行：`McpAutomationBridge_ReadOnlyHandlers.cpp`、`McpAutomationBridge_EditorSettings.cpp`、`McpAutomationBridge_BlueprintEdit.cpp`。
- 修改 C++ 或 schema 后，按仓库约定编译并使编辑器加载新 DLL；重连客户端更新工具缓存。不要删除整个 Binaries/Intermediate/.vs 处理更新问题。

本次整理只更新文档与调查证据，未修复以上实现限制，也未修改回归失败的资产或断言。
