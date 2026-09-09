#include "MCP/McpSchemaBuilder.h"
#include "MCP/McpToolDefinition.h"
#include "MCP/McpToolRegistry.h"

class FMcpToolEditorSettings final : public FMcpToolDefinition
{
public:
    virtual FString GetName() const override { return TEXT("manage_editor_settings"); }
    virtual FString GetCategory() const override { return TEXT("core"); }
    virtual FString GetDescription() const override
    {
        return TEXT("Read effective Unreal configuration from the running editor. Read merged INI keys/arrays, "
            "list sections, or inspect current config properties on a native settings class default object. "
            "Does not write settings or read arbitrary files. A sourceFile is the merged cache destination, not necessarily the original definition file.");
    }
    virtual TSharedPtr<FJsonObject> BuildInputSchema() const override
    {
        return FMcpSchemaBuilder()
            .StringEnum(TEXT("action"), {TEXT("read_config"), TEXT("list_sections"), TEXT("get_settings")}, TEXT("Read operation."))
            .StringEnum(TEXT("file"), {TEXT("Engine"), TEXT("Game"), TEXT("Input"), TEXT("Editor"), TEXT("EditorPerProjectUserSettings"), TEXT("GameUserSettings")}, TEXT("Configuration cache category; defaults to EditorPerProjectUserSettings."))
            .String(TEXT("section"), TEXT("Exact INI section for read_config, e.g. /Script/UnrealEd.LevelEditorViewportSettings."))
            .String(TEXT("key"), TEXT("Exact INI key. Omit to read all entries in the selected section."))
            .String(TEXT("classPath"), TEXT("Native /Script settings class for get_settings, e.g. /Script/UnrealEd.LevelEditorViewportSettings."))
            .String(TEXT("propertyName"), TEXT("Optional exact config property name. Omit to list config properties."))
            .String(TEXT("query"), TEXT("Optional section/property name substring filter."))
            .Required({TEXT("action")}).Build();
    }
};

MCP_REGISTER_TOOL(FMcpToolEditorSettings);
