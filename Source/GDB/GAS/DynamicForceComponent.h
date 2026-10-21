// DynamicForceComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicForceComponent.generated.h"

class UMoverComponent;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicForceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDynamicForceComponent();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Enqueues an instantaneous movement impulse.
	 * Integrates with Unreal's Mover Component via QueueLayeredMove to preserve rollback & resimulation prediction.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Movement")
	void QueueInstantMovementImpulse(FVector ImpulseDirection, float Intensity);

	// Consumes accumulated pending impulses (fallback for non-mover or custom tick paths)
	FVector ConsumePendingImpulse();

	UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Movement")
	bool HasPendingImpulse() const { return bHasPendingImpulse; }

	void ServerApplyLimbForce(uint8 LimbId, const FName& BoneName, float ClampedImpulse, const FVector& HitLocation, const FVector& HitNormal);
	void OnClientReceiveAuthoritativeHit(const FFastPhysicsHitRepItem& HitItem);

	UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Physics")
	void ApplyPredictiveLimbImpulse(const FName& BoneName, float ImpulseMagnitude, const FVector& HitLocation, const FVector& HitNormal);

protected:
	UPROPERTY(Replicated)
	FFastPhysicsHitRepArray ReplicatedHitBuffer;

	int32 WriteIndex = 0;
	static constexpr int32 MaxReplicatedHitBufferSize = 8;

private:
	FVector PendingImpulseVector;
	bool bHasPendingImpulse;

	UPROPERTY()
	TWeakObjectPtr<UMoverComponent> CachedMoverComp;
};