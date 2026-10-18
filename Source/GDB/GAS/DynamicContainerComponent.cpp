// DynamicContainerComponent.cpp
#include "DynamicContainerComponent.h"
#include "DynamicPhysicsActor.h"
#include "DynamicCharacter.h"
#include "DynamicAttributeSet.h"
#include "DynamicInteractableComponent.h"
#include "DynamicGameState.h"
#include "DynamicDataSerializer.h"
#include "Net/UnrealNetwork.h"

UDynamicContainerComponent::UDynamicContainerComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
    ReplicatedSlots.OwningContainer = this;
}

void UDynamicContainerComponent::BeginPlay()
{
    Super::BeginPlay();
    ReplicatedSlots.OwningContainer = this;
}

void UDynamicContainerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicContainerComponent, ContainerConfig);
    DOREPLIFETIME(UDynamicContainerComponent, ReplicatedSlots);
}

void FDynamicContainerSlotItem::PreReplicatedRemove(const FFastContainerSlotArray& InArrayOwner)
{
    if (InArrayOwner.OwningContainer)
    {
        InArrayOwner.OwningContainer->HandleSingleSlotReplicated(*this, true);
    }
}

void FDynamicContainerSlotItem::PostReplicatedAdd(const FFastContainerSlotArray& InArrayOwner)
{
    if (InArrayOwner.OwningContainer)
    {
        InArrayOwner.OwningContainer->HandleSingleSlotReplicated(*this, false);
    }
}

void FDynamicContainerSlotItem::PostReplicatedChange(const FFastContainerSlotArray& InArrayOwner)
{
    if (InArrayOwner.OwningContainer)
    {
        InArrayOwner.OwningContainer->HandleSingleSlotReplicated(*this, false);
    }
}

void UDynamicContainerComponent::HandleSingleSlotReplicated(const FDynamicContainerSlotItem& Item, bool bWasRemoved)
{
    RebuildLocalSlotCache();
    OnContainerSlotChanged.Broadcast(Item.GridX, Item.GridY, Item.StoredActorArchetypeTag, bWasRemoved ? 0 : Item.StackCount);
}

void UDynamicContainerComponent::OnRep_ReplicatedSlots()
{
    RebuildLocalSlotCache();
    for (const FDynamicContainerSlotItem& Item : ReplicatedSlots.Items)
    {
        OnContainerSlotChanged.Broadcast(Item.GridX, Item.GridY, Item.StoredActorArchetypeTag, Item.StackCount);
    }
}

void UDynamicContainerComponent::RebuildLocalSlotCache()
{
    FlatKeyToArrayIndexMap.Empty(ReplicatedSlots.Items.Num());
    CachedTotalWeight = 0.0f;

    for (int32 i = 0; i < ReplicatedSlots.Items.Num(); ++i)
    {
        const FDynamicContainerSlotItem& Item = ReplicatedSlots.Items[i];
        int32 FlatKey = Item.GridY * ContainerConfig.GridWidth + Item.GridX;
        FlatKeyToArrayIndexMap.Add(FlatKey, i);
        CachedTotalWeight += ResolveArchetypeUnitWeight(Item.StoredActorArchetypeTag, Item.SavedAttributeState) * static_cast<float>(Item.StackCount);
    }
}

void UDynamicContainerComponent::InitializeContainerFromDef(const FDynamicContainerDef& InDef)
{
    ContainerConfig = InDef;
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

    ReplicatedSlots.Items.Empty();
    for (const FDynamicContainerSlotItem& DefaultItem : ContainerConfig.DefaultStoredItems)
    {
        if (DefaultItem.StoredActorArchetypeTag.IsValid() && DefaultItem.StackCount > 0)
        {
            FDynamicContainerSlotItem& Added = ReplicatedSlots.Items.Add_GetRef(DefaultItem);
            ReplicatedSlots.MarkItemDirty(Added);
        }
    }
    ReplicatedSlots.MarkArrayDirty();
    RebuildLocalSlotCache();
}

float UDynamicContainerComponent::ResolveArchetypeUnitWeight(FGameplayTag ArchetypeTag, const TArray<FDynamicAttributeItem>& AttributeOverrides) const
{
    static const FGameplayTag WeightTag = FGameplayTag::RequestGameplayTag(FName("Attribute.Object.Weight"), false);
    if (WeightTag.IsValid())
    {
        for (const FDynamicAttributeItem& Attr : AttributeOverrides)
        {
            if (Attr.LimbId == 0 && Attr.AttributeTag == WeightTag) return FMath::Max(0.0f, Attr.Value);
        }
    }

    if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
    {
        if (const FDynamicPhysicsActorDef* Def = GS->MasterPhysicsActorRegistry.Find(ArchetypeTag))
        {
            if (WeightTag.IsValid())
            {
                if (const float* FoundWeight = Def->DefaultGlobalAttributes.LocalLookupMap.Find(WeightTag))
                {
                    return FMath::Max(0.0f, *FoundWeight);
                }
            }
            return FMath::Max(0.0f, Def->MassOverride);
        }
    }
    return 0.0f;
}

