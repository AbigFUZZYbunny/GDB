// DynamicGameState.cpp
#include "DynamicGameState.h"
#include "Net/UnrealNetwork.h"

ADynamicGameState::ADynamicGameState() { bReplicates = true; }

void ADynamicGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADynamicGameState, ProjectileArchetypesArr);
    DOREPLIFETIME(ADynamicGameState, AoeArchetypesArr);
    DOREPLIFETIME(ADynamicGameState, AbilityMasterRegistryArr);
    DOREPLIFETIME(ADynamicGameState, SharedConditionTreesArr);
    DOREPLIFETIME(ADynamicGameState, MasterMilestoneRegistryArr);
    DOREPLIFETIME(ADynamicGameState, MasterThresholdRegistryArr);
    DOREPLIFETIME(ADynamicGameState, MasterEffectRegistryArr);
    DOREPLIFETIME(ADynamicGameState, AttributeMasterRegistryArr);
    DOREPLIFETIME(ADynamicGameState, CalculationSettings);
}

void ADynamicGameState::RegisterNewDynamicAbility(FGameplayTag AbilityTag, FDynamicAbilityDef AbilityDef, TMap<FName, FDynamicConditionTreeContainer> AssociatedConditions)
{
    if (!HasAuthority() || !AbilityTag.IsValid()) return;
    AbilityMasterRegistry.Add(AbilityTag, AbilityDef);
    FTagAbilityDefPair NewAbPair; NewAbPair.KeyTag = AbilityTag; NewAbPair.ValueDef = AbilityDef;
    AbilityMasterRegistryArr.Add(NewAbPair);
    for (const auto& CondPair : AssociatedConditions) {
        SharedConditionTrees.Add(CondPair.Key, CondPair.Value);
        FNameConditionPair NewCondPair; NewCondPair.KeyName = CondPair.Key; NewCondPair.ValueNode = CondPair.Value;
        SharedConditionTreesArr.Add(NewCondPair);
    }
}

void ADynamicGameState::RegisterNewDynamicEffect(FGameplayTag EffectTag, FDynamicEffectDef EffectDef, TMap<FName, FDynamicConditionTreeContainer> AssociatedConditions)
{
    if (!HasAuthority() || !EffectTag.IsValid()) return;
    MasterEffectRegistry.Add(EffectTag, EffectDef);
    FTagEffectPair NewEffectPair; NewEffectPair.KeyTag = EffectTag; NewEffectPair.ValueDef = EffectDef;
    MasterEffectRegistryArr.Add(NewEffectPair);
    for (const auto& CondPair : AssociatedConditions) {
        SharedConditionTrees.Add(CondPair.Key, CondPair.Value);
        FNameConditionPair NewCondPair; NewCondPair.KeyName = CondPair.Key; NewCondPair.ValueNode = CondPair.Value;
        SharedConditionTreesArr.Add(NewCondPair);
    }
}

void ADynamicGameState::SetPhysicsConfig(const FPhysicsDamageConfigData& ConfigData)
{
    CalculationSettings = ConfigData.CalculationSettings;
    LimbLookupMap.Empty();
    BoneToLimbMap.Empty();
    for (const FPhysicsLimbConfig& Limb : ConfigData.Limbs)
    {
        LimbLookupMap.Add(Limb.LimbId, Limb);
        for (const FName& BoneName : Limb.Bones) BoneToLimbMap.Add(BoneName, Limb.LimbId);
    }
}

uint8 ADynamicGameState::GetLimbIdForBone(const FName& BoneName) const {
    if (const uint8* FoundId = BoneToLimbMap.Find(BoneName)) return *FoundId;
    return 1; // Fallback: Torso
}
const FPhysicsLimbConfig* ADynamicGameState::GetLimbData(uint8 LimbId) const {
    return LimbLookupMap.Find(LimbId);
}

void ADynamicGameState::OnRep_MasterEffectRegistryArr() {
    MasterEffectRegistry.Empty();
    for (const FTagEffectPair& Element : MasterEffectRegistryArr) MasterEffectRegistry.Add(Element.KeyTag, Element.ValueDef);
}
void ADynamicGameState::OnRep_AttributeMasterRegistryArr() {
    AttributeMasterRegistry.Empty();
    for (const auto& Element : AttributeMasterRegistryArr) AttributeMasterRegistry.Add(Element.KeyTag, Element.ValueDef);
}
void ADynamicGameState::OnRep_ProjectileArchetypesArr() {
    ProjectileArchetypes.Empty();
    for (auto& Element : ProjectileArchetypesArr) { Element.ValueArchetype.Metrics.SyncMapFromChangesArray(); ProjectileArchetypes.Add(Element.KeyTag, Element.ValueArchetype); }
}
void ADynamicGameState::OnRep_AoeArchetypesArr() {
    AoeArchetypes.Empty();
    for (auto& Element : AoeArchetypesArr) { Element.ValueArchetype.Metrics.SyncMapFromChangesArray(); AoeArchetypes.Add(Element.KeyTag, Element.ValueArchetype); }
}
void ADynamicGameState::OnRep_AbilityMasterRegistryArr() {
    AbilityMasterRegistry.Empty();
    for (auto& Element : AbilityMasterRegistryArr) {
        Element.ValueDef.NumericParams.SyncMapFromChangesArray();
        Element.ValueDef.ProjectileConfig.RuntimeMetricModifiers.SyncMapFromChangesArray();
        Element.ValueDef.AoeConfig.RuntimeMetricModifiers.SyncMapFromChangesArray();
        AbilityMasterRegistry.Add(Element.KeyTag, Element.ValueDef);
    }
}
void ADynamicGameState::OnRep_SharedConditionTreesArr() {
    SharedConditionTrees.Empty();
    for (const auto& Element : SharedConditionTreesArr) SharedConditionTrees.Add(Element.KeyName, Element.ValueNode);
}
void ADynamicGameState::OnRep_MasterMilestoneRegistryArr() {
    MasterMilestoneRegistry.Empty();
    for (const auto& Element : MasterMilestoneRegistryArr) MasterMilestoneRegistry.Add(Element.KeyTag, Element.ValueContainer);
}
void ADynamicGameState::OnRep_MasterThresholdRegistryArr() {
    MasterThresholdRegistry.Empty();
    for (const auto& Element : MasterThresholdRegistryArr) MasterThresholdRegistry.Add(Element.KeyTag, Element.ValueContainer);
}