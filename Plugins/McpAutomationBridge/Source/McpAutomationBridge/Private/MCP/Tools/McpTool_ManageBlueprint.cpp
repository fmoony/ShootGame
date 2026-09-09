#include "MCP/McpSchemaBuilder.h"
#include "MCP/McpToolDefinition.h"
#include "MCP/McpToolRegistry.h"

class FMcpToolManageBlueprint final : public FMcpToolDefinition
{
public:
    virtual FString GetName() const override { return TEXT("manage_blueprint"); }
    virtual FString GetCategory() const override { return TEXT("core"); }

    virtual FString GetDescription() const override
    {
        return TEXT(
            "Inspect and edit /Game Blueprints. Create/move/reparent assets, add K2 nodes, connect/disconnect pins, "
            "set defaults, move/delete nodes, compile/save, open graphs and capture actual editor graph screenshots. "
            "Edits are rejected during PIE. Graph edits are unsaved until save_blueprint. Use returned node IDs. "
            "Connections preserve existing links unless breakExisting=true. Move requires save=true.");
    }

    virtual TSharedPtr<FJsonObject> BuildInputSchema() const override
    {
        return FMcpSchemaBuilder()
            .StringEnum(TEXT("action"), {
                TEXT("list_blueprints"), TEXT("get_blueprint"), TEXT("get_graph_details"),
                TEXT("get_node_details"), TEXT("get_pin_details"), TEXT("get_class_defaults"), TEXT("create_blueprint"),
                TEXT("move_blueprint"), TEXT("reparent_blueprint"), TEXT("compile_blueprint"),
                TEXT("save_blueprint"), TEXT("add_node"), TEXT("connect_pins"), TEXT("disconnect_pins"),
                TEXT("set_pin_default"), TEXT("move_node"), TEXT("delete_node"),
                TEXT("open_blueprint"), TEXT("capture_graph"), TEXT("copy_anim_blueprint")
            }, TEXT("Blueprint action to perform."))
            .String(TEXT("path"), TEXT("Folder for list_blueprints, or Blueprint asset path for inspection."))
            .String(TEXT("blueprintPath"), TEXT("Blueprint object or package path, for example /Game/BP_Player.BP_Player."))
            .String(TEXT("graphName"), TEXT("Graph name or relative graph path from get_blueprint; use paths for duplicate transition names."))
            .String(TEXT("sourceBlueprintPath"), TEXT("copy_anim_blueprint: concrete source AnimBP with native AnimInstance parent. Copies graphs, nested state machines and defaults into an empty same-skeleton target; saves a target backup first."))
            .String(TEXT("nodeId"), TEXT("Node GUID or UObject name."))
            .String(TEXT("nodeName"), TEXT("Alternative node title or UObject name."))
            .String(TEXT("pinName"), TEXT("Pin name for get_pin_details."))
            .String(TEXT("propertyName"), TEXT("Exact editable property for get_class_defaults. Omit to list editable properties."))
            .String(TEXT("componentName"), TEXT("Optional component template name for get_class_defaults. Omit to read class defaults and list component names."))
            .String(TEXT("parentClass"), TEXT("Native /Script class or Blueprint generated class path. Create defaults to /Script/Engine.Actor."))
            .String(TEXT("destinationPath"), TEXT("New /Game package path for move_blueprint. No overwrite."))
            .Bool(TEXT("save"), TEXT("Save create/reparent result after successful compilation. Required true for move."))
            .StringEnum(TEXT("nodeType"), {TEXT("call_function"), TEXT("event"), TEXT("custom_event"), TEXT("branch"), TEXT("sequence"), TEXT("comment")}, TEXT("Node type for add_node."))
            .String(TEXT("functionClass"), TEXT("Function owner class, e.g. /Script/Engine.KismetSystemLibrary."))
            .String(TEXT("functionName"), TEXT("Reflected function name, e.g. PrintString or ReceiveBeginPlay."))
            .String(TEXT("eventName"), TEXT("Unique custom event name."))
            .String(TEXT("comment"), TEXT("Comment text for the node."))
            .Integer(TEXT("x"), TEXT("Node position X."))
            .Integer(TEXT("y"), TEXT("Node position Y."))
            .String(TEXT("sourceNodeId"), TEXT("Source node GUID or object name."))
            .String(TEXT("sourcePinName"), TEXT("Output pin name."))
            .String(TEXT("targetNodeId"), TEXT("Target node GUID or object name."))
            .String(TEXT("targetPinName"), TEXT("Input pin name."))
            .Bool(TEXT("breakExisting"), TEXT("Allow schema to replace existing links; default false."))
            .String(TEXT("defaultValue"), TEXT("UE text value for an unconnected input pin."))
            .Number(TEXT("viewX"), TEXT("Graph view origin X for open_blueprint."))
            .Number(TEXT("viewY"), TEXT("Graph view origin Y for open_blueprint."))
            .Number(TEXT("zoom"), TEXT("Graph view zoom for open_blueprint, 0.1 to 1.0. Otherwise zoom to fit."))
            .String(TEXT("query"), TEXT("Name/path substring for list_blueprints."))
            .Bool(TEXT("recursive"), TEXT("Include child folders. Defaults to true."))
            .Integer(TEXT("offset"), TEXT("Zero-based node or Blueprint offset."))
            .Integer(TEXT("limit"), TEXT("Maximum results, clamped to 500."))
            .Required({TEXT("action")})
            .Build();
    }
};

MCP_REGISTER_TOOL(FMcpToolManageBlueprint);
