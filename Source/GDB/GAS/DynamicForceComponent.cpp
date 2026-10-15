// DynamicForceComponent.cpp
#include "DynamicForceComponent.h"

UDynamicForceComponent::UDynamicForceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PendingImpulseVector = FVector::ZeroVector;
	bHasPendingImpulse = false;
}

void UDynamicForceComponent::QueueInstantMovementImpulse(FVector ImpulseDirection, float Intensity)
{
	PendingImpulseVector = ImpulseDirection.GetSafeNormal() * Intensity;
	bHasPendingImpulse = true;
}

FVector UDynamicForceComponent::ConsumePendingImpulse()
{
	if (!bHasPendingImpulse) return FVector::ZeroVector;
	
	FVector ReturnForce = PendingImpulseVector;
	PendingImpulseVector = FVector::ZeroVector;
	bHasPendingImpulse = false;
	return ReturnForce;
}