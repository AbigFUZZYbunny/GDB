#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicRadialMenuComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRadialPresetChangedSignature, FGameplayTag, MenuTag, int32, NewPresetIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDynamicRadialGatheredSignature, FGameplayTag, MenuTag, const FRadialPreset&, GatheredPreset);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnRadialActionTriggeredSignature, FGameplayTag, MenuTag, FGameplayTag, ActionTag, FGuid, ContextID);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicRadialMenuComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDynamicRadialMenuComponent();

    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic Radial")
    FOnRadialPresetChangedSignature OnRadialPresetChanged;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic Radial")
    FOnDynamicRadialGatheredSignature OnDynamicRadialGathered;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic Radial")
    FOnRadialActionTriggeredSignature OnRadialActionTriggered;

    UPROPERTY(Replicated)
    TArray<FDynamicRadialConfig> ConfiguredMenus;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Dynamic Radial")
    TMap<FGameplayTag, FRadialPreset> TransientGatheredMenus;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic Radial")
    void SetSlotBinding(FGameplayTag MenuTag, int32 PresetIndex, int32 SlotIndex, FGameplayTag AssignedTag, FGuid ContextID = FGuid());

    UFUNCTION(BlueprintCallable, Category = "Dynamic Radial")
    void CycleActivePreset(FGameplayTag MenuTag, bool bForward);

    UFUNCTION(Server, Reliable, Category = "Dynamic Radial")
    void Server_CycleActivePreset(FGameplayTag MenuTag, int32 NewIndex);

    UFUNCTION(BlueprintCallable, Category = "Dynamic Radial")
    void ExecuteRadialSlot(FGameplayTag MenuTag, int32 SlotIndex);

    UFUNCTION(Server, Reliable, Category = "Dynamic Radial")
    void Server_ExecuteRadialSlot(FGameplayTag MenuTag, int32 PresetIndex, int32 SlotIndex);

    UFUNCTION(BlueprintPure, Category = "Dynamic Radial")
    int32 GetSlotIndexFromInputVector(FVector2D InputDelta, int32 TotalSlots = 8) const;

    UFUNCTION(BlueprintCallable, Category = "Dynamic Radial")
    void GatherDynamicRadial(FGameplayTag MenuTag);

    UFUNCTION(BlueprintCallable, Category = "Dynamic Radial")
    bool HandleRadialInputAction(FGameplayTag InputActionTag, bool bIsPressed);

    // FIXED: Passed as OutConfig reference to comply with UHT
    UFUNCTION(BlueprintPure, Category = "Dynamic Radial")
    bool GetMenuConfig(FGameplayTag MenuTag, FDynamicRadialConfig& OutConfig) const;

    // Helper for direct C++ calls without copying
    const FDynamicRadialConfig* FindMenuConfigPtr(FGameplayTag MenuTag) const;

    // Unified Token Resolver
    UFUNCTION(BlueprintPure, Category = "Dynamic Menu|Navigation")
    FGameplayTag ResolveMenuButtonAction(FGameplayTag MenuTag, const FString& ItemName) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic Menu|Localization")
    FString ResolveMenuTitle(FGameplayTag MenuTag, const FString& ItemName) const;

private:
    FDynamicRadialConfig* FindMenuConfigMutable(FGameplayTag MenuTag);
    void ExecuteResolvedAction(FGameplayTag MenuTag, const FRadialSlotData& SlotData);
    static FString ExtractMenuTagLeaf(FGameplayTag MenuTag);
};