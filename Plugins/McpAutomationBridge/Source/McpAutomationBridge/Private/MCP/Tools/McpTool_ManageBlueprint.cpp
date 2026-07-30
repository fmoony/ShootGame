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
            "Read-only Blueprint inspection. Lists Blueprint assets and reads graph names, nodes, pins, "
            "default values and pin-to-pin links. This build cannot create or modify Blueprints.");
    }

    virtual TSharedPtr<FJsonObject> BuildInputSchema() const override
    {
        return FMcpSchemaBuilder()
            .StringEnum(TEXT("action"), {
                TEXT("list_blueprints"), TEXT("get_blueprint"), TEXT("get_graph_details"),
                TEXT("get_node_details"), TEXT("get_pin_details")
            }, TEXT("Read-only Blueprint query to perform."))
            .String(TEXT("path"), TEXT("Folder for list_blueprints, or Blueprint asset path for inspection."))
            .String(TEXT("blueprintPath"), TEXT("Blueprint object or package path, for example /Game/BP_Player.BP_Player."))
            .String(TEXT("graphName"), TEXT("Graph name, for example EventGraph."))
            .String(TEXT("nodeId"), TEXT("Node GUID or UObject name."))
            .String(TEXT("nodeName"), TEXT("Alternative node title or UObject name."))
            .String(TEXT("pinName"), TEXT("Pin name for get_pin_details."))
            .String(TEXT("query"), TEXT("Name/path substring for list_blueprints."))
            .Bool(TEXT("recursive"), TEXT("Include child folders. Defaults to true."))
            .Integer(TEXT("offset"), TEXT("Zero-based node or Blueprint offset."))
            .Integer(TEXT("limit"), TEXT("Maximum results, clamped to 500."))
            .Required({TEXT("action")})
            .Build();
    }
};

MCP_REGISTER_TOOL(FMcpToolManageBlueprint);
