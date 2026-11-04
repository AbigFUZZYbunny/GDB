// DynamicGroundItemSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DynamicGASTypes.h"
#include "DynamicItemChunkActor.h"
#include "DynamicGroundItemSubsystem.generated.h"

class ADynamicPhysicsActor;

UCLASS()
class GDB_API UDynamicGroundItemSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { return TStatId(); }

	// Converts an awake physics actor into a sleeping HISM instance to save server CPU
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Virtualization")
	void VirtualizePhysicsActor(ADynamicPhysicsActor* ActorToSleep);

	// Awakes a sleeping HISM instance back into a fully physical actor (e.g., when a player kicks it or picks it up)
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Virtualization")
	ADynamicPhysicsActor* WakeGroundItem(const FGuid& InstanceID, FIntPoint ChunkCoord, AActor* Instigator);

	void SerializeWorldItemState(struct FWorldPersistenceSnapshot& OutSnapshot);
	void RestoreWorldItemState(const struct FWorldPersistenceSnapshot& InSnapshot);

	UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Virtualization")
	bool ExtractItemFromHitResult(const FHitResult& ViewHit, FGuid& OutInstanceID, FIntPoint& OutChunkCoord) const;

private:
	UPROPERTY()
	TMap<FIntPoint, ADynamicItemChunkActor*> ActiveItemChunks;

	UPROPERTY()
	TMap<FIntPoint, FItemChunkSnapshot> VirtualizedItemChunks;

	float StreamingTimer = 0.0f;
	float LoadRadius = 25000.0f;   // Items stream in at 250m
	float UnloadRadius = 30000.0f; 

	ADynamicItemChunkActor* GetOrCreateChunkAtLocation(const FVector& WorldLocation);
	void ProcessItemStreaming();
};