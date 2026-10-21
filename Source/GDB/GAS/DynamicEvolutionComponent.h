// DynamicEvolutionComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DynamicGASTypes.h"
#include "DynamicEvolutionComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnEvolutionStageAdvancedSignature, FGameplayTag, NewStageTag, float, CurrentProgress, float, CurrentScale);

/**
 * Event-Driven Evolution Component.
 * Completely eliminates frame/interval polling ticks across thousands of world actors.
 * Uses predictive completion timestamps and wakes up strictly on stage milestones or environmental shifts.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicEvolutionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDynamicEvolutionComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Evolution")
    FOnEvolutionStageAdvancedSignature OnEvolutionStageAdvanced;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    FGameplayTag EvolutionProfileTag;

    UPROPERTY(ReplicatedUsing = OnRep_EvolutionData, BlueprintReadOnly, Category = "Dynamic GAS|Evolution")
    float CurrentProgressValue = 0.0f;

    UPROPERTY(ReplicatedUsing = OnRep_EvolutionData, BlueprintReadOnly, Category = "Dynamic GAS|Evolution")
    FGameplayTag CurrentStageTag;

    UPROPERTY(ReplicatedUsing = OnRep_TargetProgressTimestamp, BlueprintReadOnly, Category = "Dynamic GAS|Evolution")
    double TargetStageCompletionWorldTime = 0.0;

    UFUNCTION()
    void OnRep_EvolutionData();

    UFUNCTION()
    void OnRep_TargetProgressTimestamp();

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Evolution")
    void ServerAddProgress(float DeltaAmount);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
    FDynamicTagFloatMapWrapper LocalEnvironmentOffsets;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Evolution")
    float CalculateEnvironmentalProgressMultiplier() const;

    // Recalculates expected completion time and schedules the next wake-up timer
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Evolution")
    void RecalculateEvolutionSchedule();

private:
    void HandleScheduledStageReached();
    void EvaluateEvolution(float NormalizedProgress);

    FTimerHandle TimerHandle_StageAdvancement;
    double LastScheduledCalculationWorldTime = 0.0;
};