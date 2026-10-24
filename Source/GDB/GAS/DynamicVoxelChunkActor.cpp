#include "DynamicVoxelChunkActor.h"
#include "DynamicVoxelSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "Async/Async.h"
#include "Runtime/Engine/Public/Net/UnrealNetwork.h"

struct FProcMeshTangent;

ADynamicVoxelChunkActor::ADynamicVoxelChunkActor()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    bAlwaysRelevant = false;
    NetPriority = 1.0f;
    SetNetUpdateFrequency(2.0f);
    
    ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProceduralMesh"));
    ProceduralMesh->bUseAsyncCooking = true;
    ProceduralMesh->bCastDynamicShadow = true;
    ProceduralMesh->SetCollisionProfileName(TEXT("BlockAll"));
    RootComponent = ProceduralMesh;

    ReplicatedDeltas.OwningChunk = this;
    CachedVoxelData.Init(0, CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE);
}

void ADynamicVoxelChunkActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(ADynamicVoxelChunkActor, ChunkCoord, COND_InitialOnly);
    DOREPLIFETIME(ADynamicVoxelChunkActor, ReplicatedDeltas);
}

void ADynamicVoxelChunkActor::BeginPlay()
{
    Super::BeginPlay();
    ReplicatedDeltas.OwningChunk = this;
}

void ADynamicVoxelChunkActor::InitializeChunk(FIntPoint InCoord, int32 InChunkSize, float InVoxelSize)
{
    ChunkCoord = InCoord;
    VoxelSizeWorldUnits = InVoxelSize;
    
    const float WorldWidth = InChunkSize * VoxelSizeWorldUnits;
    SetActorLocation(FVector(ChunkCoord.X * WorldWidth, ChunkCoord.Y * WorldWidth, 0.0f));
    SetNetCullDistanceSquared(FMath::Square(WorldWidth * 3.0f));

    if (GetWorld()->GetNetMode() != NM_Client)
    {
        RequestDataGeneration();
    }
}

void ADynamicVoxelChunkActor::OnRep_ChunkCoord()
{
    // Clients run deterministic visual generation, server replicates interaction state overrides
    RequestDataGeneration();
}

void ADynamicVoxelChunkActor::RequestDataGeneration()
{
    if (UWorld* World = GetWorld())
    {
        if (UDynamicVoxelSubsystem* Subsystem = World->GetSubsystem<UDynamicVoxelSubsystem>())
        {
            Subsystem->GenerateChunkDataAsync(ChunkCoord, CHUNK_SIZE, VoxelSizeWorldUnits, this);
        }
    }
}

void ADynamicVoxelChunkActor::ReceiveGeneratedData(TArray<uint8>&& InGeneratedData)
{
    CachedVoxelData = MoveTemp(InGeneratedData);

    for (const FVoxelDeltaItem& Item : ReplicatedDeltas.Items)
    {
        if (CachedVoxelData.IsValidIndex(Item.VoxelIndex))
        {
            CachedVoxelData[Item.VoxelIndex] = Item.VoxelTypeID;
        }
    }

    ScheduleAsyncMeshGeneration();
}

void ADynamicVoxelChunkActor::UpdateVoxelData(int32 VoxelIndex, uint8 NewTypeID, uint8 NewHP)
{
    if (!CachedVoxelData.IsValidIndex(VoxelIndex)) return;
    CachedVoxelData[VoxelIndex] = NewTypeID;

    if (HasAuthority())
    {
        FVoxelDeltaItem NewDelta;
        NewDelta.VoxelIndex = VoxelIndex;
        NewDelta.VoxelTypeID = NewTypeID;
        NewDelta.CurrentHP = NewHP;

        int32 FoundIdx = ReplicatedDeltas.Items.IndexOfByPredicate([VoxelIndex](const FVoxelDeltaItem& Item) {
            return Item.VoxelIndex == VoxelIndex;
        });

        if (FoundIdx != INDEX_NONE)
        {
            ReplicatedDeltas.Items[FoundIdx] = NewDelta;
            ReplicatedDeltas.MarkItemDirty(ReplicatedDeltas.Items[FoundIdx]);
        }
        else
        {
            FVoxelDeltaItem& Added = ReplicatedDeltas.Items.Add_GetRef(NewDelta);
            ReplicatedDeltas.MarkItemDirty(Added);
        }
    }
    
    ScheduleAsyncMeshGeneration();
}

void ADynamicVoxelChunkActor::OnRep_VoxelDeltas()
{
    for (const FVoxelDeltaItem& Item : ReplicatedDeltas.Items)
    {
        if (CachedVoxelData.IsValidIndex(Item.VoxelIndex))
        {
            CachedVoxelData[Item.VoxelIndex] = Item.VoxelTypeID;
        }
    }
    ScheduleAsyncMeshGeneration();
}

void ADynamicVoxelChunkActor::HandleSingleVoxelReplicated(const FVoxelDeltaItem& Item)
{
    if (CachedVoxelData.IsValidIndex(Item.VoxelIndex))
    {
        CachedVoxelData[Item.VoxelIndex] = Item.VoxelTypeID;
        ScheduleAsyncMeshGeneration();
    }
}

void ADynamicVoxelChunkActor::SetLODLevel(int32 NewLOD)
{
    if (CurrentLOD == NewLOD) return;
    CurrentLOD = FMath::Clamp(NewLOD, 1, 4);
    ScheduleAsyncMeshGeneration();
}

