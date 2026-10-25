// DynamicContainerComponent.cpp
#include "DynamicContainerComponent.h"
#include "DynamicPhysicsActor.h"
#include "DynamicCharacter.h"
#include "DynamicAttributeSet.h"
#include "DynamicInteractableComponent.h"
#include "DynamicGameState.h"
#include "TimerManager.h"
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

void UDynamicContainerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TimerHandle_TombstoneCompression);
    }
    Super::EndPlay(EndPlayReason);
}

void UDynamicContainerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME_CONDITION(UDynamicContainerComponent, ContainerConfig, COND_InitialOnly);
    DOREPLIFETIME(UDynamicContainerComponent, ReplicatedSlots);
}

void FDynamicContainerSlotItem::PreReplicatedRemove(const FFastContainerSlotArray& InArrayOwner)
{
    if (InArrayOwner.OwningContainer) InArrayOwner.OwningContainer->HandleSingleSlotReplicated(*this, true);
}

void FDynamicContainerSlotItem::PostReplicatedAdd(const FFastContainerSlotArray& InArrayOwner)
{
    if (InArrayOwner.OwningContainer) InArrayOwner.OwningContainer->HandleSingleSlotReplicated(*this, false);
}

void FDynamicContainerSlotItem::PostReplicatedChange(const FFastContainerSlotArray& InArrayOwner)
{
    if (InArrayOwner.OwningContainer) InArrayOwner.OwningContainer->HandleSingleSlotReplicated(*this, false);
}

void UDynamicContainerComponent::HandleSingleSlotReplicated(const FDynamicContainerSlotItem& Item, bool bWasRemoved)
{
    RebuildLocalSlotCache();
    const bool bIsEmpty = bWasRemoved || !Item.StoredActorArchetypeTag.IsValid() || Item.StackCount <= 0;
    OnContainerSlotChanged.Broadcast(Item.GridX, Item.GridY, Item.StoredActorArchetypeTag, bIsEmpty ? 0 : Item.StackCount);
}

void UDynamicContainerComponent::OnRep_ReplicatedSlots()
{
    RebuildLocalSlotCache();
    for (const FDynamicContainerSlotItem& Item : ReplicatedSlots.Items)
    {
        const int32 DisplayCount = (!Item.StoredActorArchetypeTag.IsValid() || Item.StackCount <= 0) ? 0 : Item.StackCount;
        OnContainerSlotChanged.Broadcast(Item.GridX, Item.GridY, Item.StoredActorArchetypeTag, DisplayCount);
    }

    // Fully flush and reconstruct UI slot widget layouts on array compression / net sync
    OnContainerFullRebuild.Broadcast();
}

void UDynamicContainerComponent::RebuildLocalSlotCache()
{
    FlatKeyToReplicationIDMap.Empty(ReplicatedSlots.Items.Num());
    ReplicationIDToIndexMap.Empty(ReplicatedSlots.Items.Num());
    CachedTotalWeight = 0.0f;
    PendingTombstoneCount = 0;

    for (int32 i = 0; i < ReplicatedSlots.Items.Num(); ++i)
    {
        const FDynamicContainerSlotItem& Item = ReplicatedSlots.Items[i];
        if (Item.StackCount <= 0 || !Item.StoredActorArchetypeTag.IsValid())
        {
            PendingTombstoneCount++;
            continue;
        }
        const int32 FlatKey = Item.GridY * ContainerConfig.GridWidth + Item.GridX;
        FlatKeyToReplicationIDMap.Add(FlatKey, Item.ReplicationID);
        ReplicationIDToIndexMap.Add(Item.ReplicationID, i);

        CachedTotalWeight += ResolveArchetypeUnitWeight(Item.StoredActorArchetypeTag, Item.SavedAttributeState) * static_cast<float>(Item.StackCount);
    }
}

void UDynamicContainerComponent::ClearContainerForPooling()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TimerHandle_TombstoneCompression);
    }
    ReplicatedSlots.Items.Empty();
    ReplicatedSlots.MarkArrayDirty();
    FlatKeyToReplicationIDMap.Empty();
    ReplicationIDToIndexMap.Empty();
    CachedTotalWeight = 0.0f;
    PendingTombstoneCount = 0;
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

