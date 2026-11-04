// DynamicGroundItemSubsystem.cpp
#include "DynamicGroundItemSubsystem.h"
#include "DynamicGameState.h"
#include "DynamicPhysicsActor.h"
#include "DynamicAttributeSet.h"
#include "DynamicInteractableComponent.h"
#include "DynamicContainerComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

void UDynamicGroundItemSubsystem::Initialize(FSubsystemCollectionBase& Collection) { Super::Initialize(Collection); }
void UDynamicGroundItemSubsystem::Deinitialize() { ActiveItemChunks.Empty(); VirtualizedItemChunks.Empty(); Super::Deinitialize(); }

void UDynamicGroundItemSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (GetWorld() && GetWorld()->GetNetMode() != NM_Client)
    {
        StreamingTimer += DeltaTime;
        if (StreamingTimer >= 2.0f)
        {
            StreamingTimer = 0.0f;
            ProcessItemStreaming();
        }
    }
}

ADynamicItemChunkActor* UDynamicGroundItemSubsystem::GetOrCreateChunkAtLocation(const FVector& WorldLocation)
{
    FIntPoint Coord(
        FMath::FloorToInt(WorldLocation.X / ADynamicItemChunkActor::CHUNK_SIZE_UNITS),
        FMath::FloorToInt(WorldLocation.Y / ADynamicItemChunkActor::CHUNK_SIZE_UNITS)
    );

    if (ADynamicItemChunkActor** Found = ActiveItemChunks.Find(Coord)) return *Found;

    if (UWorld* World = GetWorld())
    {
        FActorSpawnParameters Params;
        Params.Owner = World->GetGameState();
        if (ADynamicItemChunkActor* NewChunk = World->SpawnActor<ADynamicItemChunkActor>(ADynamicItemChunkActor::StaticClass(), FTransform::Identity, Params))
        {
            NewChunk->InitializeChunk(Coord, ADynamicItemChunkActor::CHUNK_SIZE_UNITS);
            ActiveItemChunks.Add(Coord, NewChunk);
            return NewChunk;
        }
    }
    return nullptr;
}

void UDynamicGroundItemSubsystem::VirtualizePhysicsActor(ADynamicPhysicsActor* ActorToSleep)
{
    if (!ActorToSleep || !ActorToSleep->HasAuthority() || !ActorToSleep->GetActorArchetypeTag().IsValid()) return;

    ADynamicItemChunkActor* Chunk = GetOrCreateChunkAtLocation(ActorToSleep->GetActorLocation());
    if (!Chunk) return;

    FGroundItemRecordItem NewRecord;
    NewRecord.InstanceID = FGuid::NewGuid();
    NewRecord.Payload.ActorPersistentGuid = ActorToSleep->GetItemOwnerGuid();
    NewRecord.Payload.ActorArchetypeTag = ActorToSleep->GetActorArchetypeTag();
    NewRecord.Payload.WorldTransform = ActorToSleep->GetActorTransform();
    NewRecord.Payload.LinearVelocity = FVector::ZeroVector;
    NewRecord.Payload.AngularVelocity = FVector::ZeroVector;

    if (const UDynamicAttributeSet* AttrSet = ActorToSleep->GetDynamicAttributeSet())
    {
        NewRecord.Payload.SavedAttributes = AttrSet->FastAttributes.Items;
    }
    if (const UDynamicInteractableComponent* Interactable = ActorToSleep->GetInteractableComponent())
    {
        NewRecord.Payload.SavedInteractableStates = Interactable->GetCurrentStateTags();
    }
    if (const UDynamicContainerComponent* Container = ActorToSleep->GetContainerComponent())
    {
        NewRecord.Payload.ContainerInventory = Container->GetReplicatedSlots().Items;
    }

    FGroundItemRecordItem& Added = Chunk->ReplicatedGroundItems.Items.Add_GetRef(NewRecord);
    Chunk->ReplicatedGroundItems.MarkItemDirty(Added);
    Chunk->UpdateInstancedMeshes();

    // The physical actor goes back to the inactive pool, freeing up Physics Thread time
    ActorToSleep->DeactivateAndReturnToPool();
}

