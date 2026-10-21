// DynamicEquipmentComponent.cpp
#include "DynamicEquipmentComponent.h"
#include "DynamicPhysicsActor.h"
#include "DynamicGameState.h"
#include "DynamicCharacter.h"
#include "DynamicAttributeSet.h"
#include "DynamicInteractableComponent.h"
#include "DynamicEvolutionComponent.h"
#include "DynamicCustomizationComponent.h"
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
    if (InArrayOwner.OwningEquipmentComp) InArrayOwner.OwningEquipmentComp->HandleSingleSlotReplicated(*this, true);
}
void FDynamicEquipmentSlotItem::PostReplicatedAdd(const FFastEquipmentSlotArray& InArrayOwner)
{
    if (InArrayOwner.OwningEquipmentComp) InArrayOwner.OwningEquipmentComp->HandleSingleSlotReplicated(*this, false);
}
void FDynamicEquipmentSlotItem::PostReplicatedChange(const FFastEquipmentSlotArray& InArrayOwner)
{
    if (InArrayOwner.OwningEquipmentComp) InArrayOwner.OwningEquipmentComp->HandleSingleSlotReplicated(*this, false);
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

bool UDynamicEquipmentComponent::CanEquipItem(ADynamicPhysicsActor* ActorToEquip) const
{
    if (!ActorToEquip) return false;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    const FDynamicPhysicsActorDef* Def = GS->MasterPhysicsActorRegistry.Find(ActorToEquip->GetActorArchetypeTag());
    if (!Def) return false;

    ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner());
    if (!CharOwner) return false;

    if (UDynamicCustomizationComponent* Customization = CharOwner->GetCustomizationComponent())
    {
        float CharSize = Customization->GetMorphSliderValue(FGameplayTag::RequestGameplayTag("Slider.Body.Height"));
        if (CharSize < Def->EquipmentRequirement.FittedSizeMin || CharSize > Def->EquipmentRequirement.FittedSizeMax)
        {
            return false;
        }
    }

    for (const FGameplayTag& ReqSlot : Def->EquipmentRequirement.RequiredEquipmentSlots)
    {
        if (GetEquippedItemInSlot(ReqSlot).IsValid()) return false;
    }

    return true;
}

