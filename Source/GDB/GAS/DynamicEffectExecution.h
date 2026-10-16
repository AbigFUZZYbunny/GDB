// DynamicEffectExecution.h
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"
#include "DynamicEffectExecution.generated.h"

UCLASS()
class GDB_API UDynamicEffectExecution : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UDynamicEffectExecution();

	// Native GAS execution gateway loop
	virtual void Execute_Implementation(
		const FGameplayEffectCustomExecutionParameters& ExecutionParams, 
		FGameplayEffectCustomExecutionOutput& ExecutionOutput
	) const override;
};