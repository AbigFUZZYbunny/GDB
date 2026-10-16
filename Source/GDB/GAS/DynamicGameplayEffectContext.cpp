// DynamicGameplayEffectContext.cpp
#include "DynamicGameplayEffectContext.h"

UScriptStruct* FDynamicGameplayEffectContext::GetScriptStruct() const
{
	// Returns the exact static structure reflection descriptor to the engine
	return FDynamicGameplayEffectContext::StaticStruct();
}

FDynamicGameplayEffectContext* FDynamicGameplayEffectContext::Duplicate() const
{
	// Allocates a fresh structural heap copy for duplication loops
	FDynamicGameplayEffectContext* NewContext = new FDynamicGameplayEffectContext();
	*NewContext = *this;
    
	// Perform standard base copy passes cleanly
	if (GetInstigator())
	{
		NewContext->AddInstigator(GetInstigator(), GetEffectCauser());
	}
	return NewContext;
}

bool FDynamicGameplayEffectContext::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	// 1. Serialize all standard upstream base class context fields (Actors, Hit Results, Locations)
	FGameplayEffectContext::NetSerialize(Ar, Map, bOutSuccess);

	// 2. Safely compress and push our data-driven sub-properties across the network buffer
	Ar << CustomConditionTreeName;
    
	// Serialize the payload tag container natively using standard engine operator references
	ContextPayloadTag.NetSerialize(Ar, Map, bOutSuccess);

	bOutSuccess = true;
	return true;
}