bool UDynamicContainerComponent::IsGridSpaceFree(int32 GridX, int32 GridY, int32 SizeX, int32 SizeY, int32 IgnoreFlatKey) const
{
    if (GridX < 0 || GridY < 0 || (GridX + SizeX) > ContainerConfig.GridWidth || (GridY + SizeY) > ContainerConfig.GridHeight)
    {
        return false;
    }

    for (int32 X = GridX; X < GridX + SizeX; ++X)
    {
        for (int32 Y = GridY; Y < GridY + SizeY; ++Y)
        {
            if (ContainerConfig.BlockedGridCells.Contains(FIntPoint(X, Y))) return false;
        }
    }

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    for (const FDynamicContainerSlotItem& Item : ReplicatedSlots.Items)
    {
        int32 ItemFlatKey = Item.GridY * ContainerConfig.GridWidth + Item.GridX;
        if (ItemFlatKey == IgnoreFlatKey) continue;

        const FDynamicPhysicsActorDef* Def = GS->MasterPhysicsActorRegistry.Find(Item.StoredActorArchetypeTag);
        if (!Def) continue;

        int32 ItemW = Item.bIsRotated ? Def->ItemConfig.GridSizeY : Def->ItemConfig.GridSizeX;
        int32 ItemH = Item.bIsRotated ? Def->ItemConfig.GridSizeX : Def->ItemConfig.GridSizeY;

        if (GridX < (Item.GridX + ItemW) && (GridX + SizeX) > Item.GridX &&
            GridY < (Item.GridY + ItemH) && (GridY + SizeY) > Item.GridY)
        {
            return false;
        }
    }
    return true;
}

bool UDynamicContainerComponent::FindSpaceForItem(FGameplayTag ArchetypeTag, int32& OutGridX, int32& OutGridY, bool& OutRotated) const
{
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    const FDynamicPhysicsActorDef* Def = GS->MasterPhysicsActorRegistry.Find(ArchetypeTag);
    if (!Def) return false;

    // Normal orientation check
    for (int32 Y = 0; Y <= ContainerConfig.GridHeight - Def->ItemConfig.GridSizeY; ++Y)
    {
        for (int32 X = 0; X <= ContainerConfig.GridWidth - Def->ItemConfig.GridSizeX; ++X)
        {
            if (IsGridSpaceFree(X, Y, Def->ItemConfig.GridSizeX, Def->ItemConfig.GridSizeY))
            {
                OutGridX = X; OutGridY = Y; OutRotated = false;
                return true;
            }
        }
    }

    // Rotated orientation check
    if (Def->ItemConfig.bCanRotate)
    {
        for (int32 Y = 0; Y <= ContainerConfig.GridHeight - Def->ItemConfig.GridSizeX; ++Y)
        {
            for (int32 X = 0; X <= ContainerConfig.GridWidth - Def->ItemConfig.GridSizeY; ++X)
            {
                if (IsGridSpaceFree(X, Y, Def->ItemConfig.GridSizeY, Def->ItemConfig.GridSizeX))
                {
                    OutGridX = X; OutGridY = Y; OutRotated = true;
                    return true;
                }
            }
        }
    }
    return false;
}

bool UDynamicContainerComponent::ServerStorePhysicalActor(ADynamicPhysicsActor* ActorToStore, AActor* InstigatorActor)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !IsValid(ActorToStore) || ActorToStore == GetOwner()) return false;

    const FGameplayTag ArchetypeTag = ActorToStore->GetActorArchetypeTag();
    if (ContainerConfig.AllowedItemTags.Num() > 0 && !ContainerConfig.AllowedItemTags.HasTag(ArchetypeTag)) return false;
    if (ContainerConfig.ForbiddenItemTags.Num() > 0 && ContainerConfig.ForbiddenItemTags.HasTag(ArchetypeTag)) return false;

    int32 FoundX = 0, FoundY = 0;
    bool bRotated = false;
    if (!FindSpaceForItem(ArchetypeTag, FoundX, FoundY, bRotated)) return false;

    FDynamicContainerSlotItem NewSlot;
    NewSlot.GridX = FoundX;
    NewSlot.GridY = FoundY;
    NewSlot.bIsRotated = bRotated;
    NewSlot.StoredActorArchetypeTag = ArchetypeTag;
    NewSlot.StackCount = 1;

    if (const UDynamicAttributeSet* AttrSet = ActorToStore->GetDynamicAttributeSet())
    {
        NewSlot.SavedAttributeState = AttrSet->FastAttributes.Items;
    }
    if (const UDynamicInteractableComponent* Interactable = ActorToStore->GetInteractableComponent())
    {
        NewSlot.SavedInteractableStates = Interactable->GetCurrentStateTags();
    }

    FDynamicContainerSlotItem& Added = ReplicatedSlots.Items.Add_GetRef(NewSlot);
    ReplicatedSlots.MarkItemDirty(Added);
    RebuildLocalSlotCache();
    ActorToStore->DeactivateAndReturnToPool();
    return true;
}

