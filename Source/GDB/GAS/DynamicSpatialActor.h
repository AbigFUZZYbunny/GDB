// DynamicSpatialActor.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DynamicGASTypes.h"
#include "DynamicSpatialActor.generated.h"

class USphereComponent;
class UProjectileMovementComponent;
class UAbilitySystemComponent;

UCLASS()
class GDB_API ADynamicSpatialActor : public AActor
{
    GENERATED_BODY()

public:
    ADynamicSpatialActor();

    virtual void Tick(float DeltaTime) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // Activates the pooled actor with a unified SpatialDelivery configuration
    void ActivateSpatialDelivery(
        UAbilitySystemComponent* InSourceASC,
        const FDynamicSpatialDeliveryDef& InDeliveryDef,
        const TArray<FSourcePassBackRule>& InPassBackRules,
        float InResolvedSpeed,
        float InPowerMultiplier,
        AActor* InHomingTarget = nullptr);

    // Resets state and returns actor to ADynamicGameState pool
    void DeactivateAndReturnToPool();

protected:
    UFUNCTION()
    void OnSphereHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

    UFUNCTION()
    void OnSphereBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    UFUNCTION()
    void OnSphereEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

    UFUNCTION()
    void OnRep_IsActiveInWorld();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spatial")
    USphereComponent* CollisionSphere;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spatial")
    UProjectileMovementComponent* ProjectileMovement;

    UPROPERTY(ReplicatedUsing = OnRep_IsActiveInWorld)
    bool bIsActiveInWorld = false;

    UPROPERTY(Replicated)
    ESpatialDeliveryMotion MotionType = ESpatialDeliveryMotion::StraightVelocity;

    UPROPERTY(Replicated)
    ESpatialShapeBehavior ShapeBehavior = ESpatialShapeBehavior::PointImpact;

    UPROPERTY(Replicated)
    float HomingTurnRate = 240.0f;

    UPROPERTY(Replicated)
    TWeakObjectPtr<AActor> HomingTarget;

private:
    void TickShapeAndAura(float DeltaTime);
    void TickGuidance(float DeltaTime);
    void ApplyImpactAndEffects(AActor* TargetActor, const FName& HitBone, const FVector& HitLocation, const FVector& HitNormal);
    void ApplyPassBackToSource(EPassBackTriggerEvent EventType, float DamageAmount);

    bool bIsProjectileMode = true;
    float ElapsedTime = 0.0f;
    float TickAccumulator = 0.0f;
    float CurrentRadius = 16.0f;
    float PowerMultiplier = 1.0f;

    TWeakObjectPtr<UAbilitySystemComponent> SourceASC;
    FDynamicSpatialDeliveryDef DeliveryDef;
    TArray<FSourcePassBackRule> PassBackRules;

    UPROPERTY()
    TSet<TObjectPtr<AActor>> ActiveTargets;
};