// DynamicDataSerializer.cpp
#include "DynamicDataSerializer.h"
#include "DynamicCharacter.h"
#include "DynamicPhysicsActor.h"
#include "DynamicCustomizationComponent.h"
#include "DynamicAttributeSet.h"
#include "DynamicBuildingSubsystem.h"
#include "DynamicEquipmentComponent.h"
#include "DynamicContainerComponent.h"
#include "DynamicInteractableComponent.h"
#include "DynamicLifeComponent.h"
#include "DynamicSocialComponent.h"
#include "DynamicLanguageComponent.h"
#include "DynamicQuestComponent.h"
#include "DynamicGameState.h"
#include "DynamicVoxelSubsystem.h"
#include "Engine/OverlapResult.h"
#include "Async/Async.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "Engine/Engine.h"

void UDynamicDataSerializer::DeserializeMapWrapper(const TSharedPtr<FJsonObject>& InObject, const FString& FieldName, FDynamicTagFloatMapWrapper& OutWrapper)
{
    OutWrapper.ReplicatedEntries.Empty();
    if (!InObject.IsValid() || !InObject->HasField(FieldName)) return;

    TSharedPtr<FJsonObject> MapSection = InObject->GetObjectField(FieldName);
    if (!MapSection.IsValid()) return;

    if (MapSection->HasField(TEXT("ReplicatedEntries")))
    {
        const TArray<TSharedPtr<FJsonValue>>& ArrayField = MapSection->GetArrayField(TEXT("ReplicatedEntries"));
        for (const auto& Element : ArrayField)
        {
            TSharedPtr<FJsonObject> EntryObj = Element->AsObject();
            if (EntryObj.IsValid())
            {
                FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*EntryObj->GetStringField(TEXT("KeyTag"))), false);
                if (KeyTag.IsValid())
                {
                    FTagFloatPair Entry;
                    Entry.KeyTag = KeyTag;
                    Entry.Value = static_cast<float>(EntryObj->GetNumberField(TEXT("Value")));
                    OutWrapper.ReplicatedEntries.Add(Entry);
                }
            }
        }
    }
    OutWrapper.SyncMapFromChangesArray();
}

TSharedPtr<FJsonObject> UDynamicDataSerializer::SerializeMapWrapper(const FDynamicTagFloatMapWrapper& InWrapper)
{
    TSharedRef<FJsonObject> WrapperObj = MakeShareable(new FJsonObject());
    TArray<TSharedPtr<FJsonValue>> ArrayJson;

    for (const FTagFloatPair& Pair : InWrapper.ReplicatedEntries)
    {
        TSharedRef<FJsonObject> EntryObj = MakeShareable(new FJsonObject());
        EntryObj->SetStringField(TEXT("KeyTag"), Pair.KeyTag.ToString());
        EntryObj->SetNumberField(TEXT("Value"), Pair.Value);
        ArrayJson.Add(MakeShareable(new FJsonValueObject(EntryObj)));
    }

    WrapperObj->SetArrayField(TEXT("ReplicatedEntries"), ArrayJson);
    return WrapperObj;
}

void UDynamicDataSerializer::DeserializeLocalizationMap(const TSharedPtr<FJsonObject>& InObject, const FString& FieldName, FDynamicLocalizationProfile& OutProfile)
{
    OutProfile.ReplicatedEntries.Empty();
    if (!InObject.IsValid() || !InObject->HasField(FieldName)) return;

    TSharedPtr<FJsonObject> MapSection = InObject->GetObjectField(FieldName);
    if (!MapSection.IsValid()) return;

    if (MapSection->HasField(TEXT("ReplicatedEntries")))
    {
        const TArray<TSharedPtr<FJsonValue>>& ArrayField = MapSection->GetArrayField(TEXT("ReplicatedEntries"));
        for (const auto& Element : ArrayField)
        {
            TSharedPtr<FJsonObject> EntryObj = Element->AsObject();
            if (EntryObj.IsValid())
            {
                FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*EntryObj->GetStringField(TEXT("KeyTag"))), false);
                if (KeyTag.IsValid())
                {
                    FTagStringPair Entry;
                    Entry.KeyTag = KeyTag;
                    Entry.DisplayValue = EntryObj->GetStringField(TEXT("Value"));
                    OutProfile.ReplicatedEntries.Add(Entry);
                }
            }
        }
    }
    OutProfile.SyncMapFromChangesArray();
}

TSharedPtr<FJsonObject> UDynamicDataSerializer::SerializeLocalizationMap(const FDynamicLocalizationProfile& InProfile)
{
    TSharedRef<FJsonObject> WrapperObj = MakeShareable(new FJsonObject());
    TArray<TSharedPtr<FJsonValue>> ArrayJson;

    for (const FTagStringPair& Pair : InProfile.ReplicatedEntries)
    {
        TSharedRef<FJsonObject> EntryObj = MakeShareable(new FJsonObject());
        EntryObj->SetStringField(TEXT("KeyTag"), Pair.KeyTag.ToString());
        EntryObj->SetStringField(TEXT("Value"), Pair.DisplayValue);
        ArrayJson.Add(MakeShareable(new FJsonValueObject(EntryObj)));
    }

    WrapperObj->SetArrayField(TEXT("ReplicatedEntries"), ArrayJson);
    return WrapperObj;
}

bool UDynamicDataSerializer::DeserializeMasterRegistry(const FString& InJsonString, ADynamicGameState* TargetGameState)
{
    if (!TargetGameState) return false;

    FMasterRegistryStagingData Staging;
    if (!DeserializeMasterRegistryToStaging(InJsonString, Staging))
    {
        return false;
    }

    TargetGameState->MergeStagingRegistry(MoveTemp(Staging));
    return true;
}

void UDynamicDataSerializer::DeserializeMasterRegistryAsync(
    const FString& InJsonString, 
    ADynamicGameState* TargetGameState, 
    FOnMasterRegistryDeserializedSignature OnComplete)
{
    DeserializeMasterRegistryAsyncNative(InJsonString, TargetGameState, [OnComplete](bool bSuccess)
    {
        OnComplete.ExecuteIfBound(bSuccess);
    });
}

void UDynamicDataSerializer::DeserializeMasterRegistryAsyncNative(
    const FString& InJsonString, 
    ADynamicGameState* TargetGameState, 
    TFunction<void(bool)> OnComplete)
{
    if (!TargetGameState)
    {
        if (OnComplete) OnComplete(false);
        return;
    }

    // Dispatch heavy JSON reflection & parsing to thread pool
    Async(EAsyncExecution::ThreadPool, [InJsonString, TargetGameState, OnComplete]()
    {
        FMasterRegistryStagingData Staging;
        const bool bSuccess = DeserializeMasterRegistryToStaging(InJsonString, Staging);

        // Marshal back to GameThread for pointer swaps
        Async(EAsyncExecution::TaskGraphMainThread, [TargetGameState, Staging = MoveTemp(Staging), bSuccess, OnComplete]() mutable
        {
            if (IsValid(TargetGameState) && bSuccess)
            {
                TargetGameState->MergeStagingRegistry(MoveTemp(Staging));
            }
            if (OnComplete)
            {
                OnComplete(bSuccess && IsValid(TargetGameState));
            }
        });
    });
}

