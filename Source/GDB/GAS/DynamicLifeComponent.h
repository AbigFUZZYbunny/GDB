// DynamicLifeComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DynamicGASTypes.h"
#include "DynamicLifeComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnContinuousGrowthUpdatedSignature, float, CurrentScale, float, MotorStrength, FGameplayTag, LocomotionTag);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGeneExpressedSignature, FGameplayTag, LocusTag, FGameplayTag, ExpressedAlleleTag);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicLifeComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDynamicLifeComponent();

    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Life")
    FOnContinuousGrowthUpdatedSignature OnContinuousGrowthUpdated;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Life")
    FGeneExpressedSignature OnGeneExpressed;

    UPROPERTY(ReplicatedUsing = OnRep_AgeData, BlueprintReadOnly, Category = "Dynamic GAS|Life")
    double BirthWorldTime = 0.0;

    UPROPERTY(ReplicatedUsing = OnRep_Locomotion, BlueprintReadOnly, Category = "Dynamic GAS|Life")
    FGameplayTag CurrentLocomotionAnimTag;

    UPROPERTY(ReplicatedUsing = OnRep_Genome)
    FFastGenomeArray ReplicatedGenome;

    UFUNCTION() void OnRep_AgeData();
    UFUNCTION() void OnRep_Locomotion();
    UFUNCTION() void OnRep_Genome();

    void HandleGeneReplicated(const FDynamicGenePairItem& Item);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Life")
    float GetCurrentAgeYears() const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Life")
    float GetNormalizedAge() const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Life")
    void ServerEvaluateContinuousGrowth();

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Life")
    void ServerInheritGenetics(const UDynamicLifeComponent* Mother, const UDynamicLifeComponent* Father);
    
    // Returns the active life stage tag (e.g. Age.Stage.Child vs Age.Stage.Adult) based on current age
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Life")
    FGameplayTag GetCurrentLifeStageTag() const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Life")
    bool IsCharacterAdult() const;

private:
    void ApplyLerpedGrowthState(float NormalizedAge);
    void ApplyGenePhenotypes();
    FGameplayTag ResolveDominance(FGameplayTag Locus, FGameplayTag AlleleA, FGameplayTag AlleleB) const;

    FTimerHandle TimerHandle_GrowthTick;

    // Cache of authoring-time baseline morph values, preventing recursive drift during aging calculations
    TMap<FGameplayTag, float> CachedBaseMorphSliders;
    bool bBaseMorphsCached = false;
};