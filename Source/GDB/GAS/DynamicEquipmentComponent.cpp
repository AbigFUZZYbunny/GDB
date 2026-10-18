// DynamicEquipmentComponent.cpp
#include "DynamicEquipmentComponent.h"
#include "DynamicPhysicsActor.h"
#include "DynamicGameState.h"
#include "Net/UnrealNetwork.h"

UDynamicEquipmentComponent::UDynamicEquipmentComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
    ReplicatedEquipmentSlots.OwningEquipmentComp = this;
}

void UDynamicEquipmentComponent::BeginPlay()
{
    Super::BeginPlay();
    ReplicatedEquipmentSlots.OwningEquipmentComp = this;
}

void UDynamicEquipmentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicEquipmentComponent, ConfiguredSlots);
    DOREPLIFETIME(UDynamicEquipmentComponent, ReplicatedEquipmentSlots);
}

void FDynamicEquipmentSlotItem::PreReplicatedRemove(const FFastEquipmentSlotArray& InArrayOwner)
{
    if (InArrayOwner.OwningEquipmentComp) 
    {
        InArrayOwner.OwningEquipmentComp->HandleSingleSlotReplicated(*this, true);
    }
}

void FDynamicEquipmentSlotItem::PostReplicatedAdd(const FFastEquipmentSlotArray& InArrayOwner)
{
    if (InArrayOwner.OwningEquipmentComp) 
    {
        InArrayOwner.OwningEquipmentComp->HandleSingleSlotReplicated(*this, false);
    }
}

void FDynamicEquipmentSlotItem::PostReplicatedChange(const FFastEquipmentSlotArray& InArrayOwner)
{
    if (InArrayOwner.OwningEquipmentComp) 
    {
        InArrayOwner.OwningEquipmentComp->HandleSingleSlotReplicated(*this, false);
    }
}

void UDynamicEquipmentComponent::HandleSingleSlotReplicated(const FDynamicEquipmentSlotItem& Item, bool bIsUnequipped)
{
    RebuildLocalCache();
    OnEquipmentChanged.Broadcast(Item.SlotTag, bIsUnequipped ? FGameplayTag::EmptyTag : Item.EquippedItemArchetypeTag, !bIsUnequipped);
}

void UDynamicEquipmentComponent::OnRep_ReplicatedEquipmentSlots()
{
    RebuildLocalCache();
    for (const FDynamicEquipmentSlotItem& Item : ReplicatedEquipmentSlots.Items)
    {
        OnEquipmentChanged.Broadcast(Item.SlotTag, Item.EquippedItemArchetypeTag, Item.EquippedItemArchetypeTag.IsValid());
    }
}

void UDynamicEquipmentComponent::RebuildLocalCache()
{
    SlotTagToArrayIndexMap.Empty(ReplicatedEquipmentSlots.Items.Num());
    for (int32 i = 0; i < ReplicatedEquipmentSlots.Items.Num(); ++i)
    {
        SlotTagToArrayIndexMap.Add(ReplicatedEquipmentSlots.Items[i].SlotTag, i);
    }
}

void UDynamicEquipmentComponent::InitializeEquipmentSlots(const TArray<FDynamicEquipmentSlotProfile>& InSlots)
{
    ConfiguredSlots = InSlots;
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

    ReplicatedEquipmentSlots.Items.Empty();
    for (const FDynamicEquipmentSlotProfile& Profile : InSlots)
    {
        FDynamicEquipmentSlotItem EmptySlot;
        EmptySlot.SlotTag = Profile.SlotTag;
        EmptySlot.LinkedLimbId = Profile.LimbId;
        EmptySlot.EquippedItemArchetypeTag = FGameplayTag::EmptyTag;
        ReplicatedEquipmentSlots.Items.Add(EmptySlot);
    }
    ReplicatedEquipmentSlots.MarkArrayDirty();
    RebuildLocalCache();
}

bool UDynamicEquipmentComponent::CanEquipItem(FGameplayTag ItemArchetypeTag) const
{
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    const FDynamicPhysicsActorDef* Def = GS->MasterPhysicsActorRegistry.Find(ItemArchetypeTag);
    if (!Def) return false;

    return true;
}

bool UDynamicEquipmentComponent::ServerEquipItem(ADynamicPhysicsActor* ActorToEquip)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !IsValid(ActorToEquip)) return false;
    return true;
}

ADynamicPhysicsActor* UDynamicEquipmentComponent::ServerUnequipSlot(FGameplayTag SlotTag, const FTransform& SpawnTransform)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return nullptr;
    return nullptr;
}

FGameplayTag UDynamicEquipmentComponent::GetEquippedItemInSlot(FGameplayTag SlotTag) const
{
    if (const int32* FoundIdx = SlotTagToArrayIndexMap.Find(SlotTag))
    {
        if (ReplicatedEquipmentSlots.Items.IsValidIndex(*FoundIdx))
        {
            return ReplicatedEquipmentSlots.Items[*FoundIdx].EquippedItemArchetypeTag;
        }
    }
    return FGameplayTag::EmptyTag;
}