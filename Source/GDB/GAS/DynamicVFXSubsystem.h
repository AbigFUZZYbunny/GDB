// DynamicVFXSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicVFXSubsystem.generated.h"

class UNiagaraSystem;

UCLASS()
class GDB_API UDynamicVFXSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // Standard instantaneous VFX
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|VFX")
    void PlayVFXLayer(
        FGameplayTag EventTag,
        const FDynamicTagToVFXLayerMapWrapper& VfxEventMap,
        const FVector& Location, 
        const FVector& Normal, 
        float Magnitude, 
        AActor* InstigatorActor = nullptr,
        AActor* HitTargetActor = nullptr
    );

    // Continuous Channeled VFX (Updates existing Niagara Data Channel payloads)
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|VFX")
    void UpdateChanneledVFXLayer(
        FGuid ChannelID,
        FGameplayTag EventTag,
        const FDynamicTagToVFXLayerMapWrapper& VfxEventMap,
        const FVector& StartLocation,
        const FVector& EndLocation,
        const FVector& Normal,
        float Magnitude,
        AActor* InstigatorActor = nullptr,
        AActor* HitTargetActor = nullptr
    );

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|VFX")
    void PlayDynamicVFX(
        FGameplayTag VfxTag, 
        FGuid ChannelID,
        const FVector& StartLocation, 
        const FVector& EndLocation, 
        const FVector& Normal, 
        float Magnitude, 
        AActor* InstigatorActor = nullptr,
        AActor* HitTargetActor = nullptr
    );

    FLinearColor ResolveDynamicColor(const FDynamicVFXDef& VfxDef, AActor* InstigatorActor, AActor* TargetActor) const;

protected:
    // Blueprint Native Event mapping directly to Niagara Data Channel 'Write' nodes
    UFUNCTION(BlueprintNativeEvent, Category = "Dynamic GAS|VFX")
    void PushToNiagaraDataChannel(
        FName ChannelName,
        int32 PayloadTypeID,
        FGuid ChannelID,
        const FVector& StartLocation, 
        const FVector& EndLocation, 
        const FVector& Normal, 
        float ScaledRadius,
        float Duration,
        FLinearColor Color,
        UNiagaraSystem* FallbackSystem
    );
};