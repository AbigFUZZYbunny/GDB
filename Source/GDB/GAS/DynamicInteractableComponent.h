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

    // Fired on both Server and Client when state tags change or an interaction executes
    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Interaction")
    FDynamicInteractionExecutedSignature OnInteractionStateChanged;

    // Initial state tags configured in editor or JSON (e.g., State.Interactable.Door, State.Interactable.Closed, State.Interactable.Locked)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    FGameplayTagContainer StartupStateTags;

    // Tag-indexed kinematic target anchors (e.g., Interaction.Target.DoorHandle -> Socket "DoorKnob" or Handle Actor)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Interaction")
    TArray<FInteractionTargetPoint> KinematicTargetPoints;

    UPROPERTY(ReplicatedUsing = OnRep_ReplicatedStateTags)
    FFastInteractableStateArray ReplicatedStateTags;

    UFUNCTION()
    void OnRep_ReplicatedStateTags();

    // O(1) validation check used by UDynamicGameplayAbility::CanActivateAbility
    bool CanAcceptInteraction(const FDynamicInteractionProfile& Profile, const AActor* QueryInstigator) const;

    // Server-authoritative state transition and physics execution
    bool ServerExecuteInteraction(AActor* InstigatorActor, const FDynamicInteractionProfile& Profile, float ChargeAlpha = 1.0f);

    // Resolves the exact world transform of a handle/lever/grip for GASP kinematic hand placement
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Interaction")
    bool ResolveKinematicTargetTransform(FGameplayTag TargetPointTag, FTransform& OutWorldTransform, USceneComponent*& OutTargetComponent) const;

    const FGameplayTagContainer& GetCurrentStateTags() const { return LocalStateCache; }
    bool HasStateTag(FGameplayTag TagToCheck) const { return LocalStateCache.HasTag(TagToCheck); }

    void AddStateTag(FGameplayTag TagToAdd);
    void RemoveStateTag(FGameplayTag TagToRemove);

private:
    FGameplayTagContainer LocalStateCache;
};