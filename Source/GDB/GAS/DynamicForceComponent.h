// DynamicForceComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DynamicForceComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicForceComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UDynamicForceComponent();

	/** Queue an instantaneous velocity override that the Mover state step can safely pull */
	void QueueInstantMovementImpulse(FVector ImpulseDirection, float Intensity);

	/** Pull and flush the pending impulse vector */
	FVector ConsumePendingImpulse();

private:
	FVector PendingImpulseVector;
	bool bHasPendingImpulse;
};