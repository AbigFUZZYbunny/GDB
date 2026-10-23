// DynamicAILinkComponent.cpp
#include "DynamicAILinkComponent.h"
#include "DynamicCloudAISubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

UDynamicAILinkComponent::UDynamicAILinkComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UDynamicAILinkComponent::BeginPlay()
{
    Super::BeginPlay();
    if (GetOwner() && GetOwner()->HasAuthority())
    {
        if (UWorld* World = GetWorld())
        {
            if (UDynamicCloudAISubsystem* CloudSubsystem = World->GetSubsystem<UDynamicCloudAISubsystem>())
            {
                CloudSubsystem->RegisterMinerNode(this);
            }
        }
    }
}

void UDynamicAILinkComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (GetOwner() && GetOwner()->HasAuthority())
    {
        if (UWorld* World = GetWorld())
        {
            if (UDynamicCloudAISubsystem* CloudSubsystem = World->GetSubsystem<UDynamicCloudAISubsystem>())
            {
                CloudSubsystem->UnregisterMinerNode(this);
            }
        }
    }
    Super::EndPlay(EndPlayReason);
}

void UDynamicAILinkComponent::Client_AssignAITask_Implementation(const FGuid& TaskId, const FString& Prompt, const FString& Model, const FString& Endpoint, const FString& ApiKey)
{
    ActiveTaskId = TaskId;
    LastParsedSseIndex = 0;

    FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
    Request->SetVerb(TEXT("POST"));
    Request->SetURL(Endpoint);
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    if (!ApiKey.IsEmpty())
    {
        Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
    }

    TSharedRef<FJsonObject> RootPayload = MakeShareable(new FJsonObject());
    RootPayload->SetStringField(TEXT("model"), Model);
    RootPayload->SetBoolField(TEXT("stream"), true); // Reactivate streaming

    TArray<TSharedPtr<FJsonValue>> Messages;
    TSharedRef<FJsonObject> UserMessage = MakeShareable(new FJsonObject());
    UserMessage->SetStringField(TEXT("role"), TEXT("user"));
    UserMessage->SetStringField(TEXT("content"), Prompt);
    Messages.Add(MakeShareable(new FJsonValueObject(UserMessage)));
    RootPayload->SetArrayField(TEXT("messages"), Messages);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(RootPayload, Writer);
    Request->SetContentAsString(OutputString);

    // Bind using UE5's updated 64-bit progress event
    Request->OnRequestProgress64().BindUObject(this, &UDynamicAILinkComponent::OnStreamProgress64);
    Request->OnProcessRequestComplete().BindUObject(this, &UDynamicAILinkComponent::OnStreamComplete);
    Request->ProcessRequest();
}

void UDynamicAILinkComponent::OnStreamProgress64(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived)
{
    if (!Request.IsValid() || !Request->GetResponse().IsValid()) return;

    const FString FullResponse = Request->GetResponse()->GetContentAsString();
    if (FullResponse.Len() <= LastParsedSseIndex) return;

    const FString NewChunk = FullResponse.Right(FullResponse.Len() - LastParsedSseIndex);
    LastParsedSseIndex = FullResponse.Len();

    Server_SubmitTokenChunk(ActiveTaskId, NewChunk);
}

void UDynamicAILinkComponent::OnStreamComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
    if (bConnectedSuccessfully && Response.IsValid() && Response->GetResponseCode() >= 200 && Response->GetResponseCode() < 300)
    {
        Server_SubmitFinalResult(ActiveTaskId, Response->GetContentAsString());
    }
    else
    {
        Server_SubmitFinalResult(ActiveTaskId, TEXT("STREAM_FAILED"));
    }
}

bool UDynamicAILinkComponent::Server_SubmitTokenChunk_Validate(const FGuid& TaskId, const FString& TokenChunk)
{
    return TaskId.IsValid();
}

void UDynamicAILinkComponent::Server_SubmitTokenChunk_Implementation(const FGuid& TaskId, const FString& TokenChunk)
{
    // Handled via server-side packet processing
}

bool UDynamicAILinkComponent::Server_SubmitFinalResult_Validate(const FGuid& TaskId, const FString& FinalJsonResult)
{
    return TaskId.IsValid();
}

void UDynamicAILinkComponent::Server_SubmitFinalResult_Implementation(const FGuid& TaskId, const FString& FinalJsonResult)
{
    NodeStats.bIsCurrentlyProcessing = false;

    if (APlayerController* PC = Cast<APlayerController>(GetOwner()))
    {
        if (APlayerState* PS = PC->PlayerState)
        {
            NodeStats.AveragePingMs = FMath::Lerp(NodeStats.AveragePingMs, PS->GetPingInMilliseconds(), 0.2f);
        }
    }

    if (UWorld* World = GetWorld())
    {
        if (UDynamicCloudAISubsystem* CloudSubsystem = World->GetSubsystem<UDynamicCloudAISubsystem>())
        {
            CloudSubsystem->HandleMinerCompletion(this, TaskId, FinalJsonResult);
        }
    }
}