bool UDynamicContainerComponent::ServerAddActorByTag(FGameplayTag ArchetypeTag, int32 Quantity, const TArray<FDynamicAttributeItem>& CustomAttributes)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !ArchetypeTag.IsValid() || Quantity <= 0) return false;

    int32 FoundX = 0, FoundY = 0;
    bool bRotated = false;
    if (!FindSpaceForItem(ArchetypeTag, FoundX, FoundY, bRotated)) return false;

    FDynamicContainerSlotItem NewSlot;
    NewSlot.GridX = FoundX;
    NewSlot.GridY = FoundY;
    NewSlot.bIsRotated = bRotated;
    NewSlot.StoredActorArchetypeTag = ArchetypeTag;
    NewSlot.StackCount = Quantity;
    NewSlot.SavedAttributeState = CustomAttributes;

    FDynamicContainerSlotItem& Added = ReplicatedSlots.Items.Add_GetRef(NewSlot);
    ReplicatedSlots.MarkItemDirty(Added);
    RebuildLocalSlotCache();
    return true;
}

ADynamicPhysicsActor* UDynamicContainerComponent::ServerExtractActorToWorld(int32 GridX, int32 GridY, int32 QuantityToExtract, const FTransform& SpawnTransform, AActor* InstigatorActor)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || QuantityToExtract <= 0) return nullptr;

    int32 TargetFlatKey = GridY * ContainerConfig.GridWidth + GridX;
    const int32* FoundIdxPtr = FlatKeyToArrayIndexMap.Find(TargetFlatKey);
    if (!FoundIdxPtr || !ReplicatedSlots.Items.IsValidIndex(*FoundIdxPtr)) return nullptr;

    int32 FoundIndex = *FoundIdxPtr;
    FDynamicContainerSlotItem Snapshot = ReplicatedSlots.Items[FoundIndex];
    if (Snapshot.StackCount < QuantityToExtract) return nullptr;

    ReplicatedSlots.Items[FoundIndex].StackCount -= QuantityToExtract;
    if (ReplicatedSlots.Items[FoundIndex].StackCount <= 0)
    {
        ReplicatedSlots.Items.RemoveAt(FoundIndex);
        ReplicatedSlots.MarkArrayDirty();
    }
    else
    {
        ReplicatedSlots.MarkItemDirty(ReplicatedSlots.Items[FoundIndex]);
    }
    RebuildLocalSlotCache();

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return nullptr;

    ADynamicPhysicsActor* Spawned = GS->AcquirePhysicsActor(Snapshot.StoredActorArchetypeTag, SpawnTransform, GetOwner(), Cast<APawn>(InstigatorActor));
    if (Spawned)
    {
        Spawned->RestoreFromContainerSnapshot(Snapshot);
    }
    return Spawned;
}

ADynamicPhysicsActor* UDynamicContainerComponent::ServerExtractActorToHands(int32 GridX, int32 GridY, ADynamicCharacter* Looter, FName HandBone)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !Looter) return nullptr;

    FTransform HandTransform = Looter->GetMeshComponent()->GetSocketTransform(HandBone);
    ADynamicPhysicsActor* Extracted = ServerExtractActorToWorld(GridX, GridY, 1, HandTransform, Looter);
    if (Extracted)
    {
        if (UPrimitiveComponent* Prim = Extracted->GetActivePhysicsPrimitive())
        {
            Prim->SetSimulatePhysics(false);
            Extracted->AttachToComponent(Looter->GetMeshComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, HandBone);
        }
        if (UDynamicInteractableComponent* Interactable = Extracted->GetInteractableComponent())
        {
            Interactable->RemoveStateTag(FGameplayTag::RequestGameplayTag("State.Interactable.Moveable"));
            Interactable->AddStateTag(FGameplayTag::RequestGameplayTag("State.Interactable.Carried"));
        }
    }
    return Extracted;
}

int32 UDynamicContainerComponent::GetTotalCountOfArchetype(FGameplayTag ArchetypeTag) const
{
    int32 Total = 0;
    for (const FDynamicContainerSlotItem& Item : ReplicatedSlots.Items)
    {
        if (Item.StoredActorArchetypeTag == ArchetypeTag) Total += Item.StackCount;
    }
    return Total;
}