#include "DynamicInputSubsystem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "EnhancedInputSubsystems.h"

void UDynamicInputSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    LoadInputDefinitions();
}

void UDynamicInputSubsystem::LoadInputDefinitions()
{
    const FString FilePath = FPaths::ProjectContentDir() / TEXT("Data/Config_Inputs.json");
    FString JsonContent;
    if (FFileHelper::LoadFileToString(JsonContent, *FilePath))
    {
        TSharedPtr<FJsonObject> RootObj;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
        if (FJsonSerializer::Deserialize(Reader, RootObj) && RootObj.IsValid() && RootObj->HasField(TEXT("InputMasterRegistry")))
        {
            for (auto& Pair : RootObj->GetObjectField(TEXT("InputMasterRegistry"))->Values)
            {
                FDynamicInputActionDef Def;
                FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &Def, 0, 0);
                if (Def.ActionTag.IsValid())
                {
                    InputMasterRegistry.Add(Def.ActionTag, Def);
                }
            }
            BuildMappingContext();
        }
    }
}

UInputAction* UDynamicInputSubsystem::GetOrCreateChordAction(const FString& ChordKeyName)
{
    if (UInputAction** Found = ChordActionsCache.Find(ChordKeyName))
    {
        return *Found;
    }

    UInputAction* ChordAction = NewObject<UInputAction>(this, FName(*FString::Printf(TEXT("IA_Chord_%s"), *ChordKeyName)));
    ChordAction->ValueType = EInputActionValueType::Boolean;
    ChordActionsCache.Add(ChordKeyName, ChordAction);
    return ChordAction;
}

void UDynamicInputSubsystem::BuildMappingContext()
{
    GeneratedActions.Empty();
    ChordActionsCache.Empty();
    DynamicMappingContext = NewObject<UInputMappingContext>(this, TEXT("GeneratedDynamicIMC"));

    for (const auto& RegistryPair : InputMasterRegistry)
    {
        const FDynamicInputActionDef& Def = RegistryPair.Value;
        
        UInputAction* Action = NewObject<UInputAction>(this, FName(*Def.ActionTag.ToString()));
        Action->ValueType = Def.ValueType;
        GeneratedActions.Add(Def.ActionTag, Action);

        for (const FDynamicInputBindingDef& BindDef : Def.DefaultBindings)
        {
            FEnhancedActionKeyMapping& Mapping = DynamicMappingContext->MapKey(Action, FKey(*BindDef.KeyName));

            if (BindDef.bNegate)
            {
                Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(DynamicMappingContext));
            }

            if (BindDef.SwizzleOrder != EDynamicInputSwizzleOrder::None)
            {
                UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(DynamicMappingContext);
                Swizzle->Order = static_cast<EInputAxisSwizzle>(BindDef.SwizzleOrder);
                Mapping.Modifiers.Add(Swizzle);
            }

            if (!BindDef.ChordKeyName.IsEmpty())
            {
                UInputAction* ChordAct = GetOrCreateChordAction(BindDef.ChordKeyName);
                UInputTriggerChordAction* ChordTrigger = NewObject<UInputTriggerChordAction>(DynamicMappingContext);
                ChordTrigger->ChordAction = ChordAct;
                Mapping.Triggers.Add(ChordTrigger);

                DynamicMappingContext->MapKey(ChordAct, FKey(*BindDef.ChordKeyName));
            }
        }
    }
}

void UDynamicInputSubsystem::RebindKey(FGameplayTag ActionTag, const FString& NewKeyName, int32 BindingIndex)
{
    if (FDynamicInputActionDef* Def = InputMasterRegistry.Find(ActionTag))
    {
        if (Def->DefaultBindings.IsValidIndex(BindingIndex))
        {
            Def->DefaultBindings[BindingIndex].KeyName = NewKeyName;
            BuildMappingContext();

            if (ULocalPlayer* LP = GetLocalPlayer())
            {
                if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
                {
                    Subsystem->ClearAllMappings();
                    Subsystem->AddMappingContext(DynamicMappingContext, 0);
                }
            }
        }
    }
}