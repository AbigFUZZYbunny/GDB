// AT_WaitChargeTime.h
#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "AT_WaitChargeTime.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FChargeTaskDelegate, float, CurrentTime, float, DeltaTime);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FChargeTaskSimpleDelegate);

UCLASS()
class GDB_API UAT_WaitChargeTime : public UAbilityTask
{
	GENERATED_BODY()

public:
	UAT_WaitChargeTime(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintAssignable)
	FChargeTaskDelegate OnChargeTick;

	UPROPERTY(BlueprintAssignable)
	FChargeTaskSimpleDelegate OnMaxChargeReached;

	/**
	 * Ticks natively with the ability system task runner instead of spamming FTimerManager.
	 * @param TickRate 0.0f ticks every frame smoothly; > 0.0f throttles delegate broadcasts to that interval.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
	static UAT_WaitChargeTime* WaitChargeTime(UGameplayAbility* OwningAbility, float MaxChargeTime, float TickRate = 0.0f);

	virtual void Activate() override;
	virtual void TickTask(float DeltaTime) override;

private:
	float MaxDuration = 0.0f;
	float UpdateInterval = 0.0f;
	float TimeSinceLastBroadcast = 0.0f;
	float ElapsedTime = 0.0f;
};