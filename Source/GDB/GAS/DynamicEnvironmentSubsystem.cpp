// DynamicEnvironmentSubsystem.cpp
#include "DynamicEnvironmentSubsystem.h"
#include "DynamicGameState.h"
#include "Engine/World.h"

void UDynamicEnvironmentSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	AccumulatedRealSeconds = 0.0f;
}

void UDynamicEnvironmentSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

TStatId UDynamicEnvironmentSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDynamicEnvironmentSubsystem, STATGROUP_Tickables);
}

bool UDynamicEnvironmentSubsystem::IsTickable() const
{
	if (IsTemplate()) return false;
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

void UDynamicEnvironmentSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	AccumulatedRealSeconds += DeltaTime;
	if (AccumulatedRealSeconds >= SimulationStepIntervalSeconds)
	{
		const float StepDelta = AccumulatedRealSeconds;
		AccumulatedRealSeconds = 0.0f;
		TickEnvironment(StepDelta);
	}
}

void UDynamicEnvironmentSubsystem::TickEnvironment(float DeltaRealSeconds)
{
	if (UWorld* World = GetWorld())
	{
		if (World->GetNetMode() != NM_Client)
		{
			if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
			{
				const float GameHours = (DeltaRealSeconds * GS->CalendarConfig.TimeScalar) / 3600.0f;
				GS->ServerTickEnvironmentSimulation(GameHours);
			}
		}
	}
}