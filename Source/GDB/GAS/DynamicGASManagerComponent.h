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

	UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Progression")
	FGameplayAbilitySpecHandle GrantChainedAbilitySpec(UAbilitySystemComponent* TargetASC, const FDynamicAbilitySlotChain& SlotChain);
};