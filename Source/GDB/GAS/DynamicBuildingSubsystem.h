// DynamicBuildingSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DynamicGASTypes.h"
#include "DynamicBuildingChunkActor.h"
#include "DynamicBuildingSubsystem.generated.h"

UCLASS()
class GDB_API UDynamicBuildingSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { return TStatId(); }

	// Completely free-form placement, unrestricted by grids
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Building")
	bool ServerPlaceStructurePiece(const FTransform& FreeTransform, FGameplayTag PieceTag, FGameplayTag MaterialTag, AActor* Instigator);

	// Iterates through prefab JSON definition and drops individual boards
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Building")
	bool ServerPlacePrefab(const FTransform& OriginTransform, FGameplayTag PrefabTag, FGameplayTag MaterialTag, AActor* Instigator);

	void SerializeWorldBuildingState(struct FWorldPersistenceSnapshot& OutSnapshot);
	void RestoreWorldBuildingState(const struct FWorldPersistenceSnapshot& InSnapshot);

private:
	UPROPERTY()
	TMap<FIntPoint, ADynamicBuildingChunkActor*> ActiveBuildingChunks;

	float StreamingTimer = 0.0f;
	float StreamingRadius = 40000.0f;

	ADynamicBuildingChunkActor* GetOrCreateChunkAtLocation(const FVector& WorldLocation);
	void ProcessBuildingStreaming();
};