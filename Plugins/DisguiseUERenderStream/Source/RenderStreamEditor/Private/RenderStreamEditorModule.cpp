// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "RenderStreamEditorModule.h"
#include "Modules/ModuleManager.h"

#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "PropertyEditorDelegates.h"
#include "PropertyEditorModule.h"
#include "UObject/UObjectBase.h"
#include "Json/Public/Serialization/JsonSerializer.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/World.h"

#include "ISettingsModule.h"
#include "RenderStreamSettings.h"

DEFINE_LOG_CATEGORY(LogRenderStreamEditor);

#define LOCTEXT_NAMESPACE "RenderStreamEditor"

void FRenderStreamEditorModule::StartupModule()
{
    // register settings
    ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

    if (SettingsModule != nullptr)
    {
        SettingsModule->RegisterSettings("Project", "Plugins", "DisguiseRenderStream",
            LOCTEXT("RuntimeSettingsName", "Disguise RenderStream"),
            LOCTEXT("RuntimeSettingsDescription", "Project settings for Disguise RenderStream plugin"),
            GetMutableDefault<URenderStreamSettings>()
        );
    }

    FWorldDelegates::LevelAddedToWorld.AddRaw(this, &FRenderStreamEditorModule::OnSchemasChanged);
    FWorldDelegates::LevelRemovedFromWorld.AddRaw(this, &FRenderStreamEditorModule::OnSchemasChanged);
    FWorldDelegates::OnPostWorldInitialization.AddRaw(this, &FRenderStreamEditorModule::OnSchemasChanged);
    if (GEditor)
    {
        GEditor->OnBlueprintCompiled().AddRaw(this, &FRenderStreamEditorModule::OnSchemasChanged);
    }
}

void FRenderStreamEditorModule::ShutdownModule()
{
    FWorldDelegates::LevelAddedToWorld.RemoveAll(this);
    FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);
    if (GEditor)
        GEditor->OnBlueprintCompiled().RemoveAll(this);

    // unregister settings
    ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

    if (SettingsModule != nullptr)
    {
        SettingsModule->UnregisterSettings("Project", "Plugins", "DisguiseRenderStream");
    }
}

TSharedPtr< FJsonObject > createField(FString group, FString displayName_, FString suffix, FString key_, FString undecoratedSuffix, float min, float max, float step, float defaultValue, TArray<FString> options = {})
{
    FString key = key_ + (undecoratedSuffix.IsEmpty() ? "" : "_" + undecoratedSuffix);
    FString displayName = displayName_ + (suffix.IsEmpty() ? "" : " " + suffix);

    if (options.Num() > 0)
    {
        min = 0;
        max = options.Num();
        step = 1;
    }

    TSharedPtr< FJsonObject > JsonParameter = MakeShareable(new FJsonObject);
    JsonParameter->SetStringField("group", group);
    JsonParameter->SetStringField("displayName", displayName);
    JsonParameter->SetStringField("key", key);
    JsonParameter->SetNumberField("min", min);
    JsonParameter->SetNumberField("max", max);
    JsonParameter->SetNumberField("step", step);
    JsonParameter->SetNumberField("defaultValue", defaultValue);
    TArray< TSharedPtr<FJsonValue> > JsonParameterOptions;
    for (const FString& option : options)
        JsonParameterOptions.Add(MakeShareable(new FJsonValueString(option)));
    JsonParameter->SetArrayField("options", JsonParameterOptions);
    JsonParameter->SetNumberField("dmxOffset", -1);
    JsonParameter->SetNumberField("dmxType", 2); // Dmx16LittleEndian

    return JsonParameter;
}

TArray<FString> EnumOptions(const FNumericProperty* NumericProperty)
{
    TArray<FString> Options;
    if (!NumericProperty->IsEnum())
        return Options;

    const UEnum* Enum = NumericProperty->GetIntPropertyEnum();
    if (!Enum)
        return Options;

    const int64 Max = Enum->GetMaxEnumValue();
    for (int64 i = 0; i < Max; ++i)
        Options.Push(Enum->GetDisplayNameTextByIndex(i).ToString());
    return Options;
}

TSharedPtr<FJsonObject> FRenderStreamEditorModule::GenerateSchema(FString Scene, const AActor* Root, const AActor* PersistentRoot)
{
    TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

    TArray< TSharedPtr<FJsonValue> > JsonParameters = GenerateJSONParameters(PersistentRoot);
    const int32 nPersistentParameters = JsonParameters.Num();

    JsonParameters.Append(GenerateJSONParameters(Root));
    const int32 nLevelParameters = JsonParameters.Num() - nPersistentParameters;

    JsonObject->SetStringField("name", Scene);
    JsonObject->SetArrayField("parameters", JsonParameters);
    JsonObject->SetNumberField("nPersistentParameters", nPersistentParameters);
    JsonObject->SetNumberField("nLevelParameters", nLevelParameters);

    return JsonObject;
}

