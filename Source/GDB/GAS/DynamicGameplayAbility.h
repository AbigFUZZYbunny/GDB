// DynamicGameplayAbility.h
#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "DynamicGASTypes.h"
#include "DynamicGameplayAbility.generated.h"

class UDynamicInteractableComponent;
class UAT_WaitChargeTime;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDynamicAbilityConfigChangedSignature, const FDynamicAbilityDef&, NewConfig);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDynamicComboWindowChangedSignature, FGameplayTag, AvailableComboAbilityTag, bool, bIsOpen);

UCLASS()
class GDB_API UDynamicGameplayAbility : public UGameplayAbility
{
    GENERATED_BODY()

public:
    UDynamicGameplayAbility();

    UPROPERTY(BlueprintAssignable, Category = "Dynamic Ability|Events")
    FDynamicAbilityConfigChangedSignature OnAbilityConfigChanged;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic Ability|Combo")
    FDynamicComboWindowChangedSignature OnComboWindowChanged;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic Configuration")
    FDynamicAbilityDef AbilityConfig;

    UPROPERTY(ReplicatedUsing = OnRep_ActiveAbilityRegistryTag, BlueprintReadOnly, Category = "Dynamic Configuration")
    FGameplayTag ActiveAbilityRegistryTag;

    UFUNCTION()
    void OnRep_ActiveAbilityRegistryTag();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
    
    // Cooldown lifecycle methods
    virtual bool CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, OUT FGameplayTagContainer* OptionalRelevantTags) const override;
    virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;

    virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, OUT FGameplayTagContainer* OptionalRelevantTags) const override;
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
    virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) override;
    virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

    UFUNCTION(BlueprintCallable, Server, Reliable, Category = "Dynamic Ability")
    void UpdateConfigAtRuntime(FDynamicAbilityDef NewConfigLayout);

    UFUNCTION(BlueprintPure, Category = "Dynamic Ability|Combo")
    bool IsComboWindowOpenForAbility(FGameplayTag RequestedComboTag) const;

    UFUNCTION(BlueprintCallable, Category = "Dynamic Ability|Combo")
    bool TryTriggerComboTransition(FGameplayTag NextComboAbilityTag);

    UFUNCTION(Server, Reliable, WithValidation)
    void Server_TryTriggerComboTransition(FGameplayTag NextComboAbilityTag);

    static bool CanBypassComboGate(const FDynamicAbilityDef& DefToCheck, const UAbilitySystemComponent* CasterASC);

    EValidationPhase GetCurrentPhase() const { return CurrentPhase; }

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Delivery|Validation")
    bool IsValidTargetForSpatialDelivery(const FDynamicSpatialDeliveryDef& Delivery, const AActor* TargetActor) const;

protected:
    bool EvaluateDynamicRule(const FGameplayTag& RuleTag, const FGameplayAbilityActorInfo* ActorInfo) const;
    UDynamicInteractableComponent* FindBestNearbyInteractable(const FGameplayAbilityActorInfo* ActorInfo) const;
    void PlayStagePresentation(const FAbilityStagePresentation& StageVisual) const;
    void GrantSourcePassBack(EPassBackTriggerEvent EventType, float Scalar = 1.0f) const;
    void RefreshOpenComboWindows(float CurrentPhaseElapsed, float CurrentPhaseAlpha);
    bool TransitionIntoComboAbilityInternal(FGameplayTag NextComboAbilityTag);
    void EnterRecoveryPhase();
    void ConcludeRecoveryPhase();
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
    void HydrateConfigFromRegistry(FGameplayTag AbilityTag);
    float RuntimeTrackedChargeTime = 0.0f;
    double PhaseStartWorldTime = 0.0;
    bool bServerApprovedActivation = false;
    bool bActivatedViaComboTransition = false;
    EValidationPhase CurrentPhase = EValidationPhase::Attempt;
    FGameplayTagContainer CurrentlyOpenComboTags;
    FTimerHandle TimerHandle_RecoveryWindow;

    UPROPERTY()
    TObjectPtr<UAT_WaitChargeTime> ActiveChargeTask;
};