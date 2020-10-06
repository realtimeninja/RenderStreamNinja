// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "RenderStream.h"

#include "RenderStreamSettings.h"
#include "RenderStreamMediaCapture.h"

#include "Core/Public/Modules/ModuleManager.h"
#include "CoreUObject/Public/Misc/PackageName.h"
#include "Misc/CoreDelegates.h"
#include "Json/Public/Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"

#include "Kismet/GameplayStatics.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "ShaderCore.h"

#include "fnv.hpp"
#include <map>

DEFINE_LOG_CATEGORY(LogRenderStream);

#define LOCTEXT_NAMESPACE "FRenderStreamModule"

namespace {
    void log_default(const char* text) {
        UE_LOG(LogRenderStream, Log, TEXT("%s"), ANSI_TO_TCHAR(text));
    }

    void log_verbose(const char* text) {
        UE_LOG(LogRenderStream, Verbose, TEXT("%s"), ANSI_TO_TCHAR(text));
    }

    void log_error(const char* text) {
        UE_LOG(LogRenderStream, Error, TEXT("%s"), ANSI_TO_TCHAR(text));
    }
}

void RenderStreamStatus::setInputAndOutputStatuses(const FString& topText, const FString& bottomText, const FSlateColor& color)
{
    outputText = topText;
    inputText = bottomText;
    outputColor = color;
    inputColor = color;
    changed = true;
}

void RenderStreamStatus::setOutputStatus(const FString& text, const FSlateColor& color)
{
    outputText = text;
    outputColor = color;
    changed = true;
}

void RenderStreamStatus::setInputStatus(const FString& text, const FSlateColor& color)
{
    inputText = text;
    inputColor = color;
    changed = true;
}

void FRenderStreamModule::StartupModule()
{
    FString ShaderDirectory = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("DisguiseUERenderStream/Shaders"));
    AddShaderSourceDirectoryMapping("/DisguiseUERenderStream", ShaderDirectory);

    // This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
    m_status.setInputAndOutputStatuses("Initialising stream", "Waiting for data from d3", RSSTATUS_ORANGE);

    if (!RenderStreamLink::instance ().loadExplicit ())
    {
        UE_LOG(LogRenderStream, Error, TEXT ("Failed to load RenderStream DLL - d3 not installed?"));
        m_status.setInputAndOutputStatuses("Error", "Failed to load RenderStream DLL - d3 not installed?", RSSTATUS_RED);
    }
    else
    {
        int major, minor;
        RenderStreamLink::instance().rs_getVersion(&major, &minor);
        UE_LOG(LogRenderStream, Log, TEXT("Loaded d3renderstream.dll version %i.%i."), major, minor);

        if (major != RENDER_STREAM_VERSION_MAJOR || minor != RENDER_STREAM_VERSION_MINOR)
        {
            UE_LOG(LogRenderStream, Error, TEXT("Unsupported RenderStream library, expected version %i.%i"), RENDER_STREAM_VERSION_MAJOR, RENDER_STREAM_VERSION_MINOR);
            m_status.setInputAndOutputStatuses("Error", "Unsupported RenderStream library", RSSTATUS_RED);
            RenderStreamLink::instance().unloadExplicit();
            return;
        }

        int errCode = RenderStreamLink::instance().rs_init();
        if (errCode != 0)
        {
            UE_LOG(LogRenderStream, Error, TEXT("Unable to initialise RenderStream library error code %d"), errCode);
            m_status.setInputAndOutputStatuses("Error", "Unable to initialise RenderStream library", RSSTATUS_RED);
            RenderStreamLink::instance().unloadExplicit();
            return;
        }

        FString assetName = FApp::GetProjectName();
        if (RenderStreamLink::instance().rs_createAsset(TCHAR_TO_ANSI(*assetName), &m_assetHandle) != 0)
        {
            UE_LOG(LogRenderStream, Error, TEXT("Unable to create asset - failure to initialise."));
            m_status.setInputAndOutputStatuses("Error", "Unable to create asset - failure to initialise", RSSTATUS_RED);
            RenderStreamLink::instance().unloadExplicit();
            return;
        }
        UE_LOG(LogRenderStream, Log, TEXT("Created Asset '%s'"), *assetName);
    }

    OnBeginFrameHandle = FCoreDelegates::OnBeginFrame.AddRaw(this, &FRenderStreamModule::OnBeginFrame);
}

