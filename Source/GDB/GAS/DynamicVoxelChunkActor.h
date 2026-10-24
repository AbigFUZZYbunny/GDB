// DynamicVoxelChunkActor.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DynamicGASTypes.h"
#include "ProceduralMeshComponent.h"
#include "DynamicVoxelChunkActor.generated.h"

class UDynamicVoxelSubsystem;

UCLASS()
class GDB_API ADynamicVoxelChunkActor : public AActor
{
    GENERATED_BODY()

public:
    ADynamicVoxelChunkActor();
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel")
    UProceduralMeshComponent* ProceduralMesh;

    UPROPERTY(ReplicatedUsing = OnRep_ChunkCoord, BlueprintReadOnly, Category = "Voxel")
    FIntPoint ChunkCoord = FIntPoint::ZeroValue;

    UFUNCTION()
    void OnRep_ChunkCoord();

    UPROPERTY(ReplicatedUsing = OnRep_VoxelDeltas)
    FFastVoxelDeltaArray ReplicatedDeltas;

    UFUNCTION()
    void OnRep_VoxelDeltas();

    void HandleSingleVoxelReplicated(const FVoxelDeltaItem& Item);

    void InitializeChunk(FIntPoint InCoord, int32 InChunkSize, float InVoxelSize);
    
    void UpdateVoxelData(int32 VoxelIndex, uint8 NewTypeID, uint8 NewHP);
    void SetLODLevel(int32 NewLOD);

    // Callbacks strictly placed in public scope
    void RequestDataGeneration();
    void ReceiveGeneratedData(TArray<uint8>&& InGeneratedData);

    static constexpr int32 CHUNK_SIZE = 32;
    TArray<uint8> CachedVoxelData;

private:
    float VoxelSizeWorldUnits = 100.0f;
    int32 CurrentLOD = 1;
    bool bIsGeneratingMesh = false;
    bool bMeshUpdateQueued = false;
    
    void ScheduleAsyncMeshGeneration();
    void ApplyMeshToComponent(const TArray<FVector>& Vertices, const TArray<int32>& Triangles, const TArray<FVector>& Normals, const TArray<FVector2D>& UVs, const TArray<FColor>& Colors);

    FORCEINLINE int32 GetIndex3D(int32 X, int32 Y, int32 Z) const
    {
        return X + (Y * CHUNK_SIZE) + (Z * CHUNK_SIZE * CHUNK_SIZE);
    }
};