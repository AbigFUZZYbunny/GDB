// DynamicEffectFactory.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayEffect.h"
#include "DynamicGASTypes.h"
#include "DynamicEffectFactory.generated.h"

class UAbilitySystemComponent;
class ADynamicGameState;

UCLASS()
class GDB_API UDynamicEffectFactory : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Resolves an O(1) pre-constructed, cached UGameplayEffect CDO from ADynamicGameState
	 * and constructs an active GameplayEffectSpecHandle without GC heap allocations.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Effect Factory")
	static FGameplayEffectSpecHandle CreateDynamicEffectSpec(
		UAbilitySystemComponent* SourceASC,
		FGameplayTag EffectDataTag,
		int32 Level = 1
	);

	/**
	 * Factory generator that builds and configures a reusable UGameplayEffect object.
	 * Invoked during GameState registry hydration to pre-warm the effect cache.
	 */
	static UGameplayEffect* ConstructGameplayEffectObject(UObject* Outer, const FDynamicEffectDef& Def, FGameplayTag EffectTag);
};