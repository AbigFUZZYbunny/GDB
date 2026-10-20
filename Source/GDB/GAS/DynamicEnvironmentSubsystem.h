#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DynamicEnvironmentSubsystem.generated.h"

UCLASS()
class GDB_API UDynamicEnvironmentSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Tick function called by World to advance weather and elemental advection
	void TickEnvironment(float DeltaRealSeconds);
};