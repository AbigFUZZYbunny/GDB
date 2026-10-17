// DynamicDataSerializer.cpp
#include "DynamicDataSerializer.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "DynamicGameState.h"

template<typename TEnum>
FString EnumToString(const FString& EnumName, TEnum Value)
{
    const UEnum* EnumPtr = FindFirstObject<UEnum>(*EnumName, EFindFirstObjectOptions::None);
    return EnumPtr ? EnumPtr->GetNameStringByValue(static_cast<int64>(Value)) : FString("Invalid");
}

template<typename TEnum>
TEnum StringToEnum(const FString& EnumName, const FString& ValueStr)
{
    const UEnum* EnumPtr = FindFirstObject<UEnum>(*EnumName, EFindFirstObjectOptions::None);
    return EnumPtr ? static_cast<TEnum>(EnumPtr->GetValueByNameString(ValueStr)) : static_cast<TEnum>(0);
}

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
                    Entry.Value = EntryObj->GetNumberField(TEXT("Value"));
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

bool UDynamicDataSerializer::DeserializeMasterRegistry(const FString& InJsonString, ADynamicGameState* TargetGameState)
{
    if (!TargetGameState) return false;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJsonString);
    TSharedPtr<FJsonObject> RootObject;
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid()) return false;

    // 1. Deserialize Master Attributes Registry
    if (RootObject->HasField(TEXT("AttributeMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("AttributeMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            FDynamicAttributeDef AttrDef;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &AttrDef, 0, 0);
            TargetGameState->AttributeMasterRegistry.Add(KeyTag, AttrDef);
            FTagAttributePair SyncPair; SyncPair.KeyTag = KeyTag; SyncPair.ValueDef = AttrDef;
            TargetGameState->AttributeMasterRegistryArr.Add(SyncPair);
        }
    }

    // 2. Deserialize Unified Delivery Archetypes
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
            FTagDeliveryArchetypePair SyncPair; SyncPair.KeyTag = KeyTag; SyncPair.ValueArchetype = ArchDef;
            TargetGameState->DeliveryArchetypesArr.Add(SyncPair);
        }
    }

    // 3. Deserialize Complete Data-Driven Abilities Map
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
            FTagAbilityDefPair SyncPair; SyncPair.KeyTag = KeyTag; SyncPair.ValueDef = AbilityDef;
            TargetGameState->AbilityMasterRegistryArr.Add(SyncPair);
        }
    }

    // 4. Deserialize Flattened Multi-Conditional Look-up Trees
    if (RootObject->HasField(TEXT("SharedConditionTrees")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("SharedConditionTrees"))->Values)
        {
            FDynamicConditionTreeContainer TreeContainer;
            FlattenAndParseJsonNode(Pair.Value->AsObject(), TreeContainer);
            TargetGameState->SharedConditionTrees.Add(FName(*Pair.Key), TreeContainer);
            FNameConditionPair SyncPair; SyncPair.KeyName = FName(*Pair.Key); SyncPair.ValueNode = TreeContainer;
            TargetGameState->SharedConditionTreesArr.Add(SyncPair);
        }
    }

    // 5. Deserialize Milestone Advancement Rules
    if (RootObject->HasField(TEXT("MasterMilestoneRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("MasterMilestoneRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            FDynamicMilestoneContainer Container;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &Container, 0, 0);
            TargetGameState->MasterMilestoneRegistry.Add(KeyTag, Container);
            FTagMilestonePair SyncPair; SyncPair.KeyTag = KeyTag; SyncPair.ValueContainer = Container;
            TargetGameState->MasterMilestoneRegistryArr.Add(SyncPair);
        }
    }

    // 6. Deserialize Structural Automated Cap Threshold Rules
    if (RootObject->HasField(TEXT("MasterThresholdRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("MasterThresholdRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            FDynamicThresholdContainer Container;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &Container, 0, 0);
            TargetGameState->MasterThresholdRegistry.Add(KeyTag, Container);
            FTagThresholdPair SyncPair; SyncPair.KeyTag = KeyTag; SyncPair.ValueContainer = Container;
            TargetGameState->MasterThresholdRegistryArr.Add(SyncPair);
        }
    }

    // 7. Deserialize Master Gameplay Effects Registry
    if (RootObject->HasField(TEXT("EffectMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("EffectMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;
            FDynamicEffectDef EffectDef;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &EffectDef, 0, 0);
            TargetGameState->MasterEffectRegistry.Add(KeyTag, EffectDef);
            FTagEffectPair SyncPair; SyncPair.KeyTag = KeyTag; SyncPair.ValueDef = EffectDef;
            TargetGameState->MasterEffectRegistryArr.Add(SyncPair);
        }
    }

    // 8. Deserialize Dedicated Server Physics Damage & Limb Configurations
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

    return true;
}

