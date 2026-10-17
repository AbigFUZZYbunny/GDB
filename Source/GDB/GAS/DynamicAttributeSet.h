// DynamicAttributeSet.h
#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicAttributeSet.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDynamicLimbAttributeChangedSignature, uint8, LimbId, FGameplayTag, AttributeTag, float, NewValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDynamicAttributeChangedSignature, FGameplayTag, AttributeTag, float, NewValue);

UCLASS()
class GDB_API UDynamicAttributeSet : public UAttributeSet
{
    GENERATED_BODY()

public:
    UDynamicAttributeSet();

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|UI")
    FDynamicAttributeChangedSignature OnDynamicAttributeChanged;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|UI")
    FDynamicLimbAttributeChangedSignature OnDynamicLimbAttributeChanged;

    UPROPERTY(ReplicatedUsing = OnRep_FastAttributes)
    FFastAttributeArray FastAttributes;

    UFUNCTION()
    void OnRep_FastAttributes();

    // Incremental O(1) handler invoked by FFastArraySerializer item callbacks
    void HandleSingleAttributeReplicated(const FDynamicAttributeItem& Item);

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // LimbId = 0 queries global character attribute; LimbId >= 1 queries specific limb attribute
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Attributes")
    float GetDynamicAttributeValue(FGameplayTag AttributeTag, uint8 LimbId = 0) const;

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Attributes")
    void SetDynamicAttributeValue(FGameplayTag AttributeTag, float NewVal, uint8 LimbId = 0);

    // Initializes all default attributes for a dynamic skeletal limb configuration
    void InitializeLimbAttributesFromConfig(const TMap<uint8, FPhysicsLimbConfig>& LimbConfigs);

    /**
     * O(1) Pre-Cached UI Query:
     * Given a parent tag (e.g. "Attribute.Stat"), returns all matching sub-attributes and their current values.
     * @param ParentTag The root tag to query
     * @param bIncludeAllSubAttributes If false, returns ONLY immediate surface children. If true, returns all deep descendants.
     * @param LimbId 0 for global character attributes, or 1..255 for specific limb attributes.
     */
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|UI")
    TArray<FTagFloatPair> GetAttributesByParentTag(FGameplayTag ParentTag, bool bIncludeAllSubAttributes = false, uint8 LimbId = 0) const;

private:
    static FORCEINLINE uint64 MakeCompositeKey(uint8 LimbId, const FGameplayTag& Tag)
    {
        return (static_cast<uint64>(LimbId) << 32) | static_cast<uint64>(GetTypeHash(Tag));
    }

    // O(1) Composite lookup map: (LimbId << 32 | TagHash) -> float Value
    TMap<uint64, float> FastCompositeLookupMap;

    // Pre-computed O(1) caches for UI queries
    TMap<FGameplayTag, FGameplayTagContainer> DirectChildrenTagCache;
    TMap<FGameplayTag, FGameplayTagContainer> AllDescendantsTagCache;

    void IndexAttributeTagIntoHierarchyCache(FGameplayTag LeafTag);
    void EvaluateMilestoneChanges(FGameplayTag AttributeTag, float OldValue, float NewValue, AActor* TargetActor, uint8 LimbId);
    void ApplyOrUndoMilestoneStep(const FDynamicMilestoneAction& Action, bool bIsUndo, UAbilitySystemComponent* TargetASC, uint8 LimbId);
};