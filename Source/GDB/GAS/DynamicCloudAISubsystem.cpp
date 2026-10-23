// DynamicCloudAISubsystem.cpp
#include "DynamicCloudAISubsystem.h"
#include "DynamicAILinkComponent.h"
#include "DynamicCharacter.h"
#include "DynamicGameState.h"
#include "DynamicSocialComponent.h"
#include "DynamicAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"

void UDynamicCloudAISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    MiningRewardAttributeTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Resource.MiningXP"), false);
}

void UDynamicCloudAISubsystem::Deinitialize()
{
    TaskQueue.Empty();
    ActiveTasks.Empty();
    ConnectedMiners.Empty();
    Super::Deinitialize();
}

void UDynamicCloudAISubsystem::RegisterMinerNode(UDynamicAILinkComponent* Miner)
{
    if (Miner)
    {
        ConnectedMiners.AddUnique(Miner);
    }
}

void UDynamicCloudAISubsystem::UnregisterMinerNode(UDynamicAILinkComponent* Miner)
{
    if (Miner)
    {
        ConnectedMiners.Remove(Miner);
    }
}

FString UDynamicCloudAISubsystem::SanitizeMarkdownJson(const FString& RawResponse)
{
    FString Trimmed = RawResponse.TrimStartAndEnd();
    if (Trimmed.StartsWith(TEXT("```json")))
    {
        Trimmed.RemoveFromStart(TEXT("```json"));
    }
    else if (Trimmed.StartsWith(TEXT("```")))
    {
        Trimmed.RemoveFromStart(TEXT("```"));
    }
    if (Trimmed.EndsWith(TEXT("```")))
    {
        Trimmed.RemoveFromEnd(TEXT("```"));
    }
    return Trimmed.TrimStartAndEnd();
}

FGuid UDynamicCloudAISubsystem::DispatchDistributedAIRequest(
    APawn* AgentPawn,
    ECloudAIRequestType RequestType,
    ECloudAITaskPriority Priority,
    const FString& ContextJsonPrompt,
    float TimeoutSeconds,
    FOnCloudAITokenStream TokenStreamCallback,
    FOnCloudAIResponseDirectCallback DirectCallback,
    FOnCloudAITaskAssigned AssignedCallback)
{
    UWorld* World = AgentPawn ? AgentPawn->GetWorld() : this->GetWorld();
    const double CurrentTime = World ? World->GetTimeSeconds() : 0.0;

    FCloudAIQueuedTask NewTask;
    NewTask.PrimaryAccumulatedJson.Reserve(2048);
    NewTask.ValidatorAccumulatedJson.Reserve(2048);
    
    NewTask.TaskId = FGuid::NewGuid();
    NewTask.Priority = Priority;
    NewTask.ContextPrompt = ContextJsonPrompt;
    NewTask.TimeQueued = CurrentTime;
    NewTask.TimeoutSeconds = TimeoutSeconds;
    
    // Bind to the updated global delegates
    NewTask.TokenStreamDelegate = TokenStreamCallback;
    NewTask.FinalCompletionDelegate = DirectCallback;
    NewTask.TaskAssignedDelegate = AssignedCallback;

    TaskQueue.Add(NewTask);
    return NewTask.TaskId;
}

void UDynamicCloudAISubsystem::CancelRemoteAIRequest(const FGuid& RequestGuid)
{
    TaskQueue.RemoveAll([&](const FCloudAIQueuedTask& Task) { return Task.TaskId == RequestGuid; });
    ActiveTasks.RemoveAll([&](const FCloudAIQueuedTask& Task) { return Task.TaskId == RequestGuid; });
}

void UDynamicCloudAISubsystem::Tick(float DeltaTime)
{
    ProcessTaskQueue();

    UWorld* World = this->GetWorld();
    const double Now = World ? World->GetTimeSeconds() : 0.0;

    for (int32 i = ActiveTasks.Num() - 1; i >= 0; --i)
    {
        if ((Now - ActiveTasks[i].TimeQueued) > ActiveTasks[i].TimeoutSeconds)
        {
            ActiveTasks[i].FinalCompletionDelegate.ExecuteIfBound(TArray<FCloudAICommandOutput>());
            ActiveTasks.RemoveAt(i);
        }
    }
}

