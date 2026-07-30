#include "McpAutomationBridgeSubsystem.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/SoftObjectPath.h"

namespace
{
bool GetAction(const TSharedPtr<FJsonObject>& Payload, FString& OutAction)
{
    if (!Payload.IsValid())
    {
        return false;
    }
    if (!Payload->TryGetStringField(TEXT("action"), OutAction) || OutAction.IsEmpty())
    {
        Payload->TryGetStringField(TEXT("subAction"), OutAction);
    }
    OutAction = OutAction.TrimStartAndEnd().ToLower();
    return !OutAction.IsEmpty();
}

bool NormalizeGamePath(const FString& Input, FString& OutPath, bool bAllowObjectPath)
{
    OutPath = Input.TrimStartAndEnd();
    if (OutPath.IsEmpty())
    {
        OutPath = TEXT("/Game");
    }

    OutPath.ReplaceInline(TEXT("\\"), TEXT("/"));
    if (OutPath.Contains(TEXT("..")))
    {
        return false;
    }

    if (OutPath.Equals(TEXT("Content"), ESearchCase::IgnoreCase) ||
        OutPath.Equals(TEXT("/Content"), ESearchCase::IgnoreCase))
    {
        OutPath = TEXT("/Game");
    }
    else if (OutPath.StartsWith(TEXT("Content/"), ESearchCase::IgnoreCase))
    {
        OutPath = TEXT("/Game/") + OutPath.RightChop(8);
    }
    else if (OutPath.StartsWith(TEXT("/Content/"), ESearchCase::IgnoreCase))
    {
        OutPath = TEXT("/Game/") + OutPath.RightChop(9);
    }

    while (OutPath.EndsWith(TEXT("/")) && OutPath.Len() > 5)
    {
        OutPath.LeftChopInline(1);
    }

    if (!OutPath.Equals(TEXT("/Game"), ESearchCase::IgnoreCase) &&
        !OutPath.StartsWith(TEXT("/Game/"), ESearchCase::IgnoreCase))
    {
        return false;
    }

    return bAllowObjectPath || !OutPath.Contains(TEXT("."));
}

void ReadPaging(const TSharedPtr<FJsonObject>& Payload, int32& OutOffset, int32& OutLimit)
{
    OutOffset = 0;
    OutLimit = 100;
    Payload->TryGetNumberField(TEXT("offset"), OutOffset);
    Payload->TryGetNumberField(TEXT("limit"), OutLimit);
    OutOffset = FMath::Max(0, OutOffset);
    OutLimit = FMath::Clamp(OutLimit, 1, 500);
}

TSharedPtr<FJsonObject> AssetToJson(const FAssetData& Asset)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("name"), Asset.AssetName.ToString());
    Json->SetStringField(TEXT("objectPath"), Asset.GetSoftObjectPath().ToString());
    Json->SetStringField(TEXT("packageName"), Asset.PackageName.ToString());
    Json->SetStringField(TEXT("folder"), Asset.PackagePath.ToString());
    Json->SetStringField(TEXT("class"), Asset.AssetClassPath.ToString());
    return Json;
}

void SortAssets(TArray<FAssetData>& Assets)
{
    Assets.Sort([](const FAssetData& Left, const FAssetData& Right)
    {
        return Left.PackageName.ToString() < Right.PackageName.ToString();
    });
}

bool MatchesAssetFilters(const FAssetData& Asset, const FString& Query, const FString& ClassFilter)
{
    if (!Query.IsEmpty() &&
        !Asset.AssetName.ToString().Contains(Query, ESearchCase::IgnoreCase) &&
        !Asset.PackageName.ToString().Contains(Query, ESearchCase::IgnoreCase))
    {
        return false;
    }

    if (!ClassFilter.IsEmpty())
    {
        const FString FullClass = Asset.AssetClassPath.ToString();
        const FString ShortClass = Asset.AssetClassPath.GetAssetName().ToString();
        if (!FullClass.Equals(ClassFilter, ESearchCase::IgnoreCase) &&
            !ShortClass.Equals(ClassFilter, ESearchCase::IgnoreCase))
        {
            return false;
        }
    }
    return true;
}

