#include "MCP/McpSchemaBuilder.h"
#include "MCP/McpToolDefinition.h"
#include "MCP/McpToolRegistry.h"

class FMcpToolManageAsset final : public FMcpToolDefinition
{
public:
    virtual FString GetName() const override { return TEXT("manage_asset"); }
    virtual FString GetCategory() const override { return TEXT("core"); }

    virtual FString GetDescription() const override
    {
        return TEXT(
            "Read-only project asset index. list/search_assets returns asset names, object paths, "
            "classes and Content Browser folders; list_folders returns project folders; exists checks an asset path.");
    }

    virtual TSharedPtr<FJsonObject> BuildInputSchema() const override
    {
        return FMcpSchemaBuilder()
            .StringEnum(TEXT("action"), {
                TEXT("list"), TEXT("search_assets"), TEXT("list_folders"), TEXT("exists")
            }, TEXT("Read-only asset query to perform."))
            .String(TEXT("path"), TEXT("Content path. Defaults to /Game."))
            .String(TEXT("query"), TEXT("Case-insensitive substring matched against asset name and package path."))
            .String(TEXT("class"), TEXT("Optional asset class name, for example Blueprint or StaticMesh."))
            .Bool(TEXT("recursive"), TEXT("Include child folders. Defaults to true."))
            .Integer(TEXT("offset"), TEXT("Zero-based result offset."))
            .Integer(TEXT("limit"), TEXT("Maximum results, clamped to 500."))
            .Required({TEXT("action")})
            .Build();
    }
};

MCP_REGISTER_TOOL(FMcpToolManageAsset);
