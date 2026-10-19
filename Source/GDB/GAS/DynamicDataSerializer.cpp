// DynamicDataSerializer.cpp
#include "DynamicDataSerializer.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "DynamicGameState.h"

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

            FDynamicAttributeDef AttrDef;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &AttrDef, 0, 0);
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

            DeserializeMapWrapper(AbilityObj, TEXT("NumericParams"), AbilityDef.NumericParams);
            DeserializeMapWrapper(AbilityObj->GetObjectField(TEXT("SpatialDelivery")), TEXT("RuntimeMetricModifiers"), AbilityDef.SpatialDelivery.RuntimeMetricModifiers);
            DeserializeMapWrapper(AbilityObj->GetObjectField(TEXT("InteractionConfig")), TEXT("InteractionMetrics"), AbilityDef.InteractionConfig.InteractionMetrics);
            DeserializeMapWrapper(AbilityObj->GetObjectField(TEXT("MovementConfig")), TEXT("MovementMetrics"), AbilityDef.MovementConfig.MovementMetrics);
            DeserializeMapWrapper(AbilityObj->GetObjectField(TEXT("MovementConfig"))->GetObjectField(TEXT("GaspMotorSettings")), TEXT("RequiredAttributeThresholds"), AbilityDef.MovementConfig.GaspMotorSettings.RequiredAttributeThresholds);

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

    if (RootObject->HasField(TEXT("CalculationSettings")))
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

    return true;
}

bool UDynamicDataSerializer::SerializeMasterRegistry(const ADynamicGameState* InGameState, FString& OutJsonString)
{
    if (!InGameState) return false;

    TSharedRef<FJsonObject> Root = MakeShareable(new FJsonObject());
    // (Serialization maps for save profiles...)
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
    WorkingNode.ComparisonValue = JsonObj->GetNumberField(TEXT("ComparisonValue"));

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

bool UDynamicDataSerializer::SerializeFastAttributes(const FFastAttributeArray& InFastAttributes, FString& OutJsonString) { return true; }
bool UDynamicDataSerializer::DeserializeFastAttributes(const FString& InJsonString, FFastAttributeArray& OutFastAttributes) { return true; }
bool UDynamicDataSerializer::SerializeContainerSlots(const FFastContainerSlotArray& InSlots, FString& OutJsonString) { return true; }
bool UDynamicDataSerializer::DeserializeContainerSlots(const FString& InJsonString, FFastContainerSlotArray& OutSlots) { return true; }
void UDynamicDataSerializer::DeserializeMapWrapper(const TSharedPtr<FJsonObject>& InObject, const FString& FieldName, FDynamicTagFloatMapWrapper& OutWrapper) {}
TSharedPtr<FJsonObject> UDynamicDataSerializer::SerializeMapWrapper(const FDynamicTagFloatMapWrapper& InWrapper) { return nullptr; }
void UDynamicDataSerializer::DeserializeLocalizationMap(const TSharedPtr<FJsonObject>& InObject, const FString& FieldName, FDynamicLocalizationProfile& OutProfile) {}
TSharedPtr<FJsonObject> UDynamicDataSerializer::SerializeLocalizationMap(const FDynamicLocalizationProfile& InProfile) { return nullptr; }