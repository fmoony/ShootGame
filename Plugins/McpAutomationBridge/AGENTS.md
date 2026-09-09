# MCP Automation Bridge

This project-local UE 5.6 plugin provides asset inspection, settings reads and explicit Blueprint editing.

## External agents

- Read [EXTERNAL_AGENT_GUIDE.md](EXTERNAL_AGENT_GUIDE.md) before invoking tools. It documents the complete public action list, actual side effects, unsupported operations, errors, and verification limits.
- Discover the loaded server through initialize/tools/list. Historical user authorization below describes implementation scope, not blanket permission to edit those assets in a new task.
- A timeout is an unknown outcome, not an automatic rollback. Reinspect before retrying mutations; save=false does not suppress the backup saved by copy_anim_blueprint.

## Scope

- Keep native Streamable HTTP MCP transport.
- Keep `manage_asset` read actions for `/Game` asset names, paths, classes, and folders.
- Keep `manage_blueprint` read actions for Blueprint graphs, nodes, pins, defaults, and links.
- User-authorized scope (2026-09-08): read effective editor settings; create/move/reparent ordinary Actor Blueprints; edit K2 nodes and links; compile/save/open/capture Blueprint graphs.
- Additional user-authorized scope: reparent Animation Blueprints and copy the useful third-person Rifle animation graphs/state machines into the newly created NewAnimBlueprint. Back up the destination, preserve the source, and verify nested graphs and animation node settings.
- Keep graph edits transactional, use UE graph schemas for links, and use Asset Tools for asset moves.
- Reject Blueprint edits during PIE/SIE. Do not add mutation outside this scope without explicit user approval.
- Do not restore dependencies from `McpAutomationBridge_FullSourceArchive` unless explicitly requested.

## Build constraints

- The active plugin belongs under `Plugins/McpAutomationBridge`.
- Keep module dependencies limited to what the active source directly uses.
- Files under `Binaries` and `Intermediate` are generated and must not be edited.
- Register native MCP tools through `MCP_REGISTER_TOOL` in `Private/MCP/Tools`.
- Verify changes with `ShootGameEditor Win64 Development` on UE 5.6.
