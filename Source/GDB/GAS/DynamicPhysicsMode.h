// PhysicsMode_DynamicGAS.h
#pragma once

#include "CoreMinimal.h"
#include "MovementMode.h" // Resolves cleanly via Mover module dependency
#include "DynamicGASTypes.h"
#include "DynamicPhysicsMode.generated.h"

UCLASS(BlueprintType)
class GDB_API UDynamicPhysicsMode : public UBaseMovementMode
{
	GENERATED_BODY()

public:
	UDynamicPhysicsMode();

	// Cache assigned by the launching GAS ability during initialization
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Movement")
	FDynamicMovementProfile RuntimeProfile;

	// --- Corrected Native 5.8 Mover BlueprintNativeEvent Overrides ---
	virtual void GenerateMove_Implementation(const FMoverSimContext& SimContext, const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, FProposedMove& OutProposedMove) const override;
	virtual void SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& OutputState) override;
};