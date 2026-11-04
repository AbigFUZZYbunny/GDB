// DynamicSpatialActor.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DynamicGASTypes.h"
#include "DynamicSpatialActor.generated.h"

class UAbilitySystemComponent;
class UStaticMeshComponent;

UCLASS()
class GDB_API ADynamicSpatialActor : public AActor, public IDynamicPoolScrubbableInterface
{
    GENERATED_BODY()

public:
    ADynamicSpatialActor();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void ResetToDefaultPoolState() override;

    void ActivateSpatialDelivery(
        UAbilitySystemComponent* InSourceASC,
        const FDynamicSpatialDeliveryDef& InDeliveryDef,
        const TArray<FSourcePassBackRule>& InPassBackRules,
        float InSpeed,
        float InPowerMultiplier,
        FVector InDirection,
        AActor* InHomingTarget = nullptr
    );

    void DeactivateAndReturnToPool();

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> MeshComponent;

    UPROPERTY()
    TWeakObjectPtr<UAbilitySystemComponent> SourceASC;

    UPROPERTY()
    FDynamicSpatialDeliveryDef DeliveryDef;

    UPROPERTY()
    TArray<FSourcePassBackRule> PassBackRules;

    float CurrentSpeed = 0.0f;
    float PowerMultiplier = 1.0f;
    float ElapsedLifetime = 0.0f;
    bool bIsActive = false;

    FVector CurrentVelocity = FVector::ZeroVector;

    UPROPERTY()
    TWeakObjectPtr<AActor> HomingTargetActor;

    TSet<TWeakObjectPtr<AActor>> ImpactedActors;
};