// DynamicWorldDirectorSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DynamicGASTypes.h"
#include "DynamicWorldDirectorSubsystem.generated.h"

class ADynamicCharacter;

USTRUCT(BlueprintType)
struct FVirtualNPCRecord
{
    GENERATED_BODY()

    UPROPERTY()
    FGuid NPCGuid;

    UPROPERTY()
    FGameplayTag CharacterTemplateTag;

    UPROPERTY()
    FVector WorldLocation = FVector::ZeroVector;

    UPROPERTY()
    FRotator WorldRotation = FRotator::ZeroRotator;

    UPROPERTY()
    bool bIsTraveling = false;

    UPROPERTY()
    FVector TravelDestination = FVector::ZeroVector;

    UPROPERTY()
    float TravelSpeed = 300.0f;

    UPROPERTY()
    double HibernationStartWorldTime = 0.0;

    UPROPERTY()
    FString SerializedPersistenceJson;
};

UCLASS()
class GDB_API UDynamicWorldDirectorSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    // Registers a newly spawned or generated NPC into the world tracker
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|World Director")
    void RegisterActiveNPC(ADynamicCharacter* NPC);

    // Unregisters an NPC from the world tracker (e.g., when they die or are permanently removed)
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|World Director")
    void UnregisterActiveNPC(ADynamicCharacter* NPC);

    // Forces an NPC into hibernation regardless of player distance (e.g., entering a fast-travel state)
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|World Director")
    void VirtualizeNPC(ADynamicCharacter* NPC);

    // Forces a virtual NPC to instantiate physically
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|World Director")
    ADynamicCharacter* InstantiateNPC(const FGuid& NPCGuid);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|World Director")
    float InstantiationRadius = 10000.0f; // Distance to wake up (100m)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|World Director")
    float HibernationRadius = 14000.0f;   // Distance to sleep (140m) - Hysteresis prevents edge-thrashing

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|World Director")
    bool bIsLocalCoopMode = false;        // Toggles between Cloud AI routing and Local Host macro-simulation

private:
    void PerformProximityChecks();
    void SimulateVirtualNPCs(float DeltaTime);
    void PerformCatchUpSimulation(ADynamicCharacter* NPC, float OfflineHours);

    TMap<FGuid, FVirtualNPCRecord> VirtualNPCRegistry;
    TArray<TWeakObjectPtr<ADynamicCharacter>> ActiveNPCs;
    
    float ProximityCheckAccumulator = 0.0f;
    float MacroSimulationAccumulator = 0.0f;
};