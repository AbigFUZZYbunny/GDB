// DynamicForceComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicForceComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicForceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDynamicForceComponent();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void QueueInstantMovementImpulse(FVector ImpulseDirection, float Intensity);
	FVector ConsumePendingImpulse();

	void ServerApplyLimbForce(uint8 LimbId, const FName& BoneName, float ClampedImpulse, const FVector& HitLocation, const FVector& HitNormal);
	void OnClientReceiveAuthoritativeHit(const FFastPhysicsHitRepItem& HitItem);

protected:
	UPROPERTY(Replicated) FFastPhysicsHitRepArray ReplicatedHitBuffer;
	int32 WriteIndex = 0;
	static constexpr int32 MaxReplicatedHitBufferSize = 8;

private:
	FVector PendingImpulseVector;
	bool bHasPendingImpulse;
};