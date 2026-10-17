// DynamicGameplayAbility.h
#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "DynamicGASTypes.h"
#include "DynamicGameplayAbility.generated.h"

class UDynamicInteractableComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDynamicAbilityConfigChangedSignature, const FDynamicAbilityDef&, NewConfig);

UCLASS()
class GDB_API UDynamicGameplayAbility : public UGameplayAbility
{
    GENERATED_BODY()

public:
    UDynamicGameplayAbility();

    UPROPERTY(BlueprintAssignable, Category = "Dynamic Ability|Events")
    FDynamicAbilityConfigChangedSignature OnAbilityConfigChanged;

    UPROPERTY(ReplicatedUsing = OnRep_AbilityConfig, BlueprintReadOnly, Category = "Dynamic Configuration")
    FDynamicAbilityDef AbilityConfig;

    UPROPERTY(Replicated, BlueprintReadWrite, Category = "Dynamic Configuration")
    TArray<FGameplayTag> CustomSpecChainTags;

    UFUNCTION()
    void OnRep_AbilityConfig();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
    virtual bool CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, OUT FGameplayTagContainer* OptionalRelevantTags) const override;
    virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, OUT FGameplayTagContainer* OptionalRelevantTags) const override;
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
    virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) override;

    UFUNCTION(BlueprintCallable, Server, Reliable, Category = "Dynamic Ability")
    void UpdateConfigAtRuntime(FDynamicAbilityDef NewConfigLayout);

    // Validates and assigns sequential tier chain (e.g. Tier 1 -> Tier 2 -> Tier 3)
    bool SetCustomSpecChain(const TArray<FGameplayTag>& InChainTags);

    static bool IsValidNextTierTransition(const FDynamicAbilityDef& CurrentStageDef, const FDynamicAbilityDef& NextStageDef);

protected:
    bool EvaluateDynamicRule(const FGameplayTag& RuleTag, const FGameplayAbilityActorInfo* ActorInfo) const;
    UDynamicInteractableComponent* FindBestNearbyInteractable(const FGameplayAbilityActorInfo* ActorInfo) const;
    void PlayStagePresentation(const FAbilityStagePresentation& StageVisual) const;
    void GrantSourcePassBack(EPassBackTriggerEvent EventType, float Scalar = 1.0f) const;

    void ExecuteSpatialDelivery(float ChargeAlpha);
    void ExecuteInteractionBehavior(float ChargeAlpha);
    void ExecuteChainBehavior(const FGameplayEventData* TriggerEventData);
    void ExecuteMovementBehavior();
    void FinalizeRelease(float FinalChargeAlpha);

    UFUNCTION()
    void HandleTickDrain(float TotalElapsedTime, float DeltaTime);

    UFUNCTION()
    void HandleMaxChargeReached();

private:
    float RuntimeTrackedChargeTime = 0.0f;
    bool bServerApprovedActivation = false;
    int32 CurrentChainStageIndex = 0;
    EValidationPhase CurrentPhase = EValidationPhase::Attempt;
};