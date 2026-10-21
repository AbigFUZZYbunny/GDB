// AT_WaitChargeTime.cpp
#include "AT_WaitChargeTime.h"

UAT_WaitChargeTime::UAT_WaitChargeTime(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bTickingTask = true;
	ElapsedTime = 0.0f;
	MaxDuration = 0.0f;
	UpdateInterval = 0.0f;
	TimeSinceLastBroadcast = 0.0f;
}

UAT_WaitChargeTime* UAT_WaitChargeTime::WaitChargeTime(UGameplayAbility* OwningAbility, float MaxChargeTime, float TickRate)
{
	UAT_WaitChargeTime* MyObj = NewAbilityTask<UAT_WaitChargeTime>(OwningAbility);
	MyObj->MaxDuration = FMath::Max(0.01f, MaxChargeTime);
	MyObj->UpdateInterval = FMath::Max(0.0f, TickRate);
	return MyObj;
}

void UAT_WaitChargeTime::Activate()
{
	Super::Activate();
	ElapsedTime = 0.0f;
	TimeSinceLastBroadcast = 0.0f;
}

void UAT_WaitChargeTime::TickTask(float DeltaTime)
{
	Super::TickTask(DeltaTime);

	if (!ShouldBroadcastAbilityTaskDelegates())
	{
		return;
	}

	ElapsedTime += DeltaTime;
	TimeSinceLastBroadcast += DeltaTime;

	const bool bThrottled = (UpdateInterval > 0.0f);
	if (!bThrottled || TimeSinceLastBroadcast >= UpdateInterval)
	{
		const float BroadcastDelta = bThrottled ? TimeSinceLastBroadcast : DeltaTime;
		TimeSinceLastBroadcast = 0.0f;
		OnChargeTick.Broadcast(FMath::Min(ElapsedTime, MaxDuration), BroadcastDelta);
	}

	if (ElapsedTime >= MaxDuration)
	{
		OnMaxChargeReached.Broadcast();
		EndTask();
	}
}