bool UDynamicDataSerializer::DeserializeMasterRegistryToStaging(const FString& InJsonString, FMasterRegistryStagingData& OutStaging)
{
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJsonString);
    TSharedPtr<FJsonObject> RootObject;
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid()) return false;

    // Helper lambda for parsing rich dialogue utterance definitions
    auto ParseDialogueUtteranceArray = [](const TSharedPtr<FJsonObject>& ParentObj, const FString& FieldName, TArray<FDynamicDialogueUtteranceDef>& OutUtterances)
    {
        OutUtterances.Empty();
        if (!ParentObj.IsValid() || !ParentObj->HasField(FieldName)) return;
        for (const auto& Val : ParentObj->GetArrayField(FieldName))
        {
            TSharedPtr<FJsonObject> EntryObj = Val->AsObject();
            if (!EntryObj.IsValid()) continue;
            FDynamicDialogueUtteranceDef Def;
            Def.UtteranceText = EntryObj->GetStringField(TEXT("UtteranceText"));
            Def.SelectionWeight = EntryObj->HasField(TEXT("SelectionWeight")) ? static_cast<float>(EntryObj->GetNumberField(TEXT("SelectionWeight"))) : 1.0f;
            if (EntryObj->HasField(TEXT("GestureMontage")))
            {
                Def.GestureMontage = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(EntryObj->GetStringField(TEXT("GestureMontage"))));
            }
            if (EntryObj->HasField(TEXT("VoiceCueTag")))
            {
                Def.VoiceCueTag = FGameplayTag::RequestGameplayTag(FName(*EntryObj->GetStringField(TEXT("VoiceCueTag"))), false);
            }
            if (EntryObj->HasField(TEXT("EmotionTag")))
            {
                Def.EmotionTag = FGameplayTag::RequestGameplayTag(FName(*EntryObj->GetStringField(TEXT("EmotionTag"))), false);
            }
            if (EntryObj->HasField(TEXT("RequiredRelationshipTags")))
            {
                for (const auto& RelVal : EntryObj->GetArrayField(TEXT("RequiredRelationshipTags")))
                {
                    FGameplayTag RelTag = FGameplayTag::RequestGameplayTag(FName(*RelVal->AsString()), false);
                    if (RelTag.IsValid()) Def.RequiredRelationshipTags.AddTag(RelTag);
                }
            }
            if (EntryObj->HasField(TEXT("AllowedMoodTags")))
            {
                for (const auto& MoodVal : EntryObj->GetArrayField(TEXT("AllowedMoodTags")))
                {
                    FGameplayTag MoodTag = FGameplayTag::RequestGameplayTag(FName(*MoodVal->AsString()), false);
                    if (MoodTag.IsValid()) Def.AllowedMoodTags.AddTag(MoodTag);
                }
            }
            if (EntryObj->HasField(TEXT("TargetAllowedMoodTags")))
            {
                for (const auto& TMoodVal : EntryObj->GetArrayField(TEXT("TargetAllowedMoodTags")))
                {
                    FGameplayTag TMoodTag = FGameplayTag::RequestGameplayTag(FName(*TMoodVal->AsString()), false);
                    if (TMoodTag.IsValid()) Def.TargetAllowedMoodTags.AddTag(TMoodTag);
                }
            }
            if (EntryObj->HasField(TEXT("RelationalMetricRequirements")))
            {
                for (const auto& ReqVal : EntryObj->GetArrayField(TEXT("RelationalMetricRequirements")))
                {
                    TSharedPtr<FJsonObject> ReqObj = ReqVal->AsObject();
                    if (!ReqObj.IsValid()) continue;
                    FCharacterAttributeRangeDef Range;
                    Range.AttributeTag = FGameplayTag::RequestGameplayTag(FName(*ReqObj->GetStringField(TEXT("AttributeTag"))), false);
                    Range.MinValue = static_cast<float>(ReqObj->GetNumberField(TEXT("MinValue")));
                    Range.MaxValue = static_cast<float>(ReqObj->GetNumberField(TEXT("MaxValue")));
                    Def.RelationalMetricRequirements.Add(Range);
                }
            }
            if (EntryObj->HasField(TEXT("AttributeRequirements")))
            {
                for (const auto& ReqVal : EntryObj->GetArrayField(TEXT("AttributeRequirements")))
                {
                    TSharedPtr<FJsonObject> ReqObj = ReqVal->AsObject();
                    if (!ReqObj.IsValid()) continue;
                    FCharacterAttributeRangeDef Range;
                    Range.AttributeTag = FGameplayTag::RequestGameplayTag(FName(*ReqObj->GetStringField(TEXT("AttributeTag"))), false);
                    Range.MinValue = static_cast<float>(ReqObj->GetNumberField(TEXT("MinValue")));
                    Range.MaxValue = static_cast<float>(ReqObj->GetNumberField(TEXT("MaxValue")));
                    Def.AttributeRequirements.Add(Range);
                }
            }
            if (EntryObj->HasField(TEXT("TargetAttributeRequirements")))
            {
                for (const auto& ReqVal : EntryObj->GetArrayField(TEXT("TargetAttributeRequirements")))
                {
                    TSharedPtr<FJsonObject> ReqObj = ReqVal->AsObject();
                    if (!ReqObj.IsValid()) continue;
                    FCharacterAttributeRangeDef Range;
                    Range.AttributeTag = FGameplayTag::RequestGameplayTag(FName(*ReqObj->GetStringField(TEXT("AttributeTag"))), false);
                    Range.MinValue = static_cast<float>(ReqObj->GetNumberField(TEXT("MinValue")));
                    Range.MaxValue = static_cast<float>(ReqObj->GetNumberField(TEXT("MaxValue")));
                    Def.TargetAttributeRequirements.Add(Range);
                }
            }
            DeserializeMapWrapper(EntryObj, TEXT("ProbabilityAttributeScalars"), Def.ProbabilityAttributeScalars);
            DeserializeMapWrapper(EntryObj, TEXT("ProbabilityTargetAttributeScalars"), Def.ProbabilityTargetAttributeScalars);
            DeserializeMapWrapper(EntryObj, TEXT("ProbabilityTargetMoodScalars"), Def.ProbabilityTargetMoodScalars);
            DeserializeMapWrapper(EntryObj, TEXT("ProbabilityRelationalScalars"), Def.ProbabilityRelationalScalars);
            DeserializeMapWrapper(EntryObj, TEXT("ProbabilityMoodScalars"), Def.ProbabilityMoodScalars);
            OutUtterances.Add(Def);
        }
    };

    if (RootObject->HasField(TEXT("AttributeMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("AttributeMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> AttrObj = Pair.Value->AsObject();
            FDynamicAttributeDef AttrDef;
            FJsonObjectConverter::JsonObjectToUStruct(AttrObj.ToSharedRef(), &AttrDef, 0, 0);
            DeserializeLocalizationMap(AttrObj, TEXT("Localization"), AttrDef.Localization);
            OutStaging.AttributeMasterRegistry.Add(KeyTag, AttrDef);
        }
    }
    if (RootObject->HasField(TEXT("DeliveryArchetypes")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("DeliveryArchetypes"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> ArchObj = Pair.Value->AsObject();
            FDynamicDeliveryArchetype ArchDef;
            FJsonObjectConverter::JsonObjectToUStruct(ArchObj.ToSharedRef(), &ArchDef, 0, 0);
            DeserializeLocalizationMap(ArchObj, TEXT("Localization"), ArchDef.Localization);
            DeserializeMapWrapper(ArchObj, TEXT("Metrics"), ArchDef.Metrics);
            OutStaging.DeliveryArchetypes.Add(KeyTag, ArchDef);
        }
    }
    if (RootObject->HasField(TEXT("AbilityMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("AbilityMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> AbilityObj = Pair.Value->AsObject();
            FDynamicAbilityDef AbilityDef;
            FJsonObjectConverter::JsonObjectToUStruct(AbilityObj.ToSharedRef(), &AbilityDef, 0, 0);
            DeserializeLocalizationMap(AbilityObj, TEXT("Localization"), AbilityDef.Localization);
            DeserializeMapWrapper(AbilityObj, TEXT("NumericParams"), AbilityDef.NumericParams);
            
            if (AbilityObj->HasField(TEXT("SpatialDelivery")))
            {
                TSharedPtr<FJsonObject> SpatialObj = AbilityObj->GetObjectField(TEXT("SpatialDelivery"));
                DeserializeMapWrapper(SpatialObj, TEXT("RuntimeMetricModifiers"), AbilityDef.SpatialDelivery.RuntimeMetricModifiers);
                
                AbilityDef.SpatialDelivery.GravityScale = SpatialObj->HasField(TEXT("GravityScale")) ? static_cast<float>(SpatialObj->GetNumberField(TEXT("GravityScale"))) : 1.0f;
                AbilityDef.SpatialDelivery.Bounciness = SpatialObj->HasField(TEXT("Bounciness")) ? static_cast<float>(SpatialObj->GetNumberField(TEXT("Bounciness"))) : 0.3f;
                AbilityDef.SpatialDelivery.bDetonateOnBounce = SpatialObj->HasField(TEXT("bDetonateOnBounce")) ? SpatialObj->GetBoolField(TEXT("bDetonateOnBounce")) : true;

                if (SpatialObj->HasField(TEXT("ImpactTypeTags")))
                {
                    for (const auto& TagVal : SpatialObj->GetArrayField(TEXT("ImpactTypeTags")))
                    {
                        FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagVal->AsString()), false);
                        if (Tag.IsValid()) AbilityDef.SpatialDelivery.ImpactTypeTags.AddTag(Tag);
                    }
                }
            }
            if (AbilityObj->HasField(TEXT("InteractionConfig")))
            {
                TSharedPtr<FJsonObject> InteractionObj = AbilityObj->GetObjectField(TEXT("InteractionConfig"));                
                DeserializeMapWrapper(InteractionObj, TEXT("InteractionMetrics"), AbilityDef.InteractionConfig.InteractionMetrics);
                
                if (InteractionObj->HasField(TEXT("DefaultImpactTypeTag")))
                {
                    AbilityDef.InteractionConfig.DefaultImpactTypeTag = FGameplayTag::RequestGameplayTag(FName(*InteractionObj->GetStringField(TEXT("DefaultImpactTypeTag"))), false);
                }
                
                if (InteractionObj->HasField(TEXT("ImpactTypeTags")))
                {
                    for (const auto& TagVal : InteractionObj->GetArrayField(TEXT("ImpactTypeTags")))
                    {
                        FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagVal->AsString()), false);
                        if (Tag.IsValid()) AbilityDef.InteractionConfig.ImpactTypeTags.AddTag(Tag);
                    }
                }

                AbilityDef.InteractionConfig.PhysicsMode = InteractionObj->HasField(TEXT("PhysicsMode")) 
                    ? StringToEnum<EInteractionPhysicsMode>(InteractionObj->GetStringField(TEXT("PhysicsMode"))) 
                    : EInteractionPhysicsMode::None;

                AbilityDef.InteractionConfig.TetherMaxLength = InteractionObj->HasField(TEXT("TetherMaxLength")) 
                    ? static_cast<float>(InteractionObj->GetNumberField(TEXT("TetherMaxLength"))) 
                    : 500.0f;

                AbilityDef.InteractionConfig.TetherBreakForce = InteractionObj->HasField(TEXT("TetherBreakForce")) 
                    ? static_cast<float>(InteractionObj->GetNumberField(TEXT("TetherBreakForce"))) 
                    : 0.0f;
            }
            if (AbilityObj->HasField(TEXT("ChainConfig")))
            {
                TSharedPtr<FJsonObject> ChainObj = AbilityObj->GetObjectField(TEXT("ChainConfig"));
                if (ChainObj->HasField(TEXT("ImpactTypeTags")))
                {
                    for (const auto& TagVal : ChainObj->GetArrayField(TEXT("ImpactTypeTags")))
                    {
                        FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagVal->AsString()), false);
                        if (Tag.IsValid()) AbilityDef.ChainConfig.ImpactTypeTags.AddTag(Tag);
                    }
                }
            }
            if (AbilityObj->HasField(TEXT("MovementConfig")))
            {
                TSharedPtr<FJsonObject> MovObj = AbilityObj->GetObjectField(TEXT("MovementConfig"));
                DeserializeMapWrapper(MovObj, TEXT("MovementMetrics"), AbilityDef.MovementConfig.MovementMetrics);
                if (MovObj->HasField(TEXT("GaspMotorSettings")))
                    DeserializeMapWrapper(MovObj->GetObjectField(TEXT("GaspMotorSettings")), TEXT("RequiredAttributeThresholds"), AbilityDef.MovementConfig.GaspMotorSettings.RequiredAttributeThresholds);
            }
            if (AbilityObj->HasField(TEXT("StageInputSequences")))
            {
                TSharedPtr<FJsonObject> SeqMapObj = AbilityObj->GetObjectField(TEXT("StageInputSequences"));
                for (const auto& SeqPair : SeqMapObj->Values)
                {
                    EValidationPhase Phase = UDynamicDataSerializer::StringToEnum<EValidationPhase>(FString(SeqPair.Key));
                    TSharedPtr<FJsonObject> SeqObj = SeqPair.Value->AsObject();
                    if (!SeqObj.IsValid()) continue;

                    FAbilityStageInputSequence SeqDef;
                    SeqDef.BypassConditionTreeName = SeqObj->HasField(TEXT("BypassConditionTreeName")) ? FName(*SeqObj->GetStringField(TEXT("BypassConditionTreeName"))) : NAME_None;
                    SeqDef.SequenceTimeoutSeconds = SeqObj->HasField(TEXT("SequenceTimeoutSeconds")) ? static_cast<float>(SeqObj->GetNumberField(TEXT("SequenceTimeoutSeconds"))) : 2.0f;
                    SeqDef.bCancelAbilityOnFailure = SeqObj->HasField(TEXT("bCancelAbilityOnFailure")) ? SeqObj->GetBoolField(TEXT("bCancelAbilityOnFailure")) : true;
                    SeqDef.bProceedOnFailure = SeqObj->HasField(TEXT("bProceedOnFailure")) ? SeqObj->GetBoolField(TEXT("bProceedOnFailure")) : false;

                    if (SeqObj->HasField(TEXT("Steps")))
                    {
                        for (const auto& StepVal : SeqObj->GetArrayField(TEXT("Steps")))
                        {
                            TSharedPtr<FJsonObject> StepObj = StepVal->AsObject();
                            FAbilityInputStep Step;
                            Step.RequiredInputAction = FGameplayTag::RequestGameplayTag(FName(*StepObj->GetStringField(TEXT("RequiredInputAction"))), false);
                            Step.bIsHold = StepObj->HasField(TEXT("bIsHold")) ? StepObj->GetBoolField(TEXT("bIsHold")) : false;
                            Step.TimeWindowSeconds = StepObj->HasField(TEXT("TimeWindowSeconds")) ? static_cast<float>(StepObj->GetNumberField(TEXT("TimeWindowSeconds"))) : 1.5f;
                            SeqDef.Steps.Add(Step);
                        }
                    }
                    DeserializeMapWrapper(SeqObj, TEXT("ParameterAdjustmentsOnFailure"), SeqDef.ParameterAdjustmentsOnFailure);
                    
                    FStageInputSequencePair PairEntry;
                    PairEntry.Phase = Phase;
                    PairEntry.Sequence = SeqDef;
                    AbilityDef.StageInputSequences.Add(PairEntry);
                }
            }
            OutStaging.AbilityMasterRegistry.Add(KeyTag, AbilityDef);
        }
    }
    if (RootObject->HasField(TEXT("MasterPhysicsActorRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("MasterPhysicsActorRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> ActorObj = Pair.Value->AsObject();
            FDynamicPhysicsActorDef ActorDef;
            FJsonObjectConverter::JsonObjectToUStruct(ActorObj.ToSharedRef(), &ActorDef, 0, 0);
            DeserializeLocalizationMap(ActorObj, TEXT("Localization"), ActorDef.Localization);
            DeserializeMapWrapper(ActorObj, TEXT("DefaultGlobalAttributes"), ActorDef.DefaultGlobalAttributes);

            if (ActorObj->HasField(TEXT("ImpactTypeTags")))
            {
                for (const auto& TagVal : ActorObj->GetArrayField(TEXT("ImpactTypeTags")))
                {
                    FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagVal->AsString()), false);
                    if (Tag.IsValid()) ActorDef.ImpactTypeTags.AddTag(Tag);
                }
            }

            if (ActorObj->HasField(TEXT("CustomLimbs")))
            {
                const TArray<TSharedPtr<FJsonValue>>& LimbsArr = ActorObj->GetArrayField(TEXT("CustomLimbs"));
                for (int32 i = 0; i < LimbsArr.Num() && ActorDef.CustomLimbs.IsValidIndex(i); ++i)
                {
                    TSharedPtr<FJsonObject> LimbObj = LimbsArr[i]->AsObject();
                    DeserializeLocalizationMap(LimbObj, TEXT("Localization"), ActorDef.CustomLimbs[i].Localization);
                    DeserializeMapWrapper(LimbObj, TEXT("DefaultLimbAttributes"), ActorDef.CustomLimbs[i].DefaultLimbAttributes);
                    DeserializeMapWrapper(LimbObj, TEXT("AngularMotorAttributeScalars"), ActorDef.CustomLimbs[i].AngularMotorAttributeScalars);
                    DeserializeMapWrapper(LimbObj, TEXT("LinearMotorAttributeScalars"), ActorDef.CustomLimbs[i].LinearMotorAttributeScalars);
                }
            }
            if (ActorObj->HasField(TEXT("AcousticMaterialTag")))
            {
                ActorDef.AcousticMaterialTag = FGameplayTag::RequestGameplayTag(FName(*ActorObj->GetStringField(TEXT("AcousticMaterialTag"))), false);
            }
            ActorDef.bIsAcousticPortal = ActorObj->HasField(TEXT("bIsAcousticPortal")) ? ActorObj->GetBoolField(TEXT("bIsAcousticPortal")) : false;
            OutStaging.MasterPhysicsActorRegistry.Add(KeyTag, ActorDef);
            if (ActorDef.CraftingRecipes.Num() > 0)
            {
                FDynamicRecipeList List;
                List.Recipes = ActorDef.CraftingRecipes;
                OutStaging.RecipeMasterRegistry.Add(KeyTag, List);
            }
        }
    }
    if (RootObject->HasField(TEXT("SharedConditionTrees")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("SharedConditionTrees"))->Values)
        {
            FDynamicConditionTreeContainer TreeContainer;
            FlattenAndParseJsonNode(Pair.Value->AsObject(), TreeContainer);
            OutStaging.SharedConditionTrees.Add(FName(*Pair.Key), TreeContainer);
        }
    }
    if (RootObject->HasField(TEXT("MasterMilestoneRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("MasterMilestoneRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            FDynamicMilestoneContainer Container;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &Container, 0, 0);
            OutStaging.MasterMilestoneRegistry.Add(KeyTag, Container);
        }
    }
    if (RootObject->HasField(TEXT("MasterThresholdRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("MasterThresholdRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            FDynamicThresholdContainer Container;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &Container, 0, 0);
            OutStaging.MasterThresholdRegistry.Add(KeyTag, Container);
        }
    }
    if (RootObject->HasField(TEXT("EffectMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("EffectMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> EffectObj = Pair.Value->AsObject();
            FDynamicEffectDef EffectDef;
            FJsonObjectConverter::JsonObjectToUStruct(EffectObj.ToSharedRef(), &EffectDef, 0, 0);
            OutStaging.MasterEffectRegistry.Add(KeyTag, EffectDef);
        }
    }
    if (RootObject->HasField(TEXT("CalculationSettings")) && RootObject->HasField(TEXT("Limbs")))
    {
        FPhysicsDamageConfigData PhysicsConfig;
        if (FJsonObjectConverter::JsonObjectToUStruct(RootObject.ToSharedRef(), &PhysicsConfig, 0, 0))
        {
            const TArray<TSharedPtr<FJsonValue>>& LimbsJsonArr = RootObject->GetArrayField(TEXT("Limbs"));
            for (int32 i = 0; i < LimbsJsonArr.Num() && PhysicsConfig.Limbs.IsValidIndex(i); ++i)
            {
                TSharedPtr<FJsonObject> LimbObj = LimbsJsonArr[i]->AsObject();
                DeserializeLocalizationMap(LimbObj, TEXT("Localization"), PhysicsConfig.Limbs[i].Localization);
                DeserializeMapWrapper(LimbObj, TEXT("DefaultLimbAttributes"), PhysicsConfig.Limbs[i].DefaultLimbAttributes);
                DeserializeMapWrapper(LimbObj, TEXT("AngularMotorAttributeScalars"), PhysicsConfig.Limbs[i].AngularMotorAttributeScalars);
                DeserializeMapWrapper(LimbObj, TEXT("LinearMotorAttributeScalars"), PhysicsConfig.Limbs[i].LinearMotorAttributeScalars);
            }
            if (RootObject->HasField(TEXT("CalculationSettings")))
            {
                TSharedPtr<FJsonObject> CalcObj = RootObject->GetObjectField(TEXT("CalculationSettings"));
                PhysicsConfig.CalculationSettings.BaselineCharacterMassKg = CalcObj->HasField(TEXT("BaselineCharacterMassKg")) ? static_cast<float>(CalcObj->GetNumberField(TEXT("BaselineCharacterMassKg"))) : 80.0f;
                PhysicsConfig.CalculationSettings.CarriedMassProximityRadius = CalcObj->HasField(TEXT("CarriedMassProximityRadius")) ? static_cast<float>(CalcObj->GetNumberField(TEXT("CarriedMassProximityRadius"))) : 300.0f;
                PhysicsConfig.CalculationSettings.ObjectWeightAttributeTag = CalcObj->HasField(TEXT("ObjectWeightAttributeTag")) ? FGameplayTag::RequestGameplayTag(FName(*CalcObj->GetStringField(TEXT("ObjectWeightAttributeTag"))), false) : FGameplayTag();
                PhysicsConfig.CalculationSettings.BaseHoldSweepRadius = CalcObj->HasField(TEXT("BaseHoldSweepRadius")) ? static_cast<float>(CalcObj->GetNumberField(TEXT("BaseHoldSweepRadius"))) : 50.0f;
                PhysicsConfig.CalculationSettings.BaseHoldSweepDistance = CalcObj->HasField(TEXT("BaseHoldSweepDistance")) ? static_cast<float>(CalcObj->GetNumberField(TEXT("BaseHoldSweepDistance"))) : 60.0f;
                PhysicsConfig.CalculationSettings.VelocityLookaheadScalar = CalcObj->HasField(TEXT("VelocityLookaheadScalar")) ? static_cast<float>(CalcObj->GetNumberField(TEXT("VelocityLookaheadScalar"))) : 0.15f;
                DeserializeMapWrapper(CalcObj, TEXT("SweepRadiusAttributeScalars"), PhysicsConfig.CalculationSettings.SweepRadiusAttributeScalars);
                DeserializeMapWrapper(CalcObj, TEXT("SweepDistanceAttributeScalars"), PhysicsConfig.CalculationSettings.SweepDistanceAttributeScalars);
                if (CalcObj->HasField(TEXT("ImpactReactionRules")))
                {
                    TSharedPtr<FJsonObject> RulesMap = CalcObj->GetObjectField(TEXT("ImpactReactionRules"));
                    for (auto& Pair : RulesMap->Values)
                    {
                        FGameplayTag ImpactTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
                        if (ImpactTag.IsValid() && Pair.Value.IsValid() && Pair.Value->AsObject().IsValid())
                        {
                            FDynamicImpactReactionRule Rule;
                            DeserializeImpactReactionRule(Pair.Value->AsObject(), Rule);
                            PhysicsConfig.CalculationSettings.ImpactReactionRules.Add(ImpactTag, Rule);
                        }
                    }
                }
            }
            OutStaging.bHasPhysicsConfig = true;
            OutStaging.PhysicsConfig = PhysicsConfig;
        }
    }
    else if (RootObject->HasField(TEXT("CalculationSettings")))
    {
        TSharedPtr<FJsonObject> CalcObj = RootObject->GetObjectField(TEXT("CalculationSettings"));
        FPhysicsCalculationSettings CalcSettings;
        if (FJsonObjectConverter::JsonObjectToUStruct(CalcObj.ToSharedRef(), &CalcSettings, 0, 0))
        {
            CalcSettings.BaselineCharacterMassKg = CalcObj->HasField(TEXT("BaselineCharacterMassKg")) ? static_cast<float>(CalcObj->GetNumberField(TEXT("BaselineCharacterMassKg"))) : 80.0f;
            CalcSettings.CarriedMassProximityRadius = CalcObj->HasField(TEXT("CarriedMassProximityRadius")) ? static_cast<float>(CalcObj->GetNumberField(TEXT("CarriedMassProximityRadius"))) : 300.0f;
            CalcSettings.ObjectWeightAttributeTag = CalcObj->HasField(TEXT("ObjectWeightAttributeTag")) ? FGameplayTag::RequestGameplayTag(FName(*CalcObj->GetStringField(TEXT("ObjectWeightAttributeTag"))), false) : FGameplayTag();
            CalcSettings.BaseHoldSweepRadius = CalcObj->HasField(TEXT("BaseHoldSweepRadius")) ? static_cast<float>(CalcObj->GetNumberField(TEXT("BaseHoldSweepRadius"))) : 50.0f;
            CalcSettings.BaseHoldSweepDistance = CalcObj->HasField(TEXT("BaseHoldSweepDistance")) ? static_cast<float>(CalcObj->GetNumberField(TEXT("BaseHoldSweepDistance"))) : 60.0f;
            CalcSettings.VelocityLookaheadScalar = CalcObj->HasField(TEXT("VelocityLookaheadScalar")) ? static_cast<float>(CalcObj->GetNumberField(TEXT("VelocityLookaheadScalar"))) : 0.15f;
            DeserializeMapWrapper(CalcObj, TEXT("SweepRadiusAttributeScalars"), CalcSettings.SweepRadiusAttributeScalars);
            DeserializeMapWrapper(CalcObj, TEXT("SweepDistanceAttributeScalars"), CalcSettings.SweepDistanceAttributeScalars);
            if (CalcObj->HasField(TEXT("ImpactReactionRules")))
            {
                TSharedPtr<FJsonObject> RulesMap = CalcObj->GetObjectField(TEXT("ImpactReactionRules"));
                for (auto& Pair : RulesMap->Values)
                {
                    FGameplayTag ImpactTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
                    if (ImpactTag.IsValid() && Pair.Value.IsValid() && Pair.Value->AsObject().IsValid())
                    {
                        FDynamicImpactReactionRule Rule;
                        DeserializeImpactReactionRule(Pair.Value->AsObject(), Rule);
                        CalcSettings.ImpactReactionRules.Add(ImpactTag, Rule);
                    }
                }
            }
            OutStaging.bHasCalcSettings = true;
            OutStaging.CalcSettings = CalcSettings;
        }
    }
    if (RootObject->HasField(TEXT("AbilityScoreConfig")))
    {
        TSharedPtr<FJsonObject> ScoreObj = RootObject->GetObjectField(TEXT("AbilityScoreConfig"));
        FAbilityScoreConfigData ScoreConfig;
        if (FJsonObjectConverter::JsonObjectToUStruct(ScoreObj.ToSharedRef(), &ScoreConfig, 0, 0))
        {
            DeserializeMapWrapper(ScoreObj, TEXT("ExecutionTypeWeights"), ScoreConfig.ExecutionTypeWeights);
            OutStaging.bHasAbilityScoreConfig = true;
            OutStaging.AbilityScoreConfig = ScoreConfig;
        }
    }
    if (RootObject->HasField(TEXT("RelationshipTypeRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("RelationshipTypeRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> RelObj = Pair.Value->AsObject();
            if (!RelObj.IsValid()) continue;
            FDynamicRelationshipTypeDef RelDef;
            RelDef.RelationshipTag = KeyTag;
            DeserializeLocalizationMap(RelObj, TEXT("Localization"), RelDef.Localization);
            RelDef.bPruneWhenAllAtFloor = RelObj->HasField(TEXT("bPruneWhenAllAtFloor")) ? RelObj->GetBoolField(TEXT("bPruneWhenAllAtFloor")) : true;
            RelDef.PruneTolerance = RelObj->HasField(TEXT("PruneTolerance")) ? static_cast<float>(RelObj->GetNumberField(TEXT("PruneTolerance"))) : 0.05f;
            if (RelObj->HasField(TEXT("SupportedMetrics")))
            {
                for (const auto& MetricVal : RelObj->GetArrayField(TEXT("SupportedMetrics")))
                {
                    TSharedPtr<FJsonObject> MetricObj = MetricVal->AsObject();
                    if (!MetricObj.IsValid()) continue;
                    FRelationshipMetricRule Rule;
                    Rule.MetricTag = FGameplayTag::RequestGameplayTag(FName(*MetricObj->GetStringField(TEXT("MetricTag"))), false);
                    if (!Rule.MetricTag.IsValid()) continue;
                    DeserializeLocalizationMap(MetricObj, TEXT("Localization"), Rule.Localization);
                    Rule.DefaultValue = MetricObj->HasField(TEXT("DefaultValue")) ? static_cast<float>(MetricObj->GetNumberField(TEXT("DefaultValue"))) : 0.0f;
                    Rule.MinValue = MetricObj->HasField(TEXT("MinValue")) ? static_cast<float>(MetricObj->GetNumberField(TEXT("MinValue"))) : -100.0f;
                    Rule.MaxValue = MetricObj->HasField(TEXT("MaxValue")) ? static_cast<float>(MetricObj->GetNumberField(TEXT("MaxValue"))) : 100.0f;
                    Rule.DecayCurve = MetricObj->HasField(TEXT("DecayCurve")) ? StringToEnum<ERelationshipDecayCurve>(MetricObj->GetStringField(TEXT("DecayCurve"))) : ERelationshipDecayCurve::Exponential;
                    Rule.DecayRatePerDay = MetricObj->HasField(TEXT("DecayRatePerDay")) ? static_cast<float>(MetricObj->GetNumberField(TEXT("DecayRatePerDay"))) : 0.05f;
                    Rule.DecayTargetFloor = MetricObj->HasField(TEXT("DecayTargetFloor")) ? static_cast<float>(MetricObj->GetNumberField(TEXT("DecayTargetFloor"))) : 0.0f;
                    Rule.VolatilityScalar = MetricObj->HasField(TEXT("VolatilityScalar")) ? static_cast<float>(MetricObj->GetNumberField(TEXT("VolatilityScalar"))) : 1.0f;
                    Rule.bClampToMinMax = MetricObj->HasField(TEXT("bClampToMinMax")) ? MetricObj->GetBoolField(TEXT("bClampToMinMax")) : true;
                    RelDef.SupportedMetrics.Add(Rule);
                }
            }
            OutStaging.RelationshipTypeRegistry.Add(KeyTag, RelDef);
        }
    }
    if (RootObject->HasField(TEXT("ProfessionMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("ProfessionMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> ProfObj = Pair.Value->AsObject();
            FDynamicProfessionDef ProfDef;
            FJsonObjectConverter::JsonObjectToUStruct(ProfObj.ToSharedRef(), &ProfDef, 0, 0);
            DeserializeLocalizationMap(ProfObj, TEXT("Localization"), ProfDef.Localization);
            DeserializeMapWrapper(ProfObj, TEXT("PrioritizedItemWeights"), ProfDef.PrioritizedItemWeights);
            OutStaging.ProfessionMasterRegistry.Add(KeyTag, ProfDef);
        }
    }
    if (RootObject->HasField(TEXT("MasterAIAgentRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("MasterAIAgentRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> AgentObj = Pair.Value->AsObject();
            FDynamicAIAgentDef AgentDef;
            FJsonObjectConverter::JsonObjectToUStruct(AgentObj.ToSharedRef(), &AgentDef, 0, 0);
            if (AgentObj->HasField(TEXT("PerceptionConfig")))
            {
                TSharedPtr<FJsonObject> PercObj = AgentObj->GetObjectField(TEXT("PerceptionConfig"));
                AgentDef.PerceptionConfig.SightRadius = PercObj->HasField(TEXT("SightRadius")) ? static_cast<float>(PercObj->GetNumberField(TEXT("SightRadius"))) : 1500.0f;
                AgentDef.PerceptionConfig.LoseSightRadius = PercObj->HasField(TEXT("LoseSightRadius")) ? static_cast<float>(PercObj->GetNumberField(TEXT("LoseSightRadius"))) : 2000.0f;
                AgentDef.PerceptionConfig.PeripheralVisionAngleDegrees = PercObj->HasField(TEXT("PeripheralVisionAngleDegrees")) ? static_cast<float>(PercObj->GetNumberField(TEXT("PeripheralVisionAngleDegrees"))) : 90.0f;
                AgentDef.PerceptionConfig.HearingRange = PercObj->HasField(TEXT("HearingRange")) ? static_cast<float>(PercObj->GetNumberField(TEXT("HearingRange"))) : 3000.0f;
                AgentDef.PerceptionConfig.bEnableDamageSense = PercObj->HasField(TEXT("bEnableDamageSense")) ? PercObj->GetBoolField(TEXT("bEnableDamageSense")) : true;
            }
            if (AgentObj->HasField(TEXT("StateTreeAsset")))
            {
                AgentDef.StateTreeAsset = TSoftObjectPtr<UStateTree>(FSoftObjectPath(AgentObj->GetStringField(TEXT("StateTreeAsset"))));
            }
            OutStaging.MasterAIAgentRegistry.Add(KeyTag, AgentDef);
        }
    }
    if (RootObject->HasField(TEXT("CharacterTemplates")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("CharacterTemplates"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> TempObj = Pair.Value->AsObject();
            FCharacterTemplateDef TempDef;
            FJsonObjectConverter::JsonObjectToUStruct(TempObj.ToSharedRef(), &TempDef, 0, 0);
            DeserializeLocalizationMap(TempObj, TEXT("Localization"), TempDef.Localization);

            // Parse client-side Mutable Asset
            if (TempObj->HasField(TEXT("MutableObjectAsset")))
            {
                TempDef.MutableObjectAsset = TSoftObjectPtr<UCustomizableObject>(FSoftObjectPath(TempObj->GetStringField(TEXT("MutableObjectAsset"))));
            }

            if (TempObj->HasField(TEXT("PersonalityProfile")))
            {
                TSharedPtr<FJsonObject> PersObj = TempObj->GetObjectField(TEXT("PersonalityProfile"));
                DeserializeMapWrapper(PersObj, TEXT("FoodAndDrinkPreferences"), TempDef.PersonalityProfile.FoodAndDrinkPreferences);
                DeserializeMapWrapper(PersObj, TEXT("ActivityPreferences"), TempDef.PersonalityProfile.ActivityPreferences);

                if (PersObj->HasField(TEXT("EmotionMorphMap")))
                {
                    const TSharedPtr<FJsonObject> EmotionMapObj = PersObj->GetObjectField(TEXT("EmotionMorphMap"));
                    if (EmotionMapObj.IsValid())
                    {
                        for (const auto& EmotionPair : EmotionMapObj->Values)
                        {
                            const FString EmotionTagStr(*EmotionPair.Key);
                            const FGameplayTag EmoTag = FGameplayTag::RequestGameplayTag(FName(*EmotionPair.Key), false);
                            if (EmoTag.IsValid() && EmotionPair.Value.IsValid() && EmotionPair.Value->Type == EJson::Object)
                            {
                                FDynamicTagFloatMapWrapper Wrapper;
                                DeserializeMapWrapper(EmotionMapObj, EmotionTagStr, Wrapper);
                                TempDef.PersonalityProfile.EmotionMorphMap.Add(EmoTag, Wrapper);
                            }
                        }
                    }
                }

                auto DeserializeReactionRule = [](const TSharedPtr<FJsonObject>& InObj, const FString& FieldName, FDynamicDialogueReactionRule& OutRule)
                {
                    if (!InObj.IsValid() || !InObj->HasField(FieldName)) return;
                    TSharedPtr<FJsonObject> RuleObj = InObj->GetObjectField(FieldName);
                    if (!RuleObj.IsValid()) return;

                    DeserializeMapWrapper(RuleObj, TEXT("SelfAttributeDeltas"), OutRule.SelfAttributeDeltas);
                    DeserializeMapWrapper(RuleObj, TEXT("RelationalMetricDeltas"), OutRule.RelationalMetricDeltas);

                    if (RuleObj->HasField(TEXT("ReactionDialogueCategoryTag")))
                    {
                        OutRule.ReactionDialogueCategoryTag = FGameplayTag::RequestGameplayTag(FName(*RuleObj->GetStringField(TEXT("ReactionDialogueCategoryTag"))), false);
                    }
                    if (RuleObj->HasField(TEXT("ReactionBehaviorTag")))
                    {
                        OutRule.ReactionBehaviorTag = FGameplayTag::RequestGameplayTag(FName(*RuleObj->GetStringField(TEXT("ReactionBehaviorTag"))), false);
                    }
                };

                DeserializeReactionRule(PersObj, TEXT("OnIgnoredReaction"), TempDef.PersonalityProfile.OnIgnoredReaction);
                DeserializeReactionRule(PersObj, TEXT("OnInterruptedReaction"), TempDef.PersonalityProfile.OnInterruptedReaction);
            }

            if (TempObj->HasField(TEXT("AgingProfile")))
            {
                FJsonObjectConverter::JsonObjectToUStruct(TempObj->GetObjectField(TEXT("AgingProfile")).ToSharedRef(), &TempDef.AgingProfile, 0, 0);
            }
            if (TempObj->HasField(TEXT("AlleleDominanceOverrides")))
            {
                DeserializeMapWrapper(TempObj->GetObjectField(TEXT("AlleleDominanceOverrides")), TEXT("ReplicatedEntries"), TempDef.AlleleDominanceOverrides);
            }
            if (TempObj->HasField(TEXT("DefaultCameraProfileTag")))
            {
                TempDef.DefaultCameraProfileTag = FGameplayTag::RequestGameplayTag(FName(*TempObj->GetStringField(TEXT("DefaultCameraProfileTag"))), false);
            }
            if (TempObj->HasField(TEXT("StanceStandConfig")))
            {
                TSharedPtr<FJsonObject> StanceObj = TempObj->GetObjectField(TEXT("StanceStandConfig"));
                TempDef.StanceStandConfig.BaseHeight = StanceObj->HasField(TEXT("BaseHeight")) ? static_cast<float>(StanceObj->GetNumberField(TEXT("BaseHeight"))) : 88.0f;
                DeserializeMapWrapper(StanceObj, TEXT("AttributeModifiers"), TempDef.StanceStandConfig.AttributeModifiers);
            }
            if (TempObj->HasField(TEXT("StanceCrouchConfig")))
            {
                TSharedPtr<FJsonObject> StanceObj = TempObj->GetObjectField(TEXT("StanceCrouchConfig"));
                TempDef.StanceCrouchConfig.BaseHeight = StanceObj->HasField(TEXT("BaseHeight")) ? static_cast<float>(StanceObj->GetNumberField(TEXT("BaseHeight"))) : 44.0f;
                DeserializeMapWrapper(StanceObj, TEXT("AttributeModifiers"), TempDef.StanceCrouchConfig.AttributeModifiers);
            }
            if (TempObj->HasField(TEXT("StanceProneConfig")))
            {
                TSharedPtr<FJsonObject> StanceObj = TempObj->GetObjectField(TEXT("StanceProneConfig"));
                TempDef.StanceProneConfig.BaseHeight = StanceObj->HasField(TEXT("BaseHeight")) ? static_cast<float>(StanceObj->GetNumberField(TEXT("BaseHeight"))) : 22.0f;
                DeserializeMapWrapper(StanceObj, TEXT("AttributeModifiers"), TempDef.StanceProneConfig.AttributeModifiers);
            }
            if (TempObj->HasField(TEXT("AcousticMaterialTag")))
            {
                TempDef.AcousticMaterialTag = FGameplayTag::RequestGameplayTag(FName(*TempObj->GetStringField(TEXT("AcousticMaterialTag"))), false);
            }
            
            OutStaging.CharacterTemplatesRegistry.Add(KeyTag, TempDef);
        }
    }
    if (RootObject->HasField(TEXT("SkeletonRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("SkeletonRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> SkelObj = Pair.Value->AsObject();
            FDynamicSkeletonDef SkelDef;
            FJsonObjectConverter::JsonObjectToUStruct(SkelObj.ToSharedRef(), &SkelDef, 0, 0);
            DeserializeLocalizationMap(SkelObj, TEXT("Localization"), SkelDef.Localization);
            
            // PARSE RAGDOLL JOINT LIMITS
            if (SkelObj->HasField(TEXT("JointLimits")))
            {
                for (const auto& JointVal : SkelObj->GetArrayField(TEXT("JointLimits")))
                {
                    TSharedPtr<FJsonObject> JointObj = JointVal->AsObject();
                    if (!JointObj.IsValid()) continue;
                    
                    FDynamicJointLimitProfile LimitProf;
                    LimitProf.BoneName = FName(*JointObj->GetStringField(TEXT("BoneName")));
                    LimitProf.BaseSwing1Limit = static_cast<float>(JointObj->GetNumberField(TEXT("BaseSwing1Limit")));
                    LimitProf.BaseSwing2Limit = static_cast<float>(JointObj->GetNumberField(TEXT("BaseSwing2Limit")));
                    LimitProf.BaseTwistLimit = static_cast<float>(JointObj->GetNumberField(TEXT("BaseTwistLimit")));
                    LimitProf.HyperextensionThresholdAngle = static_cast<float>(JointObj->GetNumberField(TEXT("HyperextensionThresholdAngle")));
                    
                    DeserializeMapWrapper(JointObj, TEXT("RangeOfMotionAttributeScalars"), LimitProf.RangeOfMotionAttributeScalars);
                    DeserializeMapWrapper(JointObj, TEXT("ResistanceAttributeScalars"), LimitProf.ResistanceAttributeScalars);
                    
                    if (JointObj->HasField(TEXT("HyperextensionEffects")))
                    {
                        for (const auto& EffVal : JointObj->GetArrayField(TEXT("HyperextensionEffects")))
                        {
                            FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*EffVal->AsString()), false);
                            if (Tag.IsValid()) LimitProf.HyperextensionEffects.Add(Tag);
                        }
                    }
                    SkelDef.JointLimits.Add(LimitProf);
                }
            }
            
            OutStaging.SkeletonMasterRegistry.Add(KeyTag, SkelDef);
        }
    }
    if (RootObject->HasField(TEXT("ConversionRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("ConversionRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> ConvObj = Pair.Value->AsObject();
            FDynamicTransformationRule RuleDef;
            FJsonObjectConverter::JsonObjectToUStruct(ConvObj.ToSharedRef(), &RuleDef, 0, 0);
            DeserializeLocalizationMap(ConvObj, TEXT("Localization"), RuleDef.Localization);
            OutStaging.ConversionRegistry.Add(KeyTag, RuleDef);
        }
    }
    if (RootObject->HasField(TEXT("EvolutionRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("EvolutionRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> EvoObj = Pair.Value->AsObject();
            FDynamicEvolutionContainer Container;
            FJsonObjectConverter::JsonObjectToUStruct(EvoObj.ToSharedRef(), &Container, 0, 0);
            OutStaging.EvolutionMasterRegistry.Add(KeyTag, Container);
        }
    }
    if (RootObject->HasField(TEXT("CalendarConfig")))
    {
        FDynamicCalendarDef CalDef;
        if (FJsonObjectConverter::JsonObjectToUStruct(RootObject->GetObjectField(TEXT("CalendarConfig")).ToSharedRef(), &CalDef, 0, 0))
        {
            OutStaging.bHasCalendarConfig = true;
            OutStaging.CalendarConfig = CalDef;
        }
    }
    if (RootObject->HasField(TEXT("GeneticsMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("GeneticsMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            FDynamicGeneLocusDef Locus;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &Locus, 0, 0);
            OutStaging.GeneticsMasterRegistry.Add(KeyTag, Locus);
        }
    }
    if (RootObject->HasField(TEXT("BiomeMasterRegistry")))
    {
        for (const auto& Pair : RootObject->GetObjectField(TEXT("BiomeMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            FDynamicBiomeDef BiomeDef;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &BiomeDef, 0, 0);
            OutStaging.BiomeMasterRegistry.Add(KeyTag, BiomeDef);
        }
    }
    if (RootObject->HasField(TEXT("WeatherMasterRegistry")))
    {
        for (const auto& Pair : RootObject->GetObjectField(TEXT("WeatherMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            FDynamicWeatherPatternDef WeatherDef;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &WeatherDef, 0, 0);
            OutStaging.WeatherMasterRegistry.Add(KeyTag, WeatherDef);
        }
    }
    if (RootObject->HasField(TEXT("EvolutionProfilesRegistry")))
    {
        for (const auto& Pair : RootObject->GetObjectField(TEXT("EvolutionProfilesRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            FEvolutionProfileDef ProfileDef;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &ProfileDef, 0, 0);
            OutStaging.EvolutionProfilesRegistry.Add(KeyTag, ProfileDef);
        }
    }
    if (RootObject->HasField(TEXT("QuestMasterRegistry")))
    {
        for (const auto& Pair : RootObject->GetObjectField(TEXT("QuestMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> QuestObj = Pair.Value->AsObject();
            FQuestDef Quest;
            FJsonObjectConverter::JsonObjectToUStruct(QuestObj.ToSharedRef(), &Quest, 0, 0);
            DeserializeLocalizationMap(QuestObj, TEXT("Localization"), Quest.Localization);
            if (QuestObj->HasField(TEXT("Steps")))
            {
                const TArray<TSharedPtr<FJsonValue>>& StepsArr = QuestObj->GetArrayField(TEXT("Steps"));
                for (int32 i = 0; i < StepsArr.Num() && Quest.Steps.IsValidIndex(i); ++i)
                {
                    TSharedPtr<FJsonObject> StepObj = StepsArr[i]->AsObject();
                    DeserializeLocalizationMap(StepObj, TEXT("Localization"), Quest.Steps[i].Localization);
                }
            }
            OutStaging.QuestMasterRegistry.Add(KeyTag, Quest);
        }
    }
    if (RootObject->HasField(TEXT("QuestGeneratorRules")))
    {
        for (const auto& Pair : RootObject->GetObjectField(TEXT("QuestGeneratorRules"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            FDynamicQuestGeneratorRule Rule;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &Rule, 0, 0);
            OutStaging.QuestGeneratorRules.Add(KeyTag, Rule);
        }
    }
    if (RootObject->HasField(TEXT("EnvironmentConfig")))
    {
        TSharedPtr<FJsonObject> EnvConfigObj = RootObject->GetObjectField(TEXT("EnvironmentConfig"));
        FDynamicEnvironmentSystemConfig Config;
        if (EnvConfigObj->HasField(TEXT("TrackedElementalTags")))
        {
            for (const auto& TagVal : EnvConfigObj->GetArrayField(TEXT("TrackedElementalTags")))
            {
                FGameplayTag ElemTag = FGameplayTag::RequestGameplayTag(FName(*TagVal->AsString()), false);
                if (ElemTag.IsValid())
                {
                    Config.TrackedElementalTags.Add(ElemTag);
                }
            }
        }
        Config.MaxElementDensityCap = EnvConfigObj->HasField(TEXT("MaxElementDensityCap"))
            ? static_cast<float>(EnvConfigObj->GetNumberField(TEXT("MaxElementDensityCap")))
            : 200.0f;
        Config.DefaultCellDensity = EnvConfigObj->HasField(TEXT("DefaultCellDensity"))
            ? static_cast<float>(EnvConfigObj->GetNumberField(TEXT("DefaultCellDensity")))
            : 50.0f;
        Config.bWrapGridBoundaries = EnvConfigObj->HasField(TEXT("bWrapGridBoundaries"))
            ? EnvConfigObj->GetBoolField(TEXT("bWrapGridBoundaries"))
            : false;
        Config.BorderDissipationRate = EnvConfigObj->HasField(TEXT("BorderDissipationRate"))
            ? static_cast<float>(EnvConfigObj->GetNumberField(TEXT("BorderDissipationRate")))
            : 0.15f;
        if (EnvConfigObj->HasField(TEXT("MetricFormulas")))
        {
            for (const auto& FormVal : EnvConfigObj->GetArrayField(TEXT("MetricFormulas")))
            {
                TSharedPtr<FJsonObject> FormObj = FormVal->AsObject();
                FDynamicEnvironmentPhysicalMetricFormula Formula;
                Formula.MetricTag = FGameplayTag::RequestGameplayTag(FName(*FormObj->GetStringField(TEXT("MetricTag"))), false);
                Formula.BaseConstant = static_cast<float>(FormObj->GetNumberField(TEXT("BaseConstant")));
                Formula.MinClamp = static_cast<float>(FormObj->GetNumberField(TEXT("MinClamp")));
                Formula.MaxClamp = static_cast<float>(FormObj->GetNumberField(TEXT("MaxClamp")));
                DeserializeMapWrapper(FormObj, TEXT("ElementWeights"), Formula.ElementWeights);
                Config.MetricFormulas.Add(Formula);
            }
        }
        OutStaging.bHasEnvironmentConfig = true;
        OutStaging.EnvironmentConfig = Config;
    }
    if (RootObject->HasField(TEXT("Languages")))
    {
        for (const auto& LangVal : RootObject->GetArrayField(TEXT("Languages")))
        {
            TSharedPtr<FJsonObject> LangObj = LangVal->AsObject();
            FGameplayTag LangTag = FGameplayTag::RequestGameplayTag(FName(*LangObj->GetStringField(TEXT("LanguageTag"))), false);
            if (!LangTag.IsValid()) continue;

            FDynamicLanguageDef LangDef;
            LangDef.LanguageTag = LangTag;
            LangDef.ComprehensionAttributeTag = FGameplayTag::RequestGameplayTag(FName(*LangObj->GetStringField(TEXT("ComprehensionAttributeTag"))), false);
            LangDef.bIsLinguaFranca = LangObj->HasField(TEXT("bIsLinguaFranca")) ? LangObj->GetBoolField(TEXT("bIsLinguaFranca")) : false;
            LangDef.FluencyMasteryThreshold = LangObj->HasField(TEXT("FluencyMasteryThreshold")) ? static_cast<float>(LangObj->GetNumberField(TEXT("FluencyMasteryThreshold"))) : 85.0f;
            LangDef.ConversationalBreakthroughThreshold = LangObj->HasField(TEXT("ConversationalBreakthroughThreshold")) ? static_cast<float>(LangObj->GetNumberField(TEXT("ConversationalBreakthroughThreshold"))) : 50.0f;

            LangDef.TeachingAttributeTag = LangObj->HasField(TEXT("TeachingAttributeTag")) 
                ? FGameplayTag::RequestGameplayTag(FName(*LangObj->GetStringField(TEXT("TeachingAttributeTag"))), false) 
                : FGameplayTag();
            LangDef.TeachingAttributeBonusScalar = LangObj->HasField(TEXT("TeachingAttributeBonusScalar"))
                ? static_cast<float>(LangObj->GetNumberField(TEXT("TeachingAttributeBonusScalar")))
                : 0.05f;
            LangDef.MinTeacherAttributeRequired = LangObj->HasField(TEXT("MinTeacherAttributeRequired"))
                ? static_cast<float>(LangObj->GetNumberField(TEXT("MinTeacherAttributeRequired")))
                : 0.0f;

            DeserializeLocalizationMap(LangObj, TEXT("Localization"), LangDef.Localization);

            if (LangObj->HasField(TEXT("StopWords")))
            {
                for (const auto& SwVal : LangObj->GetArrayField(TEXT("StopWords")))
                {
                    if (SwVal.IsValid())
                    {
                        LangDef.StopWords.Add(SwVal->AsString());
                    }
                }
            }

            if (LangObj->HasField(TEXT("PidginSubstitutions")))
            {
                const TSharedPtr<FJsonObject> SubObj = LangObj->GetObjectField(TEXT("PidginSubstitutions"));
                if (SubObj.IsValid())
                {
                    for (const auto& SubPair : SubObj->Values)
                    {
                        if (SubPair.Value.IsValid())
                        {
                            const FString KeyStr = *SubPair.Key;
                            const FString ValStr = SubPair.Value->AsString();
                            LangDef.PidginSubstitutions.FindOrAdd(KeyStr) = ValStr;
                        }
                    }
                }
            }

            if (LangObj->HasField(TEXT("PhoneticPhonemes")))
            {
                for (const auto& PhVal : LangObj->GetArrayField(TEXT("PhoneticPhonemes")))
                {
                    LangDef.PhoneticPhonemes.Add(PhVal->AsString());
                }
            }

            if (LangObj->HasField(TEXT("Vocabulary")))
            {
                for (const auto& VocVal : LangObj->GetArrayField(TEXT("Vocabulary")))
                {
                    TSharedPtr<FJsonObject> VocObj = VocVal->AsObject();
                    FDynamicVocabularyWordDef WordDef;
                    WordDef.NativeWord = VocObj->GetStringField(TEXT("NativeWord"));
                    WordDef.DifficultyRating = static_cast<float>(VocObj->GetNumberField(TEXT("DifficultyRating")));
                    WordDef.ComprehensionXPGranted = static_cast<float>(VocObj->GetNumberField(TEXT("ComprehensionXPGranted")));
                    DeserializeLocalizationMap(VocObj, TEXT("LocalizedTranslations"), WordDef.LocalizedTranslations);

                    if (VocObj->HasField(TEXT("MiniGameDecoyOptions")))
                    {
                        for (const auto& Decoy : VocObj->GetArrayField(TEXT("MiniGameDecoyOptions")))
                        {
                            WordDef.MiniGameDecoyOptions.Add(Decoy->AsString());
                        }
                    }
                    LangDef.Vocabulary.Add(WordDef);
                }
            }

            OutStaging.LanguageMasterRegistry.Add(LangTag, LangDef);
        }
    }
    if (RootObject->HasField(TEXT("NameGenerationRules")))
    {
        const TArray<TSharedPtr<FJsonValue>>& RulesArr = RootObject->GetArrayField(TEXT("NameGenerationRules"));
        for (const auto& Val : RulesArr)
        {
            TSharedPtr<FJsonObject> RuleObj = Val->AsObject();
            if (!RuleObj.IsValid()) continue;
            FNameGenerationRuleConfig RuleConfig;
            RuleConfig.CultureOrTemplateTag = FGameplayTag::RequestGameplayTag(FName(*RuleObj->GetStringField(TEXT("CultureOrTemplateTag"))), false);
            if (!RuleConfig.CultureOrTemplateTag.IsValid()) continue;
            RuleConfig.GenerationMode = StringToEnum<ENameGenerationMode>(RuleObj->GetStringField(TEXT("GenerationMode")));
            DeserializeMapWrapper(RuleObj, TEXT("CulturalAdoptionWeights"), RuleConfig.CulturalAdoptionWeights);
            auto ParseLexiconArray = [](const TSharedPtr<FJsonObject>& ParentObj, const FString& ArrayFieldName, TArray<FNameLexiconEntry>& OutPool)
            {
                if (ParentObj->HasField(ArrayFieldName))
                {
                    for (const auto& EntryVal : ParentObj->GetArrayField(ArrayFieldName))
                    {
                        TSharedPtr<FJsonObject> EntryObj = EntryVal->AsObject();
                        if (!EntryObj.IsValid()) continue;
                        FNameLexiconEntry LexEntry;
                        LexEntry.Token = EntryObj->GetStringField(TEXT("Token"));
                        LexEntry.GivenNameWeight = EntryObj->HasField(TEXT("GivenNameWeight")) ? static_cast<float>(EntryObj->GetNumberField(TEXT("GivenNameWeight"))) : 1.0f;
                        LexEntry.SurnameWeight = EntryObj->HasField(TEXT("SurnameWeight")) ? static_cast<float>(EntryObj->GetNumberField(TEXT("SurnameWeight"))) : 0.0f;
                        LexEntry.GenderRestriction = EntryObj->HasField(TEXT("GenderRestriction")) ? StringToEnum<ECharacterGender>(EntryObj->GetStringField(TEXT("GenderRestriction"))) : ECharacterGender::Neutral;
                        DeserializeMapWrapper(EntryObj, TEXT("RacialPrevalenceWeights"), LexEntry.RacialPrevalenceWeights);
                        OutPool.Add(LexEntry);
                    }
                }
            };
            ParseLexiconArray(RuleObj, TEXT("StandaloneNames"), RuleConfig.StandaloneNames);
            ParseLexiconArray(RuleObj, TEXT("PrefixPool"), RuleConfig.PrefixPool);
            ParseLexiconArray(RuleObj, TEXT("SuffixPool"), RuleConfig.SuffixPool);
            OutStaging.NameGenerationRulesRegistry.Add(RuleConfig.CultureOrTemplateTag, RuleConfig);
        }
    }
    if (RootObject->HasField(TEXT("FastPathDialogueRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("FastPathDialogueRegistry"))->Values)
        {
            FGameplayTag ProfileTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!ProfileTag.IsValid()) continue;
            TSharedPtr<FJsonObject> DefObj = Pair.Value->AsObject();
            if (!DefObj.IsValid()) continue;
            FFastPathDialogueDef DialogueDef;
            DialogueDef.DialogueProfileTag = ProfileTag;
            if (DefObj->HasField(TEXT("Entries")))
            {
                for (const auto& EntryVal : DefObj->GetArrayField(TEXT("Entries")))
                {
                    TSharedPtr<FJsonObject> EntryObj = EntryVal->AsObject();
                    if (!EntryObj.IsValid()) continue;
                    FFastPathDialogueEntry Entry;
                    Entry.CategoryTag = FGameplayTag::RequestGameplayTag(FName(*EntryObj->GetStringField(TEXT("CategoryTag"))), false);
                    Entry.UtteranceTemplate = EntryObj->GetStringField(TEXT("UtteranceTemplate"));
                    
                    ParseDialogueUtteranceArray(EntryObj, TEXT("UtteranceVariations"), Entry.UtteranceVariations);

                    if (EntryObj->HasField(TEXT("AllowedMoodTags")))
                    {
                        for (const auto& MoodVal : EntryObj->GetArrayField(TEXT("AllowedMoodTags")))
                        {
                            FGameplayTag MoodTag = FGameplayTag::RequestGameplayTag(FName(*MoodVal->AsString()), false);
                            if (MoodTag.IsValid()) Entry.AllowedMoodTags.AddTag(MoodTag);
                        }
                    }
                    if (EntryObj->HasField(TEXT("AllowedRelationshipTypeTags")))
                    {
                        for (const auto& RelVal : EntryObj->GetArrayField(TEXT("AllowedRelationshipTypeTags")))
                        {
                            FGameplayTag RelTag = FGameplayTag::RequestGameplayTag(FName(*RelVal->AsString()), false);
                            if (RelTag.IsValid()) Entry.AllowedRelationshipTypeTags.AddTag(RelTag);
                        }
                    }
                    if (EntryObj->HasField(TEXT("RelationalAttributeRequirements")))
                    {
                        for (const auto& ReqVal : EntryObj->GetArrayField(TEXT("RelationalAttributeRequirements")))
                        {
                            TSharedPtr<FJsonObject> ReqObj = ReqVal->AsObject();
                            if (ReqObj.IsValid())
                            {
                                FCharacterAttributeRangeDef Range;
                                Range.AttributeTag = FGameplayTag::RequestGameplayTag(FName(*ReqObj->GetStringField(TEXT("AttributeTag"))), false);
                                Range.MinValue = static_cast<float>(ReqObj->GetNumberField(TEXT("MinValue")));
                                Range.MaxValue = static_cast<float>(ReqObj->GetNumberField(TEXT("MaxValue")));
                                Entry.RelationalAttributeRequirements.Add(Range);
                            }
                        }
                    }
                    if (EntryObj->HasField(TEXT("VoiceCueTag")))
                    {
                        Entry.VoiceCueTag = FGameplayTag::RequestGameplayTag(FName(*EntryObj->GetStringField(TEXT("VoiceCueTag"))), false);
                    }
                    if (EntryObj->HasField(TEXT("SelectionWeight")))
                    {
                        Entry.SelectionWeight = static_cast<float>(EntryObj->GetNumberField(TEXT("SelectionWeight")));
                    }
                    DialogueDef.Entries.Add(Entry);
                }
            }
            OutStaging.FastPathDialogueRegistry.Add(ProfileTag, DialogueDef);
        }
    }
    if (RootObject->HasField(TEXT("MovementStateRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("MovementStateRegistry"))->Values)
        {
            FGameplayTag StateTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!StateTag.IsValid()) continue;
            TSharedPtr<FJsonObject> MovObj = Pair.Value->AsObject();
            if (!MovObj.IsValid()) continue;
            FDynamicMovementStateDef Def;
            Def.StateTag = StateTag;
            Def.MoverModeName = FName(*MovObj->GetStringField(TEXT("MoverModeName")));
            Def.LocomotionType = StringToEnum<EMovementLocomotionType>(MovObj->GetStringField(TEXT("LocomotionType")));
            DeserializeMovementStatConfig(MovObj, TEXT("MaxSpeed"), Def.MaxSpeed);
            DeserializeMovementStatConfig(MovObj, TEXT("Acceleration"), Def.Acceleration);
            DeserializeMovementStatConfig(MovObj, TEXT("Deceleration"), Def.Deceleration);
            DeserializeMovementStatConfig(MovObj, TEXT("GroundFriction"), Def.GroundFriction);
            DeserializeMovementStatConfig(MovObj, TEXT("GravityScale"), Def.GravityScale);
            DeserializeMovementStatConfig(MovObj, TEXT("TargetCapsuleHalfHeight"), Def.TargetCapsuleHalfHeight);
            if (MovObj->HasField(TEXT("BlockedByTags")))
            {
                for (const auto& TagVal : MovObj->GetArrayField(TEXT("BlockedByTags")))
                {
                    Def.BlockedByTags.AddTag(FGameplayTag::RequestGameplayTag(FName(*TagVal->AsString()), false));
                }
            }
            if (MovObj->HasField(TEXT("GrantsTags")))
            {
                for (const auto& TagVal : MovObj->GetArrayField(TEXT("GrantsTags")))
                {
                    Def.GrantsTags.AddTag(FGameplayTag::RequestGameplayTag(FName(*TagVal->AsString()), false));
                }
            }
            OutStaging.MovementStateRegistry.Add(StateTag, Def);
        }
    }
    if (RootObject->HasField(TEXT("NetworkMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("NetworkMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> NodeObj = Pair.Value->AsObject();
            FDynamicNetworkNodeDef NodeDef;
            if (NodeObj->HasField(TEXT("SupportedResources")))
            {
                for (const auto& ResVal : NodeObj->GetArrayField(TEXT("SupportedResources")))
                {
                    TSharedPtr<FJsonObject> ResObj = ResVal->AsObject();
                    FDynamicNetworkResourceConfig ResConfig;
                    ResConfig.ResourceTag = FGameplayTag::RequestGameplayTag(FName(*ResObj->GetStringField(TEXT("ResourceTag"))), false);
                    ResConfig.NodeType = StringToEnum<ENetworkNodeType>(ResObj->GetStringField(TEXT("NodeType")));
                    ResConfig.MaxFlowRate = ResObj->HasField(TEXT("MaxFlowRate")) ? static_cast<float>(ResObj->GetNumberField(TEXT("MaxFlowRate"))) : 100.0f;
                    ResConfig.MaxBurstCapacity = ResObj->HasField(TEXT("MaxBurstCapacity")) ? static_cast<float>(ResObj->GetNumberField(TEXT("MaxBurstCapacity"))) : 0.0f;
                    ResConfig.VolumeAttributeTag = FGameplayTag::RequestGameplayTag(FName(*ResObj->GetStringField(TEXT("VolumeAttributeTag"))), false);
                    ResConfig.MaxVolumeAttributeTag = FGameplayTag::RequestGameplayTag(FName(*ResObj->GetStringField(TEXT("MaxVolumeAttributeTag"))), false);
                    ResConfig.FlowRateAttributeTag = FGameplayTag::RequestGameplayTag(FName(*ResObj->GetStringField(TEXT("FlowRateAttributeTag"))), false);
                    ResConfig.PressureAttributeTag = FGameplayTag::RequestGameplayTag(FName(*ResObj->GetStringField(TEXT("PressureAttributeTag"))), false);
                    ResConfig.bIsEnvironmentLinked = ResObj->HasField(TEXT("bIsEnvironmentLinked")) ? ResObj->GetBoolField(TEXT("bIsEnvironmentLinked")) : false;
                    NodeDef.SupportedResources.Add(ResConfig);
                }
            }
            OutStaging.NetworkMasterRegistry.Add(KeyTag, NodeDef);
        }
    }
    if (RootObject->HasField(TEXT("AIBehaviorMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("AIBehaviorMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> DefObj = Pair.Value->AsObject();
            if (!DefObj.IsValid()) continue;
            FDynamicAIBehaviorDef BehaviorDef;
            BehaviorDef.BehaviorTag = KeyTag;
            BehaviorDef.ActionTag = FGameplayTag::RequestGameplayTag(FName(*DefObj->GetStringField(TEXT("ActionTag"))), false);
            BehaviorDef.TargetPlaceRoleTag = FGameplayTag::RequestGameplayTag(FName(*DefObj->GetStringField(TEXT("TargetPlaceRoleTag"))), false);
            BehaviorDef.ExecutionType = StringToEnum<ENpcBehaviorExecutionType>(DefObj->GetStringField(TEXT("ExecutionType")));
            BehaviorDef.AbilityTagToTrigger = FGameplayTag::RequestGameplayTag(FName(*DefObj->GetStringField(TEXT("AbilityTagToTrigger"))), false);
            BehaviorDef.MovementStateTagToSet = FGameplayTag::RequestGameplayTag(FName(*DefObj->GetStringField(TEXT("MovementStateTagToSet"))), false);
            if (DefObj->HasField(TEXT("MontageToPlay")))
            {
                BehaviorDef.MontageToPlay = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(DefObj->GetStringField(TEXT("MontageToPlay"))));
            }
            BehaviorDef.BasePriority = DefObj->HasField(TEXT("BasePriority")) ? static_cast<float>(DefObj->GetNumberField(TEXT("BasePriority"))) : 10.0f;
            BehaviorDef.PrerequisiteConditionTreeName = DefObj->HasField(TEXT("PrerequisiteConditionTreeName")) ? FName(*DefObj->GetStringField(TEXT("PrerequisiteConditionTreeName"))) : NAME_None;
            BehaviorDef.CooldownDuration = DefObj->HasField(TEXT("CooldownDuration")) ? static_cast<float>(DefObj->GetNumberField(TEXT("CooldownDuration"))) : 5.0f;
            BehaviorDef.bInterruptible = DefObj->HasField(TEXT("bInterruptible")) ? DefObj->GetBoolField(TEXT("bInterruptible")) : true;
            DeserializeMapWrapper(DefObj, TEXT("PriorityAttributeScalars"), BehaviorDef.PriorityAttributeScalars);
            DeserializeMapWrapper(DefObj, TEXT("ExecutionMetrics"), BehaviorDef.ExecutionMetrics);
            
            if (DefObj->HasField(TEXT("DialogueCategoryTag")))
            {
                BehaviorDef.DialogueCategoryTag = FGameplayTag::RequestGameplayTag(FName(*DefObj->GetStringField(TEXT("DialogueCategoryTag"))), false);
            }
            if (DefObj->HasField(TEXT("DialogueProfileTag")))
            {
                BehaviorDef.DialogueProfileTag = FGameplayTag::RequestGameplayTag(FName(*DefObj->GetStringField(TEXT("DialogueProfileTag"))), false);
            }            
            ParseDialogueUtteranceArray(DefObj, TEXT("Utterances"), BehaviorDef.Utterances);
            if (DefObj->HasField(TEXT("UtteranceTemplates")))
            {
                for (const auto& Val : DefObj->GetArrayField(TEXT("UtteranceTemplates")))
                {
                    BehaviorDef.UtteranceTemplates.Add(Val->AsString());
                }
            }
            if (DefObj->HasField(TEXT("TargetEQSConfig")))
            {
                TSharedPtr<FJsonObject> EqsObj = DefObj->GetObjectField(TEXT("TargetEQSConfig"));
                BehaviorDef.TargetEQSConfig.QueryAsset = TSoftObjectPtr<UEnvQuery>(FSoftObjectPath(EqsObj->GetStringField(TEXT("QueryAsset"))));
                if (EqsObj->HasField(TEXT("QueryParams")))
                {
                    for (const auto& ParamVal : EqsObj->GetArrayField(TEXT("QueryParams")))
                    {
                        TSharedPtr<FJsonObject> ParamObj = ParamVal->AsObject();
                        FDynamicEQSParam EqParam;
                        EqParam.ParamName = FName(*ParamObj->GetStringField(TEXT("ParamName")));
                        EqParam.Value = static_cast<float>(ParamObj->GetNumberField(TEXT("Value")));
                        BehaviorDef.TargetEQSConfig.QueryParams.Add(EqParam);
                    }
                }
            }
            OutStaging.AIBehaviorMasterRegistry.Add(KeyTag, BehaviorDef);
        }
    }
    if (RootObject->HasField(TEXT("VoxelGenerationConfig")))
    {
        TSharedPtr<FJsonObject> GenObj = RootObject->GetObjectField(TEXT("VoxelGenerationConfig"));
        FDynamicVoxelGenerationConfig Config;
        if (FJsonObjectConverter::JsonObjectToUStruct(GenObj.ToSharedRef(), &Config, 0, 0))
        {
            OutStaging.bHasVoxelGenerationConfig = true;
            OutStaging.VoxelGenerationConfig = Config;
        }
    }
    // 1. Parsing GameSettingsConfig
    if (RootObject->HasField(TEXT("GameSettingsConfig")))
    {
        TSharedPtr<FJsonObject> SettingsObj = RootObject->GetObjectField(TEXT("GameSettingsConfig"));
        
        if (SettingsObj->HasField(TEXT("ShowAbilityCombosInUI")))
        {
            TSharedPtr<FJsonObject> BoolObj = SettingsObj->GetObjectField(TEXT("ShowAbilityCombosInUI"));
            OutStaging.GameSettingsConfig.ShowAbilityCombosInUI.bIsOptionalSetting = BoolObj->HasField(TEXT("bIsOptionalSetting")) ? BoolObj->GetBoolField(TEXT("bIsOptionalSetting")) : false;
            OutStaging.GameSettingsConfig.ShowAbilityCombosInUI.bDefaultValue = BoolObj->HasField(TEXT("bDefaultValue")) ? BoolObj->GetBoolField(TEXT("bDefaultValue")) : false;
        }

        if (SettingsObj->HasField(TEXT("MasterAcousticMetaSounds")))
        {
            TSharedPtr<FJsonObject> MsObj = SettingsObj->GetObjectField(TEXT("MasterAcousticMetaSounds"));
            OutStaging.GameSettingsConfig.MasterAcousticMetaSounds.PhysicsImpact = TSoftObjectPtr<USoundBase>(FSoftObjectPath(MsObj->GetStringField(TEXT("PhysicsImpact"))));
            OutStaging.GameSettingsConfig.MasterAcousticMetaSounds.EquipmentRustle = TSoftObjectPtr<USoundBase>(FSoftObjectPath(MsObj->GetStringField(TEXT("EquipmentRustle"))));
            OutStaging.GameSettingsConfig.MasterAcousticMetaSounds.FoliageWind = TSoftObjectPtr<USoundBase>(FSoftObjectPath(MsObj->GetStringField(TEXT("FoliageWind"))));
            if (MsObj->HasField(TEXT("AmbientEnvironmentLoop")))
            {
                OutStaging.GameSettingsConfig.MasterAcousticMetaSounds.AmbientEnvironmentLoop = TSoftObjectPtr<USoundBase>(FSoftObjectPath(MsObj->GetStringField(TEXT("AmbientEnvironmentLoop"))));
            }
            if (MsObj->HasField(TEXT("MaterialScrape")))
            {
                OutStaging.GameSettingsConfig.MasterAcousticMetaSounds.MaterialScrape = TSoftObjectPtr<USoundBase>(FSoftObjectPath(MsObj->GetStringField(TEXT("MaterialScrape"))));
            }
        }

        if (SettingsObj->HasField(TEXT("AudioVirtualizationConfig")))
        {
            TSharedPtr<FJsonObject> VirtObj = SettingsObj->GetObjectField(TEXT("AudioVirtualizationConfig"));
            FJsonObjectConverter::JsonObjectToUStruct(VirtObj.ToSharedRef(), &OutStaging.GameSettingsConfig.AudioVirtualizationConfig, 0, 0);
        }
        
        OutStaging.bHasGameSettingsConfig = true;
    }
    // 2. Parsing MaterialMasterRegistry
    if (RootObject->HasField(TEXT("MaterialMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("MaterialMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> MatObj = Pair.Value->AsObject();
            if (!MatObj.IsValid()) continue;

            FDynamicMaterialDef MatDef;
            FJsonObjectConverter::JsonObjectToUStruct(MatObj.ToSharedRef(), &MatDef, 0, 0);

            // Parse New Acoustic Properties
            if (MatObj->HasField(TEXT("AcousticProperties")))
            {
                TSharedPtr<FJsonObject> AcObj = MatObj->GetObjectField(TEXT("AcousticProperties"));
                MatDef.AcousticProperties.HarmonicProfile = UDynamicDataSerializer::StringToEnum<EAcousticHarmonicProfile>(FString(AcObj->GetStringField(TEXT("HarmonicProfile"))));
                MatDef.AcousticProperties.FundamentalFrequency = static_cast<float>(AcObj->GetNumberField(TEXT("FundamentalFrequency")));
                MatDef.AcousticProperties.QFactor = static_cast<float>(AcObj->GetNumberField(TEXT("QFactor")));
                MatDef.AcousticProperties.DecayMultiplier = static_cast<float>(AcObj->GetNumberField(TEXT("DecayMultiplier")));
                MatDef.AcousticProperties.HighFreqDamping = static_cast<float>(AcObj->GetNumberField(TEXT("HighFreqDamping")));
            }

            // Phase / Adhesion
            MatDef.bIsGravityAffected = MatObj->HasField(TEXT("bIsGravityAffected")) ? MatObj->GetBoolField(TEXT("bIsGravityAffected")) : true;
            MatDef.RequiredAdhesionNeighbors = MatObj->HasField(TEXT("RequiredAdhesionNeighbors")) ? static_cast<int32>(MatObj->GetNumberField(TEXT("RequiredAdhesionNeighbors"))) : 2;
            MatDef.PhaseChangeTemperature = MatObj->HasField(TEXT("PhaseChangeTemperature")) ? static_cast<float>(MatObj->GetNumberField(TEXT("PhaseChangeTemperature"))) : 1000.0f;
            if (MatObj->HasField(TEXT("TransmutedMaterialTag"))) MatDef.TransmutedMaterialTag = FGameplayTag::RequestGameplayTag(FName(*MatObj->GetStringField(TEXT("TransmutedMaterialTag"))), false);

            // Thermodynamics
            MatDef.bIsFlammable = MatObj->HasField(TEXT("bIsFlammable")) ? MatObj->GetBoolField(TEXT("bIsFlammable")) : false;
            MatDef.IgnitionTemperature = MatObj->HasField(TEXT("IgnitionTemperature")) ? static_cast<float>(MatObj->GetNumberField(TEXT("IgnitionTemperature"))) : 300.0f;
            MatDef.BurnRatePerSecond = MatObj->HasField(TEXT("BurnRatePerSecond")) ? static_cast<float>(MatObj->GetNumberField(TEXT("BurnRatePerSecond"))) : 5.0f;
            MatDef.bCanRot = MatObj->HasField(TEXT("bCanRot")) ? MatObj->GetBoolField(TEXT("bCanRot")) : false;
            MatDef.RotWetnessThreshold = MatObj->HasField(TEXT("RotWetnessThreshold")) ? static_cast<float>(MatObj->GetNumberField(TEXT("RotWetnessThreshold"))) : 80.0f;

            if (MatObj->HasField(TEXT("DefaultYieldArchetype"))) 
            {
                MatDef.DefaultYieldArchetype = FGameplayTag::RequestGameplayTag(FName(*MatObj->GetStringField(TEXT("DefaultYieldArchetype"))), false);
            }

            // Parse Stress Response Rules
            if (MatObj->HasField(TEXT("StressResponses")))
            {
                TSharedPtr<FJsonObject> StressObj = MatObj->GetObjectField(TEXT("StressResponses"));
                for (const auto& StressPair : StressObj->Values)
                {
                    FGameplayTag StressKey = FGameplayTag::RequestGameplayTag(FName(*StressPair.Key), false);
                    if (StressKey.IsValid() && StressPair.Value->Type == EJson::Object)
                    {
                        FMaterialStressResponse Response;
                        TSharedPtr<FJsonObject> RObj = StressPair.Value->AsObject();
                        FJsonObjectConverter::JsonObjectToUStruct(RObj.ToSharedRef(), &Response, 0, 0);
                        
                        if (RObj->HasField(TEXT("YieldArchetype")))
                        {
                            Response.YieldArchetype = FGameplayTag::RequestGameplayTag(FName(*RObj->GetStringField(TEXT("YieldArchetype"))), false);
                        }
                        MatDef.StressResponses.Add(StressKey, Response);
                    }
                }
            }

            OutStaging.MaterialMasterRegistry.Add(KeyTag, MatDef);
        }
    }
    if (RootObject->HasField(TEXT("StructurePieceRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("StructurePieceRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> PieceObj = Pair.Value->AsObject();
            FDynamicStructurePieceDef PieceDef;
            FJsonObjectConverter::JsonObjectToUStruct(PieceObj.ToSharedRef(), &PieceDef, 0, 0);
            OutStaging.StructurePieceRegistry.Add(KeyTag, PieceDef);
        }
    }
    if (RootObject->HasField(TEXT("StructurePrefabRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("StructurePrefabRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> PrefabObj = Pair.Value->AsObject();
            FDynamicStructurePrefabDef PrefabDef;
            FJsonObjectConverter::JsonObjectToUStruct(PrefabObj.ToSharedRef(), &PrefabDef, 0, 0);
            OutStaging.StructurePrefabRegistry.Add(KeyTag, PrefabDef);
        }
    }
    if (RootObject->HasField(TEXT("RoomTypeRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("RoomTypeRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> RoomObj = Pair.Value->AsObject();
            
            FDynamicRoomTypeDef RoomDef;
            RoomDef.RoomTypeTag = KeyTag;
            RoomDef.ClassificationPriority = RoomObj->HasField(TEXT("ClassificationPriority")) ? static_cast<float>(RoomObj->GetNumberField(TEXT("ClassificationPriority"))) : 0.0f;
            RoomDef.MinimumVolume = RoomObj->HasField(TEXT("MinimumVolume")) ? static_cast<float>(RoomObj->GetNumberField(TEXT("MinimumVolume"))) : 100000.0f;
            
            if (RoomObj->HasField(TEXT("AcousticReverbAsset")))
            {
                RoomDef.AcousticReverbAsset = TSoftObjectPtr<UReverbEffect>(FSoftObjectPath(RoomObj->GetStringField(TEXT("AcousticReverbAsset"))));
            }

            if (RoomObj->HasField(TEXT("ContentRequirements")))
            {
                for (const auto& ReqVal : RoomObj->GetArrayField(TEXT("ContentRequirements")))
                {
                    TSharedPtr<FJsonObject> ReqObj = ReqVal->AsObject();
                    FDynamicRoomRequirement Req;
                    Req.RequiredContentTag = FGameplayTag::RequestGameplayTag(FName(*ReqObj->GetStringField(TEXT("RequiredContentTag"))), false);
                    Req.MinimumCount = static_cast<int32>(ReqObj->GetNumberField(TEXT("MinimumCount")));
                    RoomDef.ContentRequirements.Add(Req);
                }
            }
            DeserializeMapWrapper(RoomObj, TEXT("EnvironmentModifiers"), RoomDef.EnvironmentModifiers);
            OutStaging.RoomTypeRegistry.Add(KeyTag, RoomDef);
        }
    }
    if (RootObject->HasField(TEXT("LawAndOrderConfig")))
    {
        TSharedPtr<FJsonObject> LawConfigObj = RootObject->GetObjectField(TEXT("LawAndOrderConfig"));
        FLawAndOrderSystemConfig LawConfig;
        LawConfig.DefaultJurisdictionTag = FGameplayTag::RequestGameplayTag(FName(*LawConfigObj->GetStringField(TEXT("DefaultJurisdictionTag"))), false);
        
        if (LawConfigObj->HasField(TEXT("EnterPropertyActionTag")))
        {
            LawConfig.EnterPropertyActionTag = FGameplayTag::RequestGameplayTag(FName(*LawConfigObj->GetStringField(TEXT("EnterPropertyActionTag"))), false);
        }
        if (LawConfigObj->HasField(TEXT("ExitPropertyActionTag")))
        {
            LawConfig.ExitPropertyActionTag = FGameplayTag::RequestGameplayTag(FName(*LawConfigObj->GetStringField(TEXT("ExitPropertyActionTag"))), false);
        }

        OutStaging.bHasLawAndOrderConfig = true;
        OutStaging.LawAndOrderConfig = LawConfig;
    }
    if (RootObject->HasField(TEXT("CloudAIConfig")))
    {
        TSharedPtr<FJsonObject> CloudObj = RootObject->GetObjectField(TEXT("CloudAIConfig"));
        FCloudAISystemConfig CloudConfig;
        if (FJsonObjectConverter::JsonObjectToUStruct(CloudObj.ToSharedRef(), &CloudConfig, 0, 0))
        {
            OutStaging.bHasCloudAIConfig = true;
            OutStaging.CloudAIConfig = CloudConfig;
        }
    }
    if (RootObject->HasField(TEXT("CrimeMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("CrimeMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> CrimeObj = Pair.Value->AsObject();
            FDynamicCrimeDef Def;
            Def.CrimeTag = KeyTag;
            Def.BaseSeverity = CrimeObj->HasField(TEXT("BaseSeverity")) ? static_cast<float>(CrimeObj->GetNumberField(TEXT("BaseSeverity"))) : 10.0f;
            Def.bRequiresProofOfOwnership = CrimeObj->HasField(TEXT("bRequiresProofOfOwnership")) ? CrimeObj->GetBoolField(TEXT("bRequiresProofOfOwnership")) : false;
            
            Def.ExemptionConditionTree = CrimeObj->HasField(TEXT("ExemptionConditionTree")) ? FName(*CrimeObj->GetStringField(TEXT("ExemptionConditionTree"))) : NAME_None;
            Def.WitnessInterpretationTree = CrimeObj->HasField(TEXT("WitnessInterpretationTree")) ? FName(*CrimeObj->GetStringField(TEXT("WitnessInterpretationTree"))) : NAME_None;
            
            DeserializeMapWrapper(CrimeObj, TEXT("SensorySignatures"), Def.SensorySignatures);
            
            if (CrimeObj->HasField(TEXT("AssociatedWitnessBehaviors")))
            {
                for (const auto& BehavVal : CrimeObj->GetArrayField(TEXT("AssociatedWitnessBehaviors")))
                {
                    Def.AssociatedWitnessBehaviors.Add(FGameplayTag::RequestGameplayTag(FName(*BehavVal->AsString()), false));
                }
            }
            OutStaging.CrimeMasterRegistry.Add(KeyTag, Def);
        }
    }
    if (RootObject->HasField(TEXT("JurisdictionMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("JurisdictionMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            
            TSharedPtr<FJsonObject> JurObj = Pair.Value->AsObject();
            FDynamicJurisdictionDef Def;
            Def.JurisdictionTag = KeyTag;
            Def.GoverningFactionTag = FGameplayTag::RequestGameplayTag(FName(*JurObj->GetStringField(TEXT("GoverningFactionTag"))), false);
            Def.PropertyAccessConditionTree = JurObj->HasField(TEXT("PropertyAccessConditionTree")) ? FName(*JurObj->GetStringField(TEXT("PropertyAccessConditionTree"))) : NAME_None;

            DeserializeTagToTagMapWrapper(JurObj, TEXT("ActionToCrimeMap"), Def.ActionToCrimeMap);

            if (JurObj->HasField(TEXT("LocalLaws")))
            {
                for (auto& LawPair : JurObj->GetObjectField(TEXT("LocalLaws"))->Values)
                {
                    FGameplayTag CrimeTag = FGameplayTag::RequestGameplayTag(FName(*LawPair.Key), false);
                    if (CrimeTag.IsValid())
                    {
                        FCrimeSeverityOverride Override;
                        FJsonObjectConverter::JsonObjectToUStruct(LawPair.Value->AsObject().ToSharedRef(), &Override, 0, 0);
                        Def.LocalLaws.Add(CrimeTag, Override);
                    }
                }
            }

            if (JurObj->HasField(TEXT("EscalationOfForceRules")))
            {
                for (const auto& RuleVal : JurObj->GetArrayField(TEXT("EscalationOfForceRules")))
                {
                    TSharedPtr<FJsonObject> RuleObj = RuleVal->AsObject();
                    if (!RuleObj.IsValid()) continue;
                    
                    FEscalationOfForceRule ForceRule;
                    ForceRule.EvaluationConditionTree = FName(*RuleObj->GetStringField(TEXT("EvaluationConditionTree")));
                    ForceRule.AuthorizedForceLevel = FGameplayTag::RequestGameplayTag(FName(*RuleObj->GetStringField(TEXT("AuthorizedForceLevel"))), false);
                    ForceRule.Priority = static_cast<float>(RuleObj->GetNumberField(TEXT("Priority")));
                    Def.EscalationOfForceRules.Add(ForceRule);
                }
            }

            OutStaging.JurisdictionMasterRegistry.Add(KeyTag, Def);
        }
    }
    if (RootObject->HasField(TEXT("VoiceProfileMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("VoiceProfileMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> VoiceObj = Pair.Value->AsObject();
            if (!VoiceObj.IsValid()) continue;

            FVoiceProfileDef VoiceDef;
            VoiceDef.VoiceProfileTag = KeyTag;
            VoiceDef.ModelIdentifier = VoiceObj->HasField(TEXT("ModelIdentifier")) ? VoiceObj->GetStringField(TEXT("ModelIdentifier")) : TEXT("tts_models/en/vctk/vits");
            VoiceDef.SpeakerId = VoiceObj->HasField(TEXT("SpeakerId")) ? VoiceObj->GetStringField(TEXT("SpeakerId")) : TEXT("p226");
            VoiceDef.BasePitchMultiplier = VoiceObj->HasField(TEXT("BasePitchMultiplier")) ? static_cast<float>(VoiceObj->GetNumberField(TEXT("BasePitchMultiplier"))) : 1.0f;
            VoiceDef.BaseTempoRate = VoiceObj->HasField(TEXT("BaseTempoRate")) ? static_cast<float>(VoiceObj->GetNumberField(TEXT("BaseTempoRate"))) : 1.0f;
            
            if (VoiceObj->HasField(TEXT("VoiceGender")))
            {
                VoiceDef.VoiceGender = FGameplayTag::RequestGameplayTag(FName(*VoiceObj->GetStringField(TEXT("VoiceGender"))), false);
            }

            if (VoiceObj->HasField(TEXT("AllowedTemplateTags")))
            {
                for (const auto& TagVal : VoiceObj->GetArrayField(TEXT("AllowedTemplateTags")))
                {
                    FGameplayTag TemplateTag = FGameplayTag::RequestGameplayTag(FName(*TagVal->AsString()), false);
                    if (TemplateTag.IsValid())
                    {
                        VoiceDef.AllowedTemplateTags.Add(TemplateTag);
                    }
                }
            }

            DeserializeMapWrapper(VoiceObj, TEXT("EmotionPitchOffsets"), VoiceDef.EmotionPitchOffsets);
            DeserializeMapWrapper(VoiceObj, TEXT("EmotionSpeedOffsets"), VoiceDef.EmotionSpeedOffsets);

            OutStaging.VoiceProfileMasterRegistry.Add(KeyTag, VoiceDef);
        }
    }
    if (RootObject->HasField(TEXT("PhonemeVisemeRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("PhonemeVisemeRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> RegObj = Pair.Value->AsObject();
            if (!RegObj.IsValid()) continue;

            FPhonemeVisemeRegistryDef RegDef;
            RegDef.RegistryTag = KeyTag;

            if (RegObj->HasField(TEXT("VisemeMappings")))
            {
                for (const auto& MapVal : RegObj->GetArrayField(TEXT("VisemeMappings")))
                {
                    TSharedPtr<FJsonObject> MapObj = MapVal->AsObject();
                    if (!MapObj.IsValid()) continue;

                    FVisemeMorphMapping Mapping;
                    Mapping.PhonemePattern = MapObj->GetStringField(TEXT("PhonemePattern"));
                    Mapping.MorphTargetName = FName(*MapObj->GetStringField(TEXT("MorphTargetName")));
                    Mapping.WeightMultiplier = MapObj->HasField(TEXT("WeightMultiplier")) ? static_cast<float>(MapObj->GetNumberField(TEXT("WeightMultiplier"))) : 1.0f;
                    RegDef.VisemeMappings.Add(Mapping);
                }
            }

            OutStaging.PhonemeVisemeRegistry.Add(KeyTag, RegDef);
        }
    }
    if (RootObject->HasField(TEXT("DialectMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("DialectMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> DiaObj = Pair.Value->AsObject();
            if (!DiaObj.IsValid()) continue;

            FDynamicDialectDef DiaDef;
            DiaDef.DialectTag = KeyTag;
            DiaDef.ParentLanguageTag = FGameplayTag::RequestGameplayTag(FName(*DiaObj->GetStringField(TEXT("ParentLanguageTag"))), false);
            DiaDef.DialectDisplayName = DiaObj->HasField(TEXT("DialectDisplayName")) ? DiaObj->GetStringField(TEXT("DialectDisplayName")) : TEXT("");
            DiaDef.PitchModifier = DiaObj->HasField(TEXT("PitchModifier")) ? static_cast<float>(DiaObj->GetNumberField(TEXT("PitchModifier"))) : 0.0f;
            DiaDef.TempoModifier = DiaObj->HasField(TEXT("TempoModifier")) ? static_cast<float>(DiaObj->GetNumberField(TEXT("TempoModifier"))) : 0.0f;

            if (DiaObj->HasField(TEXT("ColloquialPrefixes")))
            {
                for (const auto& Val : DiaObj->GetArrayField(TEXT("ColloquialPrefixes")))
                {
                    DiaDef.ColloquialPrefixes.Add(Val->AsString());
                }
            }
            if (DiaObj->HasField(TEXT("ColloquialSuffixes")))
            {
                for (const auto& Val : DiaObj->GetArrayField(TEXT("ColloquialSuffixes")))
                {
                    DiaDef.ColloquialSuffixes.Add(Val->AsString());
                }
            }
            if (DiaObj->HasField(TEXT("PhoneticShifts")))
            {
                for (const auto& ShiftVal : DiaObj->GetArrayField(TEXT("PhoneticShifts")))
                {
                    TSharedPtr<FJsonObject> ShiftObj = ShiftVal->AsObject();
                    if (ShiftObj.IsValid())
                    {
                        FDialectPhoneticShift Shift;
                        Shift.OriginalPhoneme = ShiftObj->GetStringField(TEXT("OriginalPhoneme"));
                        Shift.ShiftedPhoneme = ShiftObj->GetStringField(TEXT("ShiftedPhoneme"));
                        DiaDef.PhoneticShifts.Add(Shift);
                    }
                }
            }
            if (DiaObj->HasField(TEXT("SlangSubstitutions")))
            {
                TSharedPtr<FJsonObject> SlangObj = DiaObj->GetObjectField(TEXT("SlangSubstitutions"));
                if (SlangObj.IsValid())
                {
                    for (const auto& SlangPair : SlangObj->Values)
                    {
                        if (SlangPair.Value.IsValid())
                        {
                            const FString SlangKey = *SlangPair.Key;
                            const FString SlangVal = SlangPair.Value->AsString();
                            DiaDef.SlangSubstitutions.Add(SlangKey, SlangVal);
                        }
                    }
                }
            }

            OutStaging.DialectMasterRegistry.Add(KeyTag, DiaDef);
        }
    }
    if (RootObject->HasField(TEXT("MarketMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("MarketMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> MarketObj = Pair.Value->AsObject();
            if (!MarketObj.IsValid()) continue;

            FDynamicMarketRegionDef MarketDef;
            MarketDef.MarketTag = KeyTag;
            MarketDef.GoverningJurisdictionTag = FGameplayTag::RequestGameplayTag(FName(*MarketObj->GetStringField(TEXT("GoverningJurisdictionTag"))), false);
            MarketDef.BaseInflationRate = MarketObj->HasField(TEXT("BaseInflationRate")) ? static_cast<float>(MarketObj->GetNumberField(TEXT("BaseInflationRate"))) : 1.0f;

            if (MarketObj->HasField(TEXT("BaselineCommodities")))
            {
                for (const auto& Val : MarketObj->GetArrayField(TEXT("BaselineCommodities")))
                {
                    TSharedPtr<FJsonObject> ComObj = Val->AsObject();
                    if (ComObj.IsValid())
                    {
                        FDynamicMarketCommodity Commodity;
                        Commodity.ItemArchetypeTag = FGameplayTag::RequestGameplayTag(FName(*ComObj->GetStringField(TEXT("ItemArchetypeTag"))), false);
                        Commodity.BasePrice = ComObj->HasField(TEXT("BasePrice")) ? static_cast<float>(ComObj->GetNumberField(TEXT("BasePrice"))) : 10.0f;
                        Commodity.PriceElasticity = ComObj->HasField(TEXT("PriceElasticity")) ? static_cast<float>(ComObj->GetNumberField(TEXT("PriceElasticity"))) : 1.0f;
                        Commodity.CurrentSupply = ComObj->HasField(TEXT("CurrentSupply")) ? static_cast<float>(ComObj->GetNumberField(TEXT("CurrentSupply"))) : 100.0f;
                        Commodity.CurrentDemand = ComObj->HasField(TEXT("CurrentDemand")) ? static_cast<float>(ComObj->GetNumberField(TEXT("CurrentDemand"))) : 100.0f;
                        
                        if (ComObj->HasField(TEXT("ConsumingProfessions")))
                        {
                            for (const auto& ProfVal : ComObj->GetArrayField(TEXT("ConsumingProfessions")))
                            {
                                Commodity.ConsumingProfessions.Add(FGameplayTag::RequestGameplayTag(FName(*ProfVal->AsString()), false));
                            }
                        }
                        if (ComObj->HasField(TEXT("ProducingProfessions")))
                        {
                            for (const auto& ProfVal : ComObj->GetArrayField(TEXT("ProducingProfessions")))
                            {
                                Commodity.ProducingProfessions.Add(FGameplayTag::RequestGameplayTag(FName(*ProfVal->AsString()), false));
                            }
                        }
                        MarketDef.BaselineCommodities.Add(Commodity);
                    }
                }
            }

            OutStaging.MarketMasterRegistry.Add(KeyTag, MarketDef);
        }
    }
    
    if (RootObject->HasField(TEXT("FactionMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("FactionMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            TSharedPtr<FJsonObject> FacObj = Pair.Value->AsObject();
            FDynamicFactionDef FacDef;
            
            FacDef.FactionTag = KeyTag;
            DeserializeLocalizationMap(FacObj, TEXT("Localization"), FacDef.Localization);
            FacDef.BaseInfluence = FacObj->HasField(TEXT("BaseInfluence")) ? static_cast<float>(FacObj->GetNumberField(TEXT("BaseInfluence"))) : 100.0f;
            
            if (FacObj->HasField(TEXT("AlliedFactions")))
            {
                for (const auto& TagVal : FacObj->GetArrayField(TEXT("AlliedFactions")))
                {
                    FacDef.AlliedFactions.AddTag(FGameplayTag::RequestGameplayTag(FName(*TagVal->AsString()), false));
                }
            }
            if (FacObj->HasField(TEXT("RivalFactions")))
            {
                for (const auto& TagVal : FacObj->GetArrayField(TEXT("RivalFactions")))
                {
                    FacDef.RivalFactions.AddTag(FGameplayTag::RequestGameplayTag(FName(*TagVal->AsString()), false));
                }
            }

            if (FacObj->HasField(TEXT("FactionColorTheme")))
            {
                TSharedPtr<FJsonObject> ColObj = FacObj->GetObjectField(TEXT("FactionColorTheme"));
                // Fallback struct parsing for the color gradient
                FJsonObjectConverter::JsonObjectToUStruct(ColObj.ToSharedRef(), &FacDef.FactionColorTheme, 0, 0);
            }

            OutStaging.FactionMasterRegistry.Add(KeyTag, FacDef);
        }
    }
    
    if (RootObject->HasField(TEXT("DamageTypeMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("DamageTypeMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;

            TSharedPtr<FJsonObject> DmgObj = Pair.Value->AsObject();
            if (!DmgObj.IsValid()) continue;

            FDynamicDamageTypeDef Def;
            Def.DamageTypeTag = KeyTag;
            
            if (DmgObj->HasField(TEXT("ParentCategoryTag")))
            {
                Def.ParentCategoryTag = FGameplayTag::RequestGameplayTag(FName(*DmgObj->GetStringField(TEXT("ParentCategoryTag"))), false);
            }

            DeserializeLocalizationMap(DmgObj, TEXT("Localization"), Def.Localization);

            if (DmgObj->HasField(TEXT("PrimaryResistanceAttributeTag")))
            {
                Def.PrimaryResistanceAttributeTag = FGameplayTag::RequestGameplayTag(FName(*DmgObj->GetStringField(TEXT("PrimaryResistanceAttributeTag"))), false);
            }

            if (DmgObj->HasField(TEXT("DefaultImpactTypeTag")))
            {
                Def.DefaultImpactTypeTag = FGameplayTag::RequestGameplayTag(FName(*DmgObj->GetStringField(TEXT("DefaultImpactTypeTag"))), false);
            }

            Def.DevComment = DmgObj->HasField(TEXT("DevComment")) ? DmgObj->GetStringField(TEXT("DevComment")) : TEXT("");

            if (DmgObj->HasField(TEXT("AssociatedStatusEffects")))
            {
                for (const auto& Val : DmgObj->GetArrayField(TEXT("AssociatedStatusEffects")))
                {
                    FGameplayTag StatusTag = FGameplayTag::RequestGameplayTag(FName(*Val->AsString()), false);
                    if (StatusTag.IsValid())
                    {
                        Def.AssociatedStatusEffects.Add(StatusTag);
                    }
                }
            }

            OutStaging.DamageTypeMasterRegistry.Add(KeyTag, Def);
        }
    }
    
    // ------------------------------------------------------------------------
    // DYNAMIC EQS QUERY REGISTRY PARSING
    // ------------------------------------------------------------------------
    if (RootObject->HasField(TEXT("EQSMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("EQSMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;

            TSharedPtr<FJsonObject> EqObj = Pair.Value->AsObject();
            if (!EqObj.IsValid()) continue;

            FDynamicEQSDef Def;
            Def.QueryTag = KeyTag;
            Def.GeneratorType = StringToEnum<EDynamicEQSGeneratorType>(EqObj->GetStringField(TEXT("GeneratorType")));
            Def.SelectMode = StringToEnum<EDynamicEQSSelectMode>(EqObj->GetStringField(TEXT("SelectMode")));
            Def.BaseSearchRadius = EqObj->HasField(TEXT("BaseSearchRadius")) ? static_cast<float>(EqObj->GetNumberField(TEXT("BaseSearchRadius"))) : 3000.0f;
            Def.GridDensityOrItemSpacing = EqObj->HasField(TEXT("GridDensityOrItemSpacing")) ? static_cast<float>(EqObj->GetNumberField(TEXT("GridDensityOrItemSpacing"))) : 250.0f;

            if (EqObj->HasField(TEXT("ContextRoleTag")))
            {
                Def.ContextRoleTag = FGameplayTag::RequestGameplayTag(FName(*EqObj->GetStringField(TEXT("ContextRoleTag"))), false);
            }

            if (EqObj->HasField(TEXT("Tests")))
            {
                for (const auto& TestVal : EqObj->GetArrayField(TEXT("Tests")))
                {
                    TSharedPtr<FJsonObject> TObj = TestVal->AsObject();
                    if (!TObj.IsValid()) continue;

                    FDynamicEQSTestDef TestDef;
                    TestDef.TestType = StringToEnum<EDynamicEQSTestType>(TObj->GetStringField(TEXT("TestType")));
                    TestDef.TestPurpose = StringToEnum<EDynamicEQSTestPurpose>(TObj->GetStringField(TEXT("TestPurpose")));
                    TestDef.Weight = TObj->HasField(TEXT("Weight")) ? static_cast<float>(TObj->GetNumberField(TEXT("Weight"))) : 1.0f;
                    TestDef.MinThreshold = TObj->HasField(TEXT("MinThreshold")) ? static_cast<float>(TObj->GetNumberField(TEXT("MinThreshold"))) : 0.0f;
                    TestDef.MaxThreshold = TObj->HasField(TEXT("MaxThreshold")) ? static_cast<float>(TObj->GetNumberField(TEXT("MaxThreshold"))) : 5000.0f;
                    TestDef.bInvertScore = TObj->HasField(TEXT("bInvertScore")) ? TObj->GetBoolField(TEXT("bInvertScore")) : false;

                    if (TObj->HasField(TEXT("RoleTagToMatch")))
                    {
                        TestDef.RoleTagToMatch = FGameplayTag::RequestGameplayTag(FName(*TObj->GetStringField(TEXT("RoleTagToMatch"))), false);
                    }
                    if (TObj->HasField(TEXT("ConditionTreeName")))
                    {
                        TestDef.ConditionTreeName = FName(*TObj->GetStringField(TEXT("ConditionTreeName")));
                    }

                    Def.Tests.Add(TestDef);
                }
            }

            OutStaging.EQSMasterRegistry.Add(KeyTag, Def);
        }
    }

    // ------------------------------------------------------------------------
    // DYNAMIC STATE TREE REGISTRY PARSING
    // ------------------------------------------------------------------------
    if (RootObject->HasField(TEXT("StateTreeMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("StateTreeMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;

            TSharedPtr<FJsonObject> TreeObj = Pair.Value->AsObject();
            if (!TreeObj.IsValid()) continue;

            FDynamicStateTreeDef TreeDef;
            TreeDef.TreeTag = KeyTag;
            TreeDef.RootNodeIndex = TreeObj->HasField(TEXT("RootNodeIndex")) ? static_cast<int32>(TreeObj->GetNumberField(TEXT("RootNodeIndex"))) : 0;
            TreeDef.bIsLLMFallbackTree = TreeObj->HasField(TEXT("bIsLLMFallbackTree")) ? TreeObj->GetBoolField(TEXT("bIsLLMFallbackTree")) : false;
            TreeDef.FallbackTimeoutSeconds = TreeObj->HasField(TEXT("FallbackTimeoutSeconds")) ? static_cast<float>(TreeObj->GetNumberField(TEXT("FallbackTimeoutSeconds"))) : 4.0f;

            if (TreeObj->HasField(TEXT("Nodes")))
            {
                for (const auto& NodeVal : TreeObj->GetArrayField(TEXT("Nodes")))
                {
                    TSharedPtr<FJsonObject> NObj = NodeVal->AsObject();
                    if (!NObj.IsValid()) continue;

                    FDynamicStateTreeNodeDef NodeDef;
                    NodeDef.NodeIndex = static_cast<int32>(NObj->GetNumberField(TEXT("NodeIndex")));
                    NodeDef.NodeName = FName(*NObj->GetStringField(TEXT("NodeName")));
                    NodeDef.StateTag = FGameplayTag::RequestGameplayTag(FName(*NObj->GetStringField(TEXT("StateTag"))), false);
                    NodeDef.ParentNodeIndex = NObj->HasField(TEXT("ParentNodeIndex")) ? static_cast<int32>(NObj->GetNumberField(TEXT("ParentNodeIndex"))) : INDEX_NONE;
                    NodeDef.SelectionMode = StringToEnum<EDynamicStateSelectionMode>(NObj->GetStringField(TEXT("SelectionMode")));
                    NodeDef.NodeBasePriority = NObj->HasField(TEXT("NodeBasePriority")) ? static_cast<float>(NObj->GetNumberField(TEXT("NodeBasePriority"))) : 10.0f;

                    if (NObj->HasField(TEXT("ChildNodeIndices")))
                    {
                        for (const auto& CVal : NObj->GetArrayField(TEXT("ChildNodeIndices")))
                        {
                            NodeDef.ChildNodeIndices.Add(static_cast<int32>(CVal->AsNumber()));
                        }
                    }

                    if (NObj->HasField(TEXT("EnterConditionTreeName")))
                    {
                        NodeDef.EnterConditionTreeName = FName(*NObj->GetStringField(TEXT("EnterConditionTreeName")));
                    }

                    // Parse Tasks
                    if (NObj->HasField(TEXT("Tasks")))
                    {
                        for (const auto& TaskVal : NObj->GetArrayField(TEXT("Tasks")))
                        {
                            TSharedPtr<FJsonObject> TskObj = TaskVal->AsObject();
                            if (!TskObj.IsValid()) continue;

                            FDynamicStateTreeTaskDef TaskDef;
                            TaskDef.TaskTypeTag = FGameplayTag::RequestGameplayTag(FName(*TskObj->GetStringField(TEXT("TaskTypeTag"))), false);

                            if (TskObj->HasField(TEXT("BehaviorTag")))
                            {
                                TaskDef.BehaviorTag = FGameplayTag::RequestGameplayTag(FName(*TskObj->GetStringField(TEXT("BehaviorTag"))), false);
                            }
                            if (TskObj->HasField(TEXT("TargetEQSTag")))
                            {
                                TaskDef.TargetEQSTag = FGameplayTag::RequestGameplayTag(FName(*TskObj->GetStringField(TEXT("TargetEQSTag"))), false);
                            }
                            if (TskObj->HasField(TEXT("TargetPlaceRoleTag")))
                            {
                                TaskDef.TargetPlaceRoleTag = FGameplayTag::RequestGameplayTag(FName(*TskObj->GetStringField(TEXT("TargetPlaceRoleTag"))), false);
                            }

                            DeserializeMapWrapper(TskObj, TEXT("TaskParameters"), TaskDef.TaskParameters);
                            NodeDef.Tasks.Add(TaskDef);
                        }
                    }

                    // Parse Transitions
                    if (NObj->HasField(TEXT("Transitions")))
                    {
                        for (const auto& TransVal : NObj->GetArrayField(TEXT("Transitions")))
                        {
                            TSharedPtr<FJsonObject> TrObj = TransVal->AsObject();
                            if (!TrObj.IsValid()) continue;

                            FDynamicStateTreeTransitionDef TransDef;
                            TransDef.Trigger = StringToEnum<EDynamicStateTransitionTrigger>(TrObj->GetStringField(TEXT("Trigger")));
                            TransDef.TargetNodeIndex = TrObj->HasField(TEXT("TargetNodeIndex")) ? static_cast<int32>(TrObj->GetNumberField(TEXT("TargetNodeIndex"))) : INDEX_NONE;

                            if (TrObj->HasField(TEXT("TargetStateTag")))
                            {
                                TransDef.TargetStateTag = FGameplayTag::RequestGameplayTag(FName(*TrObj->GetStringField(TEXT("TargetStateTag"))), false);
                            }
                            if (TrObj->HasField(TEXT("ConditionTreeName")))
                            {
                                TransDef.ConditionTreeName = FName(*TrObj->GetStringField(TEXT("ConditionTreeName")));
                            }

                            NodeDef.Transitions.Add(TransDef);
                        }
                    }

                    TreeDef.Nodes.Add(NodeDef);
                }
            }

            OutStaging.StateTreeMasterRegistry.Add(KeyTag, TreeDef);
        }
    }
    
    if (RootObject->HasField(TEXT("CameraMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("CameraMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;

            TSharedPtr<FJsonObject> CamObj = Pair.Value->AsObject();
            if (!CamObj.IsValid()) continue;

            FDynamicCameraProfileDef Def;
            FJsonObjectConverter::JsonObjectToUStruct(CamObj.ToSharedRef(), &Def, 0, 0);
            Def.ProfileTag = KeyTag;

            OutStaging.CameraMasterRegistry.Add(KeyTag, Def);
        }
    }
    
    if (RootObject->HasField(TEXT("AudioMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("AudioMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;

            TSharedPtr<FJsonObject> AudioObj = Pair.Value->AsObject();
            if (!AudioObj.IsValid()) continue;

            FDynamicAudioDef Def;
            Def.AudioTag = KeyTag;
            if (AudioObj->HasField(TEXT("SoundAsset"))) Def.SoundAsset = TSoftObjectPtr<USoundBase>(FSoftObjectPath(AudioObj->GetStringField(TEXT("SoundAsset"))));
            Def.DefaultVolumeMultiplier = AudioObj->HasField(TEXT("DefaultVolumeMultiplier")) ? static_cast<float>(AudioObj->GetNumberField(TEXT("DefaultVolumeMultiplier"))) : 1.0f;
            Def.DefaultPitchMultiplier = AudioObj->HasField(TEXT("DefaultPitchMultiplier")) ? static_cast<float>(AudioObj->GetNumberField(TEXT("DefaultPitchMultiplier"))) : 1.0f;
            Def.VelocityToVolumeScalar = AudioObj->HasField(TEXT("VelocityToVolumeScalar")) ? static_cast<float>(AudioObj->GetNumberField(TEXT("VelocityToVolumeScalar"))) : 0.001f;
            
            if (AudioObj->HasField(TEXT("AssociatedStimulusTag")))
            {
                Def.AssociatedStimulusTag = FGameplayTag::RequestGameplayTag(FName(*AudioObj->GetStringField(TEXT("AssociatedStimulusTag"))), false);
            }
            Def.BaseStimulusMagnitude = AudioObj->HasField(TEXT("BaseStimulusMagnitude")) ? static_cast<float>(AudioObj->GetNumberField(TEXT("BaseStimulusMagnitude"))) : 1000.0f;

            OutStaging.AudioMasterRegistry.Add(KeyTag, Def);
        }
    }

    if (RootObject->HasField(TEXT("AudioMaterialMatrix")))
    {
        for (auto& OuterPair : RootObject->GetObjectField(TEXT("AudioMaterialMatrix"))->Values)
        {
            FGameplayTag OuterTag = FGameplayTag::RequestGameplayTag(FName(*OuterPair.Key), false);
            if (!OuterTag.IsValid()) continue;

            TSharedPtr<FJsonObject> InnerMapObj = OuterPair.Value->AsObject();
            if (!InnerMapObj.IsValid()) continue;

            FAcousticMaterialMatrixRow MatrixRow;

            for (auto& InnerPair : InnerMapObj->Values)
            {
                FGameplayTag InnerTag = FGameplayTag::RequestGameplayTag(FName(*InnerPair.Key), false);
                if (!InnerTag.IsValid()) continue;

                FAcousticInteractionProfile Profile;

                // Support both short string shorthand and full nested object schema
                if (InnerPair.Value->Type == EJson::String)
                {
                    Profile.PrimaryAudioTag = FGameplayTag::RequestGameplayTag(FName(*InnerPair.Value->AsString()), false);
                }
                else if (InnerPair.Value->Type == EJson::Object)
                {
                    TSharedPtr<FJsonObject> ProfObj = InnerPair.Value->AsObject();
                    Profile.PrimaryAudioTag = FGameplayTag::RequestGameplayTag(FName(*ProfObj->GetStringField(TEXT("PrimaryAudioTag"))), false);

                    if (ProfObj->HasField(TEXT("GuaranteedSecondaryAudioTags")))
                    {
                        for (const auto& SecVal : ProfObj->GetArrayField(TEXT("GuaranteedSecondaryAudioTags")))
                        {
                            FGameplayTag SecTag = FGameplayTag::RequestGameplayTag(FName(*SecVal->AsString()), false);
                            if (SecTag.IsValid()) Profile.GuaranteedSecondaryAudioTags.Add(SecTag);
                        }
                    }

                    if (ProfObj->HasField(TEXT("ConditionalAudioLayers")))
                    {
                        for (const auto& CondVal : ProfObj->GetArrayField(TEXT("ConditionalAudioLayers")))
                        {
                            TSharedPtr<FJsonObject> CondObj = CondVal->AsObject();
                            if (!CondObj.IsValid()) continue;

                            FAcousticConditionalLayer CondLayer;
                            CondLayer.AudioTag = FGameplayTag::RequestGameplayTag(FName(*CondObj->GetStringField(TEXT("AudioTag"))), false);
                            CondLayer.BaseProbability = CondObj->HasField(TEXT("BaseProbability")) ? static_cast<float>(CondObj->GetNumberField(TEXT("BaseProbability"))) : 0.25f;
                            CondLayer.ChanceConditionTreeName = CondObj->HasField(TEXT("ChanceConditionTreeName")) ? FName(*CondObj->GetStringField(TEXT("ChanceConditionTreeName"))) : NAME_None;
                            if (CondObj->HasField(TEXT("ProbabilityScalingAttribute")))
                            {
                                CondLayer.ProbabilityScalingAttribute = FGameplayTag::RequestGameplayTag(FName(*CondObj->GetStringField(TEXT("ProbabilityScalingAttribute"))), false);
                            }
                            CondLayer.bInvertAttributeScaling = CondObj->HasField(TEXT("bInvertAttributeScaling")) ? CondObj->GetBoolField(TEXT("bInvertAttributeScaling")) : false;
                            CondLayer.AttributeScalingWeight = CondObj->HasField(TEXT("AttributeScalingWeight")) ? static_cast<float>(CondObj->GetNumberField(TEXT("AttributeScalingWeight"))) : 1.0f;

                            Profile.ConditionalAudioLayers.Add(CondLayer);
                        }
                    }
                }

                MatrixRow.TargetMaterialProfiles.Add(InnerTag, Profile);
            }

            OutStaging.AudioMaterialMatrix.Add(OuterTag, MatrixRow);
        }
    }
    
    // ------------------------------------------------------------------------
    // SKILL TREE MASTER REGISTRY PARSING
    // ------------------------------------------------------------------------
    if (RootObject->HasField(TEXT("SkillTreeMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("SkillTreeMasterRegistry"))->Values)
        {
            FGameplayTag TreeTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!TreeTag.IsValid()) continue;

            TSharedPtr<FJsonObject> TreeObj = Pair.Value->AsObject();
            if (!TreeObj.IsValid()) continue;

            FSkillTreeDef TreeDef;
            TreeDef.TreeTag = TreeTag;
            TreeDef.ParentAttributeTag = FGameplayTag::RequestGameplayTag(FName(*TreeObj->GetStringField(TEXT("ParentAttributeTag"))), false);
            DeserializeLocalizationMap(TreeObj, TEXT("TreeLocalization"), TreeDef.TreeLocalization);

            if (TreeObj->HasField(TEXT("Nodes")))
            {
                for (const auto& NodeVal : TreeObj->GetArrayField(TEXT("Nodes")))
                {
                    TSharedPtr<FJsonObject> NObj = NodeVal->AsObject();
                    if (!NObj.IsValid()) continue;

                    FSkillTreeNodeDef NodeDef;
                    NodeDef.NodeTag = FGameplayTag::RequestGameplayTag(FName(*NObj->GetStringField(TEXT("NodeTag"))), false);
                    NodeDef.ParentAttributeTag = FGameplayTag::RequestGameplayTag(FName(*NObj->GetStringField(TEXT("ParentAttributeTag"))), false);
                    NodeDef.CurrencyAttributeTag = FGameplayTag::RequestGameplayTag(FName(*NObj->GetStringField(TEXT("CurrencyAttributeTag"))), false);
                    NodeDef.PointCostPerTier = NObj->HasField(TEXT("PointCostPerTier")) ? static_cast<float>(NObj->GetNumberField(TEXT("PointCostPerTier"))) : 1.0f;
                    NodeDef.MaxInvestableTiers = NObj->HasField(TEXT("MaxInvestableTiers")) ? static_cast<int32>(NObj->GetNumberField(TEXT("MaxInvestableTiers"))) : 1;
                    NodeDef.UnlockType = StringToEnum<ESkillNodeUnlockType>(NObj->GetStringField(TEXT("UnlockType")));

                    if (NObj->HasField(TEXT("GrantedAbilityTag")))
                    {
                        NodeDef.GrantedAbilityTag = FGameplayTag::RequestGameplayTag(FName(*NObj->GetStringField(TEXT("GrantedAbilityTag"))), false);
                    }
                    if (NObj->HasField(TEXT("PrerequisiteConditionTreeName")))
                    {
                        NodeDef.PrerequisiteConditionTreeName = FName(*NObj->GetStringField(TEXT("PrerequisiteConditionTreeName")));
                    }

                    if (NObj->HasField(TEXT("PrerequisiteNodeTags")))
                    {
                        for (const auto& PreVal : NObj->GetArrayField(TEXT("PrerequisiteNodeTags")))
                        {
                            FGameplayTag PreTag = FGameplayTag::RequestGameplayTag(FName(*PreVal->AsString()), false);
                            if (PreTag.IsValid()) NodeDef.PrerequisiteNodeTags.Add(PreTag);
                        }
                    }

                    if (NObj->HasField(TEXT("GridPosition")))
                    {
                        TSharedPtr<FJsonObject> PosObj = NObj->GetObjectField(TEXT("GridPosition"));
                        NodeDef.GridPosition.X = static_cast<float>(PosObj->GetNumberField(TEXT("X")));
                        NodeDef.GridPosition.Y = static_cast<float>(PosObj->GetNumberField(TEXT("Y")));
                    }

                    if (NObj->HasField(TEXT("IconTextures")))
                    {
                        TSharedPtr<FJsonObject> IconObj = NObj->GetObjectField(TEXT("IconTextures"));
                        NodeDef.IconTextures.LockedIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(IconObj->GetStringField(TEXT("LockedIcon"))));
                        NodeDef.IconTextures.UnselectedIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(IconObj->GetStringField(TEXT("UnselectedIcon"))));
                        NodeDef.IconTextures.SelectedIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(IconObj->GetStringField(TEXT("SelectedIcon"))));
                        NodeDef.IconTextures.ObtainedIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(IconObj->GetStringField(TEXT("ObtainedIcon"))));
                    }

                    if (NObj->HasField(TEXT("GrantedEffectTags")))
                    {
                        for (const auto& EffVal : NObj->GetArrayField(TEXT("GrantedEffectTags")))
                        {
                            FGameplayTag EffTag = FGameplayTag::RequestGameplayTag(FName(*EffVal->AsString()), false);
                            if (EffTag.IsValid()) NodeDef.GrantedEffectTags.Add(EffTag);
                        }
                    }

                    TreeDef.Nodes.Add(NodeDef);
                }
            }

            OutStaging.SkillTreeMasterRegistry.Add(TreeTag, TreeDef);
        }
    }    
    // ------------------------------------------------------------------------
    // UNIFIED MENU MASTER REGISTRY PARSING
    // ------------------------------------------------------------------------
    if (RootObject->HasField(TEXT("MenuMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("MenuMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;

            TSharedPtr<FJsonObject> MenuObj = Pair.Value->AsObject();
            if (!MenuObj.IsValid()) continue;

            FDynamicMenuDef Def;
            Def.MenuTag = KeyTag;

            if (MenuObj->HasField(TEXT("MenuStyle")))
            {
                Def.MenuStyle = StringToEnum<EDynamicMenuStyle>(MenuObj->GetStringField(TEXT("MenuStyle")));
            }

            if (MenuObj->HasField(TEXT("OpenInputActionTag")))
            {
                const FString ActionStr = MenuObj->GetStringField(TEXT("OpenInputActionTag"));
                if (!ActionStr.IsEmpty())
                {
                    Def.OpenInputActionTag = FGameplayTag::RequestGameplayTag(FName(*ActionStr), false);
                }
            }
            if (MenuObj->HasField(TEXT("CycleNextInputActionTag")))
            {
                Def.CycleNextInputActionTag = FGameplayTag::RequestGameplayTag(FName(*MenuObj->GetStringField(TEXT("CycleNextInputActionTag"))), false);
            }
            if (MenuObj->HasField(TEXT("CyclePrevInputActionTag")))
            {
                Def.CyclePrevInputActionTag = FGameplayTag::RequestGameplayTag(FName(*MenuObj->GetStringField(TEXT("CyclePrevInputActionTag"))), false);
            }

            if (MenuObj->HasField(TEXT("ButtonActionTag")))
            {
                Def.ButtonActionTagPattern = MenuObj->GetStringField(TEXT("ButtonActionTag"));
            }

            Def.PresetCount = MenuObj->HasField(TEXT("PresetCount")) ? static_cast<int32>(MenuObj->GetNumberField(TEXT("PresetCount"))) : 1;
            Def.SlotCount = MenuObj->HasField(TEXT("SlotCount")) ? static_cast<int32>(MenuObj->GetNumberField(TEXT("SlotCount"))) : 8;
            Def.bIsDynamicRuntimeGathered = MenuObj->HasField(TEXT("bIsDynamicRuntimeGathered")) ? MenuObj->GetBoolField(TEXT("bIsDynamicRuntimeGathered")) : false;
            Def.DynamicSearchRadius = MenuObj->HasField(TEXT("DynamicSearchRadius")) ? static_cast<float>(MenuObj->GetNumberField(TEXT("DynamicSearchRadius"))) : 350.0f;
            Def.AllowedPayloadTagPrefix = MenuObj->HasField(TEXT("AllowedPayloadTagPrefix")) ? MenuObj->GetStringField(TEXT("AllowedPayloadTagPrefix")) : TEXT("");

            Def.fWidth = MenuObj->HasField(TEXT("fWidth")) ? static_cast<float>(MenuObj->GetNumberField(TEXT("fWidth"))) : 100.0f;
            Def.fHeight = MenuObj->HasField(TEXT("fHeight")) ? static_cast<float>(MenuObj->GetNumberField(TEXT("fHeight"))) : 100.0f;
            Def.bCanZoom = MenuObj->HasField(TEXT("bCanZoom")) ? MenuObj->GetBoolField(TEXT("bCanZoom")) : true;

            DeserializeLocalizationMap(MenuObj, TEXT("Localization"), Def.Localization);

            OutStaging.MenuMasterRegistry.Add(KeyTag, Def);
        }
    }

    return true;
}

bool UDynamicDataSerializer::SerializeMasterRegistry(const ADynamicGameState* InGameState, FString& OutJsonString)
{
    if (!InGameState) return false;
    TSharedRef<FJsonObject> Root = MakeShareable(new FJsonObject());
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
    return FJsonSerializer::Serialize(Root, Writer);
}

int32 UDynamicDataSerializer::FlattenAndParseJsonNode(const TSharedPtr<FJsonObject>& JsonObj, FDynamicConditionTreeContainer& OutContainer)
{
    if (!JsonObj.IsValid()) return INDEX_NONE;

    // Support pre-flattened "FlatNodes" array schema (used in Conditionals.json)
    if (JsonObj->HasField(TEXT("FlatNodes")))
    {
        for (const auto& ElemVal : JsonObj->GetArrayField(TEXT("FlatNodes")))
        {
            TSharedPtr<FJsonObject> NodeObj = ElemVal->AsObject();
            if (!NodeObj.IsValid()) continue;

            FDynamicConditionNode Node;
            if (NodeObj->HasField(TEXT("ConditionType"))) Node.ConditionType = StringToEnum<EConditionType>(NodeObj->GetStringField(TEXT("ConditionType")));
            if (NodeObj->HasField(TEXT("LogicOperator"))) Node.LogicOperator = StringToEnum<ELogicOperator>(NodeObj->GetStringField(TEXT("LogicOperator")));
            if (NodeObj->HasField(TEXT("ComparisonOperator"))) Node.ComparisonOperator = StringToEnum<EComparisonOp>(NodeObj->GetStringField(TEXT("ComparisonOperator")));
            if (NodeObj->HasField(TEXT("UserDefinedKey"))) Node.UserDefinedKey = FName(*NodeObj->GetStringField(TEXT("UserDefinedKey")));
            if (NodeObj->HasField(TEXT("bCheckSource"))) Node.bCheckSource = NodeObj->GetBoolField(TEXT("bCheckSource"));
            if (NodeObj->HasField(TEXT("ComparisonValue"))) Node.ComparisonValue = static_cast<float>(NodeObj->GetNumberField(TEXT("ComparisonValue")));

            if (NodeObj->HasField(TEXT("ChildIndices")))
            {
                for (const auto& IdxVal : NodeObj->GetArrayField(TEXT("ChildIndices")))
                {
                    Node.ChildIndices.Add(static_cast<int32>(IdxVal->AsNumber()));
                }
            }
            OutContainer.FlatNodes.Add(Node);
        }
        return OutContainer.FlatNodes.Num() > 0 ? 0 : INDEX_NONE;
    }

    int32 NewNodeIndex = OutContainer.FlatNodes.AddDefaulted();
    FDynamicConditionNode& WorkingNode = OutContainer.FlatNodes[NewNodeIndex];

    if (JsonObj->HasField(TEXT("ConditionType"))) WorkingNode.ConditionType = StringToEnum<EConditionType>(JsonObj->GetStringField(TEXT("ConditionType")));
    if (JsonObj->HasField(TEXT("LogicOperator"))) WorkingNode.LogicOperator = StringToEnum<ELogicOperator>(JsonObj->GetStringField(TEXT("LogicOperator")));
    if (JsonObj->HasField(TEXT("ComparisonOperator"))) WorkingNode.ComparisonOperator = StringToEnum<EComparisonOp>(JsonObj->GetStringField(TEXT("ComparisonOperator")));
    if (JsonObj->HasField(TEXT("UserDefinedKey"))) WorkingNode.UserDefinedKey = FName(*JsonObj->GetStringField(TEXT("UserDefinedKey")));
    WorkingNode.bCheckSource = JsonObj->HasField(TEXT("bCheckSource")) ? JsonObj->GetBoolField(TEXT("bCheckSource")) : true;
    WorkingNode.ComparisonValue = JsonObj->HasField(TEXT("ComparisonValue")) ? static_cast<float>(JsonObj->GetNumberField(TEXT("ComparisonValue"))) : 0.0f;

    if (JsonObj->HasField(TEXT("ChildNodes")))
    {
        for (const auto& ElementValue : JsonObj->GetArrayField(TEXT("ChildNodes")))
        {
            int32 ChildIndex = FlattenAndParseJsonNode(ElementValue->AsObject(), OutContainer);
            if (ChildIndex != INDEX_NONE) OutContainer.FlatNodes[NewNodeIndex].ChildIndices.Add(ChildIndex);
        }
    }
    return NewNodeIndex;
}

TSharedPtr<FJsonObject> UDynamicDataSerializer::ReconstructJsonFromFlatNode(int32 NodeIndex, const FDynamicConditionTreeContainer& TreeContainer)
{
    TSharedRef<FJsonObject> JsonObj = MakeShareable(new FJsonObject());
    if (!TreeContainer.FlatNodes.IsValidIndex(NodeIndex)) return JsonObj;

    const FDynamicConditionNode& Node = TreeContainer.FlatNodes[NodeIndex];
    JsonObj->SetStringField(TEXT("ConditionType"), EnumToString<EConditionType>(Node.ConditionType));
    JsonObj->SetStringField(TEXT("LogicOperator"), EnumToString<ELogicOperator>(Node.LogicOperator));
    JsonObj->SetStringField(TEXT("ComparisonOperator"), EnumToString<EComparisonOp>(Node.ComparisonOperator));
    JsonObj->SetStringField(TEXT("UserDefinedKey"), Node.UserDefinedKey.ToString());
    JsonObj->SetBoolField(TEXT("bCheckSource"), Node.bCheckSource);
    JsonObj->SetNumberField(TEXT("ComparisonValue"), Node.ComparisonValue);

    TArray<TSharedPtr<FJsonValue>> ChildrenArr;
    for (int32 ChildIdx : Node.ChildIndices)
    {
        ChildrenArr.Add(MakeShareable(new FJsonValueObject(ReconstructJsonFromFlatNode(ChildIdx, TreeContainer))));
    }
    JsonObj->SetArrayField(TEXT("ChildNodes"), ChildrenArr);

    return JsonObj;
}

bool UDynamicDataSerializer::SerializeFastAttributes(const FFastAttributeArray& InFastAttributes, FString& OutJsonString)
{
    TSharedRef<FJsonObject> RootObject = MakeShareable(new FJsonObject());
    TSharedPtr<FJsonObject> ArrayObject = MakeShareable(new FJsonObject());

    for (const FDynamicAttributeItem& Entry : InFastAttributes.Items)
    {
        if (Entry.AttributeTag.IsValid())
        {
            ArrayObject->SetNumberField(Entry.AttributeTag.ToString(), Entry.Value);
        }
    }

    RootObject->SetObjectField(TEXT("FastAttributes"), ArrayObject);
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
    return FJsonSerializer::Serialize(RootObject, Writer);
}

bool UDynamicDataSerializer::DeserializeFastAttributes(const FString& InJsonString, FFastAttributeArray& OutFastAttributes)
{
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJsonString);
    TSharedPtr<FJsonObject> RootObject;
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid()) return false;

    if (RootObject->HasField(TEXT("FastAttributes")))
    {
        OutFastAttributes.Items.Empty();
        TSharedPtr<FJsonObject> AttrMapObj = RootObject->GetObjectField(TEXT("FastAttributes"));

        for (auto& Pair : AttrMapObj->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (KeyTag.IsValid())
            {
                FDynamicAttributeItem NewEntry;
                NewEntry.AttributeTag = KeyTag;
                NewEntry.Value = static_cast<float>(Pair.Value->AsNumber());
                OutFastAttributes.Items.Add(NewEntry);
            }
        }
        OutFastAttributes.MarkArrayDirty();
        return true;
    }
    return false;
}

bool UDynamicDataSerializer::SerializeContainerSlots(const FFastContainerSlotArray& InSlots, FString& OutJsonString)
{
    TSharedRef<FJsonObject> RootObj = MakeShareable(new FJsonObject());
    TArray<TSharedPtr<FJsonValue>> SlotsArr;

    for (const FDynamicContainerSlotItem& Item : InSlots.Items)
    {
        if (Item.StackCount <= 0 || !Item.StoredActorArchetypeTag.IsValid()) continue;
        TSharedPtr<FJsonObject> SlotObj = FJsonObjectConverter::UStructToJsonObject(Item);
        if (SlotObj.IsValid())
        {
            SlotsArr.Add(MakeShareable(new FJsonValueObject(SlotObj)));
        }
    }

    RootObj->SetArrayField(TEXT("ContainerSlots"), SlotsArr);
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
    return FJsonSerializer::Serialize(RootObj, Writer);
}

bool UDynamicDataSerializer::DeserializeContainerSlots(const FString& InJsonString, FFastContainerSlotArray& OutSlots)
{
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJsonString);
    TSharedPtr<FJsonObject> RootObj;
    if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid()) return false;

    if (RootObj->HasField(TEXT("ContainerSlots")))
    {
        OutSlots.Items.Empty();
        const TArray<TSharedPtr<FJsonValue>>& SlotsArr = RootObj->GetArrayField(TEXT("ContainerSlots"));
        for (const TSharedPtr<FJsonValue>& Val : SlotsArr)
        {
            FDynamicContainerSlotItem Item;
            if (FJsonObjectConverter::JsonObjectToUStruct(Val->AsObject().ToSharedRef(), &Item, 0, 0))
            {
                OutSlots.Items.Add(Item);
            }
        }
        OutSlots.MarkArrayDirty();
        return true;
    }
    return false;
}

// Helper function to restore container inventory slots on a deserialized dynamic physics actor
static void RestoreDraggedActorContainers(ADynamicPhysicsActor* SpawnedActor, const TArray<FDynamicContainerSlotItem>& SavedInventorySlots)
{
    if (!SpawnedActor || SavedInventorySlots.Num() == 0) return;

    UDynamicContainerComponent* PrimaryContainer = SpawnedActor->GetContainerComponent();
    if (PrimaryContainer)
    {
        PrimaryContainer->GetMutableReplicatedSlots().Items = SavedInventorySlots;
        PrimaryContainer->GetMutableReplicatedSlots().MarkArrayDirty();
        PrimaryContainer->OnRep_ReplicatedSlots();
        return;
    }

    TArray<UDynamicContainerComponent*> AllContainers;
    SpawnedActor->GetComponents<UDynamicContainerComponent>(AllContainers);

    if (AllContainers.Num() > 0)
    {
        AllContainers[0]->GetMutableReplicatedSlots().Items = SavedInventorySlots;
        AllContainers[0]->GetMutableReplicatedSlots().MarkArrayDirty();
        AllContainers[0]->OnRep_ReplicatedSlots();
    }
    else
    {
        UDynamicContainerComponent* NewContainer = NewObject<UDynamicContainerComponent>(SpawnedActor);
        NewContainer->SetIsReplicated(true);
        NewContainer->RegisterComponent();
        SpawnedActor->AddInstanceComponent(NewContainer);

        NewContainer->GetMutableReplicatedSlots().Items = SavedInventorySlots;
        NewContainer->GetMutableReplicatedSlots().MarkArrayDirty();
        NewContainer->OnRep_ReplicatedSlots();
    }
}

bool UDynamicDataSerializer::ParseSnapshotFromJsonString(const FString& InJsonPayload, FPlayerPersistenceSnapshot& OutSnapshot)
{
    if (InJsonPayload.IsEmpty()) return false;

    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJsonPayload);
    TSharedPtr<FJsonObject> RootObj;
    if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid()) return false;

    return FJsonObjectConverter::JsonObjectToUStruct(RootObj.ToSharedRef(), &OutSnapshot, 0, 0);
}

bool UDynamicDataSerializer::ApplySnapshotToPlayerCharacter(ADynamicCharacter* TargetCharacter, const FPlayerPersistenceSnapshot& Snapshot)
{
    check(IsInGameThread());
    if (!TargetCharacter || !TargetCharacter->HasAuthority()) return false;

    // 1. Identity & Mesh
    if (UDynamicCustomizationComponent* Cust = TargetCharacter->GetCustomizationComponent())
    {
        Cust->InitializeCustomization(Snapshot.CharacterTemplateTag, Snapshot.Gender);
        Cust->SkinColor = Snapshot.SkinColor;
        Cust->HairColor = Snapshot.HairColor;
        Cust->ActiveHairArchetypeTag = Snapshot.ActiveHairArchetypeTag;
        Cust->OnRep_AppearanceColors();

        for (const FTagFloatPair& Slider : Snapshot.MorphSliders)
        {
            Cust->ServerSetMorphSliderValue(Slider.KeyTag, Slider.Value);
        }
    }

    // 2. Attributes
    if (UDynamicAttributeSet* AttrSet = TargetCharacter->GetDynamicAttributeSet())
    {
        for (const FDynamicAttributeItem& SavedAttr : Snapshot.Attributes)
        {
            AttrSet->SetDynamicAttributeValue(SavedAttr.AttributeTag, SavedAttr.Value, SavedAttr.LimbId);
        }
    }

    // 3. Genetics
    if (UDynamicLifeComponent* Life = TargetCharacter->FindComponentByClass<UDynamicLifeComponent>())
    {
        Life->ReplicatedGenome.Items = Snapshot.Genome;
        Life->ReplicatedGenome.MarkArrayDirty();
        Life->OnRep_Genome();
    }

    // 4. Equipment
    if (UDynamicEquipmentComponent* Equip = TargetCharacter->FindComponentByClass<UDynamicEquipmentComponent>())
    {
        Equip->ReplicatedEquipmentSlots.Items = Snapshot.Equipment;
        Equip->ReplicatedEquipmentSlots.MarkArrayDirty();
        Equip->OnRep_ReplicatedEquipmentSlots();
    }

    // 5. Container Inventory
    if (UDynamicContainerComponent* Container = TargetCharacter->FindComponentByClass<UDynamicContainerComponent>())
    {
        Container->GetMutableReplicatedSlots().Items = Snapshot.InventorySlots;
        Container->GetMutableReplicatedSlots().MarkArrayDirty();
        Container->OnRep_ReplicatedSlots();
    }

    // 6. Quests
    if (UDynamicQuestComponent* QuestComp = TargetCharacter->FindComponentByClass<UDynamicQuestComponent>())
    {
        QuestComp->ReplicatedActiveQuests.Items = Snapshot.ActiveQuests;
        QuestComp->ReplicatedActiveQuests.MarkArrayDirty();
        QuestComp->ReplicatedActiveObjectives.Items = Snapshot.ActiveObjectives;
        QuestComp->ReplicatedActiveObjectives.MarkArrayDirty();
    }

    // 7. Language
    if (UDynamicLanguageComponent* Lang = TargetCharacter->FindComponentByClass<UDynamicLanguageComponent>())
    {
        Lang->ReplicatedKnownWords.Items = Snapshot.KnownWords;
        Lang->ReplicatedKnownWords.MarkArrayDirty();
        Lang->RebuildKnownWordCache();
    }

    // 8. Social Graph, Needs, Locations, and O(1) GameState GUID Re-Indexing
    if (UDynamicSocialComponent* Social = TargetCharacter->FindComponentByClass<UDynamicSocialComponent>())
    {
        Social->SetCharacterPersistentGuid(Snapshot.CharacterGuid.IsValid() ? Snapshot.CharacterGuid : FGuid::NewGuid());

        Social->ReplicatedRelationships.Items = Snapshot.Relationships;
        Social->ReplicatedRelationships.MarkArrayDirty();
        Social->RebuildRelationshipCache();

        Social->ReplicatedNeedsAndMoods.Items = Snapshot.Needs;
        Social->ReplicatedNeedsAndMoods.MarkArrayDirty();
        Social->RebuildNeedCache();

        Social->ReplicatedKnownPlaces.Items = Snapshot.KnownPlaces;
        Social->ReplicatedKnownPlaces.MarkArrayDirty();
        Social->RebuildPlaceCache();
    }

    // 9. Re-spawning & Re-hydrating Dragged Physics Actors (Time-Sliced)
    if (Snapshot.DraggedPhysicsActors.Num() > 0)
    {
        TSharedRef<TArray<FDraggedPhysicsActorSnapshot>> PendingSpawns = MakeShared<TArray<FDraggedPhysicsActorSnapshot>>(Snapshot.DraggedPhysicsActors);
        TWeakObjectPtr<ADynamicCharacter> WeakChar(TargetCharacter);
        
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([PendingSpawns, WeakChar](float DeltaTime) -> bool
        {
            if (!WeakChar.IsValid() || PendingSpawns->Num() == 0) return false;
            
            ADynamicCharacter* TargetChar = WeakChar.Get();
            ADynamicGameState* GS = TargetChar->GetWorld() ? TargetChar->GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
            if (!GS) return false;

            // OPTIMIZATION: Process a maximum of 2 physical spawns per tick to guarantee stable server frame rates
            int32 SpawnsProcessedThisFrame = 0;
            while (PendingSpawns->Num() > 0 && SpawnsProcessedThisFrame < 2)
            {
                FDraggedPhysicsActorSnapshot ActorSnap = PendingSpawns->Pop();
                if (ActorSnap.ActorArchetypeTag.IsValid())
                {
                    ADynamicPhysicsActor* SpawnedActor = GS->AcquirePhysicsActor(ActorSnap.ActorArchetypeTag, ActorSnap.WorldTransform, TargetChar, TargetChar);
                    if (SpawnedActor)
                    {
                        if (UDynamicAttributeSet* AttrSet = SpawnedActor->GetDynamicAttributeSet())
                        {
                            for (const FDynamicAttributeItem& Attr : ActorSnap.SavedAttributes)
                            {
                                if (Attr.AttributeTag.IsValid())
                                {
                                    AttrSet->SetDynamicAttributeValue(Attr.AttributeTag, Attr.Value, Attr.LimbId);
                                }
                            }
                        }

                        if (UDynamicInteractableComponent* Interactable = SpawnedActor->GetInteractableComponent())
                        {
                            for (const FGameplayTag& SavedStateTag : ActorSnap.SavedInteractableStates)
                            {
                                Interactable->AddStateTag(SavedStateTag);
                            }
                        }

                        RestoreDraggedActorContainers(SpawnedActor, ActorSnap.ContainerInventory);

                        if (UPrimitiveComponent* Prim = SpawnedActor->GetActivePhysicsPrimitive())
                        {
                            Prim->SetSimulatePhysics(true);
                            Prim->SetPhysicsLinearVelocity(ActorSnap.LinearVelocity);
                            Prim->SetPhysicsAngularVelocityInDegrees(ActorSnap.AngularVelocity);
                        }
                    }
                }
                SpawnsProcessedThisFrame++;
            }
            
            return PendingSpawns->Num() > 0;
        }));
    }

    return true;
}

bool UDynamicDataSerializer::SerializePlayerToSnapshotJson(const ADynamicCharacter* InCharacter, FString& OutJsonPayload)
{
    if (!InCharacter || !InCharacter->HasAuthority()) return false;

    FPlayerPersistenceSnapshot Snapshot;

    // 1. Identity & Customization
    if (const UDynamicCustomizationComponent* Cust = InCharacter->GetCustomizationComponent())
    {
        Snapshot.CharacterTemplateTag = Cust->ActiveCharacterTemplateTag;
        Snapshot.Gender = Cust->ActiveGender;
        Snapshot.SkinColor = Cust->SkinColor;
        Snapshot.HairColor = Cust->HairColor;
        Snapshot.ActiveHairArchetypeTag = Cust->ActiveHairArchetypeTag;

        for (const FDynamicCharacterMorphItem& Morph : Cust->ReplicatedMorphs.Items)
        {
            FTagFloatPair Pair;
            Pair.KeyTag = Morph.SliderTag;
            Pair.Value = Morph.SliderValue;
            Snapshot.MorphSliders.Add(Pair);
        }
    }

    // 2. Attributes
    if (const UDynamicAttributeSet* AttrSet = InCharacter->GetDynamicAttributeSet())
    {
        Snapshot.Attributes = AttrSet->FastAttributes.Items;
    }

    // 3. Genetics & Aging
    if (const UDynamicLifeComponent* Life = InCharacter->FindComponentByClass<UDynamicLifeComponent>())
    {
        Snapshot.Genome = Life->ReplicatedGenome.Items;
    }

    // 4. Equipment
    if (const UDynamicEquipmentComponent* Equip = InCharacter->FindComponentByClass<UDynamicEquipmentComponent>())
    {
        Snapshot.Equipment = Equip->ReplicatedEquipmentSlots.Items;
    }

    // 5. Container Inventory
    if (const UDynamicContainerComponent* Container = InCharacter->FindComponentByClass<UDynamicContainerComponent>())
    {
        Snapshot.InventorySlots = Container->GetReplicatedSlots().Items;
    }

    // 6. Quests
    if (const UDynamicQuestComponent* QuestComp = InCharacter->FindComponentByClass<UDynamicQuestComponent>())
    {
        Snapshot.ActiveQuests = QuestComp->ReplicatedActiveQuests.Items;
        Snapshot.ActiveObjectives = QuestComp->ReplicatedActiveObjectives.Items;
    }

    // 7. Language
    if (const UDynamicLanguageComponent* Lang = InCharacter->FindComponentByClass<UDynamicLanguageComponent>())
    {
        Snapshot.KnownWords = Lang->ReplicatedKnownWords.Items;
    }

    // 8. Social, Needs, Memory
    if (const UDynamicSocialComponent* Social = InCharacter->FindComponentByClass<UDynamicSocialComponent>())
    {
        Snapshot.CharacterGuid = Social->CharacterPersistentGuid;
        Snapshot.Relationships = Social->ReplicatedRelationships.Items;
        Snapshot.Needs = Social->ReplicatedNeedsAndMoods.Items;
        Snapshot.KnownPlaces = Social->ReplicatedKnownPlaces.Items;
    }

    // 9. Serializing Dragged / Tethered World Physics Actors without TActorIterator
    UWorld* World = InCharacter->GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    TSet<ADynamicPhysicsActor*> CandidatePhysicsActors;

    TArray<AActor*> AttachedActors;
    InCharacter->GetAttachedActors(AttachedActors, true);
    for (AActor* Attached : AttachedActors)
    {
        if (ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(Attached))
        {
            if (!PhysActor->IsPendingKillPending())
            {
                CandidatePhysicsActors.Add(PhysActor);
            }
        }
    }

    if (World)
    {
        TArray<FOverlapResult> Overlaps;
        FCollisionObjectQueryParams ObjParams;
        ObjParams.AddObjectTypesToQuery(ECC_PhysicsBody);
        ObjParams.AddObjectTypesToQuery(ECC_WorldDynamic);
        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SerializeDraggedActorsOverlap), false, InCharacter);

        if (World->OverlapMultiByObjectType(Overlaps, InCharacter->GetActorLocation(), FQuat::Identity, ObjParams, FCollisionShape::MakeSphere(600.0f), QueryParams))
        {
            for (const FOverlapResult& Overlap : Overlaps)
            {
                if (ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(Overlap.GetActor()))
                {
                    if (!PhysActor->IsPendingKillPending())
                    {
                        CandidatePhysicsActors.Add(PhysActor);
                    }
                }
            }
        }
    }

    for (ADynamicPhysicsActor* PhysActor : CandidatePhysicsActors)
    {
        FDraggedPhysicsActorSnapshot ActorSnapshot;
        ActorSnapshot.ActorArchetypeTag = PhysActor->GetActorArchetypeTag();
        ActorSnapshot.WorldTransform = PhysActor->GetActorTransform();

        if (UPrimitiveComponent* Prim = PhysActor->GetActivePhysicsPrimitive())
        {
            ActorSnapshot.LinearVelocity = Prim->GetPhysicsLinearVelocity();
            ActorSnapshot.AngularVelocity = Prim->GetPhysicsAngularVelocityInDegrees();
        }
        if (const UDynamicAttributeSet* AttrSet = PhysActor->GetDynamicAttributeSet())
        {
            ActorSnapshot.SavedAttributes = AttrSet->FastAttributes.Items;
        }
        if (const UDynamicInteractableComponent* Interactable = PhysActor->GetInteractableComponent())
        {
            ActorSnapshot.SavedInteractableStates = Interactable->GetCurrentStateTags();
        }
        if (const UDynamicContainerComponent* Container = PhysActor->GetContainerComponent())
        {
            ActorSnapshot.ContainerInventory = Container->GetReplicatedSlots().Items;
        }

        Snapshot.DraggedPhysicsActors.Add(ActorSnapshot);

        if (GS)
        {
            GS->ReleasePhysicsActorToPool(PhysActor);
        }
    }

    TSharedPtr<FJsonObject> JsonObj = FJsonObjectConverter::UStructToJsonObject(Snapshot);
    if (!JsonObj.IsValid()) return false;

    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonPayload);
    return FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
}

bool UDynamicDataSerializer::DeserializePlayerFromSnapshotJson(ADynamicCharacter* TargetCharacter, const FString& InJsonPayload)
{
    FPlayerPersistenceSnapshot Snapshot;
    if (!ParseSnapshotFromJsonString(InJsonPayload, Snapshot))
    {
        return false;
    }
    return ApplySnapshotToPlayerCharacter(TargetCharacter, Snapshot);
}

void UDynamicDataSerializer::DeserializePlayerFromSnapshotJsonAsync(
    ADynamicCharacter* TargetCharacter, 
    const FString& InJsonPayload, 
    FOnPlayerHydrationCompletedSignature OnComplete)
{
    DeserializePlayerFromSnapshotJsonAsyncNative(TargetCharacter, InJsonPayload, [OnComplete](bool bSuccess)
    {
        OnComplete.ExecuteIfBound(bSuccess);
    });
}

void UDynamicDataSerializer::DeserializePlayerFromSnapshotJsonAsyncNative(
    ADynamicCharacter* TargetCharacter, 
    const FString& InJsonPayload, 
    TFunction<void(bool)> OnComplete)
{
    if (!TargetCharacter || InJsonPayload.IsEmpty())
    {
        if (OnComplete) OnComplete(false);
        return;
    }

    TWeakObjectPtr<ADynamicCharacter> WeakChar(TargetCharacter);

    // 1. Offload heavy JSON string-to-struct parsing to worker thread
    Async(EAsyncExecution::ThreadPool, [WeakChar, InJsonPayload, OnComplete]()
    {
        FPlayerPersistenceSnapshot Snapshot;
        const bool bParsed = ParseSnapshotFromJsonString(InJsonPayload, Snapshot);

        // 2. Dispatch component hydration back onto the GameThread
        Async(EAsyncExecution::TaskGraphMainThread, [WeakChar, Snapshot = MoveTemp(Snapshot), bParsed, OnComplete]() mutable
        {
            bool bApplied = false;
            if (bParsed && WeakChar.IsValid())
            {
                bApplied = ApplySnapshotToPlayerCharacter(WeakChar.Get(), Snapshot);
            }
            if (OnComplete)
            {
                OnComplete(bApplied);
            }
        });
    });
}

void UDynamicDataSerializer::DeserializeMovementStatConfig(const TSharedPtr<FJsonObject>& InObject, const FString& FieldName, FMovementStatConfig& OutConfig)
{
    if (!InObject.IsValid() || !InObject->HasField(FieldName)) return;
    TSharedPtr<FJsonObject> StatObj = InObject->GetObjectField(FieldName);
    if (!StatObj.IsValid()) return;

    OutConfig.BaseValue = StatObj->HasField(TEXT("BaseValue")) ? static_cast<float>(StatObj->GetNumberField(TEXT("BaseValue"))) : 0.0f;
    DeserializeMapWrapper(StatObj, TEXT("AttributeScalars"), OutConfig.AttributeScalars);
}

void UDynamicDataSerializer::DeserializeImpactReactionRule(const TSharedPtr<FJsonObject>& InObject, FDynamicImpactReactionRule& OutRule)
{
    if (!InObject.IsValid()) return;

    OutRule.BaseImpactVelocityThreshold = InObject->HasField(TEXT("BaseImpactVelocityThreshold")) ? static_cast<float>(InObject->GetNumberField(TEXT("BaseImpactVelocityThreshold"))) : 750.0f;
    OutRule.BaseStabilityLossThreshold = InObject->HasField(TEXT("BaseStabilityLossThreshold")) ? static_cast<float>(InObject->GetNumberField(TEXT("BaseStabilityLossThreshold"))) : 30.0f;
    OutRule.MassImpactMultiplier = InObject->HasField(TEXT("MassImpactMultiplier")) ? static_cast<float>(InObject->GetNumberField(TEXT("MassImpactMultiplier"))) : 0.015f;
    OutRule.BaseRecoilMultiplier = InObject->HasField(TEXT("BaseRecoilMultiplier")) ? static_cast<float>(InObject->GetNumberField(TEXT("BaseRecoilMultiplier"))) : 1.0f;
    OutRule.bTriggersRagdollOnInstability = InObject->HasField(TEXT("bTriggersRagdollOnInstability")) ? InObject->GetBoolField(TEXT("bTriggersRagdollOnInstability")) : true;

    DeserializeMapWrapper(InObject, TEXT("StaggerResistanceScalars"), OutRule.StaggerResistanceScalars);
    DeserializeMapWrapper(InObject, TEXT("VelocityMitigationScalars"), OutRule.VelocityMitigationScalars);
    DeserializeMapWrapper(InObject, TEXT("DamageMitigationScalars"), OutRule.DamageMitigationScalars);
    DeserializeMapWrapper(InObject, TEXT("StabilityRetentionScalars"), OutRule.StabilityRetentionScalars);

    if (InObject->HasField(TEXT("Consequences")))
    {
        for (const auto& ConseqVal : InObject->GetArrayField(TEXT("Consequences")))
        {
            TSharedPtr<FJsonObject> ConseqObj = ConseqVal->AsObject();
            if (!ConseqObj.IsValid()) continue;

            FDynamicImpactConsequence Conseq;
            Conseq.TargetAttributeTag = FGameplayTag::RequestGameplayTag(FName(*ConseqObj->GetStringField(TEXT("TargetAttributeTag"))), false);
            Conseq.DamageScalar = ConseqObj->HasField(TEXT("DamageScalar")) ? static_cast<float>(ConseqObj->GetNumberField(TEXT("DamageScalar"))) : 1.0f;
            Conseq.bIsLimbSpecific = ConseqObj->HasField(TEXT("bIsLimbSpecific")) ? ConseqObj->GetBoolField(TEXT("bIsLimbSpecific")) : false;
            Conseq.StatusEffectThreshold = ConseqObj->HasField(TEXT("StatusEffectThreshold")) ? static_cast<float>(ConseqObj->GetNumberField(TEXT("StatusEffectThreshold"))) : 50.0f;

            if (ConseqObj->HasField(TEXT("StatusEffectsToApply")))
            {
                for (const auto& EffectVal : ConseqObj->GetArrayField(TEXT("StatusEffectsToApply")))
                {
                    FGameplayTag EffectTag = FGameplayTag::RequestGameplayTag(FName(*EffectVal->AsString()), false);
                    if (EffectTag.IsValid()) Conseq.StatusEffectsToApply.Add(EffectTag);
                }
            }
            OutRule.Consequences.Add(Conseq);
        }
    }

    if (InObject->HasField(TEXT("OverpowerRules")))
    {
        for (const auto& RuleVal : InObject->GetArrayField(TEXT("OverpowerRules")))
        {
            TSharedPtr<FJsonObject> RuleObj = RuleVal->AsObject();
            if (!RuleObj.IsValid()) continue;

            FDynamicOverpowerRule OverpowerRule;
            OverpowerRule.BasePenetrationThreshold = RuleObj->HasField(TEXT("BasePenetrationThreshold")) ? static_cast<float>(RuleObj->GetNumberField(TEXT("BasePenetrationThreshold"))) : 5000.0f;
            OverpowerRule.bIgnoreCollisionOnOverpower = RuleObj->HasField(TEXT("bIgnoreCollisionOnOverpower")) ? RuleObj->GetBoolField(TEXT("bIgnoreCollisionOnOverpower")) : false;
            
            // NEW PIERCING RULES
            OverpowerRule.bIsPiercing = RuleObj->HasField(TEXT("bIsPiercing")) ? RuleObj->GetBoolField(TEXT("bIsPiercing")) : false;
            OverpowerRule.MaxPiercingDepth = RuleObj->HasField(TEXT("MaxPiercingDepth")) ? static_cast<float>(RuleObj->GetNumberField(TEXT("MaxPiercingDepth"))) : 45.0f;
            
            OverpowerRule.IgnoreCollisionDuration = RuleObj->HasField(TEXT("IgnoreCollisionDuration")) ? static_cast<float>(RuleObj->GetNumberField(TEXT("IgnoreCollisionDuration"))) : 0.45f;
            OverpowerRule.bHyperextendLimbOnOverpower = RuleObj->HasField(TEXT("bHyperextendLimbOnOverpower")) ? RuleObj->GetBoolField(TEXT("bHyperextendLimbOnOverpower")) : false;
            OverpowerRule.bForceRagdollOnOverpower = RuleObj->HasField(TEXT("bForceRagdollOnOverpower")) ? RuleObj->GetBoolField(TEXT("bForceRagdollOnOverpower")) : false;

            DeserializeMapWrapper(RuleObj, TEXT("ResistanceAttributeScalars"), OverpowerRule.ResistanceAttributeScalars);

            if (RuleObj->HasField(TEXT("StatusEffectsToApply")))
            {
                for (const auto& EffectVal : RuleObj->GetArrayField(TEXT("StatusEffectsToApply")))
                {
                    FGameplayTag EffectTag = FGameplayTag::RequestGameplayTag(FName(*EffectVal->AsString()), false);
                    if (EffectTag.IsValid()) OverpowerRule.StatusEffectsToApply.Add(EffectTag);
                }
            }
            OutRule.OverpowerRules.Add(OverpowerRule);
        }
    }
}

static void DeserializeDialogueUtteranceArray(const TSharedPtr<FJsonObject>& ParentObj, const FString& FieldName, TArray<FDynamicDialogueUtteranceDef>& OutUtterances)
{
    OutUtterances.Empty();
    if (!ParentObj.IsValid() || !ParentObj->HasField(FieldName)) return;

    for (const auto& Val : ParentObj->GetArrayField(FieldName))
    {
        TSharedPtr<FJsonObject> EntryObj = Val->AsObject();
        if (!EntryObj.IsValid()) continue;

        FDynamicDialogueUtteranceDef Def;
        Def.UtteranceText = EntryObj->GetStringField(TEXT("UtteranceText"));
        Def.SelectionWeight = EntryObj->HasField(TEXT("SelectionWeight")) ? static_cast<float>(EntryObj->GetNumberField(TEXT("SelectionWeight"))) : 1.0f;

        if (EntryObj->HasField(TEXT("VoiceCueTag")))
        {
            Def.VoiceCueTag = FGameplayTag::RequestGameplayTag(FName(*EntryObj->GetStringField(TEXT("VoiceCueTag"))), false);
        }
        if (EntryObj->HasField(TEXT("EmotionTag")))
        {
            Def.EmotionTag = FGameplayTag::RequestGameplayTag(FName(*EntryObj->GetStringField(TEXT("EmotionTag"))), false);
        }

        // Attribute Requirements (Min/Max thresholds)
        if (EntryObj->HasField(TEXT("AttributeRequirements")))
        {
            for (const auto& ReqVal : EntryObj->GetArrayField(TEXT("AttributeRequirements")))
            {
                TSharedPtr<FJsonObject> ReqObj = ReqVal->AsObject();
                if (!ReqObj.IsValid()) continue;

                FCharacterAttributeRangeDef Range;
                Range.AttributeTag = FGameplayTag::RequestGameplayTag(FName(*ReqObj->GetStringField(TEXT("AttributeTag"))), false);
                Range.MinValue = static_cast<float>(ReqObj->GetNumberField(TEXT("MinValue")));
                Range.MaxValue = static_cast<float>(ReqObj->GetNumberField(TEXT("MaxValue")));
                Def.AttributeRequirements.Add(Range);
            }
        }

        // Probability Scalars
        UDynamicDataSerializer::DeserializeMapWrapper(EntryObj, TEXT("ProbabilityAttributeScalars"), Def.ProbabilityAttributeScalars);

        OutUtterances.Add(Def);
    }
}

bool UDynamicDataSerializer::SerializeWorldStateToJson(const UObject* WorldContextObject, FString& OutJsonPayload)
{
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (!World || World->GetNetMode() == NM_Client) return false;

    FWorldPersistenceSnapshot WorldSnap;
    WorldSnap.WorldTime = World->GetTimeSeconds();

    if (UDynamicVoxelSubsystem* VoxelSub = World->GetSubsystem<UDynamicVoxelSubsystem>())
    {
        VoxelSub->SerializeWorldVoxelState(WorldSnap);
    }
    
    if (UDynamicBuildingSubsystem* BuildSub = World->GetSubsystem<UDynamicBuildingSubsystem>())
    {
        BuildSub->SerializeWorldBuildingState(WorldSnap);
    }

    // Persist all dynamically coined vocabulary words
    if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
    {
        WorldSnap.CoinedVocabulary.Empty(GS->ReplicatedCoinedWords.Items.Num());
        for (const FDynamicCoinedWordItem& Item : GS->ReplicatedCoinedWords.Items)
        {
            FDynamicCoinedWordSnapshot Snap;
            Snap.LanguageTag = Item.LanguageTag;
            Snap.EnglishWord = Item.EnglishWord;
            Snap.CoinedNativeWord = Item.CoinedNativeWord;
            Snap.DifficultyRating = Item.DifficultyRating;
            Snap.ComprehensionXPGranted = Item.ComprehensionXPGranted;
            Snap.DecoyOptions = Item.DecoyOptions;
            WorldSnap.CoinedVocabulary.Add(Snap);
        }
    }

    TSharedPtr<FJsonObject> JsonObj = FJsonObjectConverter::UStructToJsonObject(WorldSnap);
    if (!JsonObj.IsValid()) return false;

    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonPayload);
    return FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
}

bool UDynamicDataSerializer::DeserializeWorldStateFromJson(const UObject* WorldContextObject, const FString& InJsonPayload)
{
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (!World || World->GetNetMode() == NM_Client || InJsonPayload.IsEmpty()) return false;

    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJsonPayload);
    TSharedPtr<FJsonObject> RootObj;
    if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid()) return false;

    FWorldPersistenceSnapshot WorldSnap;
    if (!FJsonObjectConverter::JsonObjectToUStruct(RootObj.ToSharedRef(), &WorldSnap, 0, 0)) return false;

    if (UDynamicVoxelSubsystem* VoxelSub = World->GetSubsystem<UDynamicVoxelSubsystem>())
    {
        VoxelSub->RestoreWorldVoxelState(WorldSnap);
    }
    
    if (UDynamicBuildingSubsystem* BuildSub = World->GetSubsystem<UDynamicBuildingSubsystem>())
    {
        BuildSub->RestoreWorldBuildingState(WorldSnap);
    }

    // Restore dynamically coined vocabulary back into the Language Master Registry and FastArray
    if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
    {
        for (const FDynamicCoinedWordSnapshot& CoinedSnap : WorldSnap.CoinedVocabulary)
        {
            FDynamicCoinedWordItem NewItem;
            NewItem.LanguageTag = CoinedSnap.LanguageTag;
            NewItem.EnglishWord = CoinedSnap.EnglishWord;
            NewItem.CoinedNativeWord = CoinedSnap.CoinedNativeWord;
            NewItem.DifficultyRating = CoinedSnap.DifficultyRating;
            NewItem.ComprehensionXPGranted = CoinedSnap.ComprehensionXPGranted;
            NewItem.DecoyOptions = CoinedSnap.DecoyOptions;

            GS->HandleSingleCoinedWordReplicated(NewItem);

            if (GS->HasAuthority())
            {
                FDynamicCoinedWordItem& AddedRef = GS->ReplicatedCoinedWords.Items.Add_GetRef(NewItem);
                GS->ReplicatedCoinedWords.MarkItemDirty(AddedRef);
            }
        }
        GS->RebuildLanguageLookupCaches();
    }

    return true;
}

void UDynamicDataSerializer::DeserializeTagToTagMapWrapper(const TSharedPtr<FJsonObject>& InObject, const FString& FieldName, FDynamicTagToTagMapWrapper& OutWrapper)
{
    OutWrapper.ReplicatedEntries.Empty();
    if (!InObject.IsValid() || !InObject->HasField(FieldName)) return;
    
    TSharedPtr<FJsonObject> MapSection = InObject->GetObjectField(FieldName);
    if (!MapSection.IsValid()) return;

    if (MapSection->HasField(TEXT("ReplicatedEntries")))
    {
        const TArray<TSharedPtr<FJsonValue>>& ArrayField = MapSection->GetArrayField(TEXT("ReplicatedEntries"));
        for (const auto& Element : ArrayField)
        {
            TSharedPtr<FJsonObject> EntryObj = Element->AsObject();
            if (EntryObj.IsValid())
            {
                FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*EntryObj->GetStringField(TEXT("KeyTag"))), false);
                FGameplayTag ValTag = FGameplayTag::RequestGameplayTag(FName(*EntryObj->GetStringField(TEXT("ValueTag"))), false);
                
                if (KeyTag.IsValid() && ValTag.IsValid())
                {
                    FTagTagPair Entry;
                    Entry.KeyTag = KeyTag;
                    Entry.ValueTag = ValTag;
                    OutWrapper.ReplicatedEntries.Add(Entry);
                }
            }
        }
    }
    OutWrapper.SyncMapFromChangesArray();
}