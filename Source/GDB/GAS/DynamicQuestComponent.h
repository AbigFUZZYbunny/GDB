#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DynamicGASTypes.h"
#include "DynamicQuestComponent.generated.h"

class ADynamicCharacter;
class ADynamicPhysicsActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestStateChangedSignature, const FGuid&, QuestGuid, FGameplayTag, QuestTag, EQuestStepState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnQuestObjectiveUpdatedSignature, const FGuid&, QuestGuid, FGameplayTag, StepTag, FGameplayTag, ObjectiveTag, int32, CurrentProgress);

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
    FOnQuestObjectiveUpdatedSignature OnQuestObjectiveUpdated;

    UPROPERTY(ReplicatedUsing = OnRep_ActiveQuests)
    FFastActiveQuestArray ReplicatedActiveQuests;

    // Completed or failed quests history (persisted across sessions)
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dynamic GAS|Quest")
    FGameplayTagContainer CompletedQuestTags;

    UFUNCTION() void OnRep_ActiveQuests();

    void HandleQuestReplicated(const FDynamicActiveQuestItem& Item, bool bRemoved);

    // --- Core Quest Actions ---
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Quest")
    bool ServerAcceptQuest(FGameplayTag QuestDefTag, const FGuid& QuestGiverGuid);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Quest")
    bool ServerProgressObjective(const FGuid& QuestGuid, FGameplayTag ObjectiveTag, int32 ProgressAmount = 1);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Quest")
    bool ServerAdvanceStepBranch(const FGuid& QuestGuid, FGameplayTag ChosenBranchTag);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Quest")
    bool ServerAbandonQuest(const FGuid& QuestGuid);

    // --- Dynamic Procedural Generation Endpoints ---
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Quest|Generation")
    bool ServerGenerateEconomicSupplyQuest(AActor* GiverActor, FGameplayTag NeededMaterialTag, int32 QuantityNeeded, float UrgencyMultiplier = 1.0f);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Quest|Generation")
    bool ServerGenerateBountyDefenseQuest(AActor* GiverActor, FGameplayTag HostileFactionOrCreatureTag, int32 KillCount, FGameplayTag AreaLandmarkTag);

    // --- Query Functions ---
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Quest")
    bool CanAcceptQuest(FGameplayTag QuestDefTag, const AActor* GiverActor) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Quest")
    bool GetActiveQuestData(const FGuid& QuestGuid, FDynamicActiveQuestItem& OutQuestItem) const;

private:
    void ApplyRewardConsequences(const FQuestRewardConsequence& Consequences, const FGuid& GiverGuid);
    void CompleteQuestInternal(int32 ItemIndex, bool bSuccess);
};