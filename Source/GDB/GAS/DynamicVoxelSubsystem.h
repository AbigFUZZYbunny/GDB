#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DynamicGASTypes.h"
#include "DynamicVoxelChunkActor.h"
#include "DynamicVoxelSubsystem.generated.h"

class ADynamicGameState;

UCLASS()
class GDB_API UDynamicVoxelSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { return TStatId(); }

	// World interaction entry point. Thread-safe execution for environment feedback.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Voxels")
	bool ServerModifyVoxelAtLocation(const FVector& WorldLocation, FGameplayTag NewVoxelTypeTag, AActor* Instigator);

	UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Voxels")
	ADynamicVoxelChunkActor* GetChunkAtLocation(const FVector& WorldLocation) const;

private:
	UPROPERTY()
	TMap<FIntPoint, ADynamicVoxelChunkActor*> LoadedChunks;

	float VoxelWorldSize = 100.0f;
	float LODUpdateTimer = 0.0f;

	void ProcessVoxelEnvironmentFeedback(const FVector& WorldLocation, FGameplayTag OldVoxelTag, FGameplayTag NewVoxelTag);
	void UpdateChunkLODs();
};