void ADynamicVoxelChunkActor::ScheduleAsyncMeshGeneration()
{
    if (bIsGeneratingMesh)
    {
        bMeshUpdateQueued = true;
        return;
    }

    bIsGeneratingMesh = true;
    bMeshUpdateQueued = false;
    
    TArray<uint8> CapturedData = CachedVoxelData;
    const float CapturedSize = VoxelSizeWorldUnits;
    const int32 LODStep = CurrentLOD;
    TWeakObjectPtr<ADynamicVoxelChunkActor> WeakThis(this);

    Async(EAsyncExecution::ThreadPool, [CapturedData, CapturedSize, LODStep, WeakThis]() {
        TArray<FVector> Vertices;
        TArray<int32> Triangles;
        TArray<FVector> Normals;
        TArray<FVector2D> UVs;
        TArray<FColor> Colors;

        const FVector VoxelExtents = FVector(CapturedSize * 0.5f * LODStep);
        int32 VertexCount = 0;

        for (int32 Z = 0; Z < CHUNK_SIZE; Z += LODStep)
        {
            for (int32 Y = 0; Y < CHUNK_SIZE; Y += LODStep)
            {
                for (int32 X = 0; X < CHUNK_SIZE; X += LODStep)
                {
                    int32 Idx = X + (Y * CHUNK_SIZE) + (Z * CHUNK_SIZE * CHUNK_SIZE);
                    uint8 Type = CapturedData[Idx];
                    if (Type == 0) continue; 

                    FVector Center = FVector(X, Y, Z) * CapturedSize + FVector(CapturedSize * 0.5f);
                    
                    auto IsSolid = [&](int32 nX, int32 nY, int32 nZ) -> bool {
                        if (nX < 0 || nY < 0 || nZ < 0 || nX >= CHUNK_SIZE || nY >= CHUNK_SIZE || nZ >= CHUNK_SIZE) return false;
                        return CapturedData[nX + (nY * CHUNK_SIZE) + (nZ * CHUNK_SIZE * CHUNK_SIZE)] != 0;
                    };

                    // Top Face
                    if (!IsSolid(X, Y, Z + LODStep)) 
                    {
                        Vertices.Add(Center + FVector(-VoxelExtents.X, -VoxelExtents.Y, VoxelExtents.Z));
                        Vertices.Add(Center + FVector(-VoxelExtents.X, VoxelExtents.Y, VoxelExtents.Z));
                        Vertices.Add(Center + FVector(VoxelExtents.X, VoxelExtents.Y, VoxelExtents.Z));
                        Vertices.Add(Center + FVector(VoxelExtents.X, -VoxelExtents.Y, VoxelExtents.Z));
                        Triangles.Append({VertexCount, VertexCount+1, VertexCount+2, VertexCount, VertexCount+2, VertexCount+3});
                        Normals.Init(FVector::UpVector, 4);
                        UVs.Append({FVector2D(0,0), FVector2D(0,1), FVector2D(1,1), FVector2D(1,0)});
                        VertexCount += 4;
                    }
                    
                    // Bottom Face
                    if (!IsSolid(X, Y, Z - LODStep)) 
                    {
                        Vertices.Add(Center + FVector(VoxelExtents.X, -VoxelExtents.Y, -VoxelExtents.Z));
                        Vertices.Add(Center + FVector(VoxelExtents.X, VoxelExtents.Y, -VoxelExtents.Z));
                        Vertices.Add(Center + FVector(-VoxelExtents.X, VoxelExtents.Y, -VoxelExtents.Z));
                        Vertices.Add(Center + FVector(-VoxelExtents.X, -VoxelExtents.Y, -VoxelExtents.Z));
                        Triangles.Append({VertexCount, VertexCount+1, VertexCount+2, VertexCount, VertexCount+2, VertexCount+3});
                        Normals.Init(FVector::DownVector, 4);
                        UVs.Append({FVector2D(0,0), FVector2D(0,1), FVector2D(1,1), FVector2D(1,0)});
                        VertexCount += 4;
                    }
                    // Implement remaining 4 faces (North, South, East, West) symmetrically
                }
            }
        }

        Async(EAsyncExecution::TaskGraphMainThread, [WeakThis, Vertices, Triangles, Normals, UVs, Colors]() mutable {
            if (ADynamicVoxelChunkActor* StrongThis = WeakThis.Get())
            {
                StrongThis->ApplyMeshToComponent(Vertices, Triangles, Normals, UVs, Colors);
            }
        });
    });
}

void ADynamicVoxelChunkActor::ApplyMeshToComponent(const TArray<FVector>& Vertices, const TArray<int32>& Triangles, const TArray<FVector>& Normals, const TArray<FVector2D>& UVs, const TArray<FColor>& Colors)
{
    bIsGeneratingMesh = false;
    if (ProceduralMesh && Vertices.Num() > 0)
    {
        ProceduralMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, Colors, TArray<FProcMeshTangent>(), true);
    }
    
    if (bMeshUpdateQueued)
    {
        ScheduleAsyncMeshGeneration();
    }
}

void FVoxelDeltaItem::PreReplicatedRemove(const FFastVoxelDeltaArray& InArrayOwner)
{
    if (InArrayOwner.OwningChunk) InArrayOwner.OwningChunk->HandleSingleVoxelReplicated(*this);
}
void FVoxelDeltaItem::PostReplicatedAdd(const FFastVoxelDeltaArray& InArrayOwner)
{
    if (InArrayOwner.OwningChunk) InArrayOwner.OwningChunk->HandleSingleVoxelReplicated(*this);
}
void FVoxelDeltaItem::PostReplicatedChange(const FFastVoxelDeltaArray& InArrayOwner)
{
    if (InArrayOwner.OwningChunk) InArrayOwner.OwningChunk->HandleSingleVoxelReplicated(*this);
}