bool UDynamicEquipmentComponent::ServerEquipItem(ADynamicPhysicsActor* ActorToEquip)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !CanEquipItem(ActorToEquip)) return false;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    const FDynamicPhysicsActorDef* Def = GS ? GS->MasterPhysicsActorRegistry.Find(ActorToEquip->GetActorArchetypeTag()) : nullptr;
    if (!Def) return false;

    ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner());
    if (!CharOwner) return false;

    if (UPrimitiveComponent* Prim = ActorToEquip->GetActivePhysicsPrimitive())
    {
        Prim->SetSimulatePhysics(false);
        Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    if (USkeletalMeshComponent* EquipMesh = ActorToEquip->GetMeshComponent())
    {
        EquipMesh->AttachToComponent(CharOwner->GetMeshComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
        if (Def->EquipmentRequirement.bUseLeaderPoseComponent)
        {
            EquipMesh->SetLeaderPoseComponent(CharOwner->GetMeshComponent(), true, true);
        }
    }

    if (!Def->EquipmentRequirement.EquipAnimMontage.IsNull())
    {
        if (UAnimMontage* Montage = Def->EquipmentRequirement.EquipAnimMontage.LoadSynchronous())
        {
            if (CharOwner->GetMeshComponent() && CharOwner->GetMeshComponent()->GetAnimInstance())
            {
                CharOwner->GetMeshComponent()->GetAnimInstance()->Montage_Play(Montage);
            }
        }
    }

    for (const FGameplayTag& ReqSlot : Def->EquipmentRequirement.RequiredEquipmentSlots)
    {
        for (FDynamicEquipmentSlotItem& SlotItem : ReplicatedEquipmentSlots.Items)
        {
            if (SlotItem.SlotTag == ReqSlot)
            {
                SlotItem.EquippedItemArchetypeTag = ActorToEquip->GetActorArchetypeTag();

                // Save item state snapshots
                if (const UDynamicAttributeSet* ItemAttr = ActorToEquip->GetDynamicAttributeSet())
                {
                    SlotItem.SavedItemAttributeState = ItemAttr->FastAttributes.Items;
                }
                if (const UDynamicInteractableComponent* ItemInteractable = ActorToEquip->GetInteractableComponent())
                {
                    SlotItem.SavedItemInteractableStates = ItemInteractable->GetCurrentStateTags();
                }

                ReplicatedEquipmentSlots.MarkItemDirty(SlotItem);
                break;
            }
        }
    }

    RebuildLocalCache();
    ActorToEquip->DeactivateAndReturnToPool();
    return true;
}

ADynamicPhysicsActor* UDynamicEquipmentComponent::ServerUnequipSlot(FGameplayTag SlotTag, const FTransform& SpawnTransform)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !SlotTag.IsValid()) return nullptr;

    const int32* FoundIdx = SlotTagToArrayIndexMap.Find(SlotTag);
    if (!FoundIdx || !ReplicatedEquipmentSlots.Items.IsValidIndex(*FoundIdx)) return nullptr;

    FDynamicEquipmentSlotItem& SlotItem = ReplicatedEquipmentSlots.Items[*FoundIdx];
    if (!SlotItem.EquippedItemArchetypeTag.IsValid()) return nullptr;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return nullptr;

    ADynamicPhysicsActor* Spawned = GS->AcquirePhysicsActor(SlotItem.EquippedItemArchetypeTag, SpawnTransform, GetOwner(), Cast<APawn>(GetOwner()));
    if (Spawned)
    {
        FDynamicContainerSlotItem Snapshot;
        Snapshot.StoredActorArchetypeTag = SlotItem.EquippedItemArchetypeTag;
        Snapshot.SavedAttributeState = SlotItem.SavedItemAttributeState;
        Snapshot.SavedInteractableStates = SlotItem.SavedItemInteractableStates;
        Spawned->RestoreFromContainerSnapshot(Snapshot);
    }

    const FGameplayTag ClearedTag = SlotItem.EquippedItemArchetypeTag;
    SlotItem.EquippedItemArchetypeTag = FGameplayTag::EmptyTag;
    SlotItem.SavedItemAttributeState.Empty();
    SlotItem.SavedItemInteractableStates.Reset();

    ReplicatedEquipmentSlots.MarkItemDirty(SlotItem);
    RebuildLocalCache();

    OnEquipmentChanged.Broadcast(SlotTag, ClearedTag, false);
    return Spawned;
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

void UDynamicEquipmentComponent::ServerGrantItemXP(float XPGrantAmount)
{
    if (ItemEvolutionComponent && GetOwner() && GetOwner()->HasAuthority())
    {
        ItemEvolutionComponent->ServerAddProgress(XPGrantAmount);
    }
}

void UDynamicEquipmentComponent::ClearEquipmentForPooling()
{
    for (FDynamicEquipmentSlotItem& SlotItem : ReplicatedEquipmentSlots.Items)
    {
        SlotItem.EquippedItemArchetypeTag = FGameplayTag::EmptyTag;
        SlotItem.SavedItemAttributeState.Empty();
        SlotItem.SavedItemInteractableStates.Reset();
    }
    ReplicatedEquipmentSlots.MarkArrayDirty();
    RebuildLocalCache();

    // Detach and clean up attached modular meshes
    if (ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner()))
    {
        TArray<USceneComponent*> Children;
        CharOwner->GetMeshComponent()->GetChildrenComponents(true, Children);
        for (USceneComponent* Child : Children)
        {
            if (USkeletalMeshComponent* SkelChild = Cast<USkeletalMeshComponent>(Child))
            {
                SkelChild->DestroyComponent();
            }
        }
    }
}