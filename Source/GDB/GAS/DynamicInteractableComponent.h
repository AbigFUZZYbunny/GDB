// DynamicInteractableComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicInteractableComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDynamicInteractionExecutedSignature, AActor*, InstigatorActor, FGameplayTag, ActionTag, const FGameplayTagContainer&, CurrentStates);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicInteractableComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDynamicInteractableComponent();
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Interaction")
    FDynamicInteractionExecutedSignature OnInteractionStateChanged;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    FGameplayTagContainer StartupStateTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    TArray<FInteractionTargetPoint> KinematicTargetPoints;

    UPROPERTY(ReplicatedUsing = OnRep_ReplicatedStateTags)
    FFastInteractableStateArray ReplicatedStateTags;

    UFUNCTION()
    void OnRep_ReplicatedStateTags();

    bool CanAcceptInteraction(const FDynamicInteractionProfile& Profile, const AActor* QueryInstigator) const;
    bool ServerExecuteInteraction(AActor* InstigatorActor, const FDynamicInteractionProfile& Profile, float ChargeAlpha = 1.0f);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Interaction")
    bool ResolveKinematicTargetTransform(FGameplayTag TargetPointTag, FTransform& OutWorldTransform, USceneComponent*& OutTargetComponent) const;

    const FGameplayTagContainer& GetCurrentStateTags() const { return LocalStateCache; }
    bool HasStateTag(FGameplayTag TagToCheck) const { return LocalStateCache.HasTag(TagToCheck); }
    void AddStateTag(FGameplayTag TagToAdd);
    void RemoveStateTag(FGameplayTag TagToRemove);

    // Explicit cache invalidation and scrubbing routines for object pooling
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Interaction")
    void InvalidateAllKinematicCaches();

    void ResetForPooling();

private:
    FGameplayTagContainer LocalStateCache;
};