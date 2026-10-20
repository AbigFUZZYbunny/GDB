// PhysicsMode_DynamicGAS.cpp
#include "DynamicPhysicsMode.h"
#include "MoverComponent.h"
#include "MoveLibrary/MovementUtils.h"

UDynamicPhysicsMode::UDynamicPhysicsMode() {}

void UDynamicPhysicsMode::GenerateMove_Implementation(const FMoverSimContext& SimContext, const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, FProposedMove& OutProposedMove) const
{
    Super::GenerateMove_Implementation(SimContext, StartState, TimeStep, OutProposedMove);

    const float SpeedParam = RuntimeProfile.VelocityMetricTag.IsValid()
        ? RuntimeProfile.MovementMetrics.LocalLookupMap.FindRef(RuntimeProfile.VelocityMetricTag)
        : 0.0f;

    if (const FCharacterDefaultInputs* CharacterInputs = StartState.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>())
    {
        FVector MoveVector = CharacterInputs->GetMoveInput();
        if (RuntimeProfile.PhysicsType == EMovementPhysicsType::LaunchVelocity)
        {
            if (MoveVector.IsNearlyZero() && GetMoverComponent())
            {
                MoveVector = GetMoverComponent()->GetOwner()->GetActorForwardVector();
            }
            OutProposedMove.LinearVelocity = MoveVector.GetSafeNormal() * SpeedParam;
        }
    }
}

void UDynamicPhysicsMode::SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& OutputState)
{
    // Native mover simulation tick
}