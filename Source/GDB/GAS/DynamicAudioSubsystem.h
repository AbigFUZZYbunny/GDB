// ============================================================================
// DynamicAudioSubsystem.h
// ============================================================================
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicAudioSubsystem.generated.h"

class UAudioComponent;

struct FActiveScrapeState
{
    TObjectPtr<UAudioComponent> AudioComp;
    float LastUpdateTime = 0.0f;
    float CurrentVolume = 0.0f;
    float TargetVolume = 0.0f;
};

UCLASS()
class GDB_API UDynamicAudioSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override { return TStatId(); }
    virtual bool IsTickable() const override;

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Audio")
    void PlayDynamicAudioAtLocation(
        FGameplayTag AudioTag, 
        const FVector& Location, 
        AActor* Instigator = nullptr, 
        float VelocityScalar = 0.0f,
        float VolumeMultiplier = 1.0f,
        float PitchMultiplier = 1.0f
    );

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Audio|Procedural")
    void PlayProceduralMaterialImpact(
        FGameplayTag MaterialA, 
        FGameplayTag MaterialB, 
        const FVector& Location, 
        AActor* Instigator, 
        float ImpactVelocity,
        float MassRatio = 1.0f
    );

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Audio|Procedural")
    void PlayEquipmentFrictionAcoustics(
        const AActor* MovingCharacter, 
        float MovementVelocity, 
        uint8 LimbId
    );

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Audio|Procedural")
    void PlayProceduralFoliageWind(
        FGameplayTag FloraMaterialTag,
        const FVector& Location,
        float WindSpeed,
        float FoliageDensity
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

    void PlayMaterialImpactAudio(
        FGameplayTag MaterialA, 
        FGameplayTag MaterialB, 
        const FVector& Location, 
        AActor* Instigator, 
        float ImpactVelocity
    );

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Audio")
    void PlayLimbGroundAcoustics(
        const AActor* ImpactingCharacter, 
        const FHitResult& FloorHit, 
        const FName& ContactBoneName,
        float ForceMultiplier = 1.0f
    );

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Audio")
    void PlayActionAcousticLayers(
        const AActor* SourceActor, 
        FGameplayTag ActionTag, 
        const FVector& Location, 
        float MovementVelocity = 0.0f
    );
    
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Audio|Procedural")
    void UpdateMaterialScrapeAudio(
        const AActor* ScrapingActor, 
        const AActor* SurfaceActor, 
        const FVector& Location, 
        float ScrapeVelocity, 
        float ContactPressure
    );

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Audio")
    FGameplayTag ResolveActorAcousticMaterial(const AActor* InActor) const;
    
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Audio|Acoustics")
    void CalculateAcousticPathProperties(
        const FVector& SourceLocation, 
        const FVector& ListenerLocation, 
        const AActor* ListenerActor,
        float& OutAttenuationRatio, 
        float& OutLowPassCutoffFreq, 
        float& OutReverbSendLevel,
        FVector& OutApparentLocation
    ) const;

private:
    void UpdateContinuousScrapes(float DeltaSeconds);
    // Tracks actively sliding objects to smoothly fade out their audio when movement stops
    TMap<TWeakObjectPtr<const AActor>, FActiveScrapeState> ActiveScrapeComponents;
    
    UAudioComponent* AcquirePooledAudioComponent(const FVector& Location);

    UFUNCTION()
    void OnAudioComponentPlaybackFinished(UAudioComponent* FinishedComponent);

    void InjectMaterialParametersToMetaSound(UAudioComponent* AudioComp, const FString& Prefix, const FAcousticMaterialProperties& Props);
    void BroadcastAcousticStimulusToAI(FGameplayTag StimulusTag, float Magnitude, const FVector& Location, AActor* Instigator);
    void UpdateContinuousEnvironmentAudio(float DeltaSeconds);

    UPROPERTY(Transient)
    TArray<TObjectPtr<UAudioComponent>> AudioComponentPool;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UAudioComponent>> ActiveAudioComponents;

    UPROPERTY(Transient)
    TObjectPtr<UAudioComponent> ContinuousAmbientAudioComponent;

    float AmbientSampleAccumulator = 0.0f;
};