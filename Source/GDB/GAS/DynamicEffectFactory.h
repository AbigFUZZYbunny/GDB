// DynamicEffectFactory.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DynamicGASTypes.h"
#include "GameplayEffect.h"
#include "DynamicEffectFactory.generated.h"

class UAbilitySystemComponent;

UCLASS()
class GDB_API UDynamicEffectFactory : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Dynamically generates a native GAS effect instance spec direct from your raw runtime json structural tables */
	UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Effect Factory")
	static FGameplayEffectSpecHandle CreateDynamicEffectSpec(
		UAbilitySystemComponent* SourceASC, 
		FGameplayTag EffectDataTag, 
		int32 Level = 1
	);
};