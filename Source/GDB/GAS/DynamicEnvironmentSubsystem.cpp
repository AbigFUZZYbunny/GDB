#include "DynamicEnvironmentSubsystem.h"
#include "DynamicGameState.h"

void UDynamicEnvironmentSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UDynamicEnvironmentSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void UDynamicEnvironmentSubsystem::TickEnvironment(float DeltaRealSeconds)
{
	if (UWorld* World = GetWorld())
	{
		if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
		{
			// Check if the world has authority (is server or standalone)
			if (World->GetNetMode() != NM_Client)
			{
				float GameHours = (DeltaRealSeconds * GS->CalendarConfig.TimeScalar) / 3600.0f;
				GS->ServerTickEnvironmentSimulation(GameHours);
			}
		}
	}
}