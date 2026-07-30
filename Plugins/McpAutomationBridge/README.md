# MCP Automation Bridge (Read Only)

This is a project-local, editor-only reduction of MCP Automation Bridge for UE 5.6.
It intentionally exposes only project discovery and Blueprint graph inspection.

## MCP endpoint

- Streamable HTTP endpoint: `http://127.0.0.1:3000/mcp`
- The server starts with the editor by default.
- Settings are under **Project Settings > Plugins > MCP Automation Bridge (Read Only)**.
- Restart Unreal Editor after enabling the plugin or changing the listen port.

## Tools

`manage_asset` actions:

- `list`
- `search_assets`
- `list_folders`
- `exists`

`manage_blueprint` actions:

- `list_blueprints`
- `get_blueprint`
- `get_graph_details`
- `get_node_details`
- `get_pin_details`

All paths are restricted to `/Game`. The plugin does not create, edit, rename, move,
compile, or delete assets and Blueprint nodes.

## Examples

List folders:

```json
{"action":"list_folders","path":"/Game","recursive":true}
```

Inspect a Blueprint and then one graph:

```json
{"action":"get_blueprint","blueprintPath":"/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter"}
```

```json
{"action":"get_graph_details","blueprintPath":"/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter","graphName":"EventGraph"}
```

The untouched full implementation is preserved outside the plugin at
`McpAutomationBridge_FullSourceArchive/` and is ignored by Git.
