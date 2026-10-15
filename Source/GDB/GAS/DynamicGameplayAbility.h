// DynamicGameplayAbility.h
#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "DynamicGASTypes.h"
#include "DynamicGameplayAbility.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDynamicAbilityConfigChangedSignature, const FDynamicAbilityDef&, NewConfig);

UCLASS()
class GDB_API UDynamicGameplayAbility : public UGameplayAbility
{
    GENERATED_BODY()

public:
    UDynamicGameplayAbility();

    UPROPERTY(BlueprintAssignable, Category = "Dynamic Ability|Events")
    FDynamicAbilityConfigChangedSignature OnAbilityConfigChanged;

    // --- Master Data-Driven Definition Profile ---
    UPROPERTY(ReplicatedUsing = OnRep_AbilityConfig, BlueprintReadOnly, Category = "Dynamic Configuration")
    FDynamicAbilityDef AbilityConfig;

    UFUNCTION()
    void OnRep_AbilityConfig();

    // --- Native GAS Operational Validations ---
    virtual bool CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, OUT FGameplayTagContainer* OptionalRelevantTags) const override;
    virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, OUT FGameplayTagContainer* OptionalRelevantTags) const override;

    // --- Native GAS Activation Pipelines ---
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
    virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) override;

    // --- Runtime Mutability Endpoint ---
    UFUNCTION(BlueprintCallable, Server, Reliable, Category = "Dynamic Ability")
    void UpdateConfigAtRuntime(FDynamicAbilityDef NewConfigLayout);

protected:
    // --- Abstracted Internal Evaluation Gates ---
    bool EvaluateDynamicRule(const FGameplayTag& RuleTag, const FGameplayAbilityActorInfo* ActorInfo) const;

    // --- Core Execution Branches ---
    void ExecuteAuraBehavior();
    void ExecuteChainBehavior(const FGameplayEventData* TriggerEventData);
    void ExecuteMovementBehavior();
    void FinalizeRelease(float FinalChargeAlpha);

    // --- Asynchronous AbilityTask Callback Links ---
    UFUNCTION()
    void HandleTickDrain(float TotalElapsedTime, float DeltaTime);

    UFUNCTION()
    void HandleMaxChargeReached();

private:
    // --- Transient Non-Replicated Lifecycle States ---
    float RuntimeTrackedChargeTime = 0.0f;
    bool bServerApprovedActivation = false;
    
    // Tracks active phase constraint maps for redirecting CheckCost/CheckCooldown routines safely
    EValidationPhase CurrentPhase = EValidationPhase::Attempt;
};