bool UDynamicDataSerializer::SerializeMasterRegistry(const ADynamicGameState* InGameState, FString& OutJsonString)
{
    if (!InGameState) return false;
    TSharedRef<FJsonObject> Root = MakeShareable(new FJsonObject());

    // 1. Serialize AttributeMasterRegistry
    if (InGameState->AttributeMasterRegistry.Num() > 0)
    {
        TSharedPtr<FJsonObject> AttrMap = MakeShareable(new FJsonObject());
        for (const auto& Pair : InGameState->AttributeMasterRegistry)
        {
            AttrMap->SetObjectField(Pair.Key.ToString(), FJsonObjectConverter::UStructToJsonObject(Pair.Value));
        }
        Root->SetObjectField(TEXT("AttributeMasterRegistry"), AttrMap);
    }

    // 2. Serialize Unified DeliveryArchetypes
    if (InGameState->DeliveryArchetypes.Num() > 0)
    {
        TSharedPtr<FJsonObject> ArchMap = MakeShareable(new FJsonObject());
        for (const auto& Pair : InGameState->DeliveryArchetypes)
        {
            TSharedPtr<FJsonObject> ArchObj = FJsonObjectConverter::UStructToJsonObject(Pair.Value);
            ArchObj->SetObjectField(TEXT("Metrics"), SerializeMapWrapper(Pair.Value.Metrics));
            ArchMap->SetObjectField(Pair.Key.ToString(), ArchObj);
        }
        Root->SetObjectField(TEXT("DeliveryArchetypes"), ArchMap);
    }

    // 3. Serialize AbilityMasterRegistry
    if (InGameState->AbilityMasterRegistry.Num() > 0)
    {
        TSharedPtr<FJsonObject> AbMap = MakeShareable(new FJsonObject());
        for (const auto& Pair : InGameState->AbilityMasterRegistry)
        {
            TSharedPtr<FJsonObject> AbObj = FJsonObjectConverter::UStructToJsonObject(Pair.Value);
            if (!AbObj.IsValid()) continue;

            AbObj->SetObjectField(TEXT("NumericParams"), SerializeMapWrapper(Pair.Value.NumericParams));
            if (AbObj->HasField(TEXT("SpatialDelivery")))
            {
                AbObj->GetObjectField(TEXT("SpatialDelivery"))->SetObjectField(TEXT("RuntimeMetricModifiers"), SerializeMapWrapper(Pair.Value.SpatialDelivery.RuntimeMetricModifiers));
            }
            if (AbObj->HasField(TEXT("MovementConfig")))
            {
                TSharedPtr<FJsonObject> MovObj = AbObj->GetObjectField(TEXT("MovementConfig"));
                MovObj->SetObjectField(TEXT("MovementMetrics"), SerializeMapWrapper(Pair.Value.MovementConfig.MovementMetrics));
                if (MovObj->HasField(TEXT("GaspMotorSettings")))
                {
                    MovObj->GetObjectField(TEXT("GaspMotorSettings"))->SetObjectField(TEXT("RequiredAttributeThresholds"), SerializeMapWrapper(Pair.Value.MovementConfig.GaspMotorSettings.RequiredAttributeThresholds));
                }
            }
            if (AbObj->HasField(TEXT("InteractionConfig")))
            {
                AbObj->GetObjectField(TEXT("InteractionConfig"))->SetObjectField(TEXT("InteractionMetrics"), SerializeMapWrapper(Pair.Value.InteractionConfig.InteractionMetrics));
            }
            AbMap->SetObjectField(Pair.Key.ToString(), AbObj);
        }
        Root->SetObjectField(TEXT("AbilityMasterRegistry"), AbMap);
    }

    // 4. Serialize SharedConditionTrees
    if (InGameState->SharedConditionTrees.Num() > 0)
    {
        TSharedPtr<FJsonObject> CondMap = MakeShareable(new FJsonObject());
        for (const auto& Pair : InGameState->SharedConditionTrees)
        {
            if (Pair.Value.FlatNodes.Num() > 0)
            {
                CondMap->SetObjectField(Pair.Key.ToString(), ReconstructJsonFromFlatNode(0, Pair.Value));
            }
        }
        Root->SetObjectField(TEXT("SharedConditionTrees"), CondMap);
    }

    // 5. Serialize MasterMilestoneRegistry
    if (InGameState->MasterMilestoneRegistry.Num() > 0)
    {
        TSharedPtr<FJsonObject> MilestoneMap = MakeShareable(new FJsonObject());
        for (const auto& Pair : InGameState->MasterMilestoneRegistry)
        {
            MilestoneMap->SetObjectField(Pair.Key.ToString(), FJsonObjectConverter::UStructToJsonObject(Pair.Value));
        }
        Root->SetObjectField(TEXT("MasterMilestoneRegistry"), MilestoneMap);
    }

    // 6. Serialize MasterThresholdRegistry
    if (InGameState->MasterThresholdRegistry.Num() > 0)
    {
        TSharedPtr<FJsonObject> ThresholdMap = MakeShareable(new FJsonObject());
        for (const auto& Pair : InGameState->MasterThresholdRegistry)
        {
            ThresholdMap->SetObjectField(Pair.Key.ToString(), FJsonObjectConverter::UStructToJsonObject(Pair.Value));
        }
        Root->SetObjectField(TEXT("MasterThresholdRegistry"), ThresholdMap);
    }

    // 7. Serialize EffectMasterRegistry
    if (InGameState->MasterEffectRegistry.Num() > 0)
    {
        TSharedPtr<FJsonObject> EffectMap = MakeShareable(new FJsonObject());
        for (const auto& Pair : InGameState->MasterEffectRegistry)
        {
            EffectMap->SetObjectField(Pair.Key.ToString(), FJsonObjectConverter::UStructToJsonObject(Pair.Value));
        }
        Root->SetObjectField(TEXT("EffectMasterRegistry"), EffectMap);
    }

    // 8. Serialize Physics Calculation Settings & Limbs (inside SerializeMasterRegistry)
    Root->SetObjectField(TEXT("CalculationSettings"), FJsonObjectConverter::UStructToJsonObject(InGameState->GetCalculationSettings()));
    TArray<TSharedPtr<FJsonValue>> LimbsJson;
    for (const auto& Pair : InGameState->GetLimbLookupMap())
    {
        TSharedPtr<FJsonObject> LimbObj = FJsonObjectConverter::UStructToJsonObject(Pair.Value);
        if (LimbObj.IsValid())
        {
            LimbObj->SetObjectField(TEXT("Localization"), SerializeLocalizationMap(Pair.Value.Localization));
            LimbObj->SetObjectField(TEXT("DefaultLimbAttributes"), SerializeMapWrapper(Pair.Value.DefaultLimbAttributes));
            LimbObj->SetObjectField(TEXT("AngularMotorAttributeScalars"), SerializeMapWrapper(Pair.Value.AngularMotorAttributeScalars));
            LimbObj->SetObjectField(TEXT("LinearMotorAttributeScalars"), SerializeMapWrapper(Pair.Value.LinearMotorAttributeScalars));
            LimbsJson.Add(MakeShareable(new FJsonValueObject(LimbObj)));
        }
    }
    Root->SetArrayField(TEXT("Limbs"), LimbsJson);

    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
    return FJsonSerializer::Serialize(Root, Writer);
}

