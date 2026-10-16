#include "DynamicDataSerializer.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "DynamicGameState.h"

// --- Reflection Translation Helpers ---
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

// --- Map Array-Wrapper Serialization Primitives ---
void UDynamicDataSerializer::DeserializeMapWrapper(const TSharedPtr<FJsonObject>& InObject, const FString& FieldName, FDynamicTagFloatMapWrapper& OutWrapper)
{
    OutWrapper.ReplicatedEntries.Empty();
    if (!InObject.IsValid() || !InObject->HasField(FieldName)) return;

    TSharedPtr<FJsonObject> MapSection = InObject->GetObjectField(FieldName);
    if (!MapSection.IsValid()) return;

    // Supports parsing out an array structured payload if raw fields match wrapper expectations
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

// --- Payload Configuration Parsers ---
void UDynamicDataSerializer::ParsePayloadProfile(const TSharedPtr<FJsonObject>& ObjectSection, FDynamicPayloadProfile& OutProfile)
{
    if (!ObjectSection.IsValid()) return;
    OutProfile.ArchetypeLinkTag = FGameplayTag::RequestGameplayTag(FName(*ObjectSection->GetStringField(TEXT("ArchetypeLinkTag"))), false);
    DeserializeMapWrapper(ObjectSection, TEXT("RuntimeMetricModifiers"), OutProfile.RuntimeMetricModifiers);
    
    OutProfile.PropagatedTriggerTags.Reset();
    if (ObjectSection->HasField(TEXT("PropagatedTriggerTags")))
    {
        for (const auto& TV : ObjectSection->GetArrayField(TEXT("PropagatedTriggerTags")))
        {
            FGameplayTag TT = FGameplayTag::RequestGameplayTag(FName(*TV->AsString()), false);
            if (TT.IsValid()) OutProfile.PropagatedTriggerTags.AddTag(TT);
        }
    }
}

// --- Core Master Deserializer Module ---
bool UDynamicDataSerializer::DeserializeMasterRegistry(const FString& InJsonString, ADynamicGameState* TargetGameState)
{
    if (!TargetGameState) return false;

    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJsonString);
    TSharedPtr<FJsonObject> RootObject;
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid()) return false;

    // 1. Deserialize Master Attributes Registry Loop
    if (RootObject->HasField(TEXT("AttributeMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("AttributeMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;

            FDynamicAttributeDef AttrDef;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &AttrDef, 0, 0);

            TargetGameState->AttributeMasterRegistry.Add(KeyTag, AttrDef);
            FTagAttributePair SyncPair;
            SyncPair.KeyTag = KeyTag;
            SyncPair.ValueDef = AttrDef;
            TargetGameState->AttributeMasterRegistryArr.Add(SyncPair);
        }
    }

    // 2. Deserialize Complete Data-Driven Abilities Map
    if (RootObject->HasField(TEXT("AbilityMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("AbilityMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;

            TSharedPtr<FJsonObject> AbilityObj = Pair.Value->AsObject();
            FDynamicAbilityDef AbilityDef;
            FJsonObjectConverter::JsonObjectToUStruct(AbilityObj.ToSharedRef(), &AbilityDef, 0, 0);

            // Manual Map wrapper transformations to bypass UHT replication constraints
            DeserializeMapWrapper(AbilityObj, TEXT("NumericParams"), AbilityDef.NumericParams);
            ParsePayloadProfile(AbilityObj->GetObjectField(TEXT("ProjectileConfig")), AbilityDef.ProjectileConfig);
            ParsePayloadProfile(AbilityObj->GetObjectField(TEXT("AoeConfig")), AbilityDef.AoeConfig);

            // Appended Extensions: Auras, Movement, and GASP settings mapping
            DeserializeMapWrapper(AbilityObj->GetObjectField(TEXT("AuraConfig")), TEXT("AuraMetrics"), AbilityDef.AuraConfig.AuraMetrics);
            DeserializeMapWrapper(AbilityObj->GetObjectField(TEXT("MovementConfig")), TEXT("MovementMetrics"), AbilityDef.MovementConfig.MovementMetrics);
            DeserializeMapWrapper(AbilityObj->GetObjectField(TEXT("MovementConfig"))->GetObjectField(TEXT("GaspMotorSettings")), TEXT("RequiredAttributeThresholds"), AbilityDef.MovementConfig.GaspMotorSettings.RequiredAttributeThresholds);

            TargetGameState->AbilityMasterRegistry.Add(KeyTag, AbilityDef);
            FTagAbilityDefPair SyncPair;
            SyncPair.KeyTag = KeyTag;
            SyncPair.ValueDef = AbilityDef;
            TargetGameState->AbilityMasterRegistryArr.Add(SyncPair);
        }
    }

    // 3. Deserialize Flattened Multi-Conditional Look-up Trees (No UHT recursion limits)
    if (RootObject->HasField(TEXT("SharedConditionTrees")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("SharedConditionTrees"))->Values)
        {
            FDynamicConditionTreeContainer TreeContainer;
            FlattenAndParseJsonNode(Pair.Value->AsObject(), TreeContainer);

            TargetGameState->SharedConditionTrees.Add(FName(*Pair.Key), TreeContainer);
            FNameConditionPair SyncPair;
            SyncPair.KeyName = FName(*Pair.Key);
            SyncPair.ValueNode = TreeContainer;
            TargetGameState->SharedConditionTreesArr.Add(SyncPair);
        }
    }

    // 4. Deserialize Milestone Advancement Rules
    if (RootObject->HasField(TEXT("MasterMilestoneRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("MasterMilestoneRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;

            FDynamicMilestoneContainer Container;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &Container, 0, 0);

            TargetGameState->MasterMilestoneRegistry.Add(KeyTag, Container);
            FTagMilestonePair SyncPair;
            SyncPair.KeyTag = KeyTag;
            SyncPair.ValueContainer = Container;
            TargetGameState->MasterMilestoneRegistryArr.Add(SyncPair);
        }
    }

    // 5. Deserialize Structural Automated Cap Threshold Rules
    if (RootObject->HasField(TEXT("MasterThresholdRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("MasterThresholdRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;

            FDynamicThresholdContainer Container;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &Container, 0, 0);

            TargetGameState->MasterThresholdRegistry.Add(KeyTag, Container);
            FTagThresholdPair SyncPair;
            SyncPair.KeyTag = KeyTag;
            SyncPair.ValueContainer = Container;
            TargetGameState->MasterThresholdRegistryArr.Add(SyncPair);
        }
    }

    // 6. Deserialize Master Gameplay Effects Registry
    if (RootObject->HasField(TEXT("EffectMasterRegistry")))
    {
        for (auto& Pair : RootObject->GetObjectField(TEXT("EffectMasterRegistry"))->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (!KeyTag.IsValid()) continue;

            FDynamicEffectDef EffectDef;
            FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), &EffectDef, 0, 0);

            TargetGameState->MasterEffectRegistry.Add(KeyTag, EffectDef);
            FTagEffectPair SyncPair;
            SyncPair.KeyTag = KeyTag;
            SyncPair.ValueDef = EffectDef;
            TargetGameState->MasterEffectRegistryArr.Add(SyncPair);
        }
    }

    // 7. Deserialize Dedicated Server Physics Damage, Limb Mapping & Collision Thresholds
    if (RootObject->HasField(TEXT("CalculationSettings")) && RootObject->HasField(TEXT("Limbs")))
    {
        FPhysicsDamageConfigData PhysicsConfig;
        if (FJsonObjectConverter::JsonObjectToUStruct(RootObject.ToSharedRef(), &PhysicsConfig, 0, 0))
        {
            TargetGameState->SetPhysicsConfig(PhysicsConfig);
        }
    }

    return true;
}

