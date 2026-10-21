// DynamicCloudAISubsystem.cpp
#include "DynamicCloudAISubsystem.h"
#include "DynamicGameState.h"
#include "DynamicDataSerializer.h"
#include "DynamicCharacter.h"
#include "DynamicSocialComponent.h"
#include "DynamicAttributeSet.h"
#include "GameplayTagsManager.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "HttpModule.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

void UDynamicCloudAISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Dynamic Backend Configuration on Startup from Command-Line Arguments
    const TCHAR* CmdLine = FCommandLine::Get();
    if (CmdLine && *CmdLine)
    {
        FString AiMode;
        if (FParse::Value(CmdLine, TEXT("cloudai="), AiMode))
        {
            if (AiMode.Equals(TEXT("cloud"), ESearchCase::IgnoreCase) || AiMode.Equals(TEXT("miner"), ESearchCase::IgnoreCase))
            {
                ExecutionBackend = ECloudAIExecutionBackend::RemoteCloudMiner;
            }
            else if (AiMode.Equals(TEXT("localhttp"), ESearchCase::IgnoreCase) || AiMode.Equals(TEXT("ollama"), ESearchCase::IgnoreCase))
            {
                ExecutionBackend = ECloudAIExecutionBackend::LocalDedicatedNodeHttp;
            }
            else if (AiMode.Equals(TEXT("heuristic"), ESearchCase::IgnoreCase) || AiMode.Equals(TEXT("fallback"), ESearchCase::IgnoreCase))
            {
                ExecutionBackend = ECloudAIExecutionBackend::LocalRuleBasedFallback;
            }
        }

        FString CustomCloudUrl;
        if (FParse::Value(CmdLine, TEXT("cloudurl="), CustomCloudUrl))
        {
            RemoteCloudEndpointUrl = CustomCloudUrl;
        }

        FString CustomLocalUrl;
        if (FParse::Value(CmdLine, TEXT("localaiurl="), CustomLocalUrl))
        {
            LocalNodeEndpointUrl = CustomLocalUrl;
        }

        FString CustomAuthKey;
        if (FParse::Value(CmdLine, TEXT("cloudaikey="), CustomAuthKey))
        {
            CloudAuthBearerToken = CustomAuthKey;
        }
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(TimerHandle_RequestWatchdog, this, &UDynamicCloudAISubsystem::CheckPendingRequestTimeouts, 1.0f, true);
    }
}

void UDynamicCloudAISubsystem::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TimerHandle_RequestWatchdog);
    }

    // Terminate active HTTP transactions
    for (auto& Pair : PendingRequests)
    {
        if (Pair.Value.HttpRequest.IsValid())
        {
            Pair.Value.HttpRequest->CancelRequest();
            Pair.Value.HttpRequest.Reset();
        }
    }

    PendingRequests.Empty();
    Super::Deinitialize();
}

FString UDynamicCloudAISubsystem::CleanMarkdownJsonString(const FString& InRawJson)
{
    FString SanitizedJson = InRawJson.TrimStartAndEnd();

    // Strip starting markdown fences
    if (SanitizedJson.StartsWith(TEXT("```json"), ESearchCase::IgnoreCase))
    {
        SanitizedJson.RemoveFromStart(TEXT("```json"));
        if (SanitizedJson.EndsWith(TEXT("```")))
        {
            SanitizedJson.RemoveFromEnd(TEXT("```"));
        }
        SanitizedJson = SanitizedJson.TrimStartAndEnd();
    }
    else if (SanitizedJson.StartsWith(TEXT("```")))
    {
        SanitizedJson.RemoveFromStart(TEXT("```"));
        if (SanitizedJson.EndsWith(TEXT("```")))
        {
            SanitizedJson.RemoveFromEnd(TEXT("```"));
        }
        SanitizedJson = SanitizedJson.TrimStartAndEnd();
    }

    // Safety fallback: isolate explicit outermost JSON object bounds if raw text surrounds the output
    int32 FirstOpenBrace = INDEX_NONE;
    int32 LastCloseBrace = INDEX_NONE;
    if (SanitizedJson.FindChar(TEXT('{'), FirstOpenBrace) && SanitizedJson.FindLastChar(TEXT('}'), LastCloseBrace))
    {
        if (FirstOpenBrace != INDEX_NONE && LastCloseBrace != INDEX_NONE && LastCloseBrace > FirstOpenBrace)
        {
            SanitizedJson = SanitizedJson.Mid(FirstOpenBrace, (LastCloseBrace - FirstOpenBrace) + 1);
        }
    }

    return SanitizedJson;
}

FGuid UDynamicCloudAISubsystem::DispatchRemoteAIRequest(
    AActor* RequestingAgent,
    ECloudAIRequestType RequestType,
    const FString& SerializedAgentContextJson,
    float RequestTimeoutSeconds)
{
    return DispatchRemoteAIRequestNative(RequestingAgent, RequestType, SerializedAgentContextJson, RequestTimeoutSeconds, FOnCloudAIResponseDirectCallback());
}

FGuid UDynamicCloudAISubsystem::DispatchRemoteAIRequestNative(
    AActor* RequestingAgent,
    ECloudAIRequestType RequestType,
    const FString& SerializedAgentContextJson,
    float RequestTimeoutSeconds,
    FOnCloudAIResponseDirectCallback DirectCallback)
{
    const FGuid NewGuid = FGuid::NewGuid();
    const double CurrentWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

    FPendingRemoteRequest Record;
    Record.RequestGuid = NewGuid;
    Record.RequesterActor = RequestingAgent;
    Record.RequestType = RequestType;
    Record.DispatchWorldTime = CurrentWorldTime;
    Record.TimeoutDuration = FMath::Max(1.0f, RequestTimeoutSeconds);
    Record.DirectCallback = DirectCallback;

    // BRANCH A: Local rule heuristic (1-5 player local co-op, instant execution)
    if (ExecutionBackend == ECloudAIExecutionBackend::LocalRuleBasedFallback)
    {
        PendingRequests.Add(NewGuid, Record);

        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().SetTimerForNextTick([this, NewGuid, RequestingAgent, RequestType, SerializedAgentContextJson]()
            {
                ExecuteLocalHeuristicDecision(NewGuid, RequestingAgent, RequestType, SerializedAgentContextJson);
            });
        }
        return NewGuid;
    }

    // BRANCH B: HTTP socket dispatch (Remote Cloud / Miner / Local LLM)
    const FString EndpointUrl = (ExecutionBackend == ECloudAIExecutionBackend::LocalDedicatedNodeHttp)
        ? LocalNodeEndpointUrl
        : RemoteCloudEndpointUrl;

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
    HttpRequest->SetURL(EndpointUrl);
    HttpRequest->SetVerb(TEXT("POST"));
    HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    HttpRequest->SetHeader(TEXT("User-Agent"), TEXT("UnrealEngine-GAS-CloudAI/1.0"));
    HttpRequest->SetHeader(TEXT("X-Request-Type"), StaticEnum<ECloudAIRequestType>()->GetNameStringByValue(static_cast<int64>(RequestType)));
    HttpRequest->SetHeader(TEXT("X-Request-Guid"), NewGuid.ToString());

    if (!CloudAuthBearerToken.IsEmpty())
    {
        HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *CloudAuthBearerToken));
    }

    TSharedRef<FJsonObject> RequestWrapper = MakeShareable(new FJsonObject());
    RequestWrapper->SetStringField(TEXT("RequestGuid"), NewGuid.ToString());
    RequestWrapper->SetStringField(TEXT("RequestType"), StaticEnum<ECloudAIRequestType>()->GetNameStringByValue(static_cast<int64>(RequestType)));
    RequestWrapper->SetNumberField(TEXT("TimeoutSeconds"), RequestTimeoutSeconds);

    TSharedPtr<FJsonObject> ContextJsonObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SerializedAgentContextJson);
    if (FJsonSerializer::Deserialize(Reader, ContextJsonObj) && ContextJsonObj.IsValid())
    {
        RequestWrapper->SetObjectField(TEXT("AgentContext"), ContextJsonObj);
    }
    else
    {
        RequestWrapper->SetStringField(TEXT("AgentContextRaw"), SerializedAgentContextJson);
    }

    FString RequestPayloadString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestPayloadString);
    FJsonSerializer::Serialize(RequestWrapper, Writer);

    HttpRequest->SetContentAsString(RequestPayloadString);
    HttpRequest->SetTimeout(Record.TimeoutDuration);

    TWeakObjectPtr<UDynamicCloudAISubsystem> WeakSubsystem(this);
    HttpRequest->OnProcessRequestComplete().BindLambda([WeakSubsystem, NewGuid](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bConnected)
    {
        if (UDynamicCloudAISubsystem* Subsystem = WeakSubsystem.Get())
        {
            Subsystem->HandleHttpResponse(NewGuid, Req, Res, bConnected);
        }
    });

    Record.HttpRequest = HttpRequest;
    PendingRequests.Add(NewGuid, Record);

    HttpRequest->ProcessRequest();
    return NewGuid;
}

void UDynamicCloudAISubsystem::HandleHttpResponse(
    const FGuid& RequestGuid,
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
    TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
    bool bConnectedSuccessfully)
{
    if (!PendingRequests.Contains(RequestGuid))
    {
        return;
    }

    if (bConnectedSuccessfully && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
    {
        const FString ContentString = Response->GetContentAsString();
        InjectRemoteAIResponse(RequestGuid, ContentString);
        return;
    }

    const int32 ResponseCode = Response.IsValid() ? Response->GetResponseCode() : 0;
    UE_LOG(LogTemp, Warning, TEXT("UDynamicCloudAISubsystem: HTTP request %s failed (Connected: %d, ResponseCode: %d)."),
        *RequestGuid.ToString(), bConnectedSuccessfully, ResponseCode);

    // Fix D: Retain record in PendingRequests so InjectRemoteAIResponse can cleanly remove and broadcast
    if (bFallbackToLocalHeuristicOnHttpFailure)
    {
        if (FPendingRemoteRequest* FoundRecord = PendingRequests.Find(RequestGuid))
        {
            AActor* Actor = FoundRecord->RequesterActor.Get();
            ECloudAIRequestType Type = FoundRecord->RequestType;
            FoundRecord->HttpRequest.Reset();
            ExecuteLocalHeuristicDecision(RequestGuid, Actor, Type, FString());
            return;
        }
    }

    InjectRemoteAIResponse(RequestGuid, FString());
}

bool UDynamicCloudAISubsystem::CancelRemoteAIRequest(const FGuid& RequestGuid)
{
    FPendingRemoteRequest Record;
    if (PendingRequests.RemoveAndCopyValue(RequestGuid, Record))
    {
        if (Record.HttpRequest.IsValid())
        {
            Record.HttpRequest->CancelRequest();
            Record.HttpRequest.Reset();
        }
        return true;
    }
    return false;
}

void UDynamicCloudAISubsystem::ExecuteLocalHeuristicDecision(
    const FGuid& RequestGuid,
    AActor* RequesterActor,
    ECloudAIRequestType RequestType,
    const FString& SerializedContextJson)
{
    TArray<FCloudAICommandOutput> HeuristicCommands;
    UDynamicSocialComponent* SocialComp = RequesterActor ? RequesterActor->FindComponentByClass<UDynamicSocialComponent>() : nullptr;
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;

    switch (RequestType)
    {
        case ECloudAIRequestType::ComplexDecision:
        {
            static const FGameplayTag HungerTag = FGameplayTag::RequestGameplayTag(TEXT("GDBSurvival.Attribute.Resource.Hunger"), false);
            static const FGameplayTag EnergyTag = FGameplayTag::RequestGameplayTag(TEXT("GDBSurvival.Attribute.Resource.Energy"), false);

            const float HungerVal = (SocialComp && HungerTag.IsValid()) ? SocialComp->GetCurrentNeedOrMoodValue(HungerTag) : 100.0f;
            const float EnergyVal = (SocialComp && EnergyTag.IsValid()) ? SocialComp->GetCurrentNeedOrMoodValue(EnergyTag) : 100.0f;

            if (HungerVal < 25.0f)
            {
                FCloudAICommandOutput EatCmd;
                EatCmd.CommandActionTag = FGameplayTag::RequestGameplayTag(TEXT("Schedule.Activity.Eat"), false);
                EatCmd.TargetEntityOrPlaceRoleTag = FGameplayTag::RequestGameplayTag(TEXT("Role.Place.Tavern"), false);
                EatCmd.UtteranceText = TEXT("I must find some food.");
                EatCmd.PriorityWeight = 200.0f;
                HeuristicCommands.Add(EatCmd);
            }
            else if (EnergyVal < 20.0f)
            {
                FCloudAICommandOutput SleepCmd;
                SleepCmd.CommandActionTag = FGameplayTag::RequestGameplayTag(TEXT("Schedule.Activity.Rest"), false);
                SleepCmd.TargetEntityOrPlaceRoleTag = FGameplayTag::RequestGameplayTag(TEXT("Role.Place.Home"), false);
                SleepCmd.UtteranceText = TEXT("I am exhausted.");
                SleepCmd.PriorityWeight = 180.0f;
                HeuristicCommands.Add(SleepCmd);
            }
            else if (SocialComp && GS)
            {
                const float CurrentHour = (GS->CalendarConfig.HoursPerDay > 0)
                    ? FMath::Fmod((GetWorld()->GetTimeSeconds() * GS->CalendarConfig.TimeScalar) / 3600.0f, static_cast<float>(GS->CalendarConfig.HoursPerDay))
                    : 12.0f;

                FGameplayTag TargetPlaceRole;
                const FGameplayTag ScheduledActivity = SocialComp->EvaluateCurrentExpectedActivity(CurrentHour, TargetPlaceRole);

                if (ScheduledActivity.IsValid())
                {
                    FCloudAICommandOutput SchedCmd;
                    SchedCmd.CommandActionTag = ScheduledActivity;
                    SchedCmd.TargetEntityOrPlaceRoleTag = TargetPlaceRole;
                    SchedCmd.UtteranceText = TEXT("");
                    SchedCmd.PriorityWeight = 110.0f;
                    HeuristicCommands.Add(SchedCmd);
                }
            }

            if (HeuristicCommands.Num() == 0)
            {
                FCloudAICommandOutput WorkCmd;
                WorkCmd.CommandActionTag = FGameplayTag::RequestGameplayTag(TEXT("Schedule.Activity.Work"), false);
                WorkCmd.TargetEntityOrPlaceRoleTag = FGameplayTag::RequestGameplayTag(TEXT("Role.Place.Workplace"), false);
                WorkCmd.UtteranceText = TEXT("");
                WorkCmd.PriorityWeight = 50.0f;
                HeuristicCommands.Add(WorkCmd);
            }
            break;
        }

        case ECloudAIRequestType::DynamicDialogue:
        {
            FCloudAICommandOutput DialogueCmd;
            DialogueCmd.CommandActionTag = FGameplayTag::RequestGameplayTag(TEXT("Action.Converse.Greet"), false);
            DialogueCmd.TargetEntityOrPlaceRoleTag = FGameplayTag::RequestGameplayTag(TEXT("Role.Place.Tavern"), false);
            DialogueCmd.UtteranceText = TEXT("Greetings, traveler.");
            DialogueCmd.PriorityWeight = 100.0f;
            HeuristicCommands.Add(DialogueCmd);
            break;
        }

        case ECloudAIRequestType::DailySchedulePlan:
        {
            FCloudAICommandOutput ScheduleCmd;
            ScheduleCmd.CommandActionTag = FGameplayTag::RequestGameplayTag(TEXT("Schedule.Activity.Work"), false);
            ScheduleCmd.TargetEntityOrPlaceRoleTag = FGameplayTag::RequestGameplayTag(TEXT("Role.Place.Workplace"), false);
            ScheduleCmd.PriorityWeight = 100.0f;
            HeuristicCommands.Add(ScheduleCmd);
            break;
        }

        default:
            break;
    }

    TSharedRef<FJsonObject> RootObj = MakeShareable(new FJsonObject());
    TArray<TSharedPtr<FJsonValue>> CmdsArray;

    for (const FCloudAICommandOutput& Cmd : HeuristicCommands)
    {
        TSharedRef<FJsonObject> CmdObj = MakeShareable(new FJsonObject());
        CmdObj->SetStringField(TEXT("CommandActionTag"), Cmd.CommandActionTag.ToString());
        CmdObj->SetStringField(TEXT("TargetEntityOrPlaceRoleTag"), Cmd.TargetEntityOrPlaceRoleTag.ToString());
        CmdObj->SetStringField(TEXT("UtteranceText"), Cmd.UtteranceText);
        CmdObj->SetNumberField(TEXT("PriorityWeight"), Cmd.PriorityWeight);
        CmdsArray.Add(MakeShareable(new FJsonValueObject(CmdObj)));
    }

    RootObj->SetArrayField(TEXT("Commands"), CmdsArray);

    FString GeneratedJson;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&GeneratedJson);
    FJsonSerializer::Serialize(RootObj, Writer);

    InjectRemoteAIResponse(RequestGuid, GeneratedJson);
}

void UDynamicCloudAISubsystem::InjectRemoteAIResponse(const FGuid& RequestGuid, const FString& ResponseJson)
{
    FPendingRemoteRequest Request;
    if (!PendingRequests.RemoveAndCopyValue(RequestGuid, Request))
    {
        return;
    }

    TArray<FCloudAICommandOutput> ValidatedCommands;

    if (!ResponseJson.IsEmpty())
    {
        // Sanitize string to strip Markdown formatting delimiters (```json ... ```)
        const FString CleanedJson = CleanMarkdownJsonString(ResponseJson);

        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CleanedJson);
        TSharedPtr<FJsonObject> RootObj;

        if (FJsonSerializer::Deserialize(Reader, RootObj) && RootObj.IsValid())
        {
            // Unwrap Ollama / llama.cpp or OpenAI outer formats
            if (!RootObj->HasField(TEXT("Commands")))
            {
                if (RootObj->HasField(TEXT("response")))
                {
                    const FString InnerClean = CleanMarkdownJsonString(RootObj->GetStringField(TEXT("response")));
                    TSharedPtr<FJsonObject> InnerObj;
                    TSharedRef<TJsonReader<>> InnerReader = TJsonReaderFactory<>::Create(InnerClean);
                    if (FJsonSerializer::Deserialize(InnerReader, InnerObj) && InnerObj.IsValid())
                    {
                        RootObj = InnerObj;
                    }
                }
                else if (RootObj->HasField(TEXT("choices")))
                {
                    const TArray<TSharedPtr<FJsonValue>>& Choices = RootObj->GetArrayField(TEXT("choices"));
                    if (Choices.Num() > 0 && Choices[0]->AsObject().IsValid())
                    {
                        const TSharedPtr<FJsonObject> MsgObj = Choices[0]->AsObject()->GetObjectField(TEXT("message"));
                        if (MsgObj.IsValid() && MsgObj->HasField(TEXT("content")))
                        {
                            const FString ContentStr = CleanMarkdownJsonString(MsgObj->GetStringField(TEXT("content")));
                            TSharedPtr<FJsonObject> InnerObj;
                            TSharedRef<TJsonReader<>> InnerReader = TJsonReaderFactory<>::Create(ContentStr);
                            if (FJsonSerializer::Deserialize(InnerReader, InnerObj) && InnerObj.IsValid())
                            {
                                RootObj = InnerObj;
                            }
                        }
                    }
                }
            }

            if (RootObj->HasField(TEXT("Commands")))
            {
                for (const auto& Val : RootObj->GetArrayField(TEXT("Commands")))
                {
                    TSharedPtr<FJsonObject> CmdObj = Val->AsObject();
                    if (!CmdObj.IsValid()) continue;

                    FCloudAICommandOutput RawCmd;
                    RawCmd.CommandActionTag = FGameplayTag::RequestGameplayTag(FName(*CmdObj->GetStringField(TEXT("CommandActionTag"))), false);

                    if (CmdObj->HasField(TEXT("TargetEntityOrPlaceRoleTag")))
                    {
                        RawCmd.TargetEntityOrPlaceRoleTag = FGameplayTag::RequestGameplayTag(FName(*CmdObj->GetStringField(TEXT("TargetEntityOrPlaceRoleTag"))), false);
                    }
                    if (CmdObj->HasField(TEXT("UtteranceText")))
                    {
                        RawCmd.UtteranceText = CmdObj->GetStringField(TEXT("UtteranceText"));
                    }
                    if (CmdObj->HasField(TEXT("PriorityWeight")))
                    {
                        RawCmd.PriorityWeight = static_cast<float>(CmdObj->GetNumberField(TEXT("PriorityWeight")));
                    }

                    FCloudAICommandOutput SanitizedCmd;
                    if (ValidateActionSpaceCommand(RawCmd, SanitizedCmd))
                    {
                        ValidatedCommands.Add(SanitizedCmd);
                    }
                }
            }
        }
    }

    if (Request.DirectCallback.IsBound())
    {
        Request.DirectCallback.Execute(ValidatedCommands);
    }

    OnRemoteAIResponseReceived.Broadcast(RequestGuid, ValidatedCommands);
}

bool UDynamicCloudAISubsystem::ValidateActionSpaceCommand(const FCloudAICommandOutput& InCommand, FCloudAICommandOutput& OutSanitizedCommand) const
{
    OutSanitizedCommand = InCommand;

    if (!InCommand.CommandActionTag.IsValid() || !UGameplayTagsManager::Get().ValidateTagCreation(InCommand.CommandActionTag.GetTagName()))
    {
        return false;
    }

    static const FGameplayTag RootAction = FGameplayTag::RequestGameplayTag(TEXT("Action"), false);
    static const FGameplayTag RootSchedule = FGameplayTag::RequestGameplayTag(TEXT("Schedule"), false);
    static const FGameplayTag RootSocial = FGameplayTag::RequestGameplayTag(TEXT("Social"), false);
    static const FGameplayTag RootAbility = FGameplayTag::RequestGameplayTag(TEXT("Ability"), false);

    const bool bIsAuthorizedCategory = InCommand.CommandActionTag.MatchesTag(RootAction) ||
                                       InCommand.CommandActionTag.MatchesTag(RootSchedule) ||
                                       InCommand.CommandActionTag.MatchesTag(RootSocial) ||
                                       InCommand.CommandActionTag.MatchesTag(RootAbility);

    if (!bIsAuthorizedCategory)
    {
        return false;
    }

    OutSanitizedCommand.PriorityWeight = FMath::Clamp(InCommand.PriorityWeight, 0.0f, 1000.0f);
    return true;
}

bool UDynamicCloudAISubsystem::ValidateConditionNodeTreeAcyclic(const FDynamicConditionTreeContainer& Tree) const
{
    if (Tree.FlatNodes.Num() == 0) return true;

    TSet<int32> Visited;
    TArray<int32> TraversalStack;
    TraversalStack.Push(0);

    while (TraversalStack.Num() > 0)
    {
        const int32 Current = TraversalStack.Pop();
        if (Visited.Contains(Current))
        {
            return false;
        }

        Visited.Add(Current);

        if (!Tree.FlatNodes.IsValidIndex(Current))
        {
            return false;
        }

        for (int32 ChildIdx : Tree.FlatNodes[Current].ChildIndices)
        {
            if (!Tree.FlatNodes.IsValidIndex(ChildIdx) || ChildIdx == Current)
            {
                return false;
            }
            TraversalStack.Push(ChildIdx);
        }
    }
    return true;
}

bool UDynamicCloudAISubsystem::InjectDynamicConditionTreeFromJson(FName TreeName, const FString& ConditionTreeJson)
{
    if (TreeName.IsNone() || ConditionTreeJson.IsEmpty()) return false;

    const FString CleanedJson = CleanMarkdownJsonString(ConditionTreeJson);
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CleanedJson);
    TSharedPtr<FJsonObject> RootObj;
    if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid()) return false;

    FDynamicConditionTreeContainer NewContainer;
    TSharedRef<FJsonObject> Wrapper = MakeShareable(new FJsonObject());
    Wrapper->SetObjectField(TreeName.ToString(), RootObj);

    FString TempJson;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&TempJson);
    FJsonSerializer::Serialize(Wrapper, Writer);

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    TSharedPtr<FJsonObject> DeserializedWrapper;
    TSharedRef<TJsonReader<>> WrapReader = TJsonReaderFactory<>::Create(TempJson);
    FJsonSerializer::Deserialize(WrapReader, DeserializedWrapper);
    if (!DeserializedWrapper.IsValid() || !DeserializedWrapper->HasField(TreeName.ToString())) return false;

    if (!ValidateConditionNodeTreeAcyclic(NewContainer))
    {
        return false;
    }

    GS->SharedConditionTrees.Add(TreeName, NewContainer);
    return true;
}

bool UDynamicCloudAISubsystem::IsRequestPending(const FGuid& RequestGuid) const
{
    return PendingRequests.Contains(RequestGuid);
}

void UDynamicCloudAISubsystem::CheckPendingRequestTimeouts()
{
    if (PendingRequests.Num() == 0) return;

    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    TArray<FGuid> TimedOutGuids;

    for (const auto& Pair : PendingRequests)
    {
        if ((Now - Pair.Value.DispatchWorldTime) >= Pair.Value.TimeoutDuration)
        {
            TimedOutGuids.Add(Pair.Key);
        }
    }

    for (const FGuid& DeadGuid : TimedOutGuids)
    {
        // Fix D: Do not erase record prior to ExecuteLocalHeuristicDecision
        if (bFallbackToLocalHeuristicOnHttpFailure)
        {
            if (FPendingRemoteRequest* FoundRecord = PendingRequests.Find(DeadGuid))
            {
                if (FoundRecord->HttpRequest.IsValid())
                {
                    FoundRecord->HttpRequest->CancelRequest();
                    FoundRecord->HttpRequest.Reset();
                }

                if (FoundRecord->RequesterActor.IsValid())
                {
                    ExecuteLocalHeuristicDecision(DeadGuid, FoundRecord->RequesterActor.Get(), FoundRecord->RequestType, FString());
                    continue;
                }
            }
        }

        FPendingRemoteRequest DeadRequest;
        if (PendingRequests.RemoveAndCopyValue(DeadGuid, DeadRequest))
        {
            if (DeadRequest.HttpRequest.IsValid())
            {
                DeadRequest.HttpRequest->CancelRequest();
                DeadRequest.HttpRequest.Reset();
            }

            if (DeadRequest.DirectCallback.IsBound())
            {
                DeadRequest.DirectCallback.Execute(TArray<FCloudAICommandOutput>());
            }
            OnRemoteAIResponseReceived.Broadcast(DeadGuid, TArray<FCloudAICommandOutput>());
        }
    }
}