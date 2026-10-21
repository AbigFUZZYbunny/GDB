// DynamicDataSerializer.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DynamicGASTypes.h"
#include "Dom/JsonObject.h"
#include "DynamicDataSerializer.generated.h"

class ADynamicGameState;
class ADynamicCharacter;

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnMasterRegistryDeserializedSignature, bool, bSuccess);

UCLASS()
class GDB_API UDynamicDataSerializer : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Synchronous execution (startup-only)
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Serialization")
    static bool DeserializeMasterRegistry(const FString& InJsonString, ADynamicGameState* TargetGameState);

    /**
     * Non-blocking background thread parser.
     * Deserializes JSON payloads into isolated staging memory, then marshals
     * pointers to the GameThread via MoveTemp to prevent server hitches.
     */
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Serialization")
    static void DeserializeMasterRegistryAsync(
        const FString& InJsonString, 
        ADynamicGameState* TargetGameState, 
        FOnMasterRegistryDeserializedSignature OnComplete
    );

    static void DeserializeMasterRegistryAsyncNative(
        const FString& InJsonString, 
        ADynamicGameState* TargetGameState, 
        TFunction<void(bool)> OnComplete = nullptr
    );

    // Thread-safe pure data parsing function without UObject dependencies
    static bool DeserializeMasterRegistryToStaging(const FString& InJsonString, FMasterRegistryStagingData& OutStaging);

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

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Persistence")
    static bool SerializePlayerToSnapshotJson(const ADynamicCharacter* InCharacter, FString& OutJsonPayload);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Persistence")
    static bool DeserializePlayerFromSnapshotJson(ADynamicCharacter* TargetCharacter, const FString& InJsonPayload);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Persistence")
    TArray<FDynamicActiveQuestObjectiveItem> ActiveObjectives;
    
    template<typename TEnum>
    static FString EnumToString(TEnum Value)
    {
        if (const UEnum* EnumPtr = StaticEnum<TEnum>())
        {
            return EnumPtr->GetNameStringByValue(static_cast<int64>(Value));
        }
        return TEXT("Invalid");
    }

    template<typename TEnum>
    static TEnum StringToEnum(const FString& ValueStr)
    {
        if (const UEnum* EnumPtr = StaticEnum<TEnum>())
        {
            return static_cast<TEnum>(EnumPtr->GetValueByNameString(ValueStr));
        }
        return static_cast<TEnum>(0);
    }

    static int32 FlattenAndParseJsonNode(const TSharedPtr<FJsonObject>& JsonObj, FDynamicConditionTreeContainer& OutContainer);
    static TSharedPtr<FJsonObject> ReconstructJsonFromFlatNode(int32 NodeIndex, const FDynamicConditionTreeContainer& TreeContainer);
    static void DeserializeMapWrapper(const TSharedPtr<FJsonObject>& InObject, const FString& FieldName, FDynamicTagFloatMapWrapper& OutWrapper);
    static TSharedPtr<FJsonObject> SerializeMapWrapper(const FDynamicTagFloatMapWrapper& InWrapper);
    static void DeserializeLocalizationMap(const TSharedPtr<FJsonObject>& InObject, const FString& FieldName, FDynamicLocalizationProfile& OutProfile);
    static TSharedPtr<FJsonObject> SerializeLocalizationMap(const FDynamicLocalizationProfile& InProfile);
};