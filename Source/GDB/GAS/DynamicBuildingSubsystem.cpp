// DynamicBuildingSubsystem.cpp
#include "DynamicBuildingSubsystem.h"
#include "DynamicGameState.h"
#include "GameFramework/PlayerController.h"

void UDynamicBuildingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UDynamicBuildingSubsystem::Deinitialize()
{
    ActiveBuildingChunks.Empty();
    Super::Deinitialize();
}

void UDynamicBuildingSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (GetWorld() && GetWorld()->GetNetMode() != NM_Client)
    {
        StreamingTimer += DeltaTime;
        if (StreamingTimer >= 2.0f)
        {
            StreamingTimer = 0.0f;
            ProcessBuildingStreaming();
        }
    }
}

ADynamicBuildingChunkActor* UDynamicBuildingSubsystem::GetOrCreateChunkAtLocation(const FVector& WorldLocation)
{
    FIntPoint Coord(
        FMath::FloorToInt(WorldLocation.X / ADynamicBuildingChunkActor::CHUNK_SIZE_UNITS),
        FMath::FloorToInt(WorldLocation.Y / ADynamicBuildingChunkActor::CHUNK_SIZE_UNITS)
    );

    if (ADynamicBuildingChunkActor** Found = ActiveBuildingChunks.Find(Coord))
    {
        return *Found;
    }

    if (UWorld* World = GetWorld())
    {
        FActorSpawnParameters Params;
        Params.Owner = World->GetGameState();
        if (ADynamicBuildingChunkActor* NewChunk = World->SpawnActor<ADynamicBuildingChunkActor>(ADynamicBuildingChunkActor::StaticClass(), FTransform::Identity, Params))
        {
            NewChunk->InitializeChunk(Coord, ADynamicBuildingChunkActor::CHUNK_SIZE_UNITS);
            ActiveBuildingChunks.Add(Coord, NewChunk);
            return NewChunk;
        }
    }
    return nullptr;
}

bool UDynamicBuildingSubsystem::ServerPlaceStructurePiece(const FTransform& FreeTransform, FGameplayTag PieceTag, FGameplayTag MaterialTag, AActor* Instigator)
{
    if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client) return false;

    ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>();
    if (!GS) return false;

    const FStructurePieceDef* PieceDef = GS->StructurePieceRegistry.Find(PieceTag);
    const FStructureMaterialDef* MatDef = GS->StructureMaterialRegistry.Find(MaterialTag);

    if (!PieceDef || !MatDef) return false;

    ADynamicBuildingChunkActor* Chunk = GetOrCreateChunkAtLocation(FreeTransform.GetLocation());
    if (!Chunk) return false;

    FStructureInstanceItem NewItem;
    NewItem.InstanceID = FGuid::NewGuid();
    NewItem.PieceTag = PieceTag;
    NewItem.MaterialTag = MaterialTag;
    NewItem.WorldTransform = FreeTransform;
    NewItem.CurrentHP = PieceDef->BaseMaxHP * MatDef->DurabilityMultiplier;
    NewItem.SupportValue = 0.0f; 

    FStructureInstanceItem& Added = Chunk->ReplicatedStructures.Items.Add_GetRef(NewItem);
    Chunk->ReplicatedStructures.MarkItemDirty(Added);
    
    // Trigger localized stress calculation
    Chunk->RecalculateStructuralIntegrityAsync();

    return true;
}

bool UDynamicBuildingSubsystem::ServerPlacePrefab(const FTransform& OriginTransform, FGameplayTag PrefabTag, FGameplayTag MaterialTag, AActor* Instigator)
{
    if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client) return false;

    ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>();
    if (!GS) return false;

    const FStructurePrefabDef* PrefabDef = GS->StructurePrefabRegistry.Find(PrefabTag);
    if (!PrefabDef) return false;

    // A skilled player places the prefab, but the system physically places each constituent piece 
    // ensuring the structural graph calculates them as distinct load-bearing elements.
    bool bSuccess = true;
    for (const FPrefabComponentDef& Comp : PrefabDef->Components)
    {
        FTransform WorldTM = Comp.RelativeTransform * OriginTransform;
        bSuccess &= ServerPlaceStructurePiece(WorldTM, Comp.PieceTag, MaterialTag, Instigator);
    }
    return bSuccess;
}

void UDynamicBuildingSubsystem::ProcessBuildingStreaming()
{
    // Streams building chunks similarly to voxel chunks, dropping actors that have no players within 400m
    // and storing their FastArray state in a virtualized hash map if needed.
    // (Implementation skipped for brevity, follows exact logic of Voxel streaming virtualization)
}

void UDynamicBuildingSubsystem::SerializeWorldBuildingState(FWorldPersistenceSnapshot& OutSnapshot)
{
    // Map ActiveBuildingChunks to OutSnapshot.ModifiedBuildingChunks
}

void UDynamicBuildingSubsystem::RestoreWorldBuildingState(const FWorldPersistenceSnapshot& InSnapshot)
{
    // Map InSnapshot.ModifiedBuildingChunks back to chunks via GetOrCreateChunkAtLocation
}