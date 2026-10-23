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
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnPlayerHydrationCompletedSignature, bool, bSuccess);

UCLASS()
class GDB_API UDynamicDataSerializer : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Serialization")
    static bool DeserializeMasterRegistry(const FString& InJsonString, ADynamicGameState* TargetGameState);

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
    
    static void DeserializeMovementStatConfig(const TSharedPtr<FJsonObject>& InObject, const FString& FieldName, FMovementStatConfig& OutConfig);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Persistence")
    static bool SerializePlayerToSnapshotJson(const ADynamicCharacter* InCharacter, FString& OutJsonPayload);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Persistence")
    static bool DeserializePlayerFromSnapshotJson(ADynamicCharacter* TargetCharacter, const FString& InJsonPayload);

    // Asynchronous thread pool parsing that marshals component hydration back to GameThread
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Persistence")
    static void DeserializePlayerFromSnapshotJsonAsync(
        ADynamicCharacter* TargetCharacter, 
        const FString& InJsonPayload, 
        FOnPlayerHydrationCompletedSignature OnComplete
    );

    static void DeserializePlayerFromSnapshotJsonAsyncNative(
        ADynamicCharacter* TargetCharacter, 
        const FString& InJsonPayload, 
        TFunction<void(bool)> OnComplete = nullptr
    );

    static bool ParseSnapshotFromJsonString(const FString& InJsonPayload, FPlayerPersistenceSnapshot& OutSnapshot);
    static bool ApplySnapshotToPlayerCharacter(ADynamicCharacter* TargetCharacter, const FPlayerPersistenceSnapshot& Snapshot);

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