#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Engine/EngineTypes.h"
#include "RenderStreamSettings.generated.h"

/**
* Implements the settings for the RenderStream plugin.
*/
UCLASS(Config = Engine, DefaultConfig)
class RENDERSTREAM_API URenderStreamSettings : public UObject
{
    GENERATED_UCLASS_BODY()

    UPROPERTY(EditAnywhere, config, Category = Settings)
    bool bGenerateScenesFromLevels;
    static const bool bGenerateScenesFromLevelsDefault = true;
};