void FRenderStreamModule::ShutdownModule()
{
    if (!RenderStreamLink::instance().isAvailable())
        return;

    UE_LOG(LogRenderStream, Log, TEXT("Shutting down RenderStream"));

    FCoreDelegates::OnBeginFrame.Remove(OnBeginFrameHandle);

    if (m_assetHandle != 0)
        RenderStreamLink::instance().rs_destroyAsset(&m_assetHandle);

    // This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
    // we call this function before unloading the module.
    if (!RenderStreamLink::instance ().unloadExplicit ())
    {
        UE_LOG (LogRenderStream, Warning, TEXT ("Failed to free render stream module."));
    }
}

bool FRenderStreamModule::SupportsAutomaticShutdown ()
{
    return true;
}

bool FRenderStreamModule::SupportsDynamicReloading ()
{
    return true;
}

void validateField(StreamFNV& fnv, FString key_, FString undecoratedSuffix, const TSharedPtr< FJsonValue >& JsonValue)
{
    if (!JsonValue)
        throw std::runtime_error("Null parameter");
    const TSharedPtr<FJsonObject>& JsonParameter = JsonValue->AsObject();
    if (!JsonParameter)
        throw std::runtime_error("Non-object parameter");

    FString key = key_ + (undecoratedSuffix.IsEmpty() ? "" : "_" + undecoratedSuffix);

    if (key != JsonParameter->GetStringField(TEXT("key")))
        throw std::runtime_error("Parameter mismatch");

    const std::string stdKey = TCHAR_TO_ANSI(*key);
    fnv.addData(reinterpret_cast<const unsigned char*>(stdKey.data()), stdKey.size());
}

