// DynamicGameplayEffectContext.h
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "DynamicGameplayEffectContext.generated.h"

USTRUCT(BlueprintType)
struct FDynamicGameplayEffectContext : public FGameplayEffectContext
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FName CustomConditionTreeName;

	virtual UScriptStruct* GetScriptStruct() const override { return FDynamicGameplayEffectContext::StaticStruct(); }
	virtual FDynamicGameplayEffectContext* Duplicate() const override;
	virtual bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) override;
};

template<>
struct TStructOpsTypeTraits<FDynamicGameplayEffectContext> : public TStructOpsTypeTraitsBase2<FDynamicGameplayEffectContext>
{
	enum { WithNetSerializer = true, WithCopy = true };
};