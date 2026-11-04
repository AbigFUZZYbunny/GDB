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
    FOnCloudAITaskAssigned AssignedCallback,
    FOnSpeechClauseReadyForTTS ClauseCallback)
{
    UWorld* World = AgentPawn ? AgentPawn->GetWorld() : GetWorld();
    const double CurrentTime = World ? World->GetTimeSeconds() : 0.0;

    FCloudAIQueuedTask NewTask;
    NewTask.PrimaryAccumulatedJson.Reserve(2048);
    NewTask.ValidatorAccumulatedJson.Reserve(2048);

    NewTask.TaskId = FGuid::NewGuid();
    NewTask.RequestingAgentPawn = AgentPawn;
    NewTask.RequestType = RequestType;
    NewTask.Priority = Priority;
    NewTask.ContextPrompt = ContextJsonPrompt;
    NewTask.TimeQueued = CurrentTime;
    NewTask.TimeoutSeconds = TimeoutSeconds;

    NewTask.TokenStreamDelegate = TokenStreamCallback;
    NewTask.FinalCompletionDelegate = DirectCallback;
    NewTask.TaskAssignedDelegate = AssignedCallback;
    NewTask.ClauseStreamDelegate = ClauseCallback;

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
    if (TaskQueue.Num() == 0) return;

    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    const FCloudAISystemConfig& Config = GS ? GS->CloudAIConfig : FCloudAISystemConfig();

    const FString ResolvedModel = !Config.DefaultModelName.IsEmpty() ? Config.DefaultModelName : ModelName;

    // 1. Instant local rule-based fallback mode for single-player / small co-op without miners
    if (Config.ExecutionBackend == ECloudAIExecutionBackend::LocalRuleBasedFallback)
    {
        for (int32 i = TaskQueue.Num() - 1; i >= 0; --i)
        {
            FCloudAIQueuedTask Task = TaskQueue[i];
            TaskQueue.RemoveAt(i);

            FCloudAIDirectiveResponse FallbackResp;
            ExecuteRuleBasedFallback(Cast<ADynamicCharacter>(Task.RequestingAgentPawn.Get()), FallbackResp);

            TArray<FCloudAICommandOutput> Output;
            for (const FCloudAICommand& Cmd : FallbackResp.Commands)
            {
                FCloudAICommandOutput OutCmd;
                OutCmd.CommandActionTag = Cmd.CommandActionTag;
                OutCmd.TargetEntityOrPlaceRoleTag = Cmd.TargetEntityOrPlaceRoleTag;
                OutCmd.UtteranceText = Cmd.UtteranceText;
                OutCmd.PriorityWeight = Cmd.PriorityWeight;
                Output.Add(OutCmd);
            }
            Task.FinalCompletionDelegate.ExecuteIfBound(Output);
        }
        return;
    }

    // 2. Filter connected miners
    ConnectedMiners.RemoveAll([](const TWeakObjectPtr<UDynamicAILinkComponent>& M) { return !M.IsValid(); });

    // OPTIMIZATION: If fewer than 2 miners are available, do NOT stall. Fall back gracefully to rule-based or server HTTP!
    if (ConnectedMiners.Num() < 2)
    {
        for (int32 i = TaskQueue.Num() - 1; i >= 0; --i)
        {
            FCloudAIQueuedTask Task = TaskQueue[i];
            TaskQueue.RemoveAt(i);

            FCloudAIDirectiveResponse FallbackResp;
            ExecuteRuleBasedFallback(Cast<ADynamicCharacter>(Task.RequestingAgentPawn.Get()), FallbackResp);

            TArray<FCloudAICommandOutput> Output;
            for (const FCloudAICommand& Cmd : FallbackResp.Commands)
            {
                FCloudAICommandOutput OutCmd;
                OutCmd.CommandActionTag = Cmd.CommandActionTag;
                OutCmd.TargetEntityOrPlaceRoleTag = Cmd.TargetEntityOrPlaceRoleTag;
                OutCmd.UtteranceText = Cmd.UtteranceText;
                OutCmd.PriorityWeight = Cmd.PriorityWeight;
                Output.Add(OutCmd);
            }
            Task.FinalCompletionDelegate.ExecuteIfBound(Output);
        }
        return;
    }

    // 3. Priority Sort: High Priority tasks dispatched first to lowest ping miners
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
                if (!Miner->NodeStats.bIsCurrentlyProcessing && Miner->NodeStats.TrustScore > Config.MinimumMinerTrustScore)
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
        if (Task.Priority == ECloudAITaskPriority::High && AvailableMiners[0]->NodeStats.AveragePingMs > Config.HighPriorityMaxPingMs)
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

        // Security hardened: Server ApiKey / internal URLs are never sent across the client RPC
        Primary->Client_AssignAITask(Task.TaskId, Task.ContextPrompt, ResolvedModel);
        Validator->Client_AssignAITask(Task.TaskId, Task.ContextPrompt, ResolvedModel);

        ActiveTasks.Add(Task);
        TaskQueue.RemoveAt(i);
    }
}

void UDynamicCloudAISubsystem::HandleMinerTokenChunk(UDynamicAILinkComponent* Miner, const FGuid& TaskId, const FString& Chunk)
{
    FCloudAIQueuedTask* Task = ActiveTasks.FindByPredicate([&](const FCloudAIQueuedTask& T) { return T.TaskId == TaskId; });
    if (!Task || !Miner) return;

    if (Task->ValidatorMiner.Get() == Miner)
    {
        Task->ValidatorAccumulatedJson += Chunk;
        return;
    }

    if (Task->PrimaryMiner.Get() != Miner) return;

    Task->PrimaryAccumulatedJson += Chunk;

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
            
            Task->StreamingWordBuffer += NewUtteranceChunk;
            Task->StreamingClauseBuffer += NewUtteranceChunk;
            
            // 1. Process Word Boundary Token Stream
            int32 LastBoundaryIdx = -1;
            for (int32 i = Task->StreamingWordBuffer.Len() - 1; i >= 0; --i)
            {
                if (!FChar::IsAlnum(Task->StreamingWordBuffer[i]) && Task->StreamingWordBuffer[i] != '\'')
                {
                    LastBoundaryIdx = i;
                    break;
                }
            }

            if (LastBoundaryIdx != -1)
            {
                const FString ToProcess = Task->StreamingWordBuffer.Left(LastBoundaryIdx + 1);
                Task->StreamingWordBuffer = Task->StreamingWordBuffer.Mid(LastBoundaryIdx + 1);

                FString StreamedChunk = ToProcess;
                if (const ADynamicCharacter* Char = Cast<ADynamicCharacter>(Task->RequestingAgentPawn.Get()))
                {
                    if (const UDynamicLanguageComponent* LangComp = Char->FindComponentByClass<UDynamicLanguageComponent>())
                    {
                        StreamedChunk = LangComp->FormulateSpokenUtterance(LangComp->ActiveSpokenLanguageTag, ToProcess);
                    }
                }
                
                Task->TokenStreamDelegate.ExecuteIfBound(StreamedChunk);
            }

            // 2. Low-Latency Clause / Sentence Boundary Detection for Streaming TTS
            int32 ClauseBoundaryIdx = -1;
            for (int32 i = 0; i < Task->StreamingClauseBuffer.Len(); ++i)
            {
                const TCHAR C = Task->StreamingClauseBuffer[i];
                if (C == '.' || C == '!' || C == '?' || C == ';' || C == '\n')
                {
                    ClauseBoundaryIdx = i;
                    break;
                }
            }

            if (ClauseBoundaryIdx != -1)
            {
                const FString FinishedClause = Task->StreamingClauseBuffer.Left(ClauseBoundaryIdx + 1).TrimStartAndEnd();
                Task->StreamingClauseBuffer = Task->StreamingClauseBuffer.Mid(ClauseBoundaryIdx + 1);

                if (!FinishedClause.IsEmpty() && Task->ClauseStreamDelegate.IsBound())
                {
                    Task->ClauseStreamDelegate.Execute(FinishedClause, false);
                }
            }
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
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    const FCloudAISystemConfig& Config = GS ? GS->CloudAIConfig : FCloudAISystemConfig();

    const FString CleanPrimary = SanitizeMarkdownJson(Task.PrimaryAccumulatedJson);
    const FString CleanValidator = SanitizeMarkdownJson(Task.ValidatorAccumulatedJson);

    // --- HANDLE BACKGROUND MEMORY CONSOLIDATION ---
    if (Task.RequestType == ECloudAIRequestType::MemoryConsolidation)
    {
        TSharedPtr<FJsonObject> RootObj;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CleanValidator); // Trust validator for simple summaries
        
        if (FJsonSerializer::Deserialize(Reader, RootObj) && RootObj.IsValid())
        {
            FString NewSummary;
            if (RootObj->TryGetStringField(TEXT("NewMemorySummary"), NewSummary))
            {
                TArray<FCloudAICommandOutput> Output;
                FCloudAICommandOutput Cmd;
                Cmd.UtteranceText = NewSummary; // Overload UtteranceText to pass the summary string back
                Output.Add(Cmd);
                Task.FinalCompletionDelegate.ExecuteIfBound(Output);
                return;
            }
        }
        Task.FinalCompletionDelegate.ExecuteIfBound(TArray<FCloudAICommandOutput>());
        return;
    }

    // --- HANDLE STANDARD DIRECTIVES ---
    FCloudAIDirectiveResponse PrimaryResponse, ValidatorResponse;
    const bool bPrimaryValid = FJsonObjectConverter::JsonObjectStringToUStruct(CleanPrimary, &PrimaryResponse, 0, 0);
    const bool bValidatorValid = FJsonObjectConverter::JsonObjectStringToUStruct(CleanValidator, &ValidatorResponse, 0, 0);

    bool bConsensusReached = false;
    if (bPrimaryValid && bValidatorValid && PrimaryResponse.Commands.Num() > 0 && ValidatorResponse.Commands.Num() > 0)
    {
        const FCloudAICommand& PriCmd = PrimaryResponse.Commands[0];
        const FCloudAICommand& ValCmd = ValidatorResponse.Commands[0];

        if (PriCmd.CommandActionTag == ValCmd.CommandActionTag &&
            PriCmd.TargetEntityOrPlaceRoleTag == ValCmd.TargetEntityOrPlaceRoleTag)
        {
            if (FMath::Abs(PriCmd.UtteranceText.Len() - ValCmd.UtteranceText.Len()) <= Config.MaxConsensusUtteranceLengthDelta)
            {
                bConsensusReached = true;
            }
        }
    }

    if (bConsensusReached)
    {
        if (!Task.StreamingWordBuffer.IsEmpty())
        {
            FString TrailingChunk = Task.StreamingWordBuffer;
            if (const ADynamicCharacter* Char = Cast<ADynamicCharacter>(Task.RequestingAgentPawn.Get()))
            {
                if (const UDynamicLanguageComponent* LangComp = Char->FindComponentByClass<UDynamicLanguageComponent>())
                {
                    TrailingChunk = LangComp->FormulateSpokenUtterance(LangComp->ActiveSpokenLanguageTag, TrailingChunk);
                }
            }
            Task.TokenStreamDelegate.ExecuteIfBound(TrailingChunk);
            Task.StreamingWordBuffer.Empty();
        }

        const FGameplayTag ResolvedRewardTag = Config.MiningRewardAttributeTag.IsValid()
            ? Config.MiningRewardAttributeTag
            : MiningRewardAttributeTag;
        const float RewardAmount = FMath::Max(0.0f, Config.MiningRewardGrantAmount);

        if (ResolvedRewardTag.IsValid() && RewardAmount > 0.0f)
        {
            auto GrantReward = [&](UDynamicAILinkComponent* Miner) {
                if (!Miner || !Miner->GetOwner()) return;
                Miner->NodeStats.TasksCompleted++;
                Miner->NodeStats.TrustScore = FMath::Min(1.0f, Miner->NodeStats.TrustScore + 0.05f);

                if (UAbilitySystemComponent* ASC = Miner->GetOwner()->FindComponentByClass<UAbilitySystemComponent>())
                {
                    if (UDynamicAttributeSet* Attr = const_cast<UDynamicAttributeSet*>(Cast<UDynamicAttributeSet>(ASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()))))
                    {
                        Attr->SetDynamicAttributeValue(ResolvedRewardTag, Attr->GetDynamicAttributeValue(ResolvedRewardTag) + RewardAmount);
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
            OutCmd.PriorityWeight = Cmd.PriorityWeight;
            OutCmd.EmotionTag = Cmd.EmotionTag; // Pass the emotion tag forward

            // Formulate validated dialogue through the speaker's language proficiency
            FString FormulatedText = ValidatorResponse.Commands[0].UtteranceText;
            if (const ADynamicCharacter* Char = Cast<ADynamicCharacter>(Task.RequestingAgentPawn.Get()))
            {
                if (const UDynamicLanguageComponent* LangComp = Char->FindComponentByClass<UDynamicLanguageComponent>())
                {
                    FormulatedText = LangComp->FormulateSpokenUtterance(LangComp->ActiveSpokenLanguageTag, FormulatedText);
                }
            }

            OutCmd.UtteranceText = FormulatedText;
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

        if (GS->EvaluateBestDynamicBehavior(Character, BestBehavior, BestScore))
        {
            FCloudAICommand Cmd;
            Cmd.CommandActionTag = BestBehavior.ActionTag.IsValid() ? BestBehavior.ActionTag : BestBehavior.BehaviorTag;
            Cmd.TargetEntityOrPlaceRoleTag = BestBehavior.TargetPlaceRoleTag;
            Cmd.PriorityWeight = BestScore;

            if (UDynamicSocialComponent* SocialComp = Character->FindComponentByClass<UDynamicSocialComponent>())
            {
                TSoftObjectPtr<UAnimMontage> OutMontage;
                FGameplayTag OutVoice;
                SocialComp->TryResolveDialogueForBehavior(BestBehavior, nullptr, Cmd.UtteranceText, OutMontage, OutVoice);
            }

            if (const UDynamicLanguageComponent* LangComp = Character->FindComponentByClass<UDynamicLanguageComponent>())
            {
                Cmd.UtteranceText = LangComp->FormulateSpokenUtterance(LangComp->ActiveSpokenLanguageTag, Cmd.UtteranceText);
            }

            OutFallbackResponse.Commands.Add(Cmd);
            return;
        }
    }

    FCloudAICommand DefaultCommand;
    DefaultCommand.CommandActionTag = FGameplayTag::RequestGameplayTag(TEXT("Schedule.Wander"), false);
    DefaultCommand.TargetEntityOrPlaceRoleTag = FGameplayTag::RequestGameplayTag(TEXT("Role.Place.Workplace"), false);
    DefaultCommand.PriorityWeight = 10.0f;
    OutFallbackResponse.Commands.Add(DefaultCommand);
}