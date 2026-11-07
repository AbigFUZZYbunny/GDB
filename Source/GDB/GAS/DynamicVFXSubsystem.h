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

protected:
	void PlayDynamicVFX(
		FGameplayTag VfxTag, 
		const FVector& Location, 
		const FVector& Normal, 
		float Magnitude, 
		AActor* InstigatorActor,
		AActor* HitTargetActor
	);

	FLinearColor ResolveDynamicColor(const FDynamicVFXDef& VfxDef, AActor* InstigatorActor, AActor* TargetActor) const;

	UFUNCTION(BlueprintNativeEvent, Category = "Dynamic GAS|VFX")
	void PushToNiagaraDataChannel(
		FName ChannelName,
		int32 PayloadTypeID,
		const FVector& Location, 
		const FVector& Normal, 
		float ScaledRadius,
		float Duration,
		FLinearColor Color,
		UNiagaraSystem* FallbackSystem
	);
};