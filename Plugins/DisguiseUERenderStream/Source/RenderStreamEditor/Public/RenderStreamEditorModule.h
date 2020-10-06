#pragma once

#include "Core.h"
#include "Modules/ModuleInterface.h"
#include "Json/Public/Dom/JsonObject.h"

class ULevel;
class UWorld;

DECLARE_LOG_CATEGORY_EXTERN(LogRenderStreamEditor, Log, All);

class FRenderStreamEditorModule : public IModuleInterface
{
public:
    //~ IModuleInterface interface
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void OnSchemaChanged();
    void OnSchemasChanged(ULevel* Level, UWorld* World);

    void GenerateSchemas(const UWorld& World);
    TSharedPtr<FJsonObject> GenerateSchema(FString Scene, const AActor* Root);
};
