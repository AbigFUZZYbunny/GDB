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

    UPROPERTY(ReplicatedUsing = OnRep_AttributeMasterRegistryArr) TArray<FTagAttributePair> AttributeMasterRegistryArr;
    UPROPERTY(ReplicatedUsing = OnRep_ProjectileArchetypesArr) TArray<FTagProjectilePair> ProjectileArchetypesArr;
    UPROPERTY(ReplicatedUsing = OnRep_AoeArchetypesArr) TArray<FTagAoePair> AoeArchetypesArr;
    UPROPERTY(ReplicatedUsing = OnRep_AbilityMasterRegistryArr) TArray<FTagAbilityDefPair> AbilityMasterRegistryArr;
    UPROPERTY(ReplicatedUsing = OnRep_SharedConditionTreesArr) TArray<FNameConditionPair> SharedConditionTreesArr;
    UPROPERTY(ReplicatedUsing = OnRep_MasterMilestoneRegistryArr) TArray<FTagMilestonePair> MasterMilestoneRegistryArr;
    UPROPERTY(ReplicatedUsing = OnRep_MasterThresholdRegistryArr) TArray<FTagThresholdPair> MasterThresholdRegistryArr;
    UPROPERTY(ReplicatedUsing = OnRep_MasterEffectRegistryArr) TArray<FTagEffectPair> MasterEffectRegistryArr;

    TMap<FGameplayTag, FDynamicAttributeDef> AttributeMasterRegistry;
    TMap<FGameplayTag, FDynamicProjectileArchetype> ProjectileArchetypes;
    TMap<FGameplayTag, FDynamicAoeArchetype> AoeArchetypes;
    TMap<FGameplayTag, FDynamicAbilityDef> AbilityMasterRegistry;
    TMap<FName, FDynamicConditionTreeContainer> SharedConditionTrees;
    TMap<FGameplayTag, FDynamicMilestoneContainer> MasterMilestoneRegistry;
    TMap<FGameplayTag, FDynamicThresholdContainer> MasterThresholdRegistry;
    TMap<FGameplayTag, FDynamicEffectDef> MasterEffectRegistry;

    void RegisterNewDynamicAbility(FGameplayTag AbilityTag, FDynamicAbilityDef AbilityDef, TMap<FName, FDynamicConditionTreeContainer> AssociatedConditions);
    void RegisterNewDynamicEffect(FGameplayTag EffectTag, FDynamicEffectDef EffectDef, TMap<FName, FDynamicConditionTreeContainer> AssociatedConditions);

    // --- Physics Mappings ---
    void SetPhysicsConfig(const FPhysicsDamageConfigData& ConfigData);
    uint8 GetLimbIdForBone(const FName& BoneName) const;
    const FPhysicsLimbConfig* GetLimbData(uint8 LimbId) const;
    const FPhysicsCalculationSettings& GetCalculationSettings() const { return CalculationSettings; }    
    const TMap<uint8, FPhysicsLimbConfig>& GetLimbLookupMap() const { return LimbLookupMap; }

protected:
    UFUNCTION() void OnRep_AttributeMasterRegistryArr();
    UFUNCTION() void OnRep_ProjectileArchetypesArr();
    UFUNCTION() void OnRep_AoeArchetypesArr();
    UFUNCTION() void OnRep_AbilityMasterRegistryArr();
    UFUNCTION() void OnRep_SharedConditionTreesArr();
    UFUNCTION() void OnRep_MasterMilestoneRegistryArr();
    UFUNCTION() void OnRep_MasterThresholdRegistryArr();
    UFUNCTION() void OnRep_MasterEffectRegistryArr();

private:
    UPROPERTY(Replicated) FPhysicsCalculationSettings CalculationSettings;
    TMap<FName, uint8> BoneToLimbMap;
    TMap<uint8, FPhysicsLimbConfig> LimbLookupMap;
};