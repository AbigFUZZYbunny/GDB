// DynamicQuestComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicQuestComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestStateChangedSignature, const FGuid&, QuestInstanceGuid, FGameplayTag, QuestDefTag, EQuestStepState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnQuestObjectiveProgressChangedSignature, const FGuid&, QuestInstanceGuid, FGameplayTag, ObjectiveTag, int32, CurrentProgress, int32, RequiredCount);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicQuestComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDynamicQuestComponent();
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Quest")
    FOnQuestStateChangedSignature OnQuestStateChanged;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Quest")
    FOnQuestObjectiveProgressChangedSignature OnObjectiveProgressChanged;

    // High-level quest item states
    UPROPERTY(ReplicatedUsing = OnRep_ActiveQuests)
    FFastActiveQuestArray ReplicatedActiveQuests;

    // Normalized objective array: updates only replicate tiny deltas per objective increment
    UPROPERTY(ReplicatedUsing = OnRep_ActiveObjectives)
    FFastActiveQuestObjectiveArray ReplicatedActiveObjectives;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dynamic GAS|Quest")
    FGameplayTagContainer CompletedQuestTags;

    UFUNCTION() void OnRep_ActiveQuests();
    UFUNCTION() void OnRep_ActiveObjectives();

    void HandleSingleQuestReplicated(const FDynamicActiveQuestItem& Item);
    void HandleSingleObjectiveReplicated(const FDynamicActiveQuestObjectiveItem& Item);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Quest")
    bool IsQuestAvailable(FGameplayTag QuestDefTag, const AActor* QueryingActor, const FGuid& QuestGiverGuid) const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Quest")
    FGuid ServerAcceptQuest(FGameplayTag QuestDefTag, const FGuid& QuestGiverGuid);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Quest")
    void ServerIncrementObjectiveProgress(FGameplayTag ObjectiveTag, int32 IncrementAmount = 1, FGameplayTag TargetEntityOrArchetypeTag = FGameplayTag());

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Quest")
    bool ServerAbandonQuest(const FGuid& QuestInstanceGuid);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Quest")
    bool GetQuestObjectiveProgress(const FGuid& QuestInstanceGuid, FGameplayTag ObjectiveTag, FDynamicActiveQuestObjectiveItem& OutObjective) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Quest")
    TArray<FDynamicActiveQuestObjectiveItem> GetObjectivesForQuest(const FGuid& QuestInstanceGuid) const;

    const FFastActiveQuestArray& GetActiveQuests() const { return ReplicatedActiveQuests; }
    FFastActiveQuestArray& GetMutableActiveQuests() { return ReplicatedActiveQuests; }

    const FFastActiveQuestObjectiveArray& GetActiveObjectives() const { return ReplicatedActiveObjectives; }
    FFastActiveQuestObjectiveArray& GetMutableActiveObjectives() { return ReplicatedActiveObjectives; }

private:
    void AdvanceQuestStep(int32 QuestIndex, FGameplayTag NextStepTag);
    void CompleteQuestInternal(int32 ItemIndex, bool bSuccess);
    void ApplyRewardConsequences(const FQuestRewardConsequence& Consequences, const FGuid& QuestGiverGuid);

    void InitializeStepObjectives(const FGuid& QuestGuid, const FQuestStepDef& StepDef);
    void CleanUpObjectivesForQuest(const FGuid& QuestGuid);
};