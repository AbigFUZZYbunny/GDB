// DynamicGameplayEffectContext.cpp
#include "DynamicGameplayEffectContext.h"

UScriptStruct* FDynamicGameplayEffectContext::GetScriptStruct() const {
	return FDynamicGameplayEffectContext::StaticStruct();
}

FDynamicGameplayEffectContext* FDynamicGameplayEffectContext::Duplicate() const {
	FDynamicGameplayEffectContext* NewContext = new FDynamicGameplayEffectContext();
	*NewContext = *this;
	if (GetInstigator()) NewContext->AddInstigator(GetInstigator(), GetEffectCauser());
	return NewContext;
}

bool FDynamicGameplayEffectContext::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) {
	FGameplayEffectContext::NetSerialize(Ar, Map, bOutSuccess);
    
	Ar << CustomConditionTreeName;
	ContextPayloadTag.NetSerialize(Ar, Map, bOutSuccess);

	uint8 RepBits = 0;
	if (Ar.IsSaving()) {
		if (TargetLimbId != 1) RepBits |= (1 << 0);
		if (KineticImpulse > 0.0f) RepBits |= (1 << 1);
		if (!FMath::IsNearlyEqual(MassRatioFactor, 1.0f)) RepBits |= (1 << 2);
		if (!QuantizedImpactNormal.IsZero()) RepBits |= (1 << 3);
		if (!QuantizedImpactLocation.IsZero()) RepBits |= (1 << 4);
	}

	Ar.SerializeBits(&RepBits, 5);

	if (RepBits & (1 << 0)) Ar << TargetLimbId;
	else if (Ar.IsLoading()) TargetLimbId = 1;

	if (RepBits & (1 << 1)) {
		uint16 PackedImpulse = Ar.IsSaving() ? FMath::Clamp<uint16>(FMath::RoundToInt(KineticImpulse), 0, 65535) : 0;
		Ar << PackedImpulse;
		if (Ar.IsLoading()) KineticImpulse = static_cast<float>(PackedImpulse);
	} else if (Ar.IsLoading()) KineticImpulse = 0.0f;

	if (RepBits & (1 << 2)) Ar << MassRatioFactor;
	else if (Ar.IsLoading()) MassRatioFactor = 1.0f;

	if (RepBits & (1 << 3)) QuantizedImpactNormal.NetSerialize(Ar, Map, bOutSuccess);
	if (RepBits & (1 << 4)) QuantizedImpactLocation.NetSerialize(Ar, Map, bOutSuccess);

	bOutSuccess = true;
	return true;
}