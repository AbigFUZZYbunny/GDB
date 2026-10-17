// DynamicGASManagerComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpec.h"
#include "DynamicGASTypes.h"
#include "DynamicGASManagerComponent.generated.h"

class UAbilitySystemComponent;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicGASManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDynamicGASManagerComponent();

	static const FDynamicConditionTreeContainer* GetConditionTreeByName(const UObject* WorldContextObject, FName ConditionName);
	float ResolveMetric(FGameplayTag ConfigProfileTag, FGameplayTag MetricTag, const FDynamicTagFloatMapWrapper& Modifiers) const;

	// Evaluates Ability Score requirements (Mastery Minimum & Mastery Point Cost) for learning an ability
	UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Progression")
	bool CanLearnAbility(UAbilitySystemComponent* TargetASC, FGameplayTag AbilityTag, FComputedAbilityScoreInfo& OutScoreInfo) const;

	// Deducts Attribute.Mastery.X.Points and grants the dynamic ability spec on Server Authority
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Progression")
	FGameplayAbilitySpecHandle TryLearnAndGrantAbility(UAbilitySystemComponent* TargetASC, FGameplayTag AbilityTag, FGameplayTag InputSlotTag);

	// Unified player input entry point: if an active ability has an open combo window for RequestedAbilityTag, transitions into it; otherwise activates from neutral (respecting ComboGate & Mastery bypass)
	UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Combo")
	bool TriggerAbilityOrCombo(UAbilitySystemComponent* TargetASC, FGameplayTag RequestedAbilityTag);
};