void FRenderStreamModule::ValidateSchema(const TSharedPtr<FJsonObject>& JsonSchema, const AActor* Root, SchemaSpec& spec)
{
    StreamFNV fnv;
    spec.nParameters = 0;

    const FString Scene = JsonSchema->GetStringField(TEXT("name"));
    const TArray< TSharedPtr<FJsonValue> > JsonParameters = JsonSchema->GetArrayField(TEXT("parameters"));

    if (Root)
    {
        UE_LOG(LogRenderStream, Log, TEXT("Validating schema for %s"), *Scene);
        for (TFieldIterator<FProperty> PropIt(Root->GetClass(), EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
        {
            const FProperty* Property = *PropIt;
            const FString Name = Property->GetName();
            if (!Property->HasAllPropertyFlags(CPF_Edit | CPF_BlueprintVisible) || Property->HasAllPropertyFlags(CPF_DisableEditOnInstance))
            {
                UE_LOG(LogRenderStream, Verbose, TEXT("Unexposed property: %s"), *Name);
            }
            else if (const FBoolProperty* BoolProperty = CastField<const FBoolProperty>(Property))
            {
                UE_LOG(LogRenderStream, Log, TEXT("Exposed bool property: %s"), *Name);
                if (JsonParameters.Num() < spec.nParameters + 1)
                    throw std::runtime_error("Property not exposed in schema");
                validateField(fnv, Name, "", JsonParameters[spec.nParameters]);
                ++spec.nParameters;
            }
            else if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(Property))
            {
                UE_LOG(LogRenderStream, Log, TEXT("Exposed int property: %s"), *Name);
                if (JsonParameters.Num() < spec.nParameters + 1)
                    throw std::runtime_error("Property not exposed in schema");
                validateField(fnv, Name, "", JsonParameters[spec.nParameters]);
                ++spec.nParameters;
            }
            else if (const FIntProperty* IntProperty = CastField<const FIntProperty>(Property))
            {
                UE_LOG(LogRenderStream, Log, TEXT("Exposed int property: %s"), *Name);
                if (JsonParameters.Num() < spec.nParameters + 1)
                    throw std::runtime_error("Property not exposed in schema");
                validateField(fnv, Name, "", JsonParameters[spec.nParameters]);
                ++spec.nParameters;
            }
            else if (const FFloatProperty* FloatProperty = CastField<const FFloatProperty>(Property))
            {
                UE_LOG(LogRenderStream, Log, TEXT("Exposed float property: %s"), *Name);
                if (JsonParameters.Num() < spec.nParameters + 1)
                    throw std::runtime_error("Property not exposed in schema");
                validateField(fnv, Name, "", JsonParameters[spec.nParameters]);
                ++spec.nParameters;
            }
            else if (const FStructProperty* StructProperty = CastField<const FStructProperty>(Property))
            {
                const void* StructAddress = StructProperty->ContainerPtrToValuePtr<void>(Root);
                if (StructProperty->Struct == TBaseStructure<FVector>::Get())
                {
                    UE_LOG(LogRenderStream, Log, TEXT("Exposed vector property: %s"), *Name);
                    if (JsonParameters.Num() < spec.nParameters + 3)
                        throw std::runtime_error("Properties not exposed in schema");
                    validateField(fnv, Name, "x", JsonParameters[spec.nParameters + 0]);
                    validateField(fnv, Name, "y", JsonParameters[spec.nParameters + 1]);
                    validateField(fnv, Name, "z", JsonParameters[spec.nParameters + 2]);
                    spec.nParameters += 3;
                }
                else if (StructProperty->Struct == TBaseStructure<FColor>::Get())
                {
                    UE_LOG(LogRenderStream, Log, TEXT("Exposed colour property: %s"), *Name);
                    if (JsonParameters.Num() < spec.nParameters + 4)
                        throw std::runtime_error("Properties not exposed in schema");
                    validateField(fnv, Name, "r", JsonParameters[spec.nParameters + 0]);
                    validateField(fnv, Name, "g", JsonParameters[spec.nParameters + 1]);
                    validateField(fnv, Name, "b", JsonParameters[spec.nParameters + 2]);
                    validateField(fnv, Name, "a", JsonParameters[spec.nParameters + 3]);
                    spec.nParameters += 4;
                }
                else if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
                {
                    UE_LOG(LogRenderStream, Log, TEXT("Exposed linear colour property: %s"), *Name);
                    if (JsonParameters.Num() < spec.nParameters + 4)
                        throw std::runtime_error("Properties not exposed in schema");
                    validateField(fnv, Name, "r", JsonParameters[spec.nParameters + 0]);
                    validateField(fnv, Name, "g", JsonParameters[spec.nParameters + 1]);
                    validateField(fnv, Name, "b", JsonParameters[spec.nParameters + 2]);
                    validateField(fnv, Name, "a", JsonParameters[spec.nParameters + 3]);
                    spec.nParameters += 4;
                }
                else
                {
                    UE_LOG(LogRenderStream, Log, TEXT("Exposed struct property: %s"), *Name);
                }
            }
            else
            {
                UE_LOG(LogRenderStream, Log, TEXT("Unsupported exposed property: %s"), *Name);
            }
        }
        if (spec.nParameters != JsonParameters.Num())
            throw std::runtime_error("Excess parameters in schema");

        spec.schemaRoot = Root;
        UE_LOG(LogRenderStream, Log, TEXT("Validated schema"));
    }

    spec.schemaHash = fnv.getHash();
}

void FRenderStreamModule::ApplyParameters(AActor* Root, const std::vector<float>& parameters)
{
    if (Root)
    {
        size_t i = 0;
        for (TFieldIterator<FProperty> PropIt(Root->GetClass(), EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
        {
            FProperty* Property = *PropIt;
            if (!Property->HasAllPropertyFlags(CPF_Edit | CPF_BlueprintVisible) || Property->HasAllPropertyFlags(CPF_DisableEditOnInstance))
            {
                continue;
            }
            else if (const FBoolProperty* BoolProperty = CastField<const FBoolProperty>(Property))
            {
                const bool v = bool(parameters.at(i));
                BoolProperty->SetPropertyValue_InContainer(Root, v);
                ++i;
            }
            else if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
            {
                const uint8 v = uint8(parameters.at(i));
                ByteProperty->SetPropertyValue_InContainer(Root, v);
                ++i;
            }
            else if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
            {
                const int32 v = int(parameters.at(i));
                IntProperty->SetPropertyValue_InContainer(Root, v);
                ++i;
            }
            else if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
            {
                const float v = parameters.at(i);
                FloatProperty->SetPropertyValue_InContainer(Root, v);
                ++i;
            }
            else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
            {
                void* StructAddress = StructProperty->ContainerPtrToValuePtr<void>(Root);
                if (StructProperty->Struct == TBaseStructure<FVector>::Get())
                {
                    FVector v(parameters.at(i), parameters.at(i + 1), parameters.at(i + 2));
                    StructProperty->CopyCompleteValue(StructAddress, &v);
                    i += 3;
                }
                else if (StructProperty->Struct == TBaseStructure<FColor>::Get())
                {
                    FColor v(parameters.at(i) * 255, parameters.at(i + 1) * 255, parameters.at(i + 2) * 255, parameters.at(i + 3) * 255);
                    StructProperty->CopyCompleteValue(StructAddress, &v);
                    i += 4;
                }
                else if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
                {
                    FLinearColor v(parameters.at(i), parameters.at(i + 1), parameters.at(i + 2), parameters.at(i + 3));
                    StructProperty->CopyCompleteValue(StructAddress, &v);
                    i += 4;
                }
            }
        }
    }
}

void FRenderStreamModule::LoadSchemas(const UWorld& World)
{
    m_specs.clear();

    FString Schema;
    FString SchemaPath = FPaths::Combine(*FPaths::ProjectContentDir(), *FString("DisguiseRenderStream"), *FString("schema.json"));
    FFileHelper::LoadFileToString(Schema, *SchemaPath);

    TArray< TSharedPtr<FJsonValue> > JsonSchemas;

    TSharedRef< TJsonReader<> > Reader = TJsonReaderFactory<>::Create(Schema);
    if (FJsonSerializer::Deserialize(Reader, JsonSchemas))
    {
#if WITH_EDITOR
        try
        {
#endif
            const TArray<ULevelStreaming*>& streamingLevels = World.GetStreamingLevels();

            std::map<FString, ULevelStreaming*> LevelLookup;
            for (ULevelStreaming* streamingLevel : streamingLevels)
            {
                FString Scene = FPackageName::GetLongPackageAssetName(streamingLevel->GetWorldAssetPackageName());
                if (streamingLevel->GetWorld())
                    Scene.RemoveFromStart(streamingLevel->GetWorld()->StreamingLevelsPrefix);
                LevelLookup[Scene] = streamingLevel;
            }

            m_specs.resize(JsonSchemas.Num());
            for (size_t i = 0; i < m_specs.size(); ++i)
            {
                if (!JsonSchemas[i])
                    throw std::runtime_error("Null schema");
                const TSharedPtr<FJsonObject>& JsonSchema = JsonSchemas[i]->AsObject();
                if (!JsonSchema)
                    throw std::runtime_error("Non-object schema");
                const FString Scene = JsonSchema->GetStringField(TEXT("name"));
                ULevelStreaming* streamingLevel = LevelLookup[Scene];
                SchemaSpec& spec = m_specs[i];
                spec.streamingLevel = LevelLookup[Scene];
                ValidateSchema(JsonSchema, streamingLevel ? streamingLevel->GetLevelScriptActor() : nullptr, spec);
                UE_LOG(LogRenderStream, Log, TEXT("Loaded schema for %s (%s): %d parameters"), *Scene, streamingLevel ? *streamingLevel->GetWorldAssetPackageName() : TEXT("null"), spec.nParameters);
            }
#if WITH_EDITOR
        }
        catch (const std::exception& e)
        {
            UE_LOG(LogRenderStream, Error, TEXT("Failed to parse schema %s: %s"), *SchemaPath, *FString(e.what()));
            Schema = TEXT("[]");
        }
#endif
    }
    else
    {
        UE_LOG(LogRenderStream, Error, TEXT("Failed to parse schema %s"), *SchemaPath);
        Schema = TEXT("[]");
    }

    if (m_specs.empty())
    {
        m_specs.resize(1);
        SchemaSpec& spec = m_specs.front();
        spec.streamingLevel = nullptr;
        spec.nParameters = 0;
        StreamFNV fnv;
        spec.schemaHash = fnv.getHash();
    }

    if (RenderStreamLink::instance().rs_setSchema(m_assetHandle, TCHAR_TO_ANSI(*Schema)) != 0)
    {
        UE_LOG(LogRenderStream, Error, TEXT("Unable to set remote parameter schema"));
    }
}

void FRenderStreamModule::OnBeginFrame()
{
    if (!RenderStreamLink::instance().isAvailable())
        return;

    // If no captures are active, maintain asset sync, but do not delay processing.
    int timeout = 0;
    if (m_activeCaptures.Num() > 0)
        timeout = 500;

    RenderStreamLink::AssetHandle updateAsset = 0;
    uint32_t ret = RenderStreamLink::instance().rs_awaitFrameData(&updateAsset, timeout, &m_frameData);
    if (ret != 0 || m_assetHandle != updateAsset || m_frameData.scene >= m_specs.size())
    {
        if (m_frameDataValid)
            m_status.setInputStatus("Stopped receiving data from d3", RSSTATUS_ORANGE);
        m_frameDataValid = false; // TODO: Mark timecode as invalid only after some multiple of the expected incoming framerate.
        return;
    }

    if (!m_frameDataValid)
        m_status.setInputStatus("Receiving data from d3", RSSTATUS_GREEN);
    m_frameDataValid = true;

    // If no captures are active, skip schema validation (and error logging).
    if (m_activeCaptures.Num() == 0)
        return;

    const SchemaSpec& spec = m_specs.at(m_frameData.scene);

    URenderStreamMediaCapture* SchemaCallbackTarget = m_activeCaptures[0].Get();
    const UWorld* World = SchemaCallbackTarget ? SchemaCallbackTarget->SchemaWorld() : nullptr;
    if (World)
    {
        for (ULevelStreaming* streamingLevel : World->GetStreamingLevels())
        {
            if (spec.streamingLevel == streamingLevel)
            {
                if (!streamingLevel->IsLevelLoaded())
                {
                    UE_LOG(LogRenderStream, Log, TEXT("Loading level %s"), *streamingLevel->GetWorldAssetPackageFName().ToString());
                    FLatentActionInfo LatentInfo;
                    LatentInfo.CallbackTarget = SchemaCallbackTarget;
                    LatentInfo.ExecutionFunction = "UpdateSchema";
                    LatentInfo.UUID = int32(uintptr_t(this));
                    LatentInfo.Linkage = 0;
                    UGameplayStatics::LoadStreamLevel(World, streamingLevel->GetWorldAssetPackageFName(), true, true, LatentInfo);
                }
                else if (spec.schemaRoot == streamingLevel->GetLevelScriptActor())
                {
                    streamingLevel->SetShouldBeVisible(true);
                    std::vector<float> parameters;
                    parameters.resize(spec.nParameters);
                    if (RenderStreamLink::instance().rs_getFrameParameters(m_assetHandle, spec.schemaHash, parameters.data(), parameters.size() * sizeof(float)) == RenderStreamLink::RS_ERROR_SUCCESS)
                    {
                        ApplyParameters(streamingLevel->GetLevelScriptActor(), parameters);
                    }
                }
            }
            else if (spec.streamingLevel != nullptr) // Leave visibility alone if scenes not generated from levels
            {
                streamingLevel->SetShouldBeVisible(false);
            }
        }
    }

    for (const TWeakObjectPtr<URenderStreamMediaCapture>& ptr : m_activeCaptures)
    {
        if (URenderStreamMediaCapture* capture = ptr.Get())
        {
            RenderStreamLink::CameraData cameraData;
            if (RenderStreamLink::instance().rs_getFrameCamera(m_assetHandle, capture->streamHandle(), &cameraData) == RenderStreamLink::RS_ERROR_SUCCESS)
            {
                capture->ApplyCameraData(m_frameData, cameraData);
            }
        }
    }
}

void FRenderStreamModule::AddActiveCapture(URenderStreamMediaCapture* InCapture)
{
    m_activeCaptures.Add(InCapture);
}

void FRenderStreamModule::RemoveActiveCapture(URenderStreamMediaCapture* InCapture)
{
    m_activeCaptures.Remove(InCapture);
}


/*static*/ FRenderStreamModule* FRenderStreamModule::Get()
{
    return &FModuleManager::GetModuleChecked<FRenderStreamModule>("RenderStream");
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FRenderStreamModule, RenderStream)
