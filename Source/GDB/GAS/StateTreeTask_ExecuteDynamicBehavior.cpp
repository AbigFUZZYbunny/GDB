#include "StateTreeTask_ExecuteDynamicBehavior.h"
#include "StateTreeExecutionContext.h"
#include "DynamicCharacter.h"
#include "DynamicGameState.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "AbilitySystemComponent.h"
#include "DynamicInteractableComponent.h"
#include "DynamicSocialComponent.h"

FStateTreeTask_ExecuteDynamicBehavior::FStateTreeTask_ExecuteDynamicBehavior()
{
    bShouldCallTick = true;
}

EStateTreeRunStatus FStateTreeTask_ExecuteDynamicBehavior::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
    FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
    if (!InstanceData.Character) return EStateTreeRunStatus::Failed;

    ADynamicGameState* GS = InstanceData.Character->GetWorld()->GetGameState<ADynamicGameState>();
    if (!GS) return EStateTreeRunStatus::Failed;

    // 1. Data-Driven Selection
    FDynamicAIBehaviorDef TargetBehavior;
    if (InstanceData.BehaviorTag.IsValid())
    {
        if (const FDynamicAIBehaviorDef* Def = GS->AIBehaviorMasterRegistry.Find(InstanceData.BehaviorTag)) TargetBehavior = *Def;
    }
    else
    {
        float Score = 0.0f;
        if (!GS->EvaluateBestDynamicBehavior(InstanceData.Character, TargetBehavior, Score)) return EStateTreeRunStatus::Failed;
    }

    // 2. Data-Driven EQS Evaluation
    if (!TargetBehavior.TargetEQSConfig.QueryAsset.IsNull())
    {
        ExecuteEQSQuery(Context, TargetBehavior, InstanceData);
        return EStateTreeRunStatus::Running; // Pauses StateTree while waiting for Async EQS
    }

    // 3. Direct Execution
    if (TargetBehavior.ExecutionType == ENpcBehaviorExecutionType::Ability)
    {
        if (UAbilitySystemComponent* ASC = InstanceData.Character->GetAbilitySystemComponent())
        {
            ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(TargetBehavior.AbilityTagToTrigger));
            return EStateTreeRunStatus::Succeeded;
        }
    }

    return EStateTreeRunStatus::Running;
}

void FStateTreeTask_ExecuteDynamicBehavior::ExecuteEQSQuery(FStateTreeExecutionContext& Context, const FDynamicAIBehaviorDef& BehaviorDef, FInstanceDataType& InstanceData) const
{
    UEnvQuery* Query = BehaviorDef.TargetEQSConfig.QueryAsset.LoadSynchronous();
    if (!Query) return;

    FEnvQueryRequest QueryRequest(Query, InstanceData.Character);
    for (const FDynamicEQSParam& Param : BehaviorDef.TargetEQSConfig.QueryParams)
    {
        QueryRequest.SetFloatParam(Param.ParamName, Param.Value);
    }

    // Executes EQS asynchronously, pushing results back to StateTree Instance Data
    QueryRequest.Execute(EEnvQueryRunMode::SingleResult, nullptr, FQueryFinishedSignature::CreateLambda([&InstanceData](TSharedPtr<FEnvQueryResult> Result)
    {
        if (Result->IsSuccessful())
        {
            InstanceData.EvaluatedActor = Result->GetItemAsActor(0);
            InstanceData.EvaluatedLocation = Result->GetItemAsLocation(0);
        }
    }));
}

EStateTreeRunStatus FStateTreeTask_ExecuteDynamicBehavior::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
    return EStateTreeRunStatus::Running;
}

void FStateTreeTask_ExecuteDynamicBehavior::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
}