void GatherBlueprintGraphs(UBlueprint* Blueprint, TArray<UEdGraph*>& OutGraphs)
{
    if (!Blueprint)
    {
        return;
    }
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (Graph) OutGraphs.Add(Graph);
    }
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (Graph) OutGraphs.Add(Graph);
    }
    for (UEdGraph* Graph : Blueprint->MacroGraphs)
    {
        if (Graph) OutGraphs.Add(Graph);
    }
    for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
    {
        if (Graph) OutGraphs.Add(Graph);
    }
}

FString GraphKind(const UBlueprint* Blueprint, const UEdGraph* Graph)
{
    if (Blueprint->UbergraphPages.Contains(Graph)) return TEXT("event");
    if (Blueprint->FunctionGraphs.Contains(Graph)) return TEXT("function");
    if (Blueprint->MacroGraphs.Contains(Graph)) return TEXT("macro");
    if (Blueprint->DelegateSignatureGraphs.Contains(Graph)) return TEXT("delegate");
    return TEXT("unknown");
}

FString NormalizeBlueprintObjectPath(const FString& Input)
{
    FString Path = Input.TrimStartAndEnd();
    Path.ReplaceInline(TEXT("\\"), TEXT("/"));
    if (Path.EndsWith(TEXT("_C")))
    {
        Path.LeftChopInline(2);
    }
    if (!Path.Contains(TEXT(".")))
    {
        const FString AssetName = FPackageName::GetLongPackageAssetName(Path);
        if (!AssetName.IsEmpty())
        {
            Path += TEXT(".") + AssetName;
        }
    }
    return Path;
}

UBlueprint* LoadBlueprint(const FString& RequestedPath)
{
    FString SafePath;
    if (!NormalizeGamePath(RequestedPath, SafePath, true))
    {
        return nullptr;
    }

    const FString ObjectPath = NormalizeBlueprintObjectPath(SafePath);
    UObject* Object = FSoftObjectPath(ObjectPath).TryLoad();
    if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
    {
        return Blueprint;
    }
    if (UClass* GeneratedClass = Cast<UClass>(Object))
    {
        return Cast<UBlueprint>(GeneratedClass->ClassGeneratedBy);
    }
    return nullptr;
}

UEdGraph* FindGraph(UBlueprint* Blueprint, const FString& GraphName)
{
    TArray<UEdGraph*> Graphs;
    GatherBlueprintGraphs(Blueprint, Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
        {
            return Graph;
        }
    }
    return nullptr;
}

FString NodeGuidString(const UEdGraphNode* Node)
{
    return Node && Node->NodeGuid.IsValid()
        ? Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens)
        : FString();
}

FString PinContainerName(const FEdGraphPinType& PinType)
{
    switch (PinType.ContainerType)
    {
        case EPinContainerType::Array: return TEXT("array");
        case EPinContainerType::Set: return TEXT("set");
        case EPinContainerType::Map: return TEXT("map");
        default: return TEXT("none");
    }
}

TSharedPtr<FJsonObject> PinToJson(const UEdGraphPin* Pin)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("id"), Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens));
    Json->SetStringField(TEXT("name"), Pin->PinName.ToString());
    Json->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
    Json->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
    Json->SetStringField(TEXT("subCategory"), Pin->PinType.PinSubCategory.ToString());
    Json->SetStringField(TEXT("container"), PinContainerName(Pin->PinType));
    Json->SetBoolField(TEXT("isReference"), Pin->PinType.bIsReference);
    Json->SetBoolField(TEXT("isConst"), Pin->PinType.bIsConst);
    Json->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
    Json->SetStringField(TEXT("defaultText"), Pin->DefaultTextValue.ToString());
    if (const UObject* TypeObject = Pin->PinType.PinSubCategoryObject.Get())
    {
        Json->SetStringField(TEXT("typeObject"), TypeObject->GetPathName());
    }
    if (Pin->DefaultObject)
    {
        Json->SetStringField(TEXT("defaultObject"), Pin->DefaultObject->GetPathName());
    }

    TArray<TSharedPtr<FJsonValue>> Links;
    for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
    {
        if (!LinkedPin)
        {
            continue;
        }
        TSharedPtr<FJsonObject> Link = MakeShared<FJsonObject>();
        Link->SetStringField(TEXT("nodeId"), NodeGuidString(LinkedPin->GetOwningNode()));
        Link->SetStringField(TEXT("nodeName"), LinkedPin->GetOwningNode()->GetName());
        Link->SetStringField(TEXT("pinName"), LinkedPin->PinName.ToString());
        Links.Add(MakeShared<FJsonValueObject>(Link));
    }
    Json->SetArrayField(TEXT("links"), Links);
    return Json;
}

