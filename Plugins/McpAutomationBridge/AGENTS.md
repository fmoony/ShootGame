# MCP Automation Bridge (Read Only)

This project-local plugin is intentionally minimal and read-only.

## Scope

- Keep native Streamable HTTP MCP transport.
- Keep `manage_asset` read actions for `/Game` asset names, paths, classes, and folders.
- Keep `manage_blueprint` read actions for Blueprint graphs, nodes, pins, defaults, and links.
- Do not add asset or Blueprint mutation without explicit user approval.
- Do not restore dependencies from `McpAutomationBridge_FullSourceArchive` unless explicitly requested.

## Build constraints

- The active plugin belongs under `Plugins/McpAutomationBridge`.
- Keep module dependencies limited to what the active source directly uses.
- Files under `Binaries` and `Intermediate` are generated and must not be edited.
- Register native MCP tools through `MCP_REGISTER_TOOL` in `Private/MCP/Tools`.
- Verify changes with `ShootGameEditor Win64 Development` on UE 5.6.