// --- Main Serialization Pipeline ---
bool UDynamicDataSerializer::SerializeMasterRegistry(const ADynamicGameState* InGameState, FString& OutJsonString)
{
    if (!InGameState) return false;

    TSharedRef<FJsonObject> RootObject = MakeShareable(new FJsonObject());

    // 1. Serialize AttributeMasterRegistry
    if (InGameState->AttributeMasterRegistry.Num() > 0)
    {
        TSharedRef<FJsonObject> AttrMapObj = MakeShareable(new FJsonObject());
        for (const auto& Pair : InGameState->AttributeMasterRegistry)
        {
            TSharedPtr<FJsonObject> AttrObj = FJsonObjectConverter::UStructToJsonObject(Pair.Value);
            if (AttrObj.IsValid())
            {
                AttrMapObj->SetObjectField(Pair.Key.ToString(), AttrObj);
            }
        }
        RootObject->SetObjectField(TEXT("AttributeMasterRegistry"), AttrMapObj);
    }

    // 2. Serialize AbilityMasterRegistry
    if (InGameState->AbilityMasterRegistry.Num() > 0)
    {
        TSharedRef<FJsonObject> AbilityMapObj = MakeShareable(new FJsonObject());
        for (const auto& Pair : InGameState->AbilityMasterRegistry)
        {
            TSharedPtr<FJsonObject> AbObj = FJsonObjectConverter::UStructToJsonObject(Pair.Value);
            if (!AbObj.IsValid()) continue;

            // Re-inject wrapped maps and nested profiles to ensure correct schema formatting
            AbObj->SetObjectField(TEXT("NumericParams"), SerializeMapWrapper(Pair.Value.NumericParams));
            AbObj->SetObjectField(TEXT("ProjectileConfig"), SerializePayloadProfile(Pair.Value.ProjectileConfig));
            AbObj->SetObjectField(TEXT("AoeConfig"), SerializePayloadProfile(Pair.Value.AoeConfig));

            // Serialize Aura Config Metrics
            TSharedRef<FJsonObject> AuraConfigObj = MakeShareable(new FJsonObject());
            AuraConfigObj->SetStringField(TEXT("AuraLinkTag"), Pair.Value.AuraConfig.AuraLinkTag.ToString());
            AuraConfigObj->SetObjectField(TEXT("AuraMetrics"), SerializeMapWrapper(Pair.Value.AuraConfig.AuraMetrics));
            AbObj->SetObjectField(TEXT("AuraConfig"), AuraConfigObj);

            // Serialize Movement & GASP Motor Configurations
            TSharedRef<FJsonObject> MovementConfigObj = MakeShareable(new FJsonObject());
            MovementConfigObj->SetStringField(TEXT("PhysicsType"), EnumToString(TEXT("EMovementPhysicsType"), Pair.Value.MovementConfig.PhysicsType));
            MovementConfigObj->SetStringField(TEXT("MotionTagIdentifier"), Pair.Value.MovementConfig.MotionTagIdentifier.ToString());
            MovementConfigObj->SetObjectField(TEXT("MovementMetrics"), SerializeMapWrapper(Pair.Value.MovementConfig.MovementMetrics));

            TSharedRef<FJsonObject> GaspMotorObj = MakeShareable(new FJsonObject());
            GaspMotorObj->SetObjectField(TEXT("RequiredAttributeThresholds"), SerializeMapWrapper(Pair.Value.MovementConfig.GaspMotorSettings.RequiredAttributeThresholds));
            GaspMotorObj->SetNumberField(TEXT("NewtonReflectiveForceFactor"), Pair.Value.MovementConfig.GaspMotorSettings.NewtonReflectiveForceFactor);
            GaspMotorObj->SetNumberField(TEXT("PhysicsBlendWeightMultiplier"), Pair.Value.MovementConfig.GaspMotorSettings.PhysicsBlendWeightMultiplier);
            MovementConfigObj->SetObjectField(TEXT("GaspMotorSettings"), GaspMotorObj);

            AbObj->SetObjectField(TEXT("MovementConfig"), MovementConfigObj);

            AbilityMapObj->SetObjectField(Pair.Key.ToString(), AbObj);
        }
        RootObject->SetObjectField(TEXT("AbilityMasterRegistry"), AbilityMapObj);
    }

    // 3. Serialize SharedConditionTrees (Reconstructing hierarchical JSON from flat indices)
    if (InGameState->SharedConditionTrees.Num() > 0)
    {
        TSharedRef<FJsonObject> CondMapObj = MakeShareable(new FJsonObject());
        for (const auto& Pair : InGameState->SharedConditionTrees)
        {
            if (Pair.Value.FlatNodes.Num() > 0)
            {
                CondMapObj->SetObjectField(Pair.Key.ToString(), ReconstructJsonFromFlatNode(0, Pair.Value));
            }
        }
        RootObject->SetObjectField(TEXT("SharedConditionTrees"), CondMapObj);
    }

    // 4. Serialize MasterMilestoneRegistry
    if (InGameState->MasterMilestoneRegistry.Num() > 0)
    {
        TSharedRef<FJsonObject> MilestoneMapObj = MakeShareable(new FJsonObject());
        for (const auto& Pair : InGameState->MasterMilestoneRegistry)
        {
            TSharedPtr<FJsonObject> MilestoneObj = FJsonObjectConverter::UStructToJsonObject(Pair.Value);
            if (MilestoneObj.IsValid())
            {
                MilestoneMapObj->SetObjectField(Pair.Key.ToString(), MilestoneObj);
            }
        }
        RootObject->SetObjectField(TEXT("MasterMilestoneRegistry"), MilestoneMapObj);
    }

    // 5. Serialize MasterThresholdRegistry
    if (InGameState->MasterThresholdRegistry.Num() > 0)
    {
        TSharedRef<FJsonObject> ThresholdMapObj = MakeShareable(new FJsonObject());
        for (const auto& Pair : InGameState->MasterThresholdRegistry)
        {
            TSharedPtr<FJsonObject> ThresholdObj = FJsonObjectConverter::UStructToJsonObject(Pair.Value);
            if (ThresholdObj.IsValid())
            {
                ThresholdMapObj->SetObjectField(Pair.Key.ToString(), ThresholdObj);
            }
        }
        RootObject->SetObjectField(TEXT("MasterThresholdRegistry"), ThresholdMapObj);
    }

    // 6. Serialize EffectMasterRegistry
    if (InGameState->MasterEffectRegistry.Num() > 0)
    {
        TSharedRef<FJsonObject> EffectMapObj = MakeShareable(new FJsonObject());
        for (const auto& Pair : InGameState->MasterEffectRegistry)
        {
            TSharedPtr<FJsonObject> EffectObj = FJsonObjectConverter::UStructToJsonObject(Pair.Value);
            if (EffectObj.IsValid())
            {
                EffectMapObj->SetObjectField(Pair.Key.ToString(), EffectObj);
            }
        }
        RootObject->SetObjectField(TEXT("EffectMasterRegistry"), EffectMapObj);
    }

    // 7. Serialize Physics Calculation Settings & Limb Profiles
    const FPhysicsCalculationSettings& CalcSettings = InGameState->GetCalculationSettings();
    TSharedPtr<FJsonObject> CalcSettingsObj = FJsonObjectConverter::UStructToJsonObject(CalcSettings);
    if (CalcSettingsObj.IsValid())
    {
        RootObject->SetObjectField(TEXT("CalculationSettings"), CalcSettingsObj);
    }

    // Extract all limb profiles stored in the GameState's lookup map
    const TMap<uint8, FPhysicsLimbConfig>& LimbsMap = InGameState->GetLimbLookupMap();
    if (LimbsMap.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> LimbsJsonArray;
        for (const auto& LimbPair : LimbsMap)
        {
            TSharedPtr<FJsonObject> LimbObj = FJsonObjectConverter::UStructToJsonObject(LimbPair.Value);
            if (LimbObj.IsValid())
            {
                LimbsJsonArray.Add(MakeShareable(new FJsonValueObject(LimbObj)));
            }
        }
        RootObject->SetArrayField(TEXT("Limbs"), LimbsJsonArray);
    }

    // Produce pretty-printed formatted JSON output
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
    return FJsonSerializer::Serialize(RootObject, Writer);
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
    for (int32 ChildIdx : Node.ChildIndices) ChildrenArr.Add(MakeShareable(new FJsonValueObject(ReconstructJsonFromFlatNode(ChildIdx, TreeContainer))));
    JsonObj->SetArrayField(TEXT("ChildNodes"), ChildrenArr);

    return JsonObj;
}

