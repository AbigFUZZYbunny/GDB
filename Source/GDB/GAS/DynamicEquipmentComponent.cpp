// DynamicEquipmentComponent.cpp
#include "DynamicEquipmentComponent.h"
#include "DynamicPhysicsActor.h"
#include "DynamicGameState.h"
#include "DynamicCharacter.h"
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

bool UDynamicEquipmentComponent::CanEquipItem(ADynamicPhysicsActor* ActorToEquip) const
{
    if (!ActorToEquip) return false;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    const FDynamicPhysicsActorDef* Def = GS->MasterPhysicsActorRegistry.Find(ActorToEquip->GetActorArchetypeTag());
    if (!Def) return false;

    ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner());
    if (!CharOwner) return false;

    // Evaluate Size Fitting Limits
    if (UDynamicCustomizationComponent* Customization = CharOwner->GetCustomizationComponent())
    {
        float CharSize = Customization->GetMorphSliderValue(FGameplayTag::RequestGameplayTag("Slider.Body.Height"));
        if (CharSize < Def->EquipmentRequirement.FittedSizeMin || CharSize > Def->EquipmentRequirement.FittedSizeMax)
        {
            return false; // Actor's body size exceeds the physical dimensions the armor was crafted for.
        }
    }

    // Evaluate Slot Availability
    for (const FGameplayTag& ReqSlot : Def->EquipmentRequirement.RequiredEquipmentSlots)
    {
        if (GetEquippedItemInSlot(ReqSlot).IsValid()) return false;
    }

    return true;
}

bool UDynamicEquipmentComponent::ServerEquipItem(ADynamicPhysicsActor* ActorToEquip)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !CanEquipItem(ActorToEquip)) return false;

    ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>();
    const FDynamicPhysicsActorDef* Def = GS->MasterPhysicsActorRegistry.Find(ActorToEquip->GetActorArchetypeTag());

    ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner());

    // 1. Disable Physics & Collision for the world item
    if (UPrimitiveComponent* Prim = ActorToEquip->GetActivePhysicsPrimitive())
    {
        Prim->SetSimulatePhysics(false);
        Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // 2. Attach visually and bind Morphs/Animations via Leader Pose Component
    if (USkeletalMeshComponent* EquipMesh = ActorToEquip->GetMeshComponent())
    {
        EquipMesh->AttachToComponent(CharOwner->GetMeshComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
        
        if (Def->EquipmentRequirement.bUseLeaderPoseComponent)
        {
            EquipMesh->SetLeaderPoseComponent(CharOwner->GetMeshComponent(), true, true);
        }
    }

    // 3. Play Equip Animation
    if (!Def->EquipmentRequirement.EquipAnimMontage.IsNull())
    {
        if (UAnimMontage* Montage = Def->EquipmentRequirement.EquipAnimMontage.LoadSynchronous())
        {
            CharOwner->GetMeshComponent()->GetAnimInstance()->Montage_Play(Montage);
        }
    }

    // 4. Update Fast Array Slots
    for (const FGameplayTag& ReqSlot : Def->EquipmentRequirement.RequiredEquipmentSlots)
    {
        for (FDynamicEquipmentSlotItem& SlotItem : ReplicatedEquipmentSlots.Items)
        {
            if (SlotItem.SlotTag == ReqSlot)
            {
                SlotItem.EquippedItemArchetypeTag = ActorToEquip->GetActorArchetypeTag();
                // Store a weak pointer or networked ID to the equipped actor instance to track visual state
                ReplicatedEquipmentSlots.MarkItemDirty(SlotItem);
                break;
            }
        }
    }

    RebuildLocalCache();
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