void UDynamicCloudAISubsystem::ProcessTaskQueue()
{
    if (TaskQueue.Num() == 0 || ConnectedMiners.Num() < 2) return;

    ConnectedMiners.RemoveAll([](const TWeakObjectPtr<UDynamicAILinkComponent>& M) { return !M.IsValid(); });

    TaskQueue.Sort([](const FCloudAIQueuedTask& A, const FCloudAIQueuedTask& B) {
        if (A.Priority != B.Priority) return A.Priority > B.Priority;
        return A.TimeQueued < B.TimeQueued;
    });

    for (int32 i = TaskQueue.Num() - 1; i >= 0; --i)
    {
        TArray<UDynamicAILinkComponent*> AvailableMiners;
        for (const auto& WeakMiner : ConnectedMiners)
        {
            if (UDynamicAILinkComponent* Miner = WeakMiner.Get())
            {
                if (!Miner->NodeStats.bIsCurrentlyProcessing && Miner->NodeStats.TrustScore > 0.5f)
                {
                    AvailableMiners.Add(Miner);
                }
            }
        }

        if (AvailableMiners.Num() < 2) break;

        AvailableMiners.Sort([](const UDynamicAILinkComponent& A, const UDynamicAILinkComponent& B) {
            return A.NodeStats.AveragePingMs < B.NodeStats.AveragePingMs;
        });

        FCloudAIQueuedTask& Task = TaskQueue[i];

        if (Task.Priority == ECloudAITaskPriority::High && AvailableMiners[0]->NodeStats.AveragePingMs > 150.0f)
        {
            continue;
        }

        UDynamicAILinkComponent* Primary = AvailableMiners[0];
        UDynamicAILinkComponent* Validator = AvailableMiners[AvailableMiners.Num() - 1];

        Primary->NodeStats.bIsCurrentlyProcessing = true;
        Validator->NodeStats.bIsCurrentlyProcessing = true;

        Task.PrimaryMiner = Primary;
        Task.ValidatorMiner = Validator;

        const float EstimatedLatency = (Primary->NodeStats.AveragePingMs + Validator->NodeStats.AveragePingMs) / 2000.0f;
        Task.TaskAssignedDelegate.ExecuteIfBound(EstimatedLatency);

        Primary->Client_AssignAITask(Task.TaskId, Task.ContextPrompt, ModelName, EndpointUrl, ApiKey);
        Validator->Client_AssignAITask(Task.TaskId, Task.ContextPrompt, ModelName, EndpointUrl, ApiKey);

        ActiveTasks.Add(Task);
        TaskQueue.RemoveAt(i);
    }
}

FString UDynamicCloudAISubsystem::ParseSseDeltaToJsonString(const FString& SsePayload)
{
    FString ExtractedText = TEXT("");
    TArray<FString> Lines;
    SsePayload.ParseIntoArrayLines(Lines);

    for (const FString& Line : Lines)
    {
        if (Line.StartsWith(TEXT("data: ")) && !Line.Contains(TEXT("[DONE]")))
        {
            // Rapid inline extraction bypassing JSON DOM allocation overhead
            const int32 ContentIdx = Line.Find(TEXT("\"content\":\""));
            if (ContentIdx != INDEX_NONE)
            {
                const int32 StartIdx = ContentIdx + 11; // Length of "content":"
                const int32 EndIdx = Line.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, StartIdx);
                if (EndIdx != INDEX_NONE)
                {
                    ExtractedText += Line.Mid(StartIdx, EndIdx - StartIdx);
                }
            }
        }
    }
    return ExtractedText;
}

