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
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Serialization")
    static bool DeserializeMasterRegistry(const FString& InJsonString, ADynamicGameState* TargetGameState);

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Serialization")
    static bool SerializeMasterRegistry(const ADynamicGameState* InGameState, FString& OutJsonString);

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Serialization")
    static bool SerializeFastAttributes(const FFastAttributeArray& InFastAttributes, FString& OutJsonString);

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Serialization")
    static bool DeserializeFastAttributes(const FString& InJsonString, FFastAttributeArray& OutFastAttributes);
    
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Serialization")
    static bool SerializeContainerSlots(const FFastContainerSlotArray& InSlots, FString& OutJsonString);

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Serialization")
    static bool DeserializeContainerSlots(const FString& InJsonString, FFastContainerSlotArray& OutSlots);

private:
    static int32 FlattenAndParseJsonNode(const TSharedPtr<FJsonObject>& JsonObj, FDynamicConditionTreeContainer& OutContainer);
    static TSharedPtr<FJsonObject> ReconstructJsonFromFlatNode(int32 NodeIndex, const FDynamicConditionTreeContainer& TreeContainer);

    static void DeserializeMapWrapper(const TSharedPtr<FJsonObject>& InObject, const FString& FieldName, FDynamicTagFloatMapWrapper& OutWrapper);
    static TSharedPtr<FJsonObject> SerializeMapWrapper(const FDynamicTagFloatMapWrapper& InWrapper);
    
    static void DeserializeLocalizationMap(const TSharedPtr<FJsonObject>& InObject, const FString& FieldName, FDynamicLocalizationProfile& OutProfile);
    static TSharedPtr<FJsonObject> SerializeLocalizationMap(const FDynamicLocalizationProfile& InProfile);
};