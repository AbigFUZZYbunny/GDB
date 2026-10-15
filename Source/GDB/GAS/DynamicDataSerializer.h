// DynamicDataSerializer.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DynamicGASTypes.h"
#include "Dom/JsonObject.h"
#include "DynamicDataSerializer.generated.h"

class ADynamicGameState;

UCLASS()
class GDB_API UDynamicDataSerializer : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // --- High-Level GameState Ingestion / Export ---
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Serialization")
    static bool DeserializeMasterRegistry(const FString& InJsonString, ADynamicGameState* TargetGameState);

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Serialization")
    static bool SerializeMasterRegistry(const ADynamicGameState* InGameState, FString& OutJsonString);

    // --- Fast Attribute Array Standalone Helpers ---
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Serialization")
    static bool SerializeFastAttributes(const FFastAttributeArray& InFastAttributes, FString& OutJsonString);

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Serialization")
    static bool DeserializeFastAttributes(const FString& InJsonString, FFastAttributeArray& OutFastAttributes);

private:
    // Dynamic Logic Condition Tree Parsers
    static int32 FlattenAndParseJsonNode(const TSharedPtr<FJsonObject>& JsonObj, FDynamicConditionTreeContainer& OutContainer);
    static TSharedPtr<FJsonObject> ReconstructJsonFromFlatNode(int32 NodeIndex, const FDynamicConditionTreeContainer& TreeContainer);

    // Tag Map Wrapper & Payload Profile Parsers
    static void DeserializeMapWrapper(const TSharedPtr<FJsonObject>& InObject, const FString& FieldName, FDynamicTagFloatMapWrapper& OutWrapper);
    static TSharedPtr<FJsonObject> SerializeMapWrapper(const FDynamicTagFloatMapWrapper& InWrapper);

    static void ParsePayloadProfile(const TSharedPtr<FJsonObject>& ObjectSection, FDynamicPayloadProfile& OutProfile);
    static TSharedPtr<FJsonObject> SerializePayloadProfile(const FDynamicPayloadProfile& Profile);

    // Fast Array Direct Parsers
    static TSharedPtr<FJsonObject> SerializeFastAttributesInternal(const FFastAttributeArray& InFastAttributes);
    static void DeserializeFastAttributesInternal(const TSharedPtr<FJsonObject>& JsonObject, FFastAttributeArray& OutFastAttributes);
};