void UDynamicCloudAISubsystem::HandleMinerTokenChunk(UDynamicAILinkComponent* Miner, const FGuid& TaskId, const FString& Chunk)
{
    FCloudAIQueuedTask* Task = ActiveTasks.FindByPredicate([&](const FCloudAIQueuedTask& T) { return T.TaskId == TaskId; });
    if (!Task || Task->PrimaryMiner.Get() != Miner) return;

    const FString ParsedContent = ParseSseDeltaToJsonString(Chunk);
    if (ParsedContent.IsEmpty()) return;

    Task->PrimaryAccumulatedJson += ParsedContent;

    if (!Task->bStartedStreamingUtterance)
    {
        const int32 UtteranceIdx = Task->PrimaryAccumulatedJson.Find(TEXT("\"UtteranceText\":\""));
        if (UtteranceIdx != INDEX_NONE)
        {
            Task->bStartedStreamingUtterance = true;
            Task->UtteranceStartIndex = UtteranceIdx + 17;
            Task->LastBroadcastedLength = Task->UtteranceStartIndex;
        }
    }

    if (Task->bStartedStreamingUtterance)
    {
        const int32 EndQuoteIdx = Task->PrimaryAccumulatedJson.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, Task->UtteranceStartIndex);
        const int32 ReadToIdx = (EndQuoteIdx != INDEX_NONE) ? EndQuoteIdx : Task->PrimaryAccumulatedJson.Len();

        if (ReadToIdx > Task->LastBroadcastedLength)
        {
            const FString NewUtteranceChunk = Task->PrimaryAccumulatedJson.Mid(Task->LastBroadcastedLength, ReadToIdx - Task->LastBroadcastedLength);
            Task->LastBroadcastedLength = ReadToIdx;
            Task->TokenStreamDelegate.ExecuteIfBound(NewUtteranceChunk);
        }
    }
}

void UDynamicCloudAISubsystem::HandleMinerCompletion(UDynamicAILinkComponent* Miner, const FGuid& TaskId, const FString& ResultStatus)
{
    FCloudAIQueuedTask* Task = ActiveTasks.FindByPredicate([&](const FCloudAIQueuedTask& T) { return T.TaskId == TaskId; });
    if (!Task) return;

    if (Task->PrimaryMiner.Get() == Miner) Task->bPrimaryFinished = true;
    if (Task->ValidatorMiner.Get() == Miner) Task->bValidatorFinished = true;

    if (Task->bPrimaryFinished && Task->bValidatorFinished)
    {
        EvaluateConsensusAndReward(*Task);
        ActiveTasks.RemoveAll([&](const FCloudAIQueuedTask& T) { return T.TaskId == TaskId; });
    }
}

