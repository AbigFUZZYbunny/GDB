// DynamicDataSerializer.cpp
#include "DynamicDataSerializer.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "DynamicGameState.h"

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

    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJsonString);
    TSharedPtr<FJsonObject> RootObject;
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid()) return false;

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
            TargetGameState->AttributeMasterRegistry.Add(KeyTag, AttrDef);
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
            TargetGameState->DeliveryArchetypes.Add(KeyTag, ArchDef);
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

            TargetGameState->AbilityMasterRegistry.Add(KeyTag, AbilityDef);
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

            TargetGameState->MasterPhysicsActorRegistry.Add(KeyTag, ActorDef);
            if (ActorDef.CraftingRecipes.Num() > 0)
            {
                FDynamicRecipeList List;
                List.Recipes = ActorDef.CraftingRecipes;
                TargetGameState->RecipeMasterRegistry.Add(KeyTag, List);
            }
        }
    }

    if (RootObject->HasField(TEXT("SharedConditionTrees")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("SharedConditionTrees"))->Values)
        {
            FDynamicConditionTreeContainer TreeContainer;
            FlattenAndParseJsonNode(Pair.Value->AsObject(), TreeContainer);
            TargetGameState->SharedConditionTrees.Add(FName(*Pair.Key), TreeContainer);
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
            TargetGameState->MasterMilestoneRegistry.Add(KeyTag, Container);
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
            TargetGameState->MasterThresholdRegistry.Add(KeyTag, Container);
        }
    }

    if (RootObject->HasField(TEXT("EffectMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("EffectMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;

            FDynamicEffectDef EffectDef;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &EffectDef, 0, 0);
            TargetGameState->MasterEffectRegistry.Add(KeyTag, EffectDef);
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
            TargetGameState->SetPhysicsConfig(PhysicsConfig);
        }
    }
    else if (RootObject->HasField(TEXT("CalculationSettings")))
    {
        FPhysicsCalculationSettings CalcSettings;
        if (FJsonObjectConverter::JsonObjectToUStruct(RootObject->GetObjectField(TEXT("CalculationSettings")).ToSharedRef(), &CalcSettings, 0, 0))
        {
            TargetGameState->CalculationSettings = CalcSettings;
        }
    }

    if (RootObject->HasField(TEXT("AbilityScoreConfig")))
    {
        TSharedPtr<FJsonObject> ScoreObj = RootObject->GetObjectField(TEXT("AbilityScoreConfig"));
        FAbilityScoreConfigData ScoreConfig;
        if (FJsonObjectConverter::JsonObjectToUStruct(ScoreObj.ToSharedRef(), &ScoreConfig, 0, 0))
        {
            DeserializeMapWrapper(ScoreObj, TEXT("ExecutionTypeWeights"), ScoreConfig.ExecutionTypeWeights);
            TargetGameState->SetAbilityScoreConfig(ScoreConfig);
        }
    }

    if (RootObject->HasField(TEXT("RelationshipTypeRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("RelationshipTypeRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;

            TSharedPtr<FJsonObject> RelObj = Pair.Value->AsObject();
            FDynamicRelationshipTypeDef RelDef;
            FJsonObjectConverter::JsonObjectToUStruct(RelObj.ToSharedRef(), &RelDef, 0, 0);
            DeserializeLocalizationMap(RelObj, TEXT("Localization"), RelDef.Localization);
            TargetGameState->RelationshipTypeRegistry.Add(KeyTag, RelDef);
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
            TargetGameState->ProfessionMasterRegistry.Add(KeyTag, ProfDef);
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
            TargetGameState->MasterAIAgentRegistry.Add(KeyTag, AgentDef);
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
            TargetGameState->CharacterTemplatesRegistry.Add(KeyTag, TempDef);
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
            TargetGameState->SkeletonMasterRegistry.Add(KeyTag, SkelDef);
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
            TargetGameState->ConversionRegistry.Add(KeyTag, RuleDef);
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
            TargetGameState->EvolutionMasterRegistry.Add(KeyTag, Container);
        }
    }
    
    // 1. Calendar
    if (RootObject->HasField(TEXT("CalendarConfig")))
    {
        FJsonObjectConverter::JsonObjectToUStruct(RootObject->GetObjectField(TEXT("CalendarConfig")).ToSharedRef(), &TargetGameState->CalendarConfig, 0, 0);
    }

    // 2. Aging
    if (RootObject->HasField(TEXT("LifeStagesRegistry")))
    {
        const TArray<TSharedPtr<FJsonValue>>& Array = RootObject->GetArrayField(TEXT("LifeStagesRegistry"));
        for (const auto& Val : Array)
        {
            FDynamicLifeStageDef Stage;
            FJsonObjectConverter::JsonObjectToUStruct(Val->AsObject().ToSharedRef(), &Stage, 0, 0);
            TargetGameState->LifeStagesRegistry.Add(Stage);
        }
    }

    // 3. Genetics
    if (RootObject->HasField(TEXT("GeneticsMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("GeneticsMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;

            FDynamicGeneLocusDef Locus;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &Locus, 0, 0);
            TargetGameState->GeneticsMasterRegistry.Add(KeyTag, Locus);
        }
    }
    
    // 1. Biomes Registry
    if (RootObject->HasField(TEXT("BiomeMasterRegistry")))
    {
        for (const auto& Pair : RootObject->GetObjectField(TEXT("BiomeMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;

            FDynamicBiomeDef BiomeDef;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &BiomeDef, 0, 0);
            TargetGameState->BiomeMasterRegistry.Add(KeyTag, BiomeDef);
        }
    }

    // 2. Weather Patterns Registry
    if (RootObject->HasField(TEXT("WeatherMasterRegistry")))
    {
        for (const auto& Pair : RootObject->GetObjectField(TEXT("WeatherMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;

            FDynamicWeatherPatternDef WeatherDef;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &WeatherDef, 0, 0);
            TargetGameState->WeatherMasterRegistry.Add(KeyTag, WeatherDef);
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
            TargetGameState->EvolutionProfilesRegistry.Add(KeyTag, ProfileDef);
        }
    }
    
    // 1. Quests Master Registry
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

            // Deserialize Step localizations
            if (QuestObj->HasField(TEXT("Steps")))
            {
                const TArray<TSharedPtr<FJsonValue>>& StepsArr = QuestObj->GetArrayField(TEXT("Steps"));
                for (int32 i = 0; i < StepsArr.Num() && Quest.Steps.IsValidIndex(i); ++i)
                {
                    TSharedPtr<FJsonObject> StepObj = StepsArr[i]->AsObject();
                    DeserializeLocalizationMap(StepObj, TEXT("Localization"), Quest.Steps[i].Localization);
                }
            }

            TargetGameState->QuestMasterRegistry.Add(KeyTag, Quest);
        }
    }

    // 2. Dynamic Quest Generator Rules
    if (RootObject->HasField(TEXT("QuestGeneratorRules")))
    {
        for (const auto& Pair : RootObject->GetObjectField(TEXT("QuestGeneratorRules"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;

            FDynamicQuestGeneratorRule Rule;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &Rule, 0, 0);
            TargetGameState->QuestGeneratorRules.Add(KeyTag, Rule);
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