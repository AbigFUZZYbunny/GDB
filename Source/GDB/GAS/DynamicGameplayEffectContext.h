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
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Effect Context")
	FName CustomConditionTreeName = NAME_None;
    
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Effect Context")
	FGameplayTag ContextPayloadTag;

	// --- Authoritative Physics Fields ---
	UPROPERTY() uint8 TargetLimbId = 1;
	UPROPERTY() float KineticImpulse = 0.0f;
	UPROPERTY() float MassRatioFactor = 1.0f;
	UPROPERTY() FVector_NetQuantizeNormal QuantizedImpactNormal = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantize10 QuantizedImpactLocation = FVector::ZeroVector;
	
	UPROPERTY()
	TArray<FSourcePassBackRule> SourcePassBackRules;

	uint8 GetTargetLimbId() const { return TargetLimbId; }
	float GetKineticImpulse() const { return KineticImpulse; }
	float GetMassRatioFactor() const { return MassRatioFactor; }

	void SetAuthoritativePhysicsPayload(uint8 InLimbId, float InImpulse, float InMassRatio, const FVector& InNormal, const FVector& InLocation)
	{
		TargetLimbId = InLimbId;
		KineticImpulse = InImpulse;
		MassRatioFactor = InMassRatio;
		QuantizedImpactNormal = InNormal;
		QuantizedImpactLocation = InLocation;
	}

	virtual UScriptStruct* GetScriptStruct() const override;
	virtual FDynamicGameplayEffectContext* Duplicate() const override;
	virtual bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) override;
};

template<> struct TStructOpsTypeTraits<FDynamicGameplayEffectContext> : public TStructOpsTypeTraitsBase2<FDynamicGameplayEffectContext>
{
	enum { WithNetSerializer = true, WithCopy = true };
};