bool UDynamicGroundItemSubsystem::ExtractItemFromHitResult(const FHitResult& ViewHit, FGuid& OutInstanceID, FIntPoint& OutChunkCoord) const
{
    if (!ViewHit.bBlockingHit) return false;

    if (UHierarchicalInstancedStaticMeshComponent* HISM = Cast<UHierarchicalInstancedStaticMeshComponent>(ViewHit.GetComponent()))
    {
        if (ADynamicItemChunkActor* Chunk = Cast<ADynamicItemChunkActor>(ViewHit.GetActor()))
        {
            if (const TMap<int32, FGuid>* InstMap = Chunk->InstanceHitMap.Find(HISM))
            {
                if (const FGuid* TargetGuid = InstMap->Find(ViewHit.Item))
                {
                    OutInstanceID = *TargetGuid;
                    OutChunkCoord = Chunk->ChunkCoord;
                    return true;
                }
            }
        }
    }
    return false;
}

ADynamicPhysicsActor* UDynamicGroundItemSubsystem::WakeGroundItem(const FGuid& InstanceID, FIntPoint ChunkCoord, AActor* Instigator)
{
    if (!InstanceID.IsValid()) return nullptr;

    ADynamicItemChunkActor** FoundChunk = ActiveItemChunks.Find(ChunkCoord);
    if (!FoundChunk || !(*FoundChunk)) return nullptr;

    ADynamicItemChunkActor* Chunk = *FoundChunk;
    int32 FoundIdx = Chunk->ReplicatedGroundItems.Items.IndexOfByPredicate([&](const FGroundItemRecordItem& I) { return I.InstanceID == InstanceID; });
    if (FoundIdx == INDEX_NONE) return nullptr;

    FGroundItemRecordItem TargetRecord = Chunk->ReplicatedGroundItems.Items[FoundIdx];
    Chunk->ReplicatedGroundItems.Items.RemoveAtSwap(FoundIdx);
    Chunk->ReplicatedGroundItems.MarkArrayDirty();
    Chunk->UpdateInstancedMeshes();

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return nullptr;

    ADynamicPhysicsActor* WokenActor = GS->AcquirePhysicsActor(
        TargetRecord.Payload.ActorArchetypeTag, 
        TargetRecord.Payload.WorldTransform, 
        Instigator, 
        Cast<APawn>(Instigator)
    );

    if (WokenActor)
    {
        if (UDynamicAttributeSet* AttrSet = WokenActor->GetDynamicAttributeSet())
        {
            for (const FDynamicAttributeItem& Attr : TargetRecord.Payload.SavedAttributes)
            {
                if (Attr.AttributeTag.IsValid()) AttrSet->SetDynamicAttributeValue(Attr.AttributeTag, Attr.Value, Attr.LimbId);
            }
        }
        if (UDynamicInteractableComponent* Interactable = WokenActor->GetInteractableComponent())
        {
            for (const FGameplayTag& SavedStateTag : TargetRecord.Payload.SavedInteractableStates)
            {
                Interactable->AddStateTag(SavedStateTag);
            }
        }
        
        // Re-apply inventory via generic container snapshot wrapper
        FDynamicContainerSlotItem Snap;
        Snap.SavedAttributeState = TargetRecord.Payload.SavedAttributes;
        Snap.SavedInteractableStates = TargetRecord.Payload.SavedInteractableStates;
        WokenActor->RestoreFromContainerSnapshot(Snap);
        
        WokenActor->SetItemOwnerGuid(TargetRecord.Payload.ActorPersistentGuid);
        
        // Ensure it simulates physics upon waking up
        if (UPrimitiveComponent* Prim = WokenActor->GetActivePhysicsPrimitive())
        {
            Prim->SetSimulatePhysics(true);
        }
    }
    
    return WokenActor;
}

