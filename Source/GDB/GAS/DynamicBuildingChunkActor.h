// DynamicBuildingChunkActor.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DynamicGASTypes.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "DynamicBuildingChunkActor.generated.h"

UCLASS()
class GDB_API ADynamicBuildingChunkActor : public AActor
{
	GENERATED_BODY()

public:
	ADynamicBuildingChunkActor();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_ChunkCoord, BlueprintReadOnly, Category = "Building")
	FIntPoint ChunkCoord = FIntPoint::ZeroValue;

	UFUNCTION()
	void OnRep_ChunkCoord();

	UPROPERTY(ReplicatedUsing = OnRep_StructureDeltas)
	FFastStructureInstanceArray ReplicatedStructures;

	UFUNCTION()
	void OnRep_StructureDeltas();

	void HandleSingleStructureReplicated(const FStructureInstanceItem& Item);

	void InitializeChunk(FIntPoint InCoord, float InChunkSize);
    
	// Integrity graph logic executed on the ThreadPool
	void RecalculateStructuralIntegrityAsync();

	// HISM Visual Update
	void UpdateInstancedMeshes();

	static constexpr float CHUNK_SIZE_UNITS = 5000.0f;

	// Resolves stress graph adjacencies
	TMap<FGuid, TArray<FGuid>> AdjacencyGraph;

private:
	float ChunkWorldSize = 5000.0f;
	bool bGraphUpdateQueued = false;
	bool bIsCalculatingIntegrity = false;

	// Dynamic map pooling HISMs based on combined Piece + Material visually
	UPROPERTY(Transient)
	TMap<uint64, UHierarchicalInstancedStaticMeshComponent*> InstancedMeshPool;

	UHierarchicalInstancedStaticMeshComponent* GetOrCreateHISM(FGameplayTag PieceTag, FGameplayTag MaterialTag);
};