TSharedPtr<FJsonObject> NodeToJson(const UEdGraphNode* Node, bool bIncludePins)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("id"), NodeGuidString(Node));
    Json->SetStringField(TEXT("name"), Node->GetName());
    Json->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
    Json->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
    Json->SetNumberField(TEXT("x"), Node->NodePosX);
    Json->SetNumberField(TEXT("y"), Node->NodePosY);
    Json->SetStringField(TEXT("comment"), Node->NodeComment);

    if (bIncludePins)
    {
        TArray<TSharedPtr<FJsonValue>> Pins;
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin)
            {
                Pins.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
            }
        }
        Json->SetArrayField(TEXT("pins"), Pins);
    }
    return Json;
}

UEdGraphNode* FindNode(const TArray<UEdGraph*>& Graphs, const FString& Identifier, UEdGraph*& OutGraph)
{
    for (UEdGraph* Graph : Graphs)
    {
        if (!Graph)
        {
            continue;
        }
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node)
            {
                continue;
            }
            if (NodeGuidString(Node).Equals(Identifier, ESearchCase::IgnoreCase) ||
                Node->GetName().Equals(Identifier, ESearchCase::IgnoreCase) ||
                Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Equals(Identifier, ESearchCase::IgnoreCase))
            {
                OutGraph = Graph;
                return Node;
            }
        }
    }
    return nullptr;
}

bool GetBlueprintPath(const TSharedPtr<FJsonObject>& Payload, FString& OutPath)
{
    if (!Payload->TryGetStringField(TEXT("blueprintPath"), OutPath) || OutPath.IsEmpty())
    {
        if (!Payload->TryGetStringField(TEXT("assetPath"), OutPath) || OutPath.IsEmpty())
        {
            Payload->TryGetStringField(TEXT("path"), OutPath);
        }
    }
    return !OutPath.IsEmpty();
}
}

bool UMcpAutomationBridgeSubsystem::HandleManageAsset(
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload)
{
    FString Action;
    if (!GetAction(Payload, Action))
    {
        SendAutomationError(RequestId, TEXT("manage_asset requires action"), TEXT("INVALID_ARGUMENT"));
        return true;
    }

    FString RequestedPath;
    Payload->TryGetStringField(TEXT("path"), RequestedPath);
    FString SafePath;
    if (!NormalizeGamePath(RequestedPath, SafePath, Action == TEXT("exists")))
    {
        SendAutomationError(RequestId, TEXT("Only /Game project paths are allowed"), TEXT("INVALID_PATH"));
        return true;
    }

    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

    if (Action == TEXT("list_folders"))
    {
        bool bRecursive = true;
        Payload->TryGetBoolField(TEXT("recursive"), bRecursive);
        TArray<FString> Folders;
        Registry.GetSubPaths(SafePath, Folders, bRecursive);
        Folders.Sort();

        TArray<TSharedPtr<FJsonValue>> Values;
        for (const FString& Folder : Folders)
        {
            Values.Add(MakeShared<FJsonValueString>(Folder));
        }
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("root"), SafePath);
        Result->SetNumberField(TEXT("count"), Values.Num());
        Result->SetArrayField(TEXT("folders"), Values);
        SendAutomationResponse(RequestId, true, TEXT("Folders listed"), Result);
        return true;
    }

    if (Action == TEXT("exists"))
    {
        const FString ObjectPath = NormalizeBlueprintObjectPath(SafePath);
        const FAssetData Asset = Registry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("exists"), Asset.IsValid());
        if (Asset.IsValid())
        {
            Result->SetObjectField(TEXT("asset"), AssetToJson(Asset));
        }
        SendAutomationResponse(RequestId, true, TEXT("Asset existence checked"), Result);
        return true;
    }

    if (Action != TEXT("list") && Action != TEXT("search_assets"))
    {
        SendAutomationError(RequestId, TEXT("Unsupported manage_asset action"), TEXT("ACTION_NOT_SUPPORTED"));
        return true;
    }

    bool bRecursive = true;
    Payload->TryGetBoolField(TEXT("recursive"), bRecursive);
    FARFilter Filter;
    Filter.PackagePaths.Add(FName(*SafePath));
    Filter.bRecursivePaths = bRecursive;

    TArray<FAssetData> Assets;
    Registry.GetAssets(Filter, Assets);

    FString Query;
    FString ClassFilter;
    Payload->TryGetStringField(TEXT("query"), Query);
    Payload->TryGetStringField(TEXT("class"), ClassFilter);
    Assets.RemoveAll([&](const FAssetData& Asset)
    {
        return !MatchesAssetFilters(Asset, Query, ClassFilter);
    });
    SortAssets(Assets);

    int32 Offset;
    int32 Limit;
    ReadPaging(Payload, Offset, Limit);
    const int32 Total = Assets.Num();
    const int32 End = FMath::Min(Total, Offset + Limit);

    TArray<TSharedPtr<FJsonValue>> Values;
    for (int32 Index = Offset; Index < End; ++Index)
    {
        Values.Add(MakeShared<FJsonValueObject>(AssetToJson(Assets[Index])));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("root"), SafePath);
    Result->SetNumberField(TEXT("totalCount"), Total);
    Result->SetNumberField(TEXT("count"), Values.Num());
    Result->SetArrayField(TEXT("assets"), Values);
    SendAutomationResponse(RequestId, true, TEXT("Assets listed"), Result);
    return true;
}

