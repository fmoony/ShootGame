#include "McpAutomationBridgeSubsystem.h"

#include "Misc/ConfigCacheIni.h"
#include "UObject/UnrealType.h"

namespace McpEditorSettings
{
FString ReadString(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key)
{
    FString Value;
    Args->TryGetStringField(Key, Value);
    return Value;
}
}

bool UMcpAutomationBridgeSubsystem::HandleEditorSettings(
    const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    using namespace McpEditorSettings;
    const FString Action = ReadString(Payload, TEXT("action"));
    const FString Query = ReadString(Payload, TEXT("query"));
    const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    auto Fail = [&](const FString& Message, const FString& Code = TEXT("INVALID_ARGUMENT"))
    {
        SendAutomationError(RequestId, Message, Code);
        return true;
    };
    if (Action == TEXT("get_settings"))
    {
        const FString ClassPath = ReadString(Payload, TEXT("classPath"));
        if (!ClassPath.StartsWith(TEXT("/Script/"))) return Fail(TEXT("classPath must identify a native /Script settings class"));
        UClass* Class = LoadObject<UClass>(nullptr, *ClassPath);
        if (!Class || !Class->HasAnyClassFlags(CLASS_Config)) return Fail(TEXT("Native config class not found"), TEXT("CLASS_NOT_FOUND"));
        UObject* Defaults = Class->GetDefaultObject();
        const FString PropertyName = ReadString(Payload, TEXT("propertyName"));
        TArray<TSharedPtr<FJsonValue>> Properties;
        for (TFieldIterator<FProperty> It(Class); It; ++It)
        {
            FProperty* Property = *It;
            if (!Property->HasAnyPropertyFlags(CPF_Config) ||
                (!PropertyName.IsEmpty() && Property->GetName() != PropertyName) ||
                (!Query.IsEmpty() && !Property->GetName().Contains(Query))) continue;
            const TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            FString Value;
            Property->ExportText_InContainer(0, Value, Defaults, Defaults, Defaults, PPF_None);
            Entry->SetStringField(TEXT("name"), Property->GetName());
            Entry->SetStringField(TEXT("type"), Property->GetCPPType());
            Entry->SetStringField(TEXT("valueText"), Value);
            Properties.Add(MakeShared<FJsonValueObject>(Entry));
        }
        Result->SetStringField(TEXT("source"), TEXT("live_class_default_object"));
        Result->SetStringField(TEXT("classPath"), Class->GetPathName());
        Result->SetStringField(TEXT("configName"), Class->ClassConfigName.ToString());
        Result->SetBoolField(TEXT("found"), PropertyName.IsEmpty() || !Properties.IsEmpty());
        Result->SetArrayField(TEXT("properties"), Properties);
        SendAutomationResponse(RequestId, true, TEXT("Current in-memory config properties read"), Result);
        return true;
    }
    if (Action != TEXT("read_config") && Action != TEXT("list_sections"))
        return Fail(TEXT("Unknown settings action"), TEXT("ACTION_NOT_SUPPORTED"));
    if (!GConfig) return Fail(TEXT("Configuration cache unavailable"), TEXT("CONFIG_UNAVAILABLE"));
    FString File = ReadString(Payload, TEXT("file"));
    if (File.IsEmpty()) File = TEXT("EditorPerProjectUserSettings");
    FString Filename;
    if (File == TEXT("Engine")) Filename = GEngineIni;
    else if (File == TEXT("Game")) Filename = GGameIni;
    else if (File == TEXT("Input")) Filename = GInputIni;
    else if (File == TEXT("Editor")) Filename = GEditorIni;
    else if (File == TEXT("EditorPerProjectUserSettings")) Filename = GEditorPerProjectIni;
    else if (File == TEXT("GameUserSettings")) Filename = GGameUserSettingsIni;
    else return Fail(TEXT("Unsupported configuration category; filesystem paths are not accepted"), TEXT("INVALID_CONFIG_FILE"));
    if (Filename.IsEmpty()) return Fail(TEXT("This configuration category is not loaded"), TEXT("CONFIG_UNAVAILABLE"));
    // 读取合并后的缓存，不重新加载磁盘，避免改变编辑器当前状态。
    Result->SetStringField(TEXT("source"), TEXT("merged_in_memory_config_cache"));
    Result->SetStringField(TEXT("file"), File);
    // UE 5.6 的 GEngineIni 等可能只是 "Engine" 缓存键，实际目标路径保存在 Branch。
    Result->SetStringField(TEXT("cacheKey"), Filename);
    const FConfigBranch* Branch = GConfig->FindBranchWithNoReload(FName(*File), Filename);
    Result->SetStringField(TEXT("sourceFile"), Branch ? Branch->IniPath : FString());
    if (Action == TEXT("list_sections"))
    {
        TArray<FString> Sections;
        GConfig->GetSectionNames(Filename, Sections);
        Sections.Sort();
        TArray<TSharedPtr<FJsonValue>> Values;
        for (const FString& Section : Sections)
            if (Query.IsEmpty() || Section.Contains(Query)) Values.Add(MakeShared<FJsonValueString>(Section));
        Result->SetArrayField(TEXT("sections"), Values);
    }
    else
    {
        const FString SectionName = ReadString(Payload, TEXT("section"));
        if (SectionName.IsEmpty()) return Fail(TEXT("read_config requires section"));
        const FString Key = ReadString(Payload, TEXT("key"));
        Result->SetStringField(TEXT("section"), SectionName);
        const FConfigSection* Section = GConfig->GetSection(*SectionName, false, Filename);
        if (Key.IsEmpty())
        {
            TArray<FString> Entries;
            const bool bFound = GConfig->GetSection(*SectionName, Entries, Filename);
            TArray<TSharedPtr<FJsonValue>> Values;
            for (const FString& Entry : Entries) Values.Add(MakeShared<FJsonValueString>(Entry));
            Result->SetBoolField(TEXT("found"), bFound);
            Result->SetArrayField(TEXT("entries"), Values);
        }
        else
        {
            FString Value;
            const bool bValueFound = GConfig->GetString(*SectionName, *Key, Value, Filename);
            TArray<FString> Items;
            GConfig->GetArray(*SectionName, *Key, Items, Filename);
            const bool bFound = bValueFound || !Items.IsEmpty() || (Section && Section->EmptyInitializedKeys.Contains(FName(*Key)));
            TArray<TSharedPtr<FJsonValue>> Values;
            for (const FString& Item : Items) Values.Add(MakeShared<FJsonValueString>(Item));
            Result->SetStringField(TEXT("key"), Key);
            Result->SetBoolField(TEXT("found"), bFound);
            Result->SetBoolField(TEXT("valueFound"), bValueFound);
            if (bValueFound) Result->SetStringField(TEXT("value"), Value);
            Result->SetArrayField(TEXT("values"), Values);
        }
    }
    SendAutomationResponse(RequestId, true, TEXT("Effective editor configuration read"), Result);
    return true;
}
