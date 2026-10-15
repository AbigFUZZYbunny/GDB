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

	UFUNCTION(BlueprintCallable, Category="Ability|Tasks", meta=(HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
	static UAT_WaitChargeTime* WaitChargeTime(UGameplayAbility* OwningAbility, float MaxChargeTime, float TickRate = 0.05f);

	virtual void Activate() override;
	virtual void OnDestroy(bool bInOwnerFinished) override;

private:
	void ExecuteTick();

	float MaxDuration;
	float UpdateInterval;
	float ElapsedTime;
	FTimerHandle TimerHandle_Tick;
};