bool UDynamicContainerComponent::IsGridSpaceFree(int32 GridX, int32 GridY, int32 SizeX, int32 SizeY, int32 IgnoreReplicationID) const
{
    if (GridX < 0 || GridY < 0 || (GridX + SizeX) > ContainerConfig.GridWidth || (GridY + SizeY) > ContainerConfig.GridHeight) return false;

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
        if (Item.StackCount <= 0 || !Item.StoredActorArchetypeTag.IsValid() || Item.ReplicationID == IgnoreReplicationID) continue;

        const FDynamicPhysicsActorDef* Def = GS->MasterPhysicsActorRegistry.Find(Item.StoredActorArchetypeTag);
        if (!Def) continue;

        const int32 ItemW = Item.bIsRotated ? Def->ItemConfig.GridSizeY : Def->ItemConfig.GridSizeX;
        const int32 ItemH = Item.bIsRotated ? Def->ItemConfig.GridSizeX : Def->ItemConfig.GridSizeY;

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
    
    // CAPTURE ITEM OWNERSHIP
    NewSlot.OwnerGuid = ActorToStore->GetItemOwnerGuid();

    if (const UDynamicAttributeSet* AttrSet = ActorToStore->GetDynamicAttributeSet())
    {
        NewSlot.SavedAttributeState = AttrSet->FastAttributes.Items;
    }
    if (const UDynamicInteractableComponent* Interactable = ActorToStore->GetInteractableComponent())
    {
        NewSlot.SavedInteractableStates = Interactable->GetCurrentStateTags();
    }

    int32 TombstoneIdx = ReplicatedSlots.Items.IndexOfByPredicate([](const FDynamicContainerSlotItem& Slot) {
        return Slot.StackCount <= 0 || !Slot.StoredActorArchetypeTag.IsValid();
    });

    if (TombstoneIdx != INDEX_NONE)
    {
        ReplicatedSlots.Items[TombstoneIdx] = NewSlot;
        ReplicatedSlots.MarkItemDirty(ReplicatedSlots.Items[TombstoneIdx]);
    }
    else
    {
        FDynamicContainerSlotItem& Added = ReplicatedSlots.Items.Add_GetRef(NewSlot);
        ReplicatedSlots.MarkItemDirty(Added);
    }

    RebuildLocalSlotCache();
    ActorToStore->DeactivateAndReturnToPool();
    return true;
}

bool UDynamicContainerComponent::ServerAddActorByTag(FGameplayTag ArchetypeTag, int32 Quantity, const TArray<FDynamicAttributeItem>& CustomAttributes, FGuid OptionalOwnerGuid)
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
    NewSlot.OwnerGuid = OptionalOwnerGuid; // ASSIGN OWNERSHIP ON CREATION

    int32 TombstoneIdx = ReplicatedSlots.Items.IndexOfByPredicate([](const FDynamicContainerSlotItem& Slot) {
        return Slot.StackCount <= 0 || !Slot.StoredActorArchetypeTag.IsValid();
    });

    if (TombstoneIdx != INDEX_NONE)
    {
        ReplicatedSlots.Items[TombstoneIdx] = NewSlot;
        ReplicatedSlots.MarkItemDirty(ReplicatedSlots.Items[TombstoneIdx]);
    }
    else
    {
        FDynamicContainerSlotItem& Added = ReplicatedSlots.Items.Add_GetRef(NewSlot);
        ReplicatedSlots.MarkItemDirty(Added);
    }
    RebuildLocalSlotCache();
    return true;
}

bool UDynamicContainerComponent::ServerRemoveStoredActorByTag(FGameplayTag ArchetypeTag, int32 Quantity)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !ArchetypeTag.IsValid() || Quantity <= 0) return false;
    if (GetTotalCountOfArchetype(ArchetypeTag) < Quantity) return false;

    int32 RemainingToRemove = Quantity;
    for (int32 i = ReplicatedSlots.Items.Num() - 1; i >= 0 && RemainingToRemove > 0; --i)
    {
        if (ReplicatedSlots.Items[i].StoredActorArchetypeTag == ArchetypeTag && ReplicatedSlots.Items[i].StackCount > 0)
        {
            const int32 Take = FMath::Min(RemainingToRemove, ReplicatedSlots.Items[i].StackCount);
            ReplicatedSlots.Items[i].StackCount -= Take;
            RemainingToRemove -= Take;

            const int32 GridX = ReplicatedSlots.Items[i].GridX;
            const int32 GridY = ReplicatedSlots.Items[i].GridY;
            const int32 NewCount = ReplicatedSlots.Items[i].StackCount;

            if (NewCount <= 0)
            {
                ReplicatedSlots.Items[i].StoredActorArchetypeTag = FGameplayTag::EmptyTag;
                ReplicatedSlots.Items[i].StackCount = 0;
                ReplicatedSlots.Items[i].SavedAttributeState.Empty();
                ReplicatedSlots.Items[i].SavedInteractableStates.Reset();
                ReplicatedSlots.MarkItemDirty(ReplicatedSlots.Items[i]);
                OnContainerSlotChanged.Broadcast(GridX, GridY, ArchetypeTag, 0);

                PendingTombstoneCount++;
                ScheduleTombstoneCompression();
            }
            else
            {
                ReplicatedSlots.MarkItemDirty(ReplicatedSlots.Items[i]);
                OnContainerSlotChanged.Broadcast(GridX, GridY, ArchetypeTag, NewCount);
            }
        }
    }

    RebuildLocalSlotCache();
    return true;
}

ADynamicPhysicsActor* UDynamicContainerComponent::ServerExtractActorToWorld(int32 GridX, int32 GridY, int32 QuantityToExtract, const FTransform& SpawnTransform, AActor* InstigatorActor)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || QuantityToExtract <= 0) return nullptr;

    const int32 TargetFlatKey = GridY * ContainerConfig.GridWidth + GridX;
    const int32* FoundRepID = FlatKeyToReplicationIDMap.Find(TargetFlatKey);
    if (!FoundRepID) return nullptr;
    
    const int32* FoundIdxPtr = ReplicationIDToIndexMap.Find(*FoundRepID);
    if (!FoundIdxPtr || !ReplicatedSlots.Items.IsValidIndex(*FoundIdxPtr)) return nullptr;

    const int32 FoundIndex = *FoundIdxPtr;
    FDynamicContainerSlotItem Snapshot = ReplicatedSlots.Items[FoundIndex];

    if (Snapshot.StackCount < QuantityToExtract) return nullptr;

    ReplicatedSlots.Items[FoundIndex].StackCount -= QuantityToExtract;

    if (ReplicatedSlots.Items[FoundIndex].StackCount <= 0)
    {
        ReplicatedSlots.Items[FoundIndex].StoredActorArchetypeTag = FGameplayTag::EmptyTag;
        ReplicatedSlots.Items[FoundIndex].StackCount = 0;
        ReplicatedSlots.Items[FoundIndex].SavedAttributeState.Empty();
        ReplicatedSlots.Items[FoundIndex].SavedInteractableStates.Reset();
        ReplicatedSlots.MarkItemDirty(ReplicatedSlots.Items[FoundIndex]);
        PendingTombstoneCount++;
        ScheduleTombstoneCompression();
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
    if (!GetOwner() || !GetOwner()->HasAuthority() || !Looter || !Looter->GetMeshComponent()) return nullptr;

    const FTransform HandTransform = Looter->GetMeshComponent()->GetSocketTransform(HandBone);
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
            Interactable->RemoveStateTag(FGameplayTag::RequestGameplayTag(TEXT("State.Interactable.Moveable")));
            Interactable->AddStateTag(FGameplayTag::RequestGameplayTag(TEXT("State.Interactable.Carried")));
        }
    }
    return Extracted;
}

int32 UDynamicContainerComponent::GetTotalCountOfArchetype(FGameplayTag ArchetypeTag) const
{
    int32 Total = 0;
    for (const FDynamicContainerSlotItem& Item : ReplicatedSlots.Items)
    {
        if (Item.StoredActorArchetypeTag == ArchetypeTag && Item.StackCount > 0)
        {
            Total += Item.StackCount;
        }
    }
    return Total;
}

void UDynamicContainerComponent::ScheduleTombstoneCompression(float DelaySeconds)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

    UWorld* World = GetWorld();
    if (!World) return;

    // Debounce compression timer: only compresses when modifications settle
    World->GetTimerManager().SetTimer(
        TimerHandle_TombstoneCompression,
        this,
        &UDynamicContainerComponent::ServerCompressContainerSlots,
        DelaySeconds,
        false
    );
}

void UDynamicContainerComponent::ServerCompressContainerSlots()
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

    bool bHasTombstones = false;
    for (const FDynamicContainerSlotItem& Item : ReplicatedSlots.Items)
    {
        if (Item.StackCount <= 0 || !Item.StoredActorArchetypeTag.IsValid())
        {
            bHasTombstones = true;
            break;
        }
    }

    if (!bHasTombstones) return;

    ReplicatedSlots.Items.RemoveAll([](const FDynamicContainerSlotItem& Slot) {
        return Slot.StackCount <= 0 || !Slot.StoredActorArchetypeTag.IsValid();
    });

    ReplicatedSlots.MarkArrayDirty();
    RebuildLocalSlotCache();

    OnContainerFullRebuild.Broadcast();
}

bool UDynamicContainerComponent::GetSlotItemAtGrid(int32 GridX, int32 GridY, FDynamicContainerSlotItem& OutSlotItem) const
{
    const int32 FlatKey = GridY * ContainerConfig.GridWidth + GridX;
    if (const int32* FoundRepID = FlatKeyToReplicationIDMap.Find(FlatKey))
    {
        if (const int32* FoundIdx = ReplicationIDToIndexMap.Find(*FoundRepID))
        {
            if (ReplicatedSlots.Items.IsValidIndex(*FoundIdx))
            {
                OutSlotItem = ReplicatedSlots.Items[*FoundIdx];
                return true;
            }
        }
    }
    return false;
}