bool UMcpAutomationBridgeSubsystem::HandleManageBlueprint(
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload)
{
    FString Action;
    if (!GetAction(Payload, Action))
    {
        SendAutomationError(RequestId, TEXT("manage_blueprint requires action"), TEXT("INVALID_ARGUMENT"));
        return true;
    }

    if (Action == TEXT("list_blueprints"))
    {
        FString RequestedPath;
        Payload->TryGetStringField(TEXT("path"), RequestedPath);
        FString SafePath;
        if (!NormalizeGamePath(RequestedPath, SafePath, false))
        {
            SendAutomationError(RequestId, TEXT("Only /Game project folders are allowed"), TEXT("INVALID_PATH"));
            return true;
        }

        bool bRecursive = true;
        Payload->TryGetBoolField(TEXT("recursive"), bRecursive);
        FARFilter Filter;
        Filter.PackagePaths.Add(FName(*SafePath));
        Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
        Filter.bRecursivePaths = bRecursive;
        Filter.bRecursiveClasses = true;

        IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        TArray<FAssetData> Assets;
        Registry.GetAssets(Filter, Assets);
        FString Query;
        Payload->TryGetStringField(TEXT("query"), Query);
        Assets.RemoveAll([&](const FAssetData& Asset)
        {
            return !MatchesAssetFilters(Asset, Query, FString());
        });
        SortAssets(Assets);

        int32 Offset;
        int32 Limit;
        ReadPaging(Payload, Offset, Limit);
        const int32 Total = Assets.Num();
        const int32 End = FMath::Min(Total, Offset + Limit);
        TArray<TSharedPtr<FJsonValue>> Values;
        for (int32 Index = Offset; Index < End; ++Index)
        {
            Values.Add(MakeShared<FJsonValueObject>(AssetToJson(Assets[Index])));
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("root"), SafePath);
        Result->SetNumberField(TEXT("totalCount"), Total);
        Result->SetNumberField(TEXT("count"), Values.Num());
        Result->SetArrayField(TEXT("blueprints"), Values);
        SendAutomationResponse(RequestId, true, TEXT("Blueprints listed"), Result);
        return true;
    }

    FString BlueprintPath;
    if (!GetBlueprintPath(Payload, BlueprintPath))
    {
        SendAutomationError(RequestId, TEXT("blueprintPath is required"), TEXT("INVALID_ARGUMENT"));
        return true;
    }

    UBlueprint* Blueprint = LoadBlueprint(BlueprintPath);
    if (!Blueprint)
    {
        SendAutomationError(RequestId, TEXT("Blueprint was not found under /Game"), TEXT("BLUEPRINT_NOT_FOUND"));
        return true;
    }

    TArray<UEdGraph*> AllGraphs;
    GatherBlueprintGraphs(Blueprint, AllGraphs);

    if (Action == TEXT("get_blueprint"))
    {
        TArray<TSharedPtr<FJsonValue>> GraphValues;
        for (UEdGraph* Graph : AllGraphs)
        {
            TSharedPtr<FJsonObject> GraphJson = MakeShared<FJsonObject>();
            GraphJson->SetStringField(TEXT("name"), Graph->GetName());
            GraphJson->SetStringField(TEXT("type"), GraphKind(Blueprint, Graph));
            GraphJson->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());
            GraphValues.Add(MakeShared<FJsonValueObject>(GraphJson));
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("name"), Blueprint->GetName());
        Result->SetStringField(TEXT("path"), Blueprint->GetPathName());
        Result->SetNumberField(TEXT("graphCount"), GraphValues.Num());
        Result->SetArrayField(TEXT("graphs"), GraphValues);
        if (Blueprint->ParentClass)
        {
            Result->SetStringField(TEXT("parentClass"), Blueprint->ParentClass->GetPathName());
        }
        SendAutomationResponse(RequestId, true, TEXT("Blueprint inspected"), Result);
        return true;
    }

    FString GraphName;
    Payload->TryGetStringField(TEXT("graphName"), GraphName);
    UEdGraph* Graph = GraphName.IsEmpty() ? nullptr : FindGraph(Blueprint, GraphName);

    if (Action == TEXT("get_graph_details"))
    {
        if (!Graph)
        {
            SendAutomationError(RequestId, TEXT("graphName does not identify a Blueprint graph"), TEXT("GRAPH_NOT_FOUND"));
            return true;
        }

        int32 Offset;
        int32 Limit;
        ReadPaging(Payload, Offset, Limit);
        const int32 Total = Graph->Nodes.Num();
        const int32 End = FMath::Min(Total, Offset + Limit);
        TArray<TSharedPtr<FJsonValue>> Nodes;
        for (int32 Index = Offset; Index < End; ++Index)
        {
            if (Graph->Nodes[Index])
            {
                Nodes.Add(MakeShared<FJsonValueObject>(NodeToJson(Graph->Nodes[Index], true)));
            }
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
        Result->SetStringField(TEXT("graphName"), Graph->GetName());
        Result->SetStringField(TEXT("graphType"), GraphKind(Blueprint, Graph));
        Result->SetNumberField(TEXT("totalNodeCount"), Total);
        Result->SetNumberField(TEXT("count"), Nodes.Num());
        Result->SetArrayField(TEXT("nodes"), Nodes);
        SendAutomationResponse(RequestId, true, TEXT("Blueprint graph inspected"), Result);
        return true;
    }

    if (Action == TEXT("get_node_details") || Action == TEXT("get_pin_details"))
    {
        FString NodeIdentifier;
        if (!Payload->TryGetStringField(TEXT("nodeId"), NodeIdentifier) || NodeIdentifier.IsEmpty())
        {
            Payload->TryGetStringField(TEXT("nodeName"), NodeIdentifier);
        }
        if (NodeIdentifier.IsEmpty())
        {
            SendAutomationError(RequestId, TEXT("nodeId or nodeName is required"), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        TArray<UEdGraph*> GraphsToSearch;
        if (Graph)
        {
            GraphsToSearch.Add(Graph);
        }
        else
        {
            GraphsToSearch = AllGraphs;
        }

        UEdGraph* OwningGraph = nullptr;
        UEdGraphNode* Node = FindNode(GraphsToSearch, NodeIdentifier, OwningGraph);
        if (!Node)
        {
            SendAutomationError(RequestId, TEXT("Blueprint node was not found"), TEXT("NODE_NOT_FOUND"));
            return true;
        }

        if (Action == TEXT("get_node_details"))
        {
            TSharedPtr<FJsonObject> Result = NodeToJson(Node, true);
            Result->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
            Result->SetStringField(TEXT("graphName"), OwningGraph->GetName());
            SendAutomationResponse(RequestId, true, TEXT("Blueprint node inspected"), Result);
            return true;
        }

        FString PinName;
        Payload->TryGetStringField(TEXT("pinName"), PinName);
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin && Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
            {
                TSharedPtr<FJsonObject> Result = PinToJson(Pin);
                Result->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
                Result->SetStringField(TEXT("graphName"), OwningGraph->GetName());
                Result->SetStringField(TEXT("nodeId"), NodeGuidString(Node));
                SendAutomationResponse(RequestId, true, TEXT("Blueprint pin inspected"), Result);
                return true;
            }
        }

        SendAutomationError(RequestId, TEXT("Blueprint pin was not found"), TEXT("PIN_NOT_FOUND"));
        return true;
    }

    SendAutomationError(RequestId, TEXT("Unsupported manage_blueprint action"), TEXT("ACTION_NOT_SUPPORTED"));
    return true;
}
