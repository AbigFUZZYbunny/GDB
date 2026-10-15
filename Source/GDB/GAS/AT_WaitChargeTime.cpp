// AT_WaitChargeTime.cpp
#include "AT_WaitChargeTime.h"
#include "TimerManager.h"

UAT_WaitChargeTime::UAT_WaitChargeTime(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	ElapsedTime = 0.0f;
	MaxDuration = 0.0f;
	UpdateInterval = 0.05f;
}

UAT_WaitChargeTime* UAT_WaitChargeTime::WaitChargeTime(UGameplayAbility* OwningAbility, float MaxChargeTime, float TickRate)
{
	UAT_WaitChargeTime* MyObj = NewAbilityTask<UAT_WaitChargeTime>(OwningAbility);
	MyObj->MaxDuration = MaxChargeTime;
	MyObj->UpdateInterval = TickRate;
	return MyObj;
}

void UAT_WaitChargeTime::Activate()
{
	Super::Activate();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(TimerHandle_Tick, this, &UAT_WaitChargeTime::ExecuteTick, UpdateInterval, true);
	}
}

void UAT_WaitChargeTime::ExecuteTick()
{
	if (ShouldBroadcastAbilityTaskDelegates())
	{
		ElapsedTime += UpdateInterval;
		OnChargeTick.Broadcast(ElapsedTime, UpdateInterval);

		if (ElapsedTime >= MaxDuration)
		{
			OnMaxChargeReached.Broadcast();
			EndTask();
		}
	}
}

void UAT_WaitChargeTime::OnDestroy(bool bInOwnerFinished)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TimerHandle_Tick);
	}
	Super::OnDestroy(bInOwnerFinished);
}