TArray< TSharedPtr<FJsonValue> >FRenderStreamEditorModule::GenerateJSONParameters(const AActor* Root)
{
    TArray< TSharedPtr<FJsonValue> > JsonParameters;
    if (!Root)
        return JsonParameters;
    for (TFieldIterator<FProperty> PropIt(Root->GetClass(), EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
    {
        const FProperty* Property = *PropIt;
        const FString Name = Property->GetName();
        const FString Category = Property->GetMetaData("Category");
        if (!Property->HasAllPropertyFlags(CPF_Edit | CPF_BlueprintVisible) || Property->HasAllPropertyFlags(CPF_DisableEditOnInstance))
        {
            UE_LOG(LogRenderStreamEditor, Verbose, TEXT("Unexposed property: %s"), *Name);
        }
        else if (const FBoolProperty* BoolProperty = CastField<const FBoolProperty>(Property))
        {
            const bool v = BoolProperty->GetPropertyValue_InContainer(Root);
            UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed bool property: %s is %d"), *Name, v);
            JsonParameters.Add(MakeShareable(new FJsonValueObject(createField(Category, Name, "", Name, "", 0.f, 1.f, 1.f, v ? 1.f : 0.f, { "Off", "On" }))));
        }
        else if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(Property))
        {
            const uint8 v = ByteProperty->GetPropertyValue_InContainer(Root);
            TArray<FString> Options = EnumOptions(ByteProperty);
            UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed int property: %s is %d [%s]"), *Name, v, *FString::Join(Options, TEXT(",")));
            const bool HasLimits = Property->HasMetaData("ClampMin") && Property->HasMetaData("ClampMax");
            const float Min = HasLimits ? FCString::Atof(*Property->GetMetaData("ClampMin")) : 0;
            const float Max = HasLimits ? FCString::Atof(*Property->GetMetaData("ClampMax")) : 255;
            JsonParameters.Add(MakeShareable(new FJsonValueObject(createField(Category, Name, "", Name, "", Min, Max, 1.f, float(v), Options))));
        }
        else if (const FIntProperty* IntProperty = CastField<const FIntProperty>(Property))
        {
            const int32 v = IntProperty->GetPropertyValue_InContainer(Root);
            TArray<FString> Options = EnumOptions(IntProperty);
            UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed int property: %s is %d [%s]"), *Name, v, *FString::Join(Options, TEXT(",")));
            const bool HasLimits = Property->HasMetaData("ClampMin") && Property->HasMetaData("ClampMax");
            const float Min = HasLimits ? FCString::Atof(*Property->GetMetaData("ClampMin")) : -1000;
            const float Max = HasLimits ? FCString::Atof(*Property->GetMetaData("ClampMax")) : +1000;
            JsonParameters.Add(MakeShareable(new FJsonValueObject(createField(Category, Name, "", Name, "", Min, Max, 1.f, float(v), Options))));
        }
        else if (const FFloatProperty* FloatProperty = CastField<const FFloatProperty>(Property))
        {
            const float v = FloatProperty->GetPropertyValue_InContainer(Root);
            UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed float property: %s is %f"), *Name, v);
            const bool HasLimits = Property->HasMetaData("ClampMin") && Property->HasMetaData("ClampMax");
            const float Min = HasLimits ? FCString::Atof(*Property->GetMetaData("ClampMin")) : -1;
            const float Max = HasLimits ? FCString::Atof(*Property->GetMetaData("ClampMax")) : +1;
            JsonParameters.Add(MakeShareable(new FJsonValueObject(createField(Category, Name, "", Name, "", Min, Max, 0.001f, v))));
        }
        else if (const FStructProperty* StructProperty = CastField<const FStructProperty>(Property))
        {
            const void* StructAddress = StructProperty->ContainerPtrToValuePtr<void>(Root);
            if (StructProperty->Struct == TBaseStructure<FVector>::Get())
            {
                FVector v;
                StructProperty->CopyCompleteValue(&v, StructAddress);
                UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed vector property: %s is <%f, %f, %f>"), *Name, v.X, v.Y, v.Z);
                JsonParameters.Add(MakeShareable(new FJsonValueObject(createField(Category, Name, "x", Name, "x", -1.f, +1.f, 0.001f, v.X))));
                JsonParameters.Add(MakeShareable(new FJsonValueObject(createField(Category, Name, "y", Name, "y", -1.f, +1.f, 0.001f, v.Y))));
                JsonParameters.Add(MakeShareable(new FJsonValueObject(createField(Category, Name, "z", Name, "z", -1.f, +1.f, 0.001f, v.Z))));
            }
            else if (StructProperty->Struct == TBaseStructure<FColor>::Get())
            {
                FColor v;
                StructProperty->CopyCompleteValue(&v, StructAddress);
                UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed colour property: %s is <%d, %d, %d, %d>"), *Name, v.R, v.G, v.B, v.A);
                JsonParameters.Add(MakeShareable(new FJsonValueObject(createField(Category, Name, "r", Name, "r", 0.f, 1.f, 0.0001f, v.R / 255.f))));
                JsonParameters.Add(MakeShareable(new FJsonValueObject(createField(Category, Name, "g", Name, "g", 0.f, 1.f, 0.0001f, v.G / 255.f))));
                JsonParameters.Add(MakeShareable(new FJsonValueObject(createField(Category, Name, "b", Name, "b", 0.f, 1.f, 0.0001f, v.B / 255.f))));
                JsonParameters.Add(MakeShareable(new FJsonValueObject(createField(Category, Name, "a", Name, "a", 0.f, 1.f, 0.0001f, v.A / 255.f))));
            }
            else if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
            {
                FLinearColor v;
                StructProperty->CopyCompleteValue(&v, StructAddress);
                UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed linear colour property: %s is <%f, %f, %f, %f>"), *Name, v.R, v.G, v.B, v.A);
                JsonParameters.Add(MakeShareable(new FJsonValueObject(createField(Category, Name, "r", Name, "r", 0.f, 1.f, 0.0001f, v.R))));
                JsonParameters.Add(MakeShareable(new FJsonValueObject(createField(Category, Name, "g", Name, "g", 0.f, 1.f, 0.0001f, v.G))));
                JsonParameters.Add(MakeShareable(new FJsonValueObject(createField(Category, Name, "b", Name, "b", 0.f, 1.f, 0.0001f, v.B))));
                JsonParameters.Add(MakeShareable(new FJsonValueObject(createField(Category, Name, "a", Name, "a", 0.f, 1.f, 0.0001f, v.A))));
            }
            else
            {
                UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed struct property: %s"), *Name);
            }
        }
        else
        {
            UE_LOG(LogRenderStreamEditor, Log, TEXT("Unsupported exposed property: %s"), *Name);
        }
    }
    UE_LOG(LogRenderStreamEditor, Log, TEXT("Generated schema"));

    return JsonParameters;
}


void FRenderStreamEditorModule::GenerateSchemas(const UWorld& World)
{
    TArray< TSharedPtr<FJsonValue> > JsonSchemas;

    const AActor* persistentActor = World.PersistentLevel->GetLevelScriptActor();

    FString Scene = "Persistent Level";
    JsonSchemas.Add(MakeShareable(new FJsonValueObject(GenerateSchema(Scene, nullptr, persistentActor))));

    const URenderStreamSettings* settings = GetDefault<URenderStreamSettings>();
    if (settings ? settings->bGenerateScenesFromLevels : URenderStreamSettings::bGenerateScenesFromLevelsDefault)
    {
        const TArray<ULevelStreaming*>& streamingLevels = World.GetStreamingLevels();

        for (ULevelStreaming* streamingLevel : streamingLevels)
        {
            Scene = FPackageName::GetLongPackageAssetName(streamingLevel->GetWorldAssetPackageName());
            if (streamingLevel->GetWorld())
                Scene.RemoveFromStart(streamingLevel->GetWorld()->StreamingLevelsPrefix);
            JsonSchemas.Add(MakeShareable(new FJsonValueObject(GenerateSchema(Scene, streamingLevel->GetLevelScriptActor(), persistentActor))));
        }
    }

    FString Schema;
    TSharedRef< TJsonWriter<> > Writer = TJsonWriterFactory<>::Create(&Schema);
    FJsonSerializer::Serialize(JsonSchemas, Writer);

    FString SchemaPath = FPaths::Combine(*FPaths::ProjectContentDir(), *FString("DisguiseRenderStream"), *FString("schema.json"));
    FFileHelper::SaveStringToFile(Schema, *SchemaPath);
}

void FRenderStreamEditorModule::OnSchemasChanged()
{
    if (GameWorld.IsValid())
        GenerateSchemas(*GameWorld.Get());
}

void FRenderStreamEditorModule::OnSchemasChanged(ULevel*, UWorld* World)
{  
    GameWorld = TWeakObjectPtr<UWorld>(World);
    if (World)
        GenerateSchemas(*World);
}

void FRenderStreamEditorModule::OnSchemasChanged(UWorld* World)
{
    GameWorld = TWeakObjectPtr<UWorld>(World);
    if (World)
        GenerateSchemas(*World);
}

void FRenderStreamEditorModule::OnSchemasChanged(UWorld* World, const UWorld::InitializationValues /* IV */)
{
    GameWorld = TWeakObjectPtr<UWorld>(World);
    if (World)
        GenerateSchemas(*World);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRenderStreamEditorModule, RenderStreamEditor);