// FIX: This single clean implementation manages the conversion pass natively.
// Ensure any duplicate blocks below this line are deleted from your file.
TSharedPtr<FJsonObject> UDynamicDataSerializer::SerializePayloadProfile(const FDynamicPayloadProfile& Profile)
{
    TSharedRef<FJsonObject> Obj = MakeShareable(new FJsonObject());
    Obj->SetStringField(TEXT("ArchetypeLinkTag"), Profile.ArchetypeLinkTag.ToString());
    Obj->SetObjectField(TEXT("RuntimeMetricModifiers"), SerializeMapWrapper(Profile.RuntimeMetricModifiers));
    
    TArray<TSharedPtr<FJsonValue>> TagsArr;
    for (const auto& T : Profile.PropagatedTriggerTags) 
    {
        TagsArr.Add(MakeShareable(new FJsonValueString(T.ToString())));
    }
    Obj->SetArrayField(TEXT("PropagatedTriggerTags"), TagsArr);
    return Obj;
}

bool UDynamicDataSerializer::SerializeFastAttributes(const FFastAttributeArray& InFastAttributes, FString& OutJsonString)
{
    TSharedRef<FJsonObject> RootObject = MakeShareable(new FJsonObject());
    TSharedPtr<FJsonObject> ArrayObject = MakeShareable(new FJsonObject());

    // Loop through your fast attribute struct data elements
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

    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        return false;
    }

    if (RootObject->HasField(TEXT("FastAttributes")))
    {
        // Clear old entries before populating fresh values from your file
        OutFastAttributes.Items.Empty();

        TSharedPtr<FJsonObject> AttrMapObj = RootObject->GetObjectField(TEXT("FastAttributes"));
        for (auto& Pair : AttrMapObj->Values)
        {
            FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(FName(*Pair.Key), false);
            if (KeyTag.IsValid())
            {
                // Instantiate a new entry slot inside your high-performance array
                FDynamicAttributeItem NewEntry;
                NewEntry.AttributeTag = KeyTag;
                NewEntry.Value = Pair.Value->AsNumber();
                
                OutFastAttributes.Items.Add(NewEntry);
            }
        }
        
        // Notify the replication system that the array layout has been modified
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
