// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Core.h"
#include "Core/Public/Modules/ModuleInterface.h"
#include "SlateCore/Public/Styling/SlateColor.h"
#include "Json/Public/Dom/JsonObject.h"
#include "Engine/LevelStreaming.h"
#include <vector>

#include "RenderStreamLink.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRenderStream, Log, All);

class URenderStreamMediaCapture;
class AActor;
class StreamFNV;

#define RSSTATUS_RED FSlateColor({ 1.0, 0.0, 0.0 })
#define RSSTATUS_GREEN FSlateColor({ 0.0, 1.0, 0.0 })
#define RSSTATUS_ORANGE FSlateColor({ 1.0, 0.5, 0.0 })

struct RenderStreamStatus
{
    FString outputText;
    FSlateColor outputColor;
    FString inputText;
    FSlateColor inputColor;
    bool changed = true;

    void setInputAndOutputStatuses(const FString& topText, const FString& bottomText, const FSlateColor& color);  // Set single status spanning both lines
    void setOutputStatus(const FString& text, const FSlateColor& color);  // Set output (top) status
    void setInputStatus(const FString& text, const FSlateColor& color);  // Set input (bottom) status
};

class FRenderStreamModule : public IModuleInterface
{
public:
    /** IModuleInterface implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    virtual bool SupportsAutomaticShutdown () override;
    virtual bool SupportsDynamicReloading () override;

public:
    void AddActiveCapture(URenderStreamMediaCapture* InCapture);
    void RemoveActiveCapture(URenderStreamMediaCapture* InCapture);

    static FRenderStreamModule* Get();

    TArray<TWeakObjectPtr<URenderStreamMediaCapture>> m_activeCaptures;
    bool m_frameDataValid = false;
    RenderStreamLink::FrameData m_frameData;
    RenderStreamLink::AssetHandle m_assetHandle = 0; // Handle to the asset the instance of the RenderStream host we are connected as.

    RenderStreamStatus m_status;
    void LoadSchemas(const UWorld& World);

private:
    struct SchemaSpec
    {
        const ULevelStreaming* streamingLevel = nullptr;
        const AActor* schemaRoot = nullptr;
        const AActor* schemaPersistentRoot = nullptr;
        uint64_t schemaHash = 0;
        size_t nParameters = 0;
    };
    std::vector<SchemaSpec> m_specs;

    void ValidateSchema(const TSharedPtr<FJsonObject>& JsonSchema, const AActor* Root, const AActor* PersistentRoot, SchemaSpec& spec);
    size_t ValidateRoot(const AActor* Root, const TArray< TSharedPtr<FJsonValue> >& JsonParameters, SchemaSpec& spec, StreamFNV& fnv) const;
    size_t ApplyParameters(AActor* schemaRoot, const std::vector<float>& parameters, const size_t offset);

    FDelegateHandle OnBeginFrameHandle;
    void OnBeginFrame();
   
};
