// DynamicGameState.cpp
#include "DynamicGameState.h"
#include "Net/UnrealNetwork.h"

ADynamicGameState::ADynamicGameState()
{
    bReplicates = true;
}

void ADynamicGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    
    // FIX: Replicate the attribute array container down to players
    DOREPLIFETIME(ADynamicGameState, AttributeMasterRegistryArr);
    
    DOREPLIFETIME(ADynamicGameState, ProjectileArchetypesArr);
    DOREPLIFETIME(ADynamicGameState, AoeArchetypesArr);
    DOREPLIFETIME(ADynamicGameState, AbilityMasterRegistryArr);
    DOREPLIFETIME(ADynamicGameState, SharedConditionTreesArr);
    DOREPLIFETIME(ADynamicGameState, MasterMilestoneRegistryArr);
    DOREPLIFETIME(ADynamicGameState, MasterThresholdRegistryArr);
}

// FIX: Added the missing replication rebuild callback handler
void ADynamicGameState::OnRep_AttributeMasterRegistryArr()
{
    AttributeMasterRegistry.Empty();
    for (const auto& Element : AttributeMasterRegistryArr)
    {
        AttributeMasterRegistry.Add(Element.KeyTag, Element.ValueDef);
    }
}


void ADynamicGameState::RegisterNewDynamicAbility(FGameplayTag AbilityTag, FDynamicAbilityDef AbilityDef, TMap<FName, FDynamicConditionTreeContainer> AssociatedConditions)
{
    if (!HasAuthority() || !AbilityTag.IsValid()) return;

    AbilityMasterRegistry.Add(AbilityTag, AbilityDef);
    for (const auto& CondPair : AssociatedConditions)
    {
        SharedConditionTrees.Add(CondPair.Key, CondPair.Value);
    }

    FTagAbilityDefPair NewAbPair; NewAbPair.KeyTag = AbilityTag; NewAbPair.ValueDef = AbilityDef;
    AbilityMasterRegistryArr.Add(NewAbPair);

    for (const auto& CondPair : AssociatedConditions)
    {
        FNameConditionPair NewCondPair; NewCondPair.KeyName = CondPair.Key; NewCondPair.ValueNode = CondPair.Value;
        SharedConditionTreesArr.Add(NewCondPair);
    }
}

void ADynamicGameState::OnRep_ProjectileArchetypesArr()
{
    ProjectileArchetypes.Empty();
    for (auto& Element : ProjectileArchetypesArr) { Element.ValueArchetype.Metrics.SyncMapFromChangesArray(); ProjectileArchetypes.Add(Element.KeyTag, Element.ValueArchetype); }
}

void ADynamicGameState::OnRep_AoeArchetypesArr()
{
    AoeArchetypes.Empty();
    for (auto& Element : AoeArchetypesArr) { Element.ValueArchetype.Metrics.SyncMapFromChangesArray(); AoeArchetypes.Add(Element.KeyTag, Element.ValueArchetype); }
}

void ADynamicGameState::OnRep_AbilityMasterRegistryArr()
{
    AbilityMasterRegistry.Empty();
    for (auto& Element : AbilityMasterRegistryArr) 
    { 
        Element.ValueDef.NumericParams.SyncMapFromChangesArray(); 
        Element.ValueDef.ProjectileConfig.RuntimeMetricModifiers.SyncMapFromChangesArray(); 
        Element.ValueDef.AoeConfig.RuntimeMetricModifiers.SyncMapFromChangesArray(); 
        AbilityMasterRegistry.Add(Element.KeyTag, Element.ValueDef); 
    }
}

void ADynamicGameState::OnRep_SharedConditionTreesArr()
{
    SharedConditionTrees.Empty();
    for (const auto& Element : SharedConditionTreesArr) { SharedConditionTrees.Add(Element.KeyName, Element.ValueNode); }
}

void ADynamicGameState::OnRep_MasterMilestoneRegistryArr()
{
    MasterMilestoneRegistry.Empty();
    for (const auto& Element : MasterMilestoneRegistryArr) { MasterMilestoneRegistry.Add(Element.KeyTag, Element.ValueContainer); }
}

void ADynamicGameState::OnRep_MasterThresholdRegistryArr()
{
    MasterThresholdRegistry.Empty();
    for (const auto& Element : MasterThresholdRegistryArr) { MasterThresholdRegistry.Add(Element.KeyTag, Element.ValueContainer); }
}