TSharedPtr<FJsonObject> UDynamicDataSerializer::ReconstructJsonFromFlatNode(int32 NodeIndex, const FDynamicConditionTreeContainer& TreeContainer)
{
    TSharedRef<FJsonObject> JsonObj = MakeShareable(new FJsonObject());
    if (!TreeContainer.FlatNodes.IsValidIndex(NodeIndex)) return JsonObj;

    const FDynamicConditionNode& Node = TreeContainer.FlatNodes[NodeIndex];
    JsonObj->SetStringField(TEXT("ConditionType"), EnumToString(TEXT("EConditionType"), Node.ConditionType));
    JsonObj->SetStringField(TEXT("LogicOperator"), EnumToString(TEXT("ELogicOperator"), Node.LogicOperator));
    JsonObj->SetStringField(TEXT("ComparisonOperator"), EnumToString(TEXT("EComparisonOp"), Node.ComparisonOperator));
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
                NewEntry.Value = Pair.Value->AsNumber();
                OutFastAttributes.Items.Add(NewEntry);
            }
        }
        OutFastAttributes.MarkArrayDirty();
        return true;
    }
    return false;
}

int32 UDynamicDataSerializer::FlattenAndParseJsonNode(const TSharedPtr<FJsonObject>& JsonObj, FDynamicConditionTreeContainer& OutContainer)
{
    if (!JsonObj.IsValid()) return INDEX_NONE;
    int32 NewNodeIndex = OutContainer.FlatNodes.AddDefaulted();
    FDynamicConditionNode& WorkingNode = OutContainer.FlatNodes[NewNodeIndex];
    WorkingNode.ConditionType = StringToEnum<EConditionType>(TEXT("EConditionType"), JsonObj->GetStringField(TEXT("ConditionType")));
    WorkingNode.LogicOperator = StringToEnum<ELogicOperator>(TEXT("ELogicOperator"), JsonObj->GetStringField(TEXT("LogicOperator")));
    WorkingNode.ComparisonOperator = StringToEnum<EComparisonOp>(TEXT("EComparisonOp"), JsonObj->GetStringField(TEXT("ComparisonOperator")));
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