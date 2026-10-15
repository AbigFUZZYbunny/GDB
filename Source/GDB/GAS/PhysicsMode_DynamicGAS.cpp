// PhysicsMode_DynamicGAS.cpp
#include "PhysicsMode_DynamicGAS.h"
#include "MoverComponent.h"
#include "MoveLibrary/MovementUtils.h"

UPhysicsMode_DynamicGAS::UPhysicsMode_DynamicGAS() {}


void UPhysicsMode_DynamicGAS::GenerateMove_Implementation(const FMoverSimContext& SimContext, const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, FProposedMove& OutProposedMove) const
{
    Super::GenerateMove_Implementation(SimContext, StartState, TimeStep, OutProposedMove);

    float SpeedParam = RuntimeProfile.MovementMetrics.LocalLookupMap.FindRef(FGameplayTag::RequestGameplayTag("Movement.Param.Velocity"));

    if (const FCharacterDefaultInputs* CharacterInputs = StartState.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>())
    {
        FVector MoveVector = CharacterInputs->GetMoveInput();

        if (RuntimeProfile.PhysicsType == EMovementPhysicsType::LaunchVelocity)
        {
            if (MoveVector.IsNearlyZero() && GetMoverComponent())
            {
                MoveVector = GetMoverComponent()->GetOwner()->GetActorForwardVector();
            }

            // Assign linear velocity straight into your active out parameter tracking pipeline
            OutProposedMove.LinearVelocity = MoveVector.GetSafeNormal() * SpeedParam;
        }
    }
}

// FIX: Changed function name from 'OnSimulationTick' to 'SimulationTick_Implementation'
void UPhysicsMode_DynamicGAS::SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& OutputState)
{
    // Custom ticking operations (such as gravity changes or physics checks) apply natively here
}