#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicAudioSubsystem.generated.h"

UCLASS()
class GDB_API UDynamicAudioSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Audio")
    void PlayDynamicAudioAtLocation(
        FGameplayTag AudioTag, 
        const FVector& Location, 
        AActor* Instigator = nullptr, 
        float ImpactVelocity = 0.0f,
        float VolumeMultiplier = 1.0f,
        float PitchMultiplier = 1.0f
    );

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Audio")
    void PlayMaterialImpactAudio(
        FGameplayTag MaterialA, 
        FGameplayTag MaterialB, 
        const FVector& Location, 
        AActor* Instigator = nullptr, 
        AActor* HitTargetActor = nullptr, 
        float ImpactVelocity = 0.0f
    );

    // Overload fixing C2664 in DynamicForceComponent.cpp
    void PlayMaterialImpactAudio(
        FGameplayTag MaterialA, 
        FGameplayTag MaterialB, 
        const FVector& Location, 
        AActor* Instigator, 
        float ImpactVelocity
    );

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Audio")
    void PlayFootstepAcousticLayers(
        const AActor* SteppingCharacter, 
        const FHitResult& FloorHit, 
        uint8 FootLimbId
    );

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Audio")
    void PlayActionAcousticLayers(
        const AActor* SourceActor, 
        FGameplayTag ActionTag, 
        const FVector& Location, 
        float MovementVelocity = 0.0f
    );

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Audio")
    FGameplayTag ResolveActorAcousticMaterial(const AActor* InActor) const;

private:
    void PlayAcousticProfile(
        const FAcousticInteractionProfile& Profile, 
        const FVector& Location, 
        AActor* Instigator, 
        AActor* HitTargetActor, 
        float ImpactVelocity
    );

    void BroadcastAcousticStimulusToAI(
        const FDynamicAudioDef& AudioDef, 
        const FVector& Location, 
        AActor* Instigator,
        float EffectiveVolume = 1.0f
    );
};