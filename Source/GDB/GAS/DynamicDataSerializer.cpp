// DynamicDataSerializer.cpp
#include "DynamicDataSerializer.h"
#include "DynamicCharacter.h"
#include "DynamicPhysicsActor.h"
#include "EngineUtils.h"
#include "DynamicCustomizationComponent.h"
#include "DynamicAttributeSet.h"
#include "DynamicEquipmentComponent.h"
#include "DynamicContainerComponent.h"
#include "DynamicInteractableComponent.h"
#include "DynamicLifeComponent.h"
#include "DynamicSocialComponent.h"
#include "DynamicLanguageComponent.h"
#include "DynamicQuestComponent.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "DynamicGameState.h"
#include "Async/Async.h"

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

            if (EntryObj->HasField(TEXT("VoiceCueTag")))
            {
                Def.VoiceCueTag = FGameplayTag::RequestGameplayTag(FName(*EntryObj->GetStringField(TEXT("VoiceCueTag"))), false);
            }
            if (EntryObj->HasField(TEXT("EmotionTag")))
            {
                Def.EmotionTag = FGameplayTag::RequestGameplayTag(FName(*EntryObj->GetStringField(TEXT("EmotionTag"))), false);
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

            DeserializeMapWrapper(EntryObj, TEXT("ProbabilityAttributeScalars"), Def.ProbabilityAttributeScalars);
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
                DeserializeMapWrapper(AbilityObj->GetObjectField(TEXT("SpatialDelivery")), TEXT("RuntimeMetricModifiers"), AbilityDef.SpatialDelivery.RuntimeMetricModifiers);
            if (AbilityObj->HasField(TEXT("InteractionConfig")))
                DeserializeMapWrapper(AbilityObj->GetObjectField(TEXT("InteractionConfig")), TEXT("InteractionMetrics"), AbilityDef.InteractionConfig.InteractionMetrics);
            if (AbilityObj->HasField(TEXT("MovementConfig")))
            {
                TSharedPtr<FJsonObject> MovObj = AbilityObj->GetObjectField(TEXT("MovementConfig"));
                DeserializeMapWrapper(MovObj, TEXT("MovementMetrics"), AbilityDef.MovementConfig.MovementMetrics);
                if (MovObj->HasField(TEXT("GaspMotorSettings")))
                    DeserializeMapWrapper(MovObj->GetObjectField(TEXT("GaspMotorSettings")), TEXT("RequiredAttributeThresholds"), AbilityDef.MovementConfig.GaspMotorSettings.RequiredAttributeThresholds);
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
            if (TempObj->HasField(TEXT("PersonalityProfile")))
            {
                TSharedPtr<FJsonObject> PersObj = TempObj->GetObjectField(TEXT("PersonalityProfile"));
                DeserializeMapWrapper(PersObj, TEXT("FoodAndDrinkPreferences"), TempDef.PersonalityProfile.FoodAndDrinkPreferences);
                DeserializeMapWrapper(PersObj, TEXT("ActivityPreferences"), TempDef.PersonalityProfile.ActivityPreferences);
            }
            if (TempObj->HasField(TEXT("AgingProfile")))
            {
                FJsonObjectConverter::JsonObjectToUStruct(TempObj->GetObjectField(TEXT("AgingProfile")).ToSharedRef(), &TempDef.AgingProfile, 0, 0);
            }
            if (TempObj->HasField(TEXT("AlleleDominanceOverrides")))
            {
                DeserializeMapWrapper(TempObj->GetObjectField(TEXT("AlleleDominanceOverrides")), TEXT("ReplicatedEntries"), TempDef.AlleleDominanceOverrides);
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
            DeserializeLocalizationMap(LangObj, TEXT("Localization"), LangDef.Localization);
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
                    
                    // Parse rich UtteranceVariations with conditions and probability weights
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

            // Parse rich condition-driven utterances
            ParseDialogueUtteranceArray(DefObj, TEXT("Utterances"), BehaviorDef.Utterances);

            OutStaging.AIBehaviorMasterRegistry.Add(KeyTag, BehaviorDef);
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

    int32 NewNodeIndex = OutContainer.FlatNodes.AddDefaulted();
    FDynamicConditionNode& WorkingNode = OutContainer.FlatNodes[NewNodeIndex];

    WorkingNode.ConditionType = StringToEnum<EConditionType>(JsonObj->GetStringField(TEXT("ConditionType")));
    WorkingNode.LogicOperator = StringToEnum<ELogicOperator>(JsonObj->GetStringField(TEXT("LogicOperator")));
    WorkingNode.ComparisonOperator = StringToEnum<EComparisonOp>(JsonObj->GetStringField(TEXT("ComparisonOperator")));
    WorkingNode.UserDefinedKey = FName(*JsonObj->GetStringField(TEXT("UserDefinedKey")));
    WorkingNode.bCheckSource = JsonObj->GetBoolField(TEXT("bCheckSource"));
    WorkingNode.ComparisonValue = static_cast<float>(JsonObj->GetNumberField(TEXT("ComparisonValue")));

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

    // 8. Social Graph, Needs, and Locations
    if (UDynamicSocialComponent* Social = TargetCharacter->FindComponentByClass<UDynamicSocialComponent>())
    {
        Social->CharacterPersistentGuid = Snapshot.CharacterGuid;
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

    // 9. Re-spawning & Re-hydrating Dragged Physics Actors
    ADynamicGameState* GS = TargetCharacter->GetWorld() ? TargetCharacter->GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (GS)
    {
        for (const FDraggedPhysicsActorSnapshot& ActorSnap : Snapshot.DraggedPhysicsActors)
        {
            if (ActorSnap.ActorArchetypeTag.IsValid())
            {
                ADynamicPhysicsActor* SpawnedActor = GS->AcquirePhysicsActor(ActorSnap.ActorArchetypeTag, ActorSnap.WorldTransform, TargetCharacter, TargetCharacter);
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
        }
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

    // 9. Serializing Dragged / Tethered World Physics Actors
    for (TActorIterator<ADynamicPhysicsActor> It(InCharacter->GetWorld()); It; ++It)
    {
        ADynamicPhysicsActor* PhysActor = *It;
        if (PhysActor && !PhysActor->IsPendingKillPending())
        {
            const float DistSq = FVector::DistSquared(PhysActor->GetActorLocation(), InCharacter->GetActorLocation());
            const bool bIsAttached = PhysActor->GetAttachParentActor() == InCharacter;

            if (bIsAttached || DistSq <= FMath::Square(600.0f))
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

                if (ADynamicGameState* GS = InCharacter->GetWorld()->GetGameState<ADynamicGameState>())
                {
                    GS->ReleasePhysicsActorToPool(PhysActor);
                }
            }
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
    OutRule.bTriggersRagdollOnInstability = InObject->HasField(TEXT("bTriggersRagdollOnInstability")) ? InObject->GetBoolField(TEXT("bTriggersRagdollOnInstability")) : true;

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