// DynamicAttributeSet.h
#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicAttributeSet.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDynamicAttributeChangedSignature, FGameplayTag, AttributeTag, float, NewValue);

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

    float GetDynamicAttributeValue(FGameplayTag AttributeTag) const;
    void SetDynamicAttributeValue(FGameplayTag AttributeTag, float NewVal);

private:
    TMap<FGameplayTag, float> FastLookupMap;
    void EvaluateMilestoneIncreases(FGameplayTag AttributeTag, float OldValue, float NewValue, AActor* TargetActor);
};