void UDynamicGroundItemSubsystem::ProcessItemStreaming()
{
    UWorld* World = GetWorld();
    if (!World) return;

    TArray<FVector> PlayerLocations;
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        if (APawn* P = It->Get()->GetPawn())
        {
            PlayerLocations.Add(P->GetActorLocation());
        }
    }

    if (PlayerLocations.Num() == 0) return;

    const float ChunkSize = ADynamicItemChunkActor::CHUNK_SIZE_UNITS;
    const float LoadRadiusSq = FMath::Square(LoadRadius);
    const float UnloadRadiusSq = FMath::Square(UnloadRadius);

    TSet<FIntPoint> RequiredChunks;

    for (const FVector& PLoc : PlayerLocations)
    {
        const int32 RadiusInChunks = FMath::CeilToInt(LoadRadius / ChunkSize);
        const FIntPoint CenterCoord(
            FMath::FloorToInt(PLoc.X / ChunkSize),
            FMath::FloorToInt(PLoc.Y / ChunkSize)
        );

        for (int32 Y = -RadiusInChunks; Y <= RadiusInChunks; ++Y)
        {
            for (int32 X = -RadiusInChunks; X <= RadiusInChunks; ++X)
            {
                const FIntPoint EvalCoord = CenterCoord + FIntPoint(X, Y);
                const FVector ChunkCenter((EvalCoord.X * ChunkSize) + (ChunkSize * 0.5f), (EvalCoord.Y * ChunkSize) + (ChunkSize * 0.5f), PLoc.Z);

                if (FVector::DistSquared(PLoc, ChunkCenter) <= LoadRadiusSq) RequiredChunks.Add(EvalCoord);
            }
        }
    }

    TArray<FIntPoint> ChunksToUnload;
    for (const auto& Pair : ActiveItemChunks)
    {
        bool bKeepLoaded = false;
        FVector ChunkCenter((Pair.Key.X * ChunkSize) + (ChunkSize * 0.5f), (Pair.Key.Y * ChunkSize) + (ChunkSize * 0.5f), 0.0f);

        for (const FVector& PLoc : PlayerLocations)
        {
            ChunkCenter.Z = PLoc.Z; 
            if (FVector::DistSquared(PLoc, ChunkCenter) <= UnloadRadiusSq)
            {
                bKeepLoaded = true;
                break;
            }
        }
        if (!bKeepLoaded) ChunksToUnload.Add(Pair.Key);
    }

    for (const FIntPoint& Coord : ChunksToUnload)
    {
        if (ADynamicItemChunkActor* Chunk = ActiveItemChunks[Coord])
        {
            if (Chunk->ReplicatedGroundItems.Items.Num() > 0)
            {
                FItemChunkSnapshot& Snap = VirtualizedItemChunks.FindOrAdd(Coord);
                Snap.ChunkCoord = Coord;
                Snap.SavedItems = Chunk->ReplicatedGroundItems.Items;
            }
            else
            {
                VirtualizedItemChunks.Remove(Coord);
            }
            Chunk->Destroy();
        }
        ActiveItemChunks.Remove(Coord);
    }

    for (const FIntPoint& Coord : RequiredChunks)
    {
        if (!ActiveItemChunks.Contains(Coord))
        {
            const FVector SpawnLoc(Coord.X * ChunkSize, Coord.Y * ChunkSize, 0.0f);
            FActorSpawnParameters Params;
            Params.Owner = World->GetGameState();
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

            if (ADynamicItemChunkActor* NewChunk = World->SpawnActor<ADynamicItemChunkActor>(ADynamicItemChunkActor::StaticClass(), FTransform(SpawnLoc), Params))
            {
                NewChunk->InitializeChunk(Coord, ChunkSize);
                
                if (const FItemChunkSnapshot* Snap = VirtualizedItemChunks.Find(Coord))
                {
                    NewChunk->ReplicatedGroundItems.Items = Snap->SavedItems;
                    NewChunk->ReplicatedGroundItems.MarkArrayDirty();
                }
                
                ActiveItemChunks.Add(Coord, NewChunk);
                NewChunk->UpdateInstancedMeshes();
            }
        }
    }
}