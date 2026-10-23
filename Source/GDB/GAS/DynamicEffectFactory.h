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
	UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Effect Factory")
	static FGameplayEffectSpecHandle CreateDynamicEffectSpec(UAbilitySystemComponent* SourceASC, FGameplayTag EffectDataTag, int32 Level = 1);
};