void UDynamicCloudAISubsystem::EvaluateConsensusAndReward(FCloudAIQueuedTask& Task)
{
    const FString CleanPrimary = SanitizeMarkdownJson(Task.PrimaryAccumulatedJson);
    const FString CleanValidator = SanitizeMarkdownJson(Task.ValidatorAccumulatedJson);

    FCloudAIDirectiveResponse PrimaryResponse, ValidatorResponse;
    const bool bPrimaryValid = FJsonObjectConverter::JsonObjectStringToUStruct(CleanPrimary, &PrimaryResponse, 0, 0);
    const bool bValidatorValid = FJsonObjectConverter::JsonObjectStringToUStruct(CleanValidator, &ValidatorResponse, 0, 0);

    bool bConsensusReached = false;
    if (bPrimaryValid && bValidatorValid && PrimaryResponse.Commands.Num() > 0 && ValidatorResponse.Commands.Num() > 0)
    {
        if (PrimaryResponse.Commands[0].CommandActionTag == ValidatorResponse.Commands[0].CommandActionTag &&
            PrimaryResponse.Commands[0].TargetEntityOrPlaceRoleTag == ValidatorResponse.Commands[0].TargetEntityOrPlaceRoleTag)
        {
            bConsensusReached = true;
        }
    }

    if (bConsensusReached)
    {
        if (MiningRewardAttributeTag.IsValid())
        {
            auto GrantReward = [&](UDynamicAILinkComponent* Miner) {
                if (!Miner) return;
                Miner->NodeStats.TasksCompleted++;
                Miner->NodeStats.TrustScore = FMath::Min(1.0f, Miner->NodeStats.TrustScore + 0.05f);
                if (UAbilitySystemComponent* ASC = Miner->GetOwner()->FindComponentByClass<UAbilitySystemComponent>())
                {
                    if (UDynamicAttributeSet* Attr = const_cast<UDynamicAttributeSet*>(Cast<UDynamicAttributeSet>(ASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()))))
                    {
                        Attr->SetDynamicAttributeValue(MiningRewardAttributeTag, Attr->GetDynamicAttributeValue(MiningRewardAttributeTag) + 1.0f);
                    }
                }
            };
            GrantReward(Task.PrimaryMiner.Get());
            GrantReward(Task.ValidatorMiner.Get());
        }

        TArray<FCloudAICommandOutput> Output;
        for (const FCloudAICommand& Cmd : PrimaryResponse.Commands)
        {
            FCloudAICommandOutput OutCmd;
            OutCmd.CommandActionTag = Cmd.CommandActionTag;
            OutCmd.TargetEntityOrPlaceRoleTag = Cmd.TargetEntityOrPlaceRoleTag;
            OutCmd.UtteranceText = Cmd.UtteranceText;
            Output.Add(OutCmd);
        }
        Task.FinalCompletionDelegate.ExecuteIfBound(Output);
    }
    else
    {
        if (Task.PrimaryMiner.IsValid()) Task.PrimaryMiner->NodeStats.TrustScore -= 0.2f;
        if (Task.ValidatorMiner.IsValid()) Task.ValidatorMiner->NodeStats.TrustScore -= 0.2f;

        Task.FinalCompletionDelegate.ExecuteIfBound(TArray<FCloudAICommandOutput>());
    }
}

void UDynamicCloudAISubsystem::ExecuteRuleBasedFallback(ADynamicCharacter* Character, FCloudAIDirectiveResponse& OutFallbackResponse)
{
    OutFallbackResponse.Commands.Empty();
    if (!Character) return;

    UWorld* World = Character->GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;

    if (GS)
    {
        FDynamicAIBehaviorDef BestBehavior;
        float BestScore = -MAX_flt;

        // Data-driven fallback evaluation directly from Master Registry
        if (GS->EvaluateBestDynamicBehavior(Character, BestBehavior, BestScore))
        {
            FCloudAICommand Cmd;
            Cmd.CommandActionTag = BestBehavior.ActionTag.IsValid() ? BestBehavior.ActionTag : BestBehavior.BehaviorTag;
            Cmd.TargetEntityOrPlaceRoleTag = BestBehavior.TargetPlaceRoleTag;
            Cmd.PriorityWeight = BestScore;

            if (BestBehavior.Utterances.Num() > 0)
            {
                if (UDynamicSocialComponent* SocialComp = Character->FindComponentByClass<UDynamicSocialComponent>())
                {
                    FDynamicDialogueUtteranceDef ChosenUtterance;
                    if (SocialComp->SelectBestUtterance(BestBehavior.Utterances, ChosenUtterance))
                    {
                        Cmd.UtteranceText = ChosenUtterance.UtteranceText;
                    }
                }
            }

            OutFallbackResponse.Commands.Add(Cmd);
            return;
        }
    }

    // Baseline fallback if no specific behavior passed conditions
    FCloudAICommand DefaultCommand;
    DefaultCommand.CommandActionTag = FGameplayTag::RequestGameplayTag(TEXT("Schedule.Wander"), false);
    DefaultCommand.TargetEntityOrPlaceRoleTag = FGameplayTag::RequestGameplayTag(TEXT("Role.Place.Workplace"), false);
    DefaultCommand.PriorityWeight = 10.0f;
    OutFallbackResponse.Commands.Add(DefaultCommand);
}