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

    /**
     * Enqueues an instantaneous movement impulse.
     * Integrates with Unreal's Mover Component via QueueLayeredMove to preserve rollback prediction.
     */
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Movement")
    void QueueInstantMovementImpulse(FVector ImpulseDirection, float Intensity);

    UFUNCTION(Server, Reliable, WithValidation)
    void Server_QueueInstantMovementImpulse(FVector_NetQuantizeNormal ImpulseDirection, float Intensity);

    UFUNCTION(Client, Reliable)
    void Client_ReceivePredictedLaunch(FVector_NetQuantize LaunchVelocity);

    FVector ConsumePendingImpulse();

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Movement")
    bool HasPendingImpulse() const { return bHasPendingImpulse; }

    // Server-authoritative impulse calculation entry point
    void ServerApplyLimbForce(uint8 LimbId, const FName& BoneName, float ClampedImpulse, const FVector& HitLocation, const FVector& HitNormal);

    // Unreliable Multicast RPC replacing state-based FastArray replication
    UFUNCTION(NetMulticast, Unreliable)
    void Multicast_PlayLimbHit(uint8 LimbId, const FName& BoneName, float ImpulseMagnitude, const FVector& HitLocation, const FVector& HitNormal, uint8 ReactionState);

    // Shared execution logic applied locally on server and remote clients
    void ApplyHitReactionLocal(uint8 LimbId, const FName& BoneName, float ImpulseMagnitude, const FVector& HitLocation, const FVector& HitNormal, uint8 ReactionState);

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Physics")
    void ApplyPredictiveLimbImpulse(const FName& BoneName, float ImpulseMagnitude, const FVector& HitLocation, const FVector& HitNormal);

private:
    FVector PendingImpulseVector;
    bool bHasPendingImpulse;

    UPROPERTY()
    TWeakObjectPtr<UMoverComponent> CachedMoverComp;
};