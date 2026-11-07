// DynamicGASManagerComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpec.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicGASManagerComponent.generated.h"

class UAbilitySystemComponent;
class ADynamicCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnSkillNodeUnlockedSignature, FGameplayTag, TreeTag, FGameplayTag, NodeTag, int32, CurrentTier);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicGASManagerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDynamicGASManagerComponent();

    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Progression")
    FOnSkillNodeUnlockedSignature OnSkillNodeUnlocked;

    UPROPERTY(ReplicatedUsing = OnRep_ReplicatedProgression)
    FFastSkillProgressionArray ReplicatedProgression;

    UFUNCTION()
    void OnRep_ReplicatedProgression();

    void HandleProgressionReplicated(const FSkillProgressionItem& Item);

    static const FDynamicConditionTreeContainer* GetConditionTreeByName(const UObject* WorldContextObject, FName ConditionName);
    float ResolveMetric(FGameplayTag ConfigProfileTag, FGameplayTag MetricTag, const FDynamicTagFloatMapWrapper& Modifiers) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Progression")
    bool CanLearnAbility(UAbilitySystemComponent* TargetASC, FGameplayTag AbilityTag, FComputedAbilityScoreInfo& OutScoreInfo) const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Progression")
    FGameplayAbilitySpecHandle TryLearnAndGrantAbility(UAbilitySystemComponent* TargetASC, FGameplayTag AbilityTag, FGameplayTag InputSlotTag);

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Combo")
    bool TriggerAbilityOrCombo(UAbilitySystemComponent* TargetASC, FGameplayTag RequestedAbilityTag);

    void ProcessActiveAbilityInputSequence(FGameplayTag ActionTag, bool bIsPressed);

    // ========================================================================
    // SKILL TREE PROGRESSION PIPELINE
    // ========================================================================

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|SkillTree")
    bool CanUnlockSkillNode(FGameplayTag TreeTag, FGameplayTag NodeTag) const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|SkillTree")
    bool ServerUnlockSkillNode(FGameplayTag TreeTag, FGameplayTag NodeTag);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|SkillTree")
    int32 GetSkillNodeInvestedTiers(FGameplayTag NodeTag) const;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|SkillTree")
    bool IsSkillNodeMaxed(FGameplayTag TreeTag, FGameplayTag NodeTag) const;

    // Resolves UI localization text by querying the granted ability or effect definition
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|SkillTree|UI")
    bool ResolveSkillNodeDisplayData(FGameplayTag TreeTag, FGameplayTag NodeTag, FString& OutName, FString& OutDescription, TSoftObjectPtr<UTexture2D>& OutActiveIcon) const;

private:
    TMap<FGameplayTag, int32> LocalProgressionCache;
    void RebuildProgressionCache();
};