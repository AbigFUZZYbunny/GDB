// DynamicGameplayEffectContext.cpp
#include "DynamicGameplayEffectContext.h"

FDynamicGameplayEffectContext* FDynamicGameplayEffectContext::Duplicate() const
{
	FDynamicGameplayEffectContext* NewContext = new FDynamicGameplayEffectContext();
	*NewContext = *this;
	if (GetHitResult())
	{
		NewContext->AddHitResult(*GetHitResult(), true);
	}
	return NewContext;
}

bool FDynamicGameplayEffectContext::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bool bSuccess = FGameplayEffectContext::NetSerialize(Ar, Map, bOutSuccess);
	Ar << CustomConditionTreeName;
	bOutSuccess = bSuccess;
	return bOutSuccess;
}