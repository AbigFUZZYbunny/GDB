// DynamicAttributeSet.h
#pragma once
#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicAttributeSet.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDynamicLimbAttributeChangedSignature, uint8, LimbId, FGameplayTag, AttributeTag, float, NewValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDynamicAttributeChangedSignature, FGameplayTag, AttributeTag, float, NewValue);

struct FScopedAttributeEvaluationGuard
{
    TSet<uint64>& StackRef;
    uint64 Key;
    bool bPushed;

    FScopedAttributeEvaluationGuard(TSet<uint64>& InStack, uint64 InKey)
        : StackRef(InStack), Key(InKey), bPushed(false)
    {
        if (!StackRef.Contains(Key))
        {
            StackRef.Add(Key);
            bPushed = true;
        }
    }
    ~FScopedAttributeEvaluationGuard()
    {
        if (bPushed)
        {
            StackRef.Remove(Key);
        }
    }
    bool IsCycleDetected() const { return !bPushed; }
};

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

    void HandleSingleAttributeReplicated(const FDynamicAttributeItem& Item);

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Attributes")
    float GetDynamicAttributeValue(FGameplayTag AttributeTag, uint8 LimbId = 0) const;

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Attributes")
    void SetDynamicAttributeValue(FGameplayTag AttributeTag, float NewVal, uint8 LimbId = 0);

    void InitializeLimbAttributesFromConfig(const TMap<uint8, FPhysicsLimbConfig>& LimbConfigs);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|UI")
    TArray<FTagFloatPair> GetAttributesByParentTag(FGameplayTag ParentTag, bool bIncludeAllSubAttributes = false, uint8 LimbId = 0) const;

private:
    // GUARANTEED NO COLLISIONS: Uses UE's internal exact FName comparison index instead of a hash function.
    static FORCEINLINE uint64 MakeCompositeKey(uint8 LimbId, const FGameplayTag& Tag)
    {
        return (static_cast<uint64>(LimbId) << 32) | static_cast<uint64>(Tag.GetTagName().GetComparisonIndex().ToUnstableInt());
    }

    TMap<uint64, float> FastCompositeLookupMap;
    TMap<FGameplayTag, FGameplayTagContainer> DirectChildrenTagCache;
    TMap<FGameplayTag, FGameplayTagContainer> AllDescendantsTagCache;

    void IndexAttributeTagIntoHierarchyCache(FGameplayTag LeafTag);
    void EvaluateMilestoneChanges(FGameplayTag AttributeTag, float OldValue, float NewValue, AActor* TargetActor, uint8 LimbId);
    void ApplyOrUndoMilestoneStep(const FDynamicMilestoneAction& Action, bool bIsUndo, UAbilitySystemComponent* TargetASC, uint8 LimbId);

    TSet<uint64> ActiveThresholdEvaluationKeys;
    void SetDynamicAttributeValueInternal(FGameplayTag AttributeTag, float NewVal, uint8 LimbId, TSet<uint64>& InOutEvaluationStack);
};