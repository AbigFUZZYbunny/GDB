#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicStateTreeComponent.generated.h"

class ADynamicCharacter;
class AAIController;
class ADynamicGameState;
class UAnimMontage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDynamicStateChangedSignature, FGameplayTag, OldStateTag, FGameplayTag, NewStateTag);

USTRUCT()
struct FActiveStateTreeTaskState
{
    GENERATED_BODY()

    UPROPERTY()
    FDynamicStateTreeTaskDef TaskDef;

    UPROPERTY()
    float ElapsedTaskTime = 0.0f;

    UPROPERTY()
    float TargetDuration = 1.0f;

    UPROPERTY()
    bool bIsRunning = false;

    UPROPERTY()
    bool bWaitingForEQS = false;

    UPROPERTY()
    FGuid ActiveCloudRequestGuid;

    UPROPERTY()
    TObjectPtr<AActor> EvaluatedActor = nullptr;

    UPROPERTY()
    FVector EvaluatedLocation = FVector::ZeroVector;

    TWeakObjectPtr<UAnimMontage> ActiveMontage = nullptr;
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class GDB_API UDynamicStateTreeComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDynamicStateTreeComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic StateTree")
    FOnDynamicStateChangedSignature OnStateChanged;

    UPROPERTY(ReplicatedUsing = OnRep_ActiveStateTreeTag, EditAnywhere, BlueprintReadWrite, Category = "Dynamic StateTree")
    FGameplayTag ActiveStateTreeTag;

    UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "Dynamic StateTree")
    FGameplayTag FallbackStateTreeTag;

    UPROPERTY(ReplicatedUsing = OnRep_CurrentStateTag, BlueprintReadOnly, Category = "Dynamic StateTree")
    FGameplayTag CurrentStateTag;

    UPROPERTY(BlueprintReadOnly, Category = "Dynamic StateTree")
    FDynamicStateContextData StateContext;

    UFUNCTION()
    void OnRep_ActiveStateTreeTag();

    UFUNCTION()
    void OnRep_CurrentStateTag();

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic StateTree")
    bool SwitchToStateTree(FGameplayTag NewTreeTag);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic StateTree")
    bool PopStateFromStack();

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic StateTree")
    void TriggerLLMTimeoutFallback();

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic StateTree")
    void SendStateTreeEvent(const FDynamicStateTreeEvent& EventPayload);

    UFUNCTION(BlueprintPure, Category = "Dynamic StateTree")
    FGameplayTag GetCurrentStateTag() const { return CurrentStateTag; }

    UFUNCTION(BlueprintPure, Category = "Dynamic StateTree")
    bool IsRunningFallbackTree() const { return bRunningFallbackTree; }

    UFUNCTION(BlueprintPure, Category = "Dynamic StateTree")
    int32 GetStateStackDepth() const { return StateNodeStack.Num(); }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic StateTree|LOD")
    float ProximityHighDetailRadius = 3000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic StateTree|LOD")
    float MediumDetailRadius = 8000.0f;
    
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic StateTree")
    bool TransitionToState(int32 TargetNodeIndex, bool bPushToStack = false);


private:
    void EvaluateTreeTicking(float DeltaTime);
    bool EvaluateTransitionsForCurrentNode(EDynamicStateTransitionTrigger TriggerEvent, const FDynamicStateTreeEvent* EventData = nullptr);
    int32 SelectBestChildNode(int32 ParentNodeIndex) const;
    void EnterNode(int32 NodeIndex);
    void ExitCurrentNode();
    void StartTask(const FDynamicStateTreeTaskDef& TaskDef);
    void TickActiveTasks(float DeltaTime);
    void StopActiveTasks();
    void EvaluateLODAndThrottling(float DeltaTime);
    bool IsNodeOnCooldown(int32 NodeIndex) const;

    int32 CurrentNodeIndex = INDEX_NONE;
    bool bRunningFallbackTree = false;

    UPROPERTY()
    TArray<int32> StateNodeStack;

    FDynamicStateTreeDef ActiveTreeDefinition;
    TArray<FActiveStateTreeTaskState> ActiveTaskStates;

    float LODCheckAccumulator = 0.0f;
    float CurrentTickInterval = 0.05f;

    TMap<int32, double> NodeLastExecutionTimes;

    TWeakObjectPtr<ADynamicCharacter> CachedCharacter;
    TWeakObjectPtr<AAIController> CachedAIController;
    TWeakObjectPtr<ADynamicGameState> CachedGameState;
};