#include "McpAutomationBridgeSubsystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorLibrary.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Framework/Application/SlateApplication.h"
#include "GraphEditor.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "ImageUtils.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "Serialization/ArchiveReplaceObjectRef.h"

namespace McpBlueprintEdit
{
FString String(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, const FString& Default = FString())
{
    FString Value = Default;
    Args->TryGetStringField(Key, Value);
    return Value;
}

bool GamePackage(const FString& Input, FString& OutPackage)
{
    OutPackage = FPackageName::ObjectPathToPackageName(Input.TrimStartAndEnd());
    return OutPackage.StartsWith(TEXT("/Game/")) && !OutPackage.Contains(TEXT("..")) &&
        !OutPackage.Contains(TEXT("__ExternalActors__")) && !OutPackage.Contains(TEXT("__ExternalObjects__")) &&
        FPackageName::IsValidLongPackageName(OutPackage);
}

FString ObjectPath(const FString& Package)
{
    return Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package);
}

UEdGraph* Graph(UBlueprint* Blueprint, const FString& Name)
{
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* Candidate : Graphs)
    {
        if (Candidate && (Candidate->GetPathName(Blueprint) == Name || Candidate->GetPathName() == Name)) return Candidate;
    }
    UEdGraph* Match = nullptr;
    for (UEdGraph* Candidate : Graphs)
    {
        if (Candidate && Candidate->GetName() == Name)
        {
            if (Match) return nullptr; // 重名转换图必须使用完整相对路径。
            Match = Candidate;
        }
    }
    return Match;
}

UEdGraphNode* Node(UEdGraph* InGraph, const FString& Id)
{
    FGuid Guid;
    const bool bGuid = FGuid::Parse(Id, Guid);
    for (UEdGraphNode* Candidate : InGraph->Nodes)
    {
        if (Candidate && ((bGuid && Candidate->NodeGuid == Guid) || Candidate->GetName() == Id)) return Candidate;
    }
    return nullptr;
}

UEdGraphPin* Pin(UEdGraphNode* InNode, const FString& Name, EEdGraphPinDirection Direction)
{
    return InNode ? InNode->FindPin(FName(*Name), Direction) : nullptr;
}

bool Compile(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Result)
{
    FCompilerResultsLog Log;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave, &Log);
    Result->SetNumberField(TEXT("errors"), Log.NumErrors);
    Result->SetNumberField(TEXT("warnings"), Log.NumWarnings);
    Result->SetNumberField(TEXT("compileStatus"), static_cast<int32>(Blueprint->Status));
    TArray<TSharedPtr<FJsonValue>> Messages;
    for (const TSharedRef<FTokenizedMessage>& Message : Log.Messages)
    {
        Messages.Add(MakeShared<FJsonValueString>(Message->ToText().ToString()));
    }
    Result->SetArrayField(TEXT("messages"), Messages);
    return Log.NumErrors == 0 && Blueprint->Status != BS_Error;
}

bool Save(UBlueprint* Blueprint)
{
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    return UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *Filename, Args);
}

bool OrdinaryParent(UClass* Parent)
{
    // 特殊资产（AnimBP、WidgetBP 等）需要专用工厂；本接口只创建普通 Actor 蓝图。
    return Parent && Parent->IsChildOf(AActor::StaticClass()) &&
        !Parent->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists) &&
        FKismetEditorUtilities::CanCreateBlueprintOfClass(Parent);
}
}

bool UMcpAutomationBridgeSubsystem::HandleBlueprintEdit(
    const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    using namespace McpBlueprintEdit;
    const FString Action = String(Payload, TEXT("action")).ToLower();
    static const TSet<FString> Actions = {
        TEXT("create_blueprint"), TEXT("move_blueprint"), TEXT("reparent_blueprint"),
        TEXT("compile_blueprint"), TEXT("save_blueprint"), TEXT("add_node"), TEXT("connect_pins"),
        TEXT("disconnect_pins"), TEXT("set_pin_default"), TEXT("move_node"), TEXT("delete_node"),
        TEXT("open_blueprint"), TEXT("capture_graph"), TEXT("copy_anim_blueprint")};
    if (!Actions.Contains(Action)) return false;

    auto Fail = [&](const FString& Message, const FString& Code = TEXT("INVALID_ARGUMENT"))
    {
        SendAutomationError(RequestId, Message, Code);
        return true;
    };
    if (!GEditor || GEditor->PlayWorld || GEditor->bIsSimulatingInEditor)
        return Fail(TEXT("Blueprint editing requires an editor outside PIE/SIE"), TEXT("EDITOR_BUSY"));

    FString PackageName;
    if (!GamePackage(String(Payload, TEXT("blueprintPath")), PackageName))
        return Fail(TEXT("blueprintPath must be a valid /Game package or object path"), TEXT("INVALID_PATH"));
    const FString AssetPath = ObjectPath(PackageName);
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    UBlueprint* Blueprint = nullptr;
    const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    bool bSave = false;
    Payload->TryGetBoolField(TEXT("save"), bSave);

    if (Action == TEXT("create_blueprint"))
    {
        if (Registry.GetAssetByObjectPath(FSoftObjectPath(AssetPath)).IsValid() ||
            FindObject<UObject>(nullptr, *AssetPath) || FPackageName::DoesPackageExist(PackageName))
            return Fail(TEXT("Destination already exists; no asset was overwritten"), TEXT("ASSET_EXISTS"));
        UClass* Parent = LoadObject<UClass>(nullptr, *String(Payload, TEXT("parentClass"), TEXT("/Script/Engine.Actor")));
        if (!OrdinaryParent(Parent)) return Fail(TEXT("Create requires a Blueprintable Actor parent class"), TEXT("INVALID_PARENT"));
        const FScopedTransaction Transaction(NSLOCTEXT("McpBridge", "CreateBlueprint", "MCP 创建蓝图"));
        UPackage* Package = CreatePackage(*PackageName);
        Package->Modify();
        Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent, Package,
            FName(*FPackageName::GetLongPackageAssetName(PackageName)), BPTYPE_Normal,
            UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), TEXT("McpAutomationBridge"));
        if (!Blueprint) return Fail(TEXT("Blueprint creation failed"), TEXT("CREATE_FAILED"));
        FAssetRegistryModule::AssetCreated(Blueprint);
        Blueprint->MarkPackageDirty();
        Result->SetBoolField(TEXT("created"), true);
    }
    else
    {
        Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
        if (!Blueprint) return Fail(TEXT("Blueprint not found"), TEXT("BLUEPRINT_NOT_FOUND"));
    }
    Result->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
    Result->SetBoolField(TEXT("saved"), false);

    if (Action == TEXT("copy_anim_blueprint"))
    {
        // 仅初始化空 AnimBP；不覆盖已有动画逻辑，不依赖原资产的生成类实现继承。
        UAnimBlueprint* Target = Cast<UAnimBlueprint>(Blueprint);
        FString SourcePackage;
        if (!GamePackage(String(Payload, TEXT("sourceBlueprintPath")), SourcePackage) || SourcePackage == PackageName)
            return Fail(TEXT("sourceBlueprintPath must be a different /Game asset"), TEXT("INVALID_PATH"));
        UAnimBlueprint* Source = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath(SourcePackage));
        if (!Target || !Source || Source->bIsTemplate || !Source->TargetSkeleton ||
            !Source->ParentClass || !Source->ParentClass->HasAnyClassFlags(CLASS_Native) ||
            !Source->ParentClass->IsChildOf(UAnimInstance::StaticClass()) ||
            !Source->ImplementedInterfaces.IsEmpty() || !Target->ImplementedInterfaces.IsEmpty() ||
            !Source->ParentAssetOverrides.IsEmpty())
            return Fail(TEXT("Requires a concrete source AnimBP with native AnimInstance parent and no interface/parent asset overrides"), TEXT("UNSUPPORTED_ANIM_BLUEPRINT"));
        if (Target->TargetSkeleton && Target->TargetSkeleton != Source->TargetSkeleton)
            return Fail(TEXT("Source and target must use the same skeleton; this operation does not retarget animations"), TEXT("SKELETON_MISMATCH"));
        TArray<UEdGraph*> OldGraphs;
        Target->GetAllGraphs(OldGraphs);
        if (!Target->NewVariables.IsEmpty() || !Target->MacroGraphs.IsEmpty() || !Target->DelegateSignatureGraphs.IsEmpty())
            return Fail(TEXT("Target must be a newly created AnimBP without user variables or helper graphs"), TEXT("TARGET_NOT_EMPTY"));
        for (UEdGraph* OldGraph : OldGraphs)
        {
            if ((OldGraph->GetName() != TEXT("EventGraph") && OldGraph->GetName() != TEXT("AnimGraph")) || OldGraph->Nodes.Num() > 3)
                return Fail(TEXT("Target already contains animation logic"), TEXT("TARGET_NOT_EMPTY"));
            for (UEdGraphNode* OldNode : OldGraph->Nodes)
            {
                const FString ClassName = OldNode->GetClass()->GetName();
                if (ClassName != TEXT("AnimGraphNode_Root") && ClassName != TEXT("K2Node_Event") &&
                    ClassName != TEXT("K2Node_CallParentFunction") && ClassName != TEXT("K2Node_CallFunction"))
                    return Fail(TEXT("Target contains non-template nodes"), TEXT("TARGET_NOT_EMPTY"));
                if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(OldNode))
                {
                    const FName Function = Call->FunctionReference.GetMemberName();
                    if (Function != TEXT("TryGetPawnOwner") && Function != TEXT("BlueprintUpdateAnimation"))
                        return Fail(TEXT("Target contains a user function call"), TEXT("TARGET_NOT_EMPTY"));
                }
            }
        }
        const TSharedPtr<FJsonObject> SourceCompile = MakeShared<FJsonObject>();
        if (!Compile(Source, SourceCompile))
        {
            SendAutomationResponse(RequestId, false, TEXT("Source compilation failed; target unchanged"), SourceCompile, TEXT("COMPILE_FAILED"));
            return true;
        }
        // 先通过资产系统备份目标，后续错误只保留未保存状态及可恢复的原资产。
        IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
        FString BackupPackage, BackupName;
        AssetTools.CreateUniqueAssetName(PackageName + TEXT("_BeforeAnimCopy"), TEXT(""), BackupPackage, BackupName);
        UBlueprint* Backup = Cast<UBlueprint>(AssetTools.DuplicateAsset(BackupName, FPackageName::GetLongPackagePath(BackupPackage), Target));
        if (!Backup || !Save(Backup)) return Fail(TEXT("Could not save target backup; target unchanged"), TEXT("BACKUP_FAILED"));
        Result->SetStringField(TEXT("backupPath"), Backup->GetPathName());
        Result->SetStringField(TEXT("previousParentClass"), GetPathNameSafe(Target->ParentClass));
        GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Target);
        const FScopedTransaction Transaction(NSLOCTEXT("McpBridge", "CopyAnimBlueprint", "MCP 复制动画图与状态机"));
        Target->Modify();
        // 先移开旧图，以保留事务对象并让复制图保持原名。所有资产引用仍指向原 Target。
        TArray<UEdGraph*> OldRoots;
        OldRoots.Append(Target->UbergraphPages);
        OldRoots.Append(Target->FunctionGraphs);
        for (UEdGraph* OldRoot : OldRoots)
        {
            OldRoot->Modify();
            OldRoot->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
        }
        Target->UbergraphPages.Reset();
        Target->FunctionGraphs.Reset();
        Target->ParentAssetOverrides.Reset();
        Target->PoseWatches.Reset();
        Target->PoseWatchFolders.Reset();
        Target->NewVariables = Source->NewVariables;
        Target->TargetSkeleton = Source->TargetSkeleton;
        Target->bIsTemplate = false;
        Target->Groups = Source->Groups;
        Target->bUseMultiThreadedAnimationUpdate = Source->bUseMultiThreadedAnimationUpdate;
        Target->bWarnAboutBlueprintUsage = Source->bWarnAboutBlueprintUsage;
        Target->bEnableLinkedAnimLayerInstanceSharing = Source->bEnableLinkedAnimLayerInstanceSharing;
        Target->SetPreviewMesh(Source->GetPreviewMesh());
        UBlueprintEditorLibrary::ReparentBlueprint(Target, Source->ParentClass);
        if (!Target->GeneratedClass || !Target->SkeletonGeneratedClass)
        {
            SendAutomationResponse(RequestId, false, TEXT("Could not generate target classes; restore from backupPath"), Result, TEXT("COMPILE_FAILED"));
            return true;
        }
        // DuplicationSeed 对齐父资产、生成类和 CDO，保留嵌套图、状态转换、缓存姿势和节点配置。
        TMap<UObject*, UObject*> Copies;
        Copies.Add(Source, Target);
        Copies.Add(Source->GeneratedClass, Target->GeneratedClass);
        Copies.Add(Source->SkeletonGeneratedClass, Target->SkeletonGeneratedClass);
        Copies.Add(Source->GeneratedClass->GetDefaultObject(), Target->GeneratedClass->GetDefaultObject());
        auto CopyGraphs = [&](const TArray<TObjectPtr<UEdGraph>>& From, TArray<TObjectPtr<UEdGraph>>& To)
        {
            for (UEdGraph* SourceGraph : From)
            {
                FObjectDuplicationParameters Params(SourceGraph, Target);
                Params.DestName = SourceGraph->GetFName();
                Params.DuplicationSeed = Copies;
                TMap<UObject*, UObject*> Created;
                Params.CreatedObjects = &Created;
                To.Add(CastChecked<UEdGraph>(StaticDuplicateObjectEx(Params)));
                Copies.Append(Created);
            }
        };
        CopyGraphs(Source->FunctionGraphs, Target->FunctionGraphs);
        CopyGraphs(Source->UbergraphPages, Target->UbergraphPages);
        CopyGraphs(Source->MacroGraphs, Target->MacroGraphs);
        CopyGraphs(Source->DelegateSignatureGraphs, Target->DelegateSignatureGraphs);
        for (UEdGraph* CopiedGraph : Target->FunctionGraphs)
        {
            FArchiveReplaceObjectRef<UObject> Replace(CopiedGraph, Copies);
        }
        for (UEdGraph* CopiedGraph : Target->UbergraphPages)
        {
            FArchiveReplaceObjectRef<UObject> Replace(CopiedGraph, Copies);
        }
        Target->RequestRefreshExtensions();
        FBlueprintEditorUtils::RefreshAllNodes(Target);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target);
        bool bSuccess = Compile(Target, Result);
        if (bSuccess)
        {
            UObject* FromDefaults = Source->GeneratedClass->GetDefaultObject();
            UObject* ToDefaults = Target->GeneratedClass->GetDefaultObject();
            ToDefaults->Modify();
            int32 DefaultsCopied = 0;
            for (TFieldIterator<FProperty> It(FromDefaults->GetClass()); It; ++It)
            {
                FProperty* From = *It;
                FProperty* To = FindFProperty<FProperty>(ToDefaults->GetClass(), From->GetFName());
                if (To && From->SameType(To) && From->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible) &&
                    !From->HasAnyPropertyFlags(CPF_Transient | CPF_InstancedReference | CPF_ContainsInstancedReference))
                {
                    To->CopyCompleteValue(To->ContainerPtrToValuePtr<void>(ToDefaults), From->ContainerPtrToValuePtr<void>(FromDefaults));
                    ++DefaultsCopied;
                }
            }
            Result->SetNumberField(TEXT("defaultsCopied"), DefaultsCopied);
        }
        Target->MarkPackageDirty();
        Result->SetStringField(TEXT("parentClass"), GetPathNameSafe(Target->ParentClass));
        Result->SetStringField(TEXT("skeleton"), GetPathNameSafe(Target->TargetSkeleton));
        Result->SetNumberField(TEXT("copiedObjectCount"), Copies.Num() - 4);
        FString Error = bSuccess ? FString() : TEXT("COMPILE_FAILED");
        if (bSuccess && bSave)
        {
            bSuccess = Save(Target);
            Result->SetBoolField(TEXT("saved"), bSuccess);
            if (!bSuccess) Error = TEXT("SAVE_FAILED");
        }
        SendAutomationResponse(RequestId, bSuccess, TEXT("Animation copy completed; inspect compile result and backupPath"), Result, Error);
        return true;
    }

    if (Action == TEXT("move_blueprint"))
    {
        FString Destination;
        if (!bSave) return Fail(TEXT("move_blueprint requires save=true to save and fix redirectors"));
        if (!GamePackage(String(Payload, TEXT("destinationPath")), Destination) || Destination == PackageName)
            return Fail(TEXT("destinationPath must be a different valid /Game package"), TEXT("INVALID_PATH"));
        const FString DestinationObject = ObjectPath(Destination);
        if (Registry.GetAssetByObjectPath(FSoftObjectPath(DestinationObject)).IsValid() ||
            FindObject<UObject>(nullptr, *DestinationObject) || FPackageName::DoesPackageExist(Destination))
            return Fail(TEXT("Destination already exists"), TEXT("ASSET_EXISTS"));

        TArray<FName> Referencers;
        Registry.GetReferencers(FName(*PackageName), Referencers);
        TArray<TSharedPtr<FJsonValue>> ReferenceValues;
        for (FName Reference : Referencers) ReferenceValues.Add(MakeShared<FJsonValueString>(Reference.ToString()));
        Result->SetArrayField(TEXT("referencersBeforeMove"), ReferenceValues);
        if (!Compile(Blueprint, Result))
        {
            SendAutomationResponse(RequestId, false, TEXT("Compilation failed before move"), Result, TEXT("COMPILE_FAILED"));
            return true;
        }
        IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
        const bool bMoved = Tools.RenameAssets({FAssetRenameData(Blueprint,
            FPackageName::GetLongPackagePath(Destination), FPackageName::GetLongPackageAssetName(Destination))});
        Result->SetBoolField(TEXT("moved"), bMoved);
        Result->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
        if (!bMoved)
        {
            SendAutomationResponse(RequestId, false, TEXT("Asset Tools could not move the Blueprint"), Result, TEXT("MOVE_FAILED"));
            return true;
        }
        const bool bSaved = Save(Blueprint);
        Result->SetBoolField(TEXT("saved"), bSaved);
        if (!bSaved)
        {
            SendAutomationResponse(RequestId, false, TEXT("Moved in memory but saving failed"), Result, TEXT("SAVE_FAILED"));
            return true;
        }
        if (UObjectRedirector* Redirector = FindObject<UObjectRedirector>(nullptr, *AssetPath))
        {
            Tools.FixupReferencers({Redirector}, false, ERedirectFixupMode::DeleteFixedUpRedirectors);
        }
        const bool bRedirectorRemains = FPackageName::DoesPackageExist(PackageName);
        Result->SetBoolField(TEXT("redirectorFixed"), !bRedirectorRemains);
        SendAutomationResponse(RequestId, !bRedirectorRemains,
            bRedirectorRemains ? TEXT("Blueprint moved and saved; source redirector could not be fully fixed") : TEXT("Blueprint moved, saved and redirector fixed"),
            Result, bRedirectorRemains ? TEXT("REDIRECTOR_REMAINS") : FString());
        return true;
    }

    if (Action == TEXT("reparent_blueprint"))
    {
        UClass* Parent = LoadObject<UClass>(nullptr, *String(Payload, TEXT("parentClass")));
        const bool bValidType = (Blueprint->GetClass() == UBlueprint::StaticClass() && OrdinaryParent(Parent)) ||
            (Cast<UAnimBlueprint>(Blueprint) && Parent && Parent->IsChildOf(UAnimInstance::StaticClass()) &&
             !Parent->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists));
        if (!bValidType ||
            (Blueprint->GeneratedClass && Parent->IsChildOf(Blueprint->GeneratedClass)))
            return Fail(TEXT("Requires an Actor/AnimInstance parent matching the Blueprint type, without inheritance cycles"), TEXT("INVALID_PARENT"));
        Result->SetStringField(TEXT("previousParentClass"), GetPathNameSafe(Blueprint->ParentClass));
        const FScopedTransaction Transaction(NSLOCTEXT("McpBridge", "ReparentBlueprint", "MCP 更改蓝图父类"));
        Blueprint->Modify();
        if (Blueprint->ParentClass != Parent) UBlueprintEditorLibrary::ReparentBlueprint(Blueprint, Parent);
        Result->SetStringField(TEXT("parentClass"), GetPathNameSafe(Blueprint->ParentClass));
        Blueprint->MarkPackageDirty();
    }

    if (Action == TEXT("create_blueprint") || Action == TEXT("reparent_blueprint") ||
        Action == TEXT("compile_blueprint") || Action == TEXT("save_blueprint"))
    {
        bool bSuccess = Compile(Blueprint, Result);
        const bool bMustSave = bSave || Action == TEXT("save_blueprint");
        FString Error = bSuccess ? FString() : TEXT("COMPILE_FAILED");
        if (bSuccess && bMustSave)
        {
            bSuccess = Save(Blueprint);
            Result->SetBoolField(TEXT("saved"), bSuccess);
            if (!bSuccess) Error = TEXT("SAVE_FAILED");
        }
        Result->SetStringField(TEXT("parentClass"), GetPathNameSafe(Blueprint->ParentClass));
        SendAutomationResponse(RequestId, bSuccess, bSuccess ? TEXT("Blueprint action completed") : TEXT("Blueprint action failed; inspect result and unsaved editor state"), Result, Error);
        return true;
    }

    const FString GraphName = String(Payload, TEXT("graphName"), TEXT("EventGraph"));
    UEdGraph* TargetGraph = Graph(Blueprint, GraphName);
    if (!TargetGraph) return Fail(TEXT("Graph not found"), TEXT("GRAPH_NOT_FOUND"));
    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(TargetGraph->GetSchema());
    Result->SetStringField(TEXT("graphName"), TargetGraph->GetName());

    if (Action == TEXT("open_blueprint") || Action == TEXT("capture_graph"))
    {
        if (!FSlateApplication::IsInitialized() || IsRunningCommandlet())
            return Fail(TEXT("A rendered editor session is required"), TEXT("NO_RENDERING"));
        UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        if (!Editors->OpenEditorForAsset(Blueprint)) return Fail(TEXT("Could not open Blueprint editor"), TEXT("OPEN_FAILED"));
        IAssetEditorInstance* Instance = Editors->FindEditorForAsset(Blueprint, true);
        if (!Instance || (Instance->GetEditorName() != FName(TEXT("BlueprintEditor")) &&
            Instance->GetEditorName() != FName(TEXT("AnimationBlueprintEditor"))))
            return Fail(TEXT("Requires a Blueprint or Animation Blueprint editor"), TEXT("UNSUPPORTED_EDITOR"));
        FBlueprintEditor* BlueprintEditor = static_cast<FBlueprintEditor*>(Instance);
        TSharedPtr<SGraphEditor> Widget = BlueprintEditor->OpenGraphAndBringToFront(TargetGraph);
        if (!Widget.IsValid()) return Fail(TEXT("Graph editor widget is not ready"), TEXT("GRAPH_NOT_READY"));
        if (Action == TEXT("open_blueprint"))
        {
            double Zoom = 0;
            if (Payload->TryGetNumberField(TEXT("zoom"), Zoom))
            {
                double X = -100, Y = -100;
                Payload->TryGetNumberField(TEXT("viewX"), X);
                Payload->TryGetNumberField(TEXT("viewY"), Y);
                Widget->SetViewLocation(FVector2f(X, Y), FMath::Clamp(static_cast<float>(Zoom), 0.1f, 1.0f));
            }
            else Widget->ZoomToFit(false);
            SendAutomationResponse(RequestId, true, TEXT("Graph opened; allow an editor frame before capture_graph"), Result);
            return true;
        }
        TArray<FColor> Pixels;
        FIntVector Size;
        if (!FSlateApplication::Get().TakeScreenshot(Widget.ToSharedRef(), Pixels, Size) || Size.X <= 0 || Size.Y <= 0)
            return Fail(TEXT("Graph has not rendered; open it first and retry after an editor frame"), TEXT("CAPTURE_FAILED"));
        TArray64<uint8> Png;
        FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
        const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("MCP/Screenshots"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        const FString Filename = Directory / (Blueprint->GetName() + TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".png"));
        if (!FFileHelper::SaveArrayToFile(Png, *Filename)) return Fail(TEXT("Could not save screenshot"), TEXT("SAVE_FAILED"));
        Result->SetStringField(TEXT("imagePath"), Filename);
        Result->SetNumberField(TEXT("width"), Size.X);
        Result->SetNumberField(TEXT("height"), Size.Y);
        SendAutomationResponse(RequestId, true, TEXT("Actual Blueprint graph widget captured"), Result);
        return true;
    }

    if (!Schema || !TargetGraph->bEditable) return Fail(TEXT("Only editable K2-derived graphs are supported"), TEXT("UNSUPPORTED_GRAPH"));

    if (Action == TEXT("add_node"))
    {
        const FString Type = String(Payload, TEXT("nodeType"));
        UFunction* Function = nullptr;
        UClass* FunctionClass = nullptr;
        if (Type == TEXT("call_function") || Type == TEXT("event"))
        {
            const FString DefaultClass = Type == TEXT("event") ? GetPathNameSafe(Blueprint->ParentClass) : FString();
            FunctionClass = LoadObject<UClass>(nullptr, *String(Payload, TEXT("functionClass"), DefaultClass));
            Function = FunctionClass ? FunctionClass->FindFunctionByName(FName(*String(Payload, TEXT("functionName")))) : nullptr;
            if (!Function) return Fail(TEXT("Reflected function not found"), TEXT("FUNCTION_NOT_FOUND"));
            if (Type == TEXT("call_function") && !UEdGraphSchema_K2::CanUserKismetCallFunction(Function))
                return Fail(TEXT("Function is not Blueprint callable"), TEXT("INVALID_FUNCTION"));
            if (Type == TEXT("event") && (!UEdGraphSchema_K2::FunctionCanBePlacedAsEvent(Function) ||
                !Blueprint->ParentClass->IsChildOf(FunctionClass) || !Blueprint->UbergraphPages.Contains(TargetGraph) ||
                FBlueprintEditorUtils::FindOverrideForFunction(Blueprint, FunctionClass, Function->GetFName())))
                return Fail(TEXT("Event must be an unused parent event in an event graph"), TEXT("INVALID_EVENT"));
        }
        static const TSet<FString> Types = {TEXT("call_function"), TEXT("event"), TEXT("custom_event"), TEXT("branch"), TEXT("sequence"), TEXT("comment")};
        if (!Types.Contains(Type)) return Fail(TEXT("Unsupported nodeType"));
        const FString EventName = String(Payload, TEXT("eventName"));
        if (Type == TEXT("custom_event") && (EventName.IsEmpty() || !Blueprint->UbergraphPages.Contains(TargetGraph) ||
            FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, EventName) != FName(*EventName)))
            return Fail(TEXT("Custom event requires a unique eventName and an event graph"));
        const FScopedTransaction Transaction(NSLOCTEXT("McpBridge", "AddNode", "MCP 添加蓝图节点"));
        Blueprint->Modify();
        TargetGraph->Modify();
        UEdGraphNode* NewNode = nullptr;
        if (Type == TEXT("call_function"))
        {
            UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(TargetGraph, NAME_None, RF_Transactional);
            Call->SetFromFunction(Function);
            NewNode = Call;
        }
        else if (Type == TEXT("event"))
        {
            UK2Node_Event* Event = NewObject<UK2Node_Event>(TargetGraph, NAME_None, RF_Transactional);
            Event->EventReference.SetExternalMember(Function->GetFName(), FunctionClass);
            Event->bOverrideFunction = true;
            NewNode = Event;
        }
        else if (Type == TEXT("custom_event"))
        {
            UK2Node_CustomEvent* Event = NewObject<UK2Node_CustomEvent>(TargetGraph, NAME_None, RF_Transactional);
            Event->CustomFunctionName = FName(*EventName);
            NewNode = Event;
        }
        else if (Type == TEXT("branch")) NewNode = NewObject<UK2Node_IfThenElse>(TargetGraph, NAME_None, RF_Transactional);
        else if (Type == TEXT("sequence")) NewNode = NewObject<UK2Node_ExecutionSequence>(TargetGraph, NAME_None, RF_Transactional);
        else NewNode = NewObject<UEdGraphNode_Comment>(TargetGraph, NAME_None, RF_Transactional);
        if (!NewNode->CanCreateUnderSpecifiedSchema(Schema))
        {
            NewNode->MarkAsGarbage();
            return Fail(TEXT("Node cannot be created under this graph schema"), TEXT("UNSUPPORTED_GRAPH"));
        }
        TargetGraph->AddNode(NewNode, true, false);
        NewNode->CreateNewGuid();
        NewNode->PostPlacedNewNode();
        NewNode->AllocateDefaultPins();
        Payload->TryGetNumberField(TEXT("x"), NewNode->NodePosX);
        Payload->TryGetNumberField(TEXT("y"), NewNode->NodePosY);
        NewNode->NodeComment = String(Payload, TEXT("comment"));
        Result->SetStringField(TEXT("nodeId"), NewNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
        Result->SetStringField(TEXT("nodeName"), NewNode->GetName());
    }
    else if (Action == TEXT("connect_pins") || Action == TEXT("disconnect_pins"))
    {
        UEdGraphNode* Source = Node(TargetGraph, String(Payload, TEXT("sourceNodeId")));
        UEdGraphNode* Target = Node(TargetGraph, String(Payload, TEXT("targetNodeId")));
        UEdGraphPin* Output = Pin(Source, String(Payload, TEXT("sourcePinName")), EGPD_Output);
        UEdGraphPin* Input = Pin(Target, String(Payload, TEXT("targetPinName")), EGPD_Input);
        if (!Output || !Input) return Fail(TEXT("Source output or target input pin not found in graph"), TEXT("PIN_NOT_FOUND"));
        const bool bLinked = Output->LinkedTo.Contains(Input);
        if ((Action == TEXT("connect_pins") && bLinked) || (Action == TEXT("disconnect_pins") && !bLinked))
        {
            Result->SetBoolField(TEXT("changed"), false);
            SendAutomationResponse(RequestId, true, TEXT("Requested connection state already holds"), Result);
            return true;
        }
        if (Action == TEXT("connect_pins"))
        {
            const FPinConnectionResponse Response = Schema->CanCreateConnection(Output, Input);
            Result->SetStringField(TEXT("schemaMessage"), Response.Message.ToString());
            if (Response.Response == CONNECT_RESPONSE_DISALLOW)
                return Fail(Response.Message.ToString(), TEXT("INCOMPATIBLE_PINS"));
            bool bBreakExisting = false;
            Payload->TryGetBoolField(TEXT("breakExisting"), bBreakExisting);
            // 自动转换节点的响应不一定包含 BREAK_OTHERS，但随后也可能替换单输入连线。
            const bool bOccupiedSinglePin =
                (Output->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec && !Output->LinkedTo.IsEmpty()) ||
                (Input->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && !Input->LinkedTo.IsEmpty());
            if (!bBreakExisting && (bOccupiedSinglePin || Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_A ||
                Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_B || Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_AB))
                return Fail(TEXT("Connection would replace an existing link; pass breakExisting=true explicitly"), TEXT("LINK_REPLACEMENT_REQUIRED"));
        }
        const FScopedTransaction Transaction(NSLOCTEXT("McpBridge", "ConnectPins", "MCP 更改蓝图连线"));
        Blueprint->Modify();
        TargetGraph->Modify();
        // Schema 可能打断第三个节点的已有连接，全部节点进入同一撤销事务。
        for (UEdGraphNode* Existing : TargetGraph->Nodes) if (Existing) Existing->Modify();
        if (Action == TEXT("disconnect_pins")) Schema->BreakSinglePinLink(Output, Input);
        else if (!Schema->TryCreateConnection(Output, Input)) return Fail(TEXT("Schema refused connection"), TEXT("CONNECTION_FAILED"));
        Result->SetBoolField(TEXT("changed"), true);
    }
    else
    {
        UEdGraphNode* Existing = Node(TargetGraph, String(Payload, TEXT("nodeId")));
        if (!Existing) return Fail(TEXT("Node GUID or object name not found"), TEXT("NODE_NOT_FOUND"));
        UEdGraphPin* Input = nullptr;
        if (Action == TEXT("set_pin_default"))
        {
            Input = Pin(Existing, String(Payload, TEXT("pinName")), EGPD_Input);
            if (!Input || !Input->LinkedTo.IsEmpty() || Input->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec || Input->bDefaultValueIsReadOnly)
                return Fail(TEXT("Default requires an editable unconnected data input pin"), TEXT("INVALID_PIN"));
            if (!Payload->HasTypedField<EJson::String>(TEXT("defaultValue"))) return Fail(TEXT("defaultValue string is required"));
            const FString Error = Schema->IsPinDefaultValid(Input, String(Payload, TEXT("defaultValue")), nullptr, FText());
            if (!Error.IsEmpty()) return Fail(Error, TEXT("INVALID_DEFAULT"));
        }
        if (Action == TEXT("delete_node") && !Existing->CanUserDeleteNode())
            return Fail(TEXT("This node cannot be deleted"), TEXT("DELETE_REFUSED"));
        const FScopedTransaction Transaction(NSLOCTEXT("McpBridge", "EditNode", "MCP 编辑蓝图节点"));
        Blueprint->Modify();
        TargetGraph->Modify();
        Existing->Modify();
        if (Action == TEXT("set_pin_default"))
        {
            Schema->TrySetDefaultValue(*Input, String(Payload, TEXT("defaultValue")));
            Result->SetStringField(TEXT("defaultValue"), Input->DefaultValue);
        }
        else if (Action == TEXT("move_node"))
        {
            Payload->TryGetNumberField(TEXT("x"), Existing->NodePosX);
            Payload->TryGetNumberField(TEXT("y"), Existing->NodePosY);
        }
        else
        {
            for (UEdGraphNode* Other : TargetGraph->Nodes) if (Other) Other->Modify();
            FBlueprintEditorUtils::RemoveNode(Blueprint, Existing, true);
        }
    }
    TargetGraph->NotifyGraphChanged();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Blueprint->MarkPackageDirty();
    SendAutomationResponse(RequestId, true, TEXT("Graph edited in memory; compile and save when finished"), Result);
    return true;
}
