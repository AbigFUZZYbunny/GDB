// DynamicCloudAISubsystem.cpp
#include "DynamicCloudAISubsystem.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"

void UDynamicCloudAISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
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
    PendingRequests.Empty();
    Super::Deinitialize();
}

FGuid UDynamicCloudAISubsystem::DispatchRemoteAIRequest(AActor* RequestingAgent, ECloudAIRequestType RequestType, const FString& SerializedAgentContextJson, float RequestTimeoutSeconds)
{
    const FGuid NewGuid = FGuid::NewGuid();

    FPendingRemoteRequest Record;
    Record.RequestGuid = NewGuid;
    Record.RequesterActor = RequestingAgent;
    Record.RequestType = RequestType;
    Record.DispatchWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    Record.TimeoutDuration = RequestTimeoutSeconds;

    PendingRequests.Add(NewGuid, Record);

    // In a production backend, this ships over HTTP POST or WebSockets to the LLM / Coin Miner cluster.
    // For local validation, we schedule a mock loopback task after a brief latency window.
    if (UWorld* World = GetWorld())
    {
        FTimerHandle MockTimer;
        World->GetTimerManager().SetTimer(MockTimer, [this, NewGuid, RequestType]()
        {
            if (!PendingRequests.Contains(NewGuid)) return;

            // Mocked LLM response JSON conforming to FCloudAICommandOutput schema
            FString MockResponseJson;
            if (RequestType == ECloudAIRequestType::ComplexDecision)
            {
                MockResponseJson = TEXT("{\"Commands\": [{\"CommandActionTag\": \"Action.Converse.Greet\", \"TargetEntityOrPlaceRoleTag\": \"Role.Place.Tavern\", \"UtteranceText\": \"Hail, traveler! Have you seen the strange weather lately?\", \"PriorityWeight\": 120.0}]}");
            }
            else
            {
                MockResponseJson = TEXT("{\"Commands\": [{\"CommandActionTag\": \"Schedule.Activity.Work\", \"TargetEntityOrPlaceRoleTag\": \"Role.Place.Workplace\", \"UtteranceText\": \"\", \"PriorityWeight\": 100.0}]}");
            }

            InjectRemoteAIResponse(NewGuid, MockResponseJson);
        }, 0.25f, false);
    }

    return NewGuid;
}

void UDynamicCloudAISubsystem::InjectRemoteAIResponse(const FGuid& RequestGuid, const FString& ResponseJson)
{
    FPendingRemoteRequest Request;
    if (!PendingRequests.RemoveAndCopyValue(RequestGuid, Request))
    {
        return;
    }

    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseJson);
    TSharedPtr<FJsonObject> RootObj;
    if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
    {
        return;
    }

    TArray<FCloudAICommandOutput> ParsedCommands;
    if (RootObj->HasField(TEXT("Commands")))
    {
        for (const auto& Val : RootObj->GetArrayField(TEXT("Commands")))
        {
            TSharedPtr<FJsonObject> CmdObj = Val->AsObject();
            if (!CmdObj.IsValid()) continue;

            FCloudAICommandOutput Cmd;
            Cmd.CommandActionTag = FGameplayTag::RequestGameplayTag(FName(*CmdObj->GetStringField(TEXT("CommandActionTag"))), false);
            if (CmdObj->HasField(TEXT("TargetEntityOrPlaceRoleTag")))
            {
                Cmd.TargetEntityOrPlaceRoleTag = FGameplayTag::RequestGameplayTag(FName(*CmdObj->GetStringField(TEXT("TargetEntityOrPlaceRoleTag"))), false);
            }
            if (CmdObj->HasField(TEXT("UtteranceText")))
            {
                Cmd.UtteranceText = CmdObj->GetStringField(TEXT("UtteranceText"));
            }
            if (CmdObj->HasField(TEXT("PriorityWeight")))
            {
                Cmd.PriorityWeight = static_cast<float>(CmdObj->GetNumberField(TEXT("PriorityWeight")));
            }
            ParsedCommands.Add(Cmd);
        }
    }

    OnRemoteAIResponseReceived.Broadcast(RequestGuid, ParsedCommands);
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
        PendingRequests.Remove(DeadGuid);
        // Broadcast empty command array signaling a timeout
        OnRemoteAIResponseReceived.Broadcast(DeadGuid, TArray<FCloudAICommandOutput>());
    }
}