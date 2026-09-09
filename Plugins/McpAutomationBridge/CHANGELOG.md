# Changelog

## 0.3.0-editor-tools

- 验收后扩展：AnimBlueprint 父类修改、空目标动画图及嵌套状态机复制（先保存备份）、动画图截图、唯一图路径及节点参数读取。
- 增加有效配置缓存及配置对象值读取。
- 增加普通 Actor 蓝图创建、移动、改父类、编译和保存。
- 增加 K2 节点增删、移动、默认值及经 Schema 校验的连线/断线，默认保护已有连线。
- 增加打开图表与真实 Slate 图表截图，以及独立 MCP 协议验收脚本。
- 编译/保存失败时保留结构化诊断；未恢复完整插件归档或升级引擎。

## 0.2.0-readonly

- Reduced the active module from 109 C++ translation units to 11.
- Replaced the broad dependency set with Core, CoreUObject, Engine, AssetRegistry,
  DeveloperSettings, Json, Projects, and Sockets.
- Kept native Streamable HTTP MCP transport.
- Kept read-only asset names, paths, folders, Blueprint graphs, nodes, pins, and links.
- Removed legacy WebSocket/TLS, UI, authoring, runtime gameplay, geometry, Niagara,
  animation, audio, material, landscape, sequencer, and source-control handlers.
