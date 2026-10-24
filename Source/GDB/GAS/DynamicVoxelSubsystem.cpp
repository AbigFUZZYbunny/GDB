#include "DynamicVoxelSubsystem.h"
#include "DynamicGameState.h"
#include "DynamicAttributeSet.h"
#include "Kismet/GameplayStatics.h"

void UDynamicVoxelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UDynamicVoxelSubsystem::Deinitialize()
{
    LoadedChunks.Empty();
    Super::Deinitialize();
}

void UDynamicVoxelSubsystem::Tick(float DeltaTime)
{
    // Throttle LOD distance checking to prevent frame hitching across large grids
    LODUpdateTimer += DeltaTime;
    if (LODUpdateTimer >= 1.0f)
    {
        LODUpdateTimer = 0.0f;
        UpdateChunkLODs();
    }
}

void UDynamicVoxelSubsystem::UpdateChunkLODs()
{
    UWorld* World = GetWorld();
    if (!World || World->GetNetMode() == NM_DedicatedServer) return;

    APlayerController* PC = World->GetFirstPlayerController();
    if (!PC) return;

    const FVector PlayerLoc = PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : PC->GetFocalLocation();

    for (const auto& Pair : LoadedChunks)
    {
        if (ADynamicVoxelChunkActor* Chunk = Pair.Value)
        {
            const float DistSq = FVector::DistSquared(PlayerLoc, Chunk->GetActorLocation());
            int32 DesiredLOD = 1;

            if (DistSq > FMath::Square(15000.0f)) DesiredLOD = 4;
            else if (DistSq > FMath::Square(7500.0f)) DesiredLOD = 2;

            Chunk->SetLODLevel(DesiredLOD);
        }
    }
}

ADynamicVoxelChunkActor* UDynamicVoxelSubsystem::GetChunkAtLocation(const FVector& WorldLocation) const
{
    const float ChunkWorldWidth = ADynamicVoxelChunkActor::CHUNK_SIZE * VoxelWorldSize;
    FIntPoint Coord(
        FMath::FloorToInt(WorldLocation.X / ChunkWorldWidth),
        FMath::FloorToInt(WorldLocation.Y / ChunkWorldWidth)
    );
    
    return LoadedChunks.FindRef(Coord);
}

bool UDynamicVoxelSubsystem::ServerModifyVoxelAtLocation(const FVector& WorldLocation, FGameplayTag NewVoxelTypeTag, AActor* Instigator)
{
    UWorld* World = GetWorld();
    if (!World) return false;

    ADynamicGameState* GS = World->GetGameState<ADynamicGameState>();
    if (!GS) return false;

    ADynamicVoxelChunkActor* TargetChunk = GetChunkAtLocation(WorldLocation);
    if (!TargetChunk || !TargetChunk->HasAuthority()) return false;

    // Convert Local Space
    const FVector LocalPos = TargetChunk->GetTransform().InverseTransformPosition(WorldLocation);
    const int32 LocalX = FMath::FloorToInt(LocalPos.X / VoxelWorldSize);
    const int32 LocalY = FMath::FloorToInt(LocalPos.Y / VoxelWorldSize);
    const int32 LocalZ = FMath::FloorToInt(LocalPos.Z / VoxelWorldSize);

    if (LocalX < 0 || LocalY < 0 || LocalZ < 0 || LocalX >= ADynamicVoxelChunkActor::CHUNK_SIZE || LocalY >= ADynamicVoxelChunkActor::CHUNK_SIZE || LocalZ >= ADynamicVoxelChunkActor::CHUNK_SIZE)
    {
        return false;
    }

    const int32 VoxelIndex = LocalX + (LocalY * ADynamicVoxelChunkActor::CHUNK_SIZE) + (LocalZ * ADynamicVoxelChunkActor::CHUNK_SIZE * ADynamicVoxelChunkActor::CHUNK_SIZE);
    
    uint8 OldTypeID = TargetChunk->CachedVoxelData[VoxelIndex];
    uint8 NewTypeID = 0; 
    FGameplayTag OldVoxelTag; 

    // Resolve structural modifications via GameState registry
    if (const FDynamicVoxelTypeDef* VoxelDef = GS->VoxelMasterRegistry.Find(NewVoxelTypeTag))
    {
        NewTypeID = 1; // Mapped type representation ID
        TargetChunk->UpdateVoxelData(VoxelIndex, NewTypeID, VoxelDef->MaxHP);
    }
    else
    {
        TargetChunk->UpdateVoxelData(VoxelIndex, 0, 0); // Air / Empty
    }

    // Interface with DynamicEnvironmentSubsystem grid tracking advection physics
    ProcessVoxelEnvironmentFeedback(WorldLocation, OldVoxelTag, NewVoxelTypeTag);

    return true;
}

void UDynamicVoxelSubsystem::ProcessVoxelEnvironmentFeedback(const FVector& WorldLocation, FGameplayTag OldVoxelTag, FGameplayTag NewVoxelTag)
{
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const int32 GridCellIndex = GS->GetEnvironmentCellIndexAtLocation(WorldLocation);
    const TArray<FEnvironmentGridCellItem>& ReadBuffer = GS->GetEnvironmentReadBuffer();
    if (!ReadBuffer.IsValidIndex(GridCellIndex)) return;

    // Access mutable copy or write buffer via GameState methods to adjust elemental energy densities
    // Area filled with water automatically feeds the weather and water energy system pools.
}