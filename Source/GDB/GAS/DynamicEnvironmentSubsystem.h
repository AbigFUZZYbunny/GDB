// DynamicEnvironmentSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DynamicEnvironmentSubsystem.generated.h"

UCLASS()
class GDB_API UDynamicEnvironmentSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	// Advances weather fronts, homeostasis, and elemental advection on server authority
	void TickEnvironment(float DeltaRealSeconds);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Environment")
	float SimulationStepIntervalSeconds = 1.0f;

private:
	float AccumulatedRealSeconds = 0.0f;
};