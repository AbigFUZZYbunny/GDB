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

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Building")
    bool ServerPlaceStructureGhost(const FTransform& IntendedTransform, FGameplayTag PieceTag, FGameplayTag MaterialTag, AActor* Instigator);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Building")
    bool ServerPlacePrefabGhosts(const FTransform& OriginTransform, FGameplayTag PrefabTag, FGameplayTag MaterialTag, AActor* Instigator);

    void SerializeWorldBuildingState(struct FWorldPersistenceSnapshot& OutSnapshot);
    void RestoreWorldBuildingState(const struct FWorldPersistenceSnapshot& InSnapshot);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Building")
    bool CalculateFreeformPlacementTransform(const FHitResult& ViewHit, FGameplayTag PieceToPlace, const FRotator& IntendedPlayerRotation, FTransform& OutTransform) const;

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Building|Environment")
    float EvaluateShelterInsulationAtLocation(const FVector& WorldLocation) const;

private:
    UPROPERTY()
    TMap<FIntPoint, ADynamicBuildingChunkActor*> ActiveBuildingChunks;

    float StreamingTimer = 0.0f;
    float StreamingRadius = 40000.0f;

    ADynamicBuildingChunkActor* GetOrCreateChunkAtLocation(const FVector& WorldLocation);
    void ProcessBuildingStreaming();
};