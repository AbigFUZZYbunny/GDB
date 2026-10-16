// DynamicGameplayEffectContext.h
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "DynamicGameplayEffectContext.generated.h"

USTRUCT(BlueprintType)
struct GDB_API FDynamicGameplayEffectContext : public FGameplayEffectContext
{
	GENERATED_BODY()

public:
	// --- Custom Data-Driven Payload Fields ---
    
	// The specific logic tree string identifier to pass into UDynamicEffectExecution
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Effect Context")
	FName CustomConditionTreeName = NAME_None;

	// Optional tag identifier used to pass structural payload overrides dynamically
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Effect Context")
	FGameplayTag ContextPayloadTag;

	// --- Mandatory Native Overrides for Custom Context Tracking ---

	virtual UScriptStruct* GetScriptStruct() const override;
	virtual FDynamicGameplayEffectContext* Duplicate() const override;
	virtual bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) override;
};

// --- Custom Type Traits to Enable Network Delta Serialization Support ---
template<>
struct TStructOpsTypeTraits<FDynamicGameplayEffectContext> : public TStructOpsTypeTraitsBase2<FDynamicGameplayEffectContext>
{
	enum
	{
		WithNetSerializer = true, // Crucial to ensure NetSerialize executes over the network
		WithCopy = true
	};
};