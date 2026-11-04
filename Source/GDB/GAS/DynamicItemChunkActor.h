// DynamicItemChunkActor.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DynamicGASTypes.h"
#include "DynamicItemChunkActor.generated.h"

class UHierarchicalInstancedStaticMeshComponent;

UCLASS()
class GDB_API ADynamicItemChunkActor : public AActor
{
	GENERATED_BODY()

public:
	ADynamicItemChunkActor();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_ChunkCoord, BlueprintReadOnly, Category = "Dynamic GAS|Virtualization")
	FIntPoint ChunkCoord = FIntPoint::ZeroValue;

	UFUNCTION()
	void OnRep_ChunkCoord();

	UPROPERTY(ReplicatedUsing = OnRep_GroundItems)
	FFastGroundItemArray ReplicatedGroundItems;

	UFUNCTION()
	void OnRep_GroundItems();

	void HandleSingleItemReplicated(const FGroundItemRecordItem& Item);
	void InitializeChunk(FIntPoint InCoord, float InChunkSize);
	void UpdateInstancedMeshes();

	static constexpr float CHUNK_SIZE_UNITS = 5000.0f;
	TMap<UHierarchicalInstancedStaticMeshComponent*, TMap<int32, FGuid>> InstanceHitMap;

protected:
	// NEW: Intercepts physical collisions from players/objects to wake up the specific HISM instance
	UFUNCTION()
	void OnHISMHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

private:
	float ChunkWorldSize = 5000.0f;

	UPROPERTY(Transient)
	TMap<FGameplayTag, UHierarchicalInstancedStaticMeshComponent*> InstancedMeshPool;

	UHierarchicalInstancedStaticMeshComponent* GetOrCreateHISM(FGameplayTag ArchetypeTag);
};