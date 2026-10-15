// DynamicGameState.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "DynamicGASTypes.h"
#include "DynamicGameState.generated.h"

USTRUCT(BlueprintType)
struct FTagAttributePair
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag KeyTag;
    UPROPERTY() FDynamicAttributeDef ValueDef;
};

UCLASS()
class GDB_API ADynamicGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    ADynamicGameState();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // --- UHT-Safe Transport Containers ---
    UPROPERTY(ReplicatedUsing = OnRep_AttributeMasterRegistryArr)
    TArray<FTagAttributePair> AttributeMasterRegistryArr; // FIX: Added missing array container

    UPROPERTY(ReplicatedUsing = OnRep_ProjectileArchetypesArr)
    TArray<FTagProjectilePair> ProjectileArchetypesArr;

    UPROPERTY(ReplicatedUsing = OnRep_AoeArchetypesArr)
    TArray<FTagAoePair> AoeArchetypesArr;

    UPROPERTY(ReplicatedUsing = OnRep_AbilityMasterRegistryArr)
    TArray<FTagAbilityDefPair> AbilityMasterRegistryArr;

    UPROPERTY(ReplicatedUsing = OnRep_SharedConditionTreesArr)
    TArray<FNameConditionPair> SharedConditionTreesArr;

    UPROPERTY(ReplicatedUsing = OnRep_MasterMilestoneRegistryArr)
    TArray<FTagMilestonePair> MasterMilestoneRegistryArr;

    UPROPERTY(ReplicatedUsing = OnRep_MasterThresholdRegistryArr)
    TArray<FTagThresholdPair> MasterThresholdRegistryArr;

    // --- Local Fast Authoritative Lookups ---
    TMap<FGameplayTag, FDynamicAttributeDef> AttributeMasterRegistry; // FIX: Added missing map
    TMap<FGameplayTag, FDynamicProjectileArchetype> ProjectileArchetypes;
    TMap<FGameplayTag, FDynamicAoeArchetype> AoeArchetypes;
    TMap<FGameplayTag, FDynamicAbilityDef> AbilityMasterRegistry;
    TMap<FName, FDynamicConditionTreeContainer> SharedConditionTrees;
    TMap<FGameplayTag, FDynamicMilestoneContainer> MasterMilestoneRegistry;
    TMap<FGameplayTag, FDynamicThresholdContainer> MasterThresholdRegistry;

    void RegisterNewDynamicAbility(FGameplayTag AbilityTag, FDynamicAbilityDef AbilityDef, TMap<FName, FDynamicConditionTreeContainer> AssociatedConditions);

protected:
    UFUNCTION() void OnRep_AttributeMasterRegistryArr(); // FIX: Added missing OnRep descriptor
    UFUNCTION() void OnRep_ProjectileArchetypesArr();
    UFUNCTION() void OnRep_AoeArchetypesArr();
    UFUNCTION() void OnRep_AbilityMasterRegistryArr();
    UFUNCTION() void OnRep_SharedConditionTreesArr();
    UFUNCTION() void OnRep_MasterMilestoneRegistryArr();
    UFUNCTION() void OnRep_MasterThresholdRegistryArr();
};