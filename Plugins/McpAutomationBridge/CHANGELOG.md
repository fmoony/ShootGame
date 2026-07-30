# Changelog

## 0.2.0-readonly

- Reduced the active module from 109 C++ translation units to 11.
- Replaced the broad dependency set with Core, CoreUObject, Engine, AssetRegistry,
  DeveloperSettings, Json, Projects, and Sockets.
- Kept native Streamable HTTP MCP transport.
- Kept read-only asset names, paths, folders, Blueprint graphs, nodes, pins, and links.
- Removed legacy WebSocket/TLS, UI, authoring, runtime gameplay, geometry, Niagara,
  animation, audio, material, landscape, sequencer, and source-control handlers.
