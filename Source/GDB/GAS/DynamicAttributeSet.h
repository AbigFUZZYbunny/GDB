// DynamicAttributeSet.h
#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicAttributeSet.generated.h"

// --- Dynamic UI Listening Delegate ---
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDynamicAttributeChangedSignature, FGameplayTag, AttributeTag, float, NewValue);

USTRUCT(BlueprintType)
struct FReplicatedAttributeMap
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes")
    TArray<FTagFloatPair> ReplicatedEntries;

    UPROPERTY(NotReplicated)
    TMap<FGameplayTag, float> LocalMapCache;

    void RebuildCacheFromTransportArray()
    {
        LocalMapCache.Empty();
        for (const FTagFloatPair& Pair : ReplicatedEntries)
        {
            if (Pair.KeyTag.IsValid()) LocalMapCache.Add(Pair.KeyTag, Pair.Value);
        }
    }
};

UCLASS()
class GDB_API UDynamicAttributeSet : public UAttributeSet
{
    GENERATED_BODY()

public:
    UDynamicAttributeSet();

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|UI")
    FDynamicAttributeChangedSignature OnDynamicAttributeChanged;
    
    UPROPERTY(ReplicatedUsing = OnRep_FastAttributes)
    FFastAttributeArray FastAttributes;

    UFUNCTION()
    void OnRep_FastAttributes();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;
    
    float GetDynamicAttributeValue(FGameplayTag AttributeTag) const;
    void SetDynamicAttributeValue(FGameplayTag AttributeTag, float NewVal);

private:
    TMap<FGameplayTag, float> FastLookupMap;
    
    void EvaluateMilestoneIncreases(FGameplayTag AttributeTag, float OldValue, float NewValue, AActor* TargetActor);
};