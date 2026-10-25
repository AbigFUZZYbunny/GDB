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

    void RequestAsynchronousRoomEvaluation();
    
    // AI Ownership Queries (FIXED: Out parameters instead of raw struct pointers)
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Building|Rooms")
    bool GetRoomOwnedByCharacter(FGuid CharacterGuid, FGameplayTag RoomTypeTag, FDynamicRoomInstance& OutRoom) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Building|Rooms")
    bool GetPropertyOwnedByCharacter(FGuid CharacterGuid, FDynamicPropertyInstance& OutProperty) const;

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Building|Rooms")
    class UDynamicContainerComponent* FindOptimalAIContainerInRoomType(FVector SearchOrigin, FGameplayTag RoomTypeTag, FGameplayTag ItemArchetypeToStore, FGuid OptionalOwnerGuid) const;

private:
    UPROPERTY()
    TMap<FIntPoint, ADynamicBuildingChunkActor*> ActiveBuildingChunks;

    UPROPERTY()
    TArray<FDynamicRoomInstance> ActiveRooms;

    UPROPERTY()
    TArray<FDynamicPropertyInstance> ActiveProperties;

    float StreamingTimer = 0.0f;
    float StreamingRadius = 40000.0f;
    bool bRoomEvaluationPending = false;

    ADynamicBuildingChunkActor* GetOrCreateChunkAtLocation(const FVector& WorldLocation);
    void ProcessBuildingStreaming();

    void ExecuteRoomFloodFillAsync();
    FGameplayTag ClassifyRoomByContents(const FBox& RoomBounds, float RoomVolume) const;
};