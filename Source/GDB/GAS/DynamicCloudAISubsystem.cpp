// DynamicCloudAISubsystem.cpp
#include "DynamicCloudAISubsystem.h"
#include "DynamicGameState.h"
#include "DynamicDataSerializer.h"
#include "GameplayTagsManager.h"
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

    // Mock loopback for testing LLM response integration
    if (UWorld* World = GetWorld())
    {
        FTimerHandle MockTimer;
        World->GetTimerManager().SetTimer(MockTimer, [this, NewGuid, RequestType]()
        {
            if (!PendingRequests.Contains(NewGuid)) return;

            FString MockResponseJson;
            if (RequestType == ECloudAIRequestType::ComplexDecision)
            {
                MockResponseJson = TEXT("{\"Commands\": [{\"CommandActionTag\": \"Action.Converse.Greet\", \"TargetEntityOrPlaceRoleTag\": \"Role.Place.Tavern\", \"UtteranceText\": \"Hail, traveler!\", \"PriorityWeight\": 120.0}]}");
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

bool UDynamicCloudAISubsystem::ValidateActionSpaceCommand(const FCloudAICommandOutput& InCommand, FCloudAICommandOutput& OutSanitizedCommand) const
{
    OutSanitizedCommand = InCommand;

    // 1. Tag must exist in native GameplayTags dictionary
    if (!InCommand.CommandActionTag.IsValid() || !UGameplayTagsManager::Get().ValidateTagCreation(InCommand.CommandActionTag.GetTagName()))
    {
        return false;
    }

    // 2. Enforce Action-Space boundaries (strip hallucinated tags)
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

    // 3. Clamp priority bounds
    OutSanitizedCommand.PriorityWeight = FMath::Clamp(InCommand.PriorityWeight, 0.0f, 1000.0f);
    return true;
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

    TArray<FCloudAICommandOutput> ValidatedCommands;
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

    OnRemoteAIResponseReceived.Broadcast(RequestGuid, ValidatedCommands);
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
            return false; // Detected cyclic reference
        }
        Visited.Add(Current);

        if (!Tree.FlatNodes.IsValidIndex(Current))
        {
            return false; // Invalid node index
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

    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ConditionTreeJson);
    TSharedPtr<FJsonObject> RootObj;
    if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid()) return false;

    // Build temporary test container via deserializer
    FDynamicConditionTreeContainer NewContainer;
    TSharedRef<FJsonObject> Wrapper = MakeShareable(new FJsonObject());
    Wrapper->SetObjectField(TreeName.ToString(), RootObj);

    FString TempJson;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&TempJson);
    FJsonSerializer::Serialize(Wrapper, Writer);

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    // Use deserializer helper to populate flat nodes
    TSharedPtr<FJsonObject> DeserializedWrapper;
    TSharedRef<TJsonReader<>> WrapReader = TJsonReaderFactory<>::Create(TempJson);
    FJsonSerializer::Deserialize(WrapReader, DeserializedWrapper);

    if (!DeserializedWrapper.IsValid() || !DeserializedWrapper->HasField(TreeName.ToString())) return false;

    // Validate acyclic structure and legal indices
    if (!ValidateConditionNodeTreeAcyclic(NewContainer))
    {
        return false;
    }

    // Safely register directly into GameState runtime registry
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
        PendingRequests.Remove(DeadGuid);
        OnRemoteAIResponseReceived.Broadcast(DeadGuid, TArray<